use hbb_common::{anyhow::Error, bail, log, ResultType};
use ndk::media::media_codec::{MediaCodec, MediaCodecDirection, MediaFormat};
use std::ops::Deref;
use std::{
    io::Write,
    sync::atomic::{AtomicBool, Ordering},
    time::Duration,
};

use crate::ImageFormat;
use crate::{
    codec::{EncoderApi, EncoderCfg},
    CodecFormat, I420ToABGR, I420ToARGB, ImageRgb,
};

/// MediaCodec mime type name
const H264_MIME_TYPE: &str = "video/avc";
const H265_MIME_TYPE: &str = "video/hevc";
const VP8_MIME_TYPE: &str = "video/x-vnd.on2.vp8";

pub static VP8_DECODER_SUPPORT: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// PATCH(surface-render): raw ANativeWindow* provided by Kotlin via JNI.
/// When set, MediaCodec renders frames directly to this surface (zero-copy,
/// hardware path), skipping the CPU I420->ARGB conversion and texture copies.
pub static SURFACE_NATIVE_WINDOW: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);

pub fn set_surface_ptr(ptr: usize) {
    SURFACE_NATIVE_WINDOW.store(ptr, std::sync::atomic::Ordering::SeqCst);
}

pub fn get_surface_ptr() -> usize {
    SURFACE_NATIVE_WINDOW.load(std::sync::atomic::Ordering::SeqCst)
}
// const VP8_MIME_TYPE: &str = "video/x-vnd.on2.vp8";
// const VP9_MIME_TYPE: &str = "video/x-vnd.on2.vp9";

// TODO MediaCodecEncoder

pub static H264_DECODER_SUPPORT: AtomicBool = AtomicBool::new(false);
pub static H265_DECODER_SUPPORT: AtomicBool = AtomicBool::new(false);

pub struct MediaCodecDecoder {
    decoder: MediaCodec,
    name: String,
    render_to_surface: bool,
    csd_sent: bool,
    /// PATCH(csd-fix): set after one full warmup poll produced nothing, so
    /// subsequent failing frames fail fast (~100ms) instead of burning the
    /// full 3s budget each time.
    warmup_exhausted: bool,
}

/// Annex-B NAL classification for CSD extraction.
/// H264: NAL type = byte & 0x1F (7=SPS, 8=PPS).
/// HEVC: NAL type = (byte & 0x7E) >> 1 (32=VPS, 33=SPS, 34=PPS).
fn is_param_nal(nal: &[u8], hevc: bool) -> bool {
    if nal.len() < 2 {
        return false;
    }
    if hevc {
        matches!((nal[0] & 0x7E) >> 1, 32 | 33 | 34)
    } else {
        matches!(nal[0] & 0x1F, 7 | 8)
    }
}

/// Split an annex-B buffer into parameter-set bytes. Start codes: 00 00 00 01
/// or 00 00 01. Returns concatenated param NALs (with their original start
/// codes), or None if the buffer has no param NALs.
pub(super) fn extract_param_sets(data: &[u8], hevc: bool) -> Option<Vec<u8>> {
    let mut starts: Vec<usize> = Vec::new();
    let n = data.len();
    let mut i = 0;
    while i + 3 <= n {
        if data[i] == 0 && data[i + 1] == 0 {
            if data[i + 2] == 1 {
                starts.push(i);
                i += 3;
                continue;
            } else if i + 4 <= n && data[i + 2] == 0 && data[i + 3] == 1 {
                starts.push(i);
                i += 4;
                continue;
            }
        }
        i += 1;
    }
    if starts.is_empty() {
        return None;
    }
    let mut csd = Vec::new();
    for (k, &s) in starts.iter().enumerate() {
        let e = if k + 1 < starts.len() {
            starts[k + 1]
        } else {
            n
        };
        let hdr = if data[s + 2] == 1 { 3 } else { 4 };
        if is_param_nal(&data[s + hdr..e], hevc) {
            csd.extend_from_slice(&data[s..e]);
        }
    }
    if csd.is_empty() {
        None
    } else {
        Some(csd)
    }
}

impl Deref for MediaCodecDecoder {
    type Target = MediaCodec;

    fn deref(&self) -> &Self::Target {
        &self.decoder
    }
}

impl MediaCodecDecoder {
    pub fn new(format: CodecFormat) -> Option<MediaCodecDecoder> {
        match format {
            CodecFormat::H264 => create_media_codec(H264_MIME_TYPE, MediaCodecDirection::Decoder),
            CodecFormat::H265 => create_media_codec(H265_MIME_TYPE, MediaCodecDirection::Decoder),
            CodecFormat::VP8 => create_media_codec(VP8_MIME_TYPE, MediaCodecDirection::Decoder),
            _ => {
                log::error!("Unsupported codec format: {:?}", format);
                None
            }
        }
    }

    // rgb [in/out] fmt and stride must be set in ImageRgb
    pub fn decode(&mut self, data: &[u8], rgb: &mut ImageRgb) -> ResultType<bool> {
        // PATCH(csd-fix): goke OMX needs the parameter sets as a CODEC_CONFIG
        // (flags=2) buffer before frames. Note NDK flag semantics differ from
        // Java: AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG=2 (1 = KEY_FRAME only).
        // The previous code queued the whole first frame with flags=1 (not
        // CSD at all) and then re-queued the same data as a frame (dup).
        // Now: extract VPS/SPS/PPS (HEVC) or SPS/PPS (AVC) from the first
        // packet, queue as flags=2 in BOTH surface and buffer modes; the
        // packet itself is always fed as a normal frame afterwards (in-band
        // parameter sets on IDR packets are tolerated by the decoder).
        if !self.csd_sent {
            self.csd_sent = true;
            let hevc = self.name.contains("hevc");
            match extract_param_sets(data, hevc) {
                Some(csd) => {
                    if let Some(mut input_buffer) = self.dequeue_input_buffer(Duration::from_millis(100))? {
                        let mut buf = input_buffer.buffer_mut();
                        if csd.len() > buf.len() {
                            bail!("csd: the input data size is bigger than input buf");
                        }
                        buf.write_all(&csd)?;
                        self.queue_input_buffer(input_buffer, 0, csd.len(), 0, 2)?;
                        log::info!("csd queued: {} bytes (flags=2)", csd.len());
                    }
                }
                None => {
                    log::info!("no param NALs in first packet ({} bytes), feeding without CSD", data.len());
                }
            }
        }
        match self.dequeue_input_buffer(Duration::from_millis(10))? {
            Some(mut input_buffer) => {
                let mut buf = input_buffer.buffer_mut();
                if data.len() > buf.len() {
                    log::error!("Failed to decode, the input data size is bigger than input buf");
                    bail!("The input data size is bigger than input buf");
                }
                buf.write_all(&data)?;
                self.queue_input_buffer(input_buffer, 0, data.len(), 0, 0)?;
            }
            None => {
                log::debug!("Failed to dequeue_input_buffer: No available input_buffer");
            }
        };

        return match self.dequeue_output_buffer(Duration::from_millis(100))? {
            Some(output_buffer) => {
                // PATCH(surface-render): render decoded frame directly to the
                // Surface (hardware compositor path), no CPU conversion/copies.
                if self.render_to_surface {
                    self.release_output_buffer(output_buffer, true)?;
                    return Ok(true);
                }
                let res_format = self.output_format();
                let w = res_format
                    .i32("width")
                    .ok_or(Error::msg("Failed to dequeue_output_buffer, width is None"))?
                    as usize;
                let h = res_format.i32("height").ok_or(Error::msg(
                    "Failed to dequeue_output_buffer, height is None",
                ))? as usize;
                let stride = res_format.i32("stride").ok_or(Error::msg(
                    "Failed to dequeue_output_buffer, stride is None",
                ))?;
                let buf = output_buffer.buffer();
                let bps = 4;
                let u = buf.len() * 2 / 3;
                let v = buf.len() * 5 / 6;
                rgb.raw.resize(h * w * bps, 0);
                let y_ptr = buf.as_ptr();
                let u_ptr = buf[u..].as_ptr();
                let v_ptr = buf[v..].as_ptr();
                unsafe {
                    match rgb.fmt() {
                        ImageFormat::ARGB => {
                            I420ToARGB(
                                y_ptr,
                                stride,
                                u_ptr,
                                stride / 2,
                                v_ptr,
                                stride / 2,
                                rgb.raw.as_mut_ptr(),
                                (w * bps) as _,
                                w as _,
                                h as _,
                            );
                        }
                        ImageFormat::ABGR => {
                            I420ToABGR(
                                y_ptr,
                                stride,
                                u_ptr,
                                stride / 2,
                                v_ptr,
                                stride / 2,
                                rgb.raw.as_mut_ptr(),
                                (w * bps) as _,
                                w as _,
                                h as _,
                            );
                        }
                        _ => {
                            bail!("Unsupported image format");
                        }
                    }
                }
                self.release_output_buffer(output_buffer, false)?;
                Ok(true)
            }
            None => {
                log::debug!("Failed to dequeue_output: No available dequeue_output");
                // PATCH(csd-fix): goke warmup (first 1080p keyframe) takes up
                // to ~2s on this box (NDK harness measurement). Returning
                // Ok(false) here makes the client mark the codec unsupported
                // after 3 frames; in surface mode poll up to ~3s before
                // giving up so real hardware output has a chance to arrive.
                // (Buffer mode keeps the short path: decoded-ram frames would
                // need the full RGB conversion below anyway.)
                if self.render_to_surface && !self.warmup_exhausted {
                    log::info!("surface warmup: polling up to 3s for first frame ({})", self.name);
                    let deadline = std::time::Instant::now() + Duration::from_millis(3000);
                    while std::time::Instant::now() < deadline {
                        match self.dequeue_output_buffer(Duration::from_millis(100))? {
                            Some(output_buffer) => {
                                self.release_output_buffer(output_buffer, true)?;
                                return Ok(true);
                            }
                            None => {}
                        }
                    }
                    self.warmup_exhausted = true;
                    log::error!("surface warmup budget exhausted, failing fast from now ({})", self.name);
                }
                Ok(false)
            }
        };
    }
}


/// MediaCodecDirection is neither Copy nor Clone; rebuild it per attempt.
fn clone_direction(d: &MediaCodecDirection) -> MediaCodecDirection {
    match d {
        MediaCodecDirection::Decoder => MediaCodecDirection::Decoder,
        MediaCodecDirection::Encoder => MediaCodecDirection::Encoder,
    }
}

fn create_media_codec(name: &str, direction: MediaCodecDirection) -> Option<MediaCodecDecoder> {
    let codec = MediaCodec::from_decoder_type(name)?;
    let media_format = MediaFormat::new();
    media_format.set_str("mime", name);
    let surface_ptr = get_surface_ptr();
    let render_to_surface = surface_ptr != 0;
    // PATCH(hw): goke OMX rejects 0x0 (declared min 128x128, align 2x2) with EINVAL(-22).
    // Configure with real stream dims; adaptive-playback + per-frame output_format() handle changes.
    media_format.set_i32("width", 1920);
    media_format.set_i32("height", 1080);
    // PATCH(surface-render): in surface mode the color format is decided by
    // the surface consumer; goke HEVC rejects format 19 with ErrorUnknown.
    if !render_to_surface {
        media_format.set_i32("color-format", 19); // COLOR_FormatYUV420Planar
    }
    let configure_res = if render_to_surface {
        match std::ptr::NonNull::new(surface_ptr as *mut _) {
            Some(ptr) => {
                log::info!("configuring decoder with surface {:#x}", surface_ptr);
                let nw = unsafe { ndk::native_window::NativeWindow::clone_from_ptr(ptr) };
                codec.configure(&media_format, Some(&nw), clone_direction(&direction))
            }
            None => codec.configure(&media_format, None, clone_direction(&direction)),
        }
    } else {
        codec.configure(&media_format, None, clone_direction(&direction))
    };
    // PATCH(csd-fix): goke OMX configure fails transiently around cold start
    // (~first second of the codec service) and right after another decoder on
    // the same surface is released (VP8->H265 switch: ErrorUnknown for ~100ms).
    // Live test 13:01 showed H265+Surface configure OK during probe but
    // ErrorUnknown 3.5s later. Retry with backoff before giving up.
    let mut configure_res = configure_res;
    if configure_res.is_err() {
        for wait in [300, 600, 1200] {
            log::info!("configure failed, retrying in {}ms ({})", wait, name);
            std::thread::sleep(Duration::from_millis(wait));
            configure_res = if render_to_surface {
                match std::ptr::NonNull::new(surface_ptr as *mut _) {
                    Some(ptr) => {
                        let nw = unsafe { ndk::native_window::NativeWindow::clone_from_ptr(ptr) };
                        codec.configure(&media_format, Some(&nw), clone_direction(&direction))
                    }
                    None => codec.configure(&media_format, None, clone_direction(&direction)),
                }
            } else {
                codec.configure(&media_format, None, clone_direction(&direction))
            };
            if configure_res.is_ok() {
                break;
            }
        }
    }
    if let Err(e) = configure_res {
        log::error!("Failed to init decoder {} after retries: {:?}", name, e);
        return None;
    };
    log::error!("decoder init success");
    if let Err(e) = codec.start() {
        log::error!("Failed to start decoder: {:?}", e);
        return None;
    };
    log::debug!("Init decoder succeeded!: {:?}", name);
    return Some(MediaCodecDecoder {
        decoder: codec,
        name: name.to_owned(),
        render_to_surface,
        csd_sent: false,
        warmup_exhausted: false,
    });
}

pub fn vp8_surface_support() -> bool {
    VP8_DECODER_SUPPORT.load(std::sync::atomic::Ordering::SeqCst)
}

fn probe_mediacodec_once() {
    // PATCH(csd-fix): probe instances must never attach the real session
    // surface — a lingering probe decoder on the Surface makes the later
    // real H265+Surface configure fail (goke allows one consumer per
    // surface). Force buffer mode for the probes.
    let saved = get_surface_ptr();
    set_surface_ptr(0);
    let h264 = MediaCodecDecoder::new(CodecFormat::H264);
    H264_DECODER_SUPPORT.store(h264.is_some(), Ordering::SeqCst);
    let h265 = MediaCodecDecoder::new(CodecFormat::H265);
    H265_DECODER_SUPPORT.store(h265.is_some(), Ordering::SeqCst);
    let _ = h264.map(|d| d.stop());
    let _ = h265.map(|d| d.stop());
    let vp8 = MediaCodecDecoder::new(CodecFormat::VP8);
    VP8_DECODER_SUPPORT.store(vp8.is_some(), Ordering::SeqCst);
    let _ = vp8.map(|d| d.stop());
    set_surface_ptr(saved);
}

/// PATCH(surface-render): synchronous probe used before codec negotiation —
/// the async check_mediacodec() races the login on cold start (goke OMX
/// takes ~13s to warm up), causing the client to advertise h265:false and
/// the server to fall back to VP9.
pub fn ensure_mediacodec_probed() {
    use std::sync::atomic::AtomicBool;
    static PROBED: AtomicBool = AtomicBool::new(false);
    if PROBED.load(Ordering::SeqCst)
        && H264_DECODER_SUPPORT.load(Ordering::SeqCst)
        && H265_DECODER_SUPPORT.load(Ordering::SeqCst)
    {
        return;
    }
    probe_mediacodec_once();
    PROBED.store(true, Ordering::SeqCst);
}

pub fn check_mediacodec() {
    std::thread::spawn(move || {
        // check decoders
        let h264 = MediaCodecDecoder::new(CodecFormat::H264);
        H264_DECODER_SUPPORT.swap(h264.is_some(), Ordering::SeqCst);
        let h265 = MediaCodecDecoder::new(CodecFormat::H265);
        H265_DECODER_SUPPORT.swap(h265.is_some(), Ordering::SeqCst);
        let _ = h264.map(|d| d.stop());
        let _ = h265.map(|d| d.stop());
        // TODO encoders
    });
}
