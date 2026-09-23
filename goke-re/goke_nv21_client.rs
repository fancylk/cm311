//! GK6323 private VDEC bridge. The root daemon owns libgk_msp; this client
//! sends Annex-B access units and presents its decoded RGBA frames in the
//! Flutter SurfaceTexture's ANativeWindow.
use std::{
    convert::{TryFrom, TryInto},
    ffi::c_void,
    io::{Read, Write},
    net::Shutdown,
    os::unix::net::UnixStream,
    ptr,
    thread::{self, JoinHandle},
};
use hbb_common::{log, ResultType};
use crate::CodecFormat;
use super::convert::{ARGBToABGR, NV21ToARGB};
use super::mediacodec::{extract_param_sets, get_surface_ptr};

const SOCKET: &str = "/data/local/tmp/goke-vdec.sock";
const HELLO: u32 = 0x474b4849;
const INPUT: u32 = 0x474b494e;
const OUTPUT_NV21: u32 = 0x47524632;
const MAX_YUV: usize = 4096 * 2304 * 3 / 2;

#[repr(C)]
struct NativeWindowBuffer {
    width: i32,
    height: i32,
    stride: i32,
    format: i32,
    bits: *mut c_void,
    reserved: [u32; 6],
}

#[link(name = "android")]
extern "C" {
    fn ANativeWindow_acquire(window: *mut c_void);
    fn ANativeWindow_release(window: *mut c_void);
    fn ANativeWindow_setBuffersGeometry(window: *mut c_void, width: i32, height: i32, format: i32) -> i32;
    fn ANativeWindow_lock(window: *mut c_void, buffer: *mut NativeWindowBuffer, dirty: *mut c_void) -> i32;
    fn ANativeWindow_unlockAndPost(window: *mut c_void) -> i32;
}

pub struct GokeDecoder {
    stream: UnixStream,
    worker: Option<JoinHandle<()>>,
    csd_sent: bool,
    hevc: bool,
}

impl GokeDecoder {
    pub fn new(codec: CodecFormat) -> Option<Self> {
        let codec_id = match codec {
            CodecFormat::H264 => 4u32,
            CodecFormat::H265 => 36u32,
            _ => return None,
        };
        let window = get_surface_ptr();
        if window == 0 {
            log::warn!("goke bridge: Surface not ready");
            return None;
        }
        let mut stream = match UnixStream::connect(SOCKET) {
            Ok(stream) => stream,
            Err(err) => { log::warn!("goke bridge: connect failed: {err}"); return None; }
        };
        if stream.write_all(&HELLO.to_le_bytes()).is_err()
            || stream.write_all(&codec_id.to_le_bytes()).is_err() {
            return None;
        }
        let reader = match stream.try_clone() {
            Ok(reader) => reader,
            Err(err) => { log::error!("goke bridge: clone failed: {err}"); return None; }
        };
        let worker = match thread::Builder::new().name("goke-render".into()).spawn(move || render_loop(reader, window)) {
            Ok(worker) => worker,
            Err(err) => { log::error!("goke bridge: render thread failed: {err}"); return None; }
        };
        log::info!("goke bridge connected: codec={codec_id}, window={window:#x}");
        Some(Self { stream, worker: Some(worker), csd_sent: false, hevc: codec_id == 36 })
    }

    fn send(&mut self, data: &[u8]) -> ResultType<()> {
        let len = u32::try_from(data.len())?;
        self.stream.write_all(&INPUT.to_le_bytes())?;
        self.stream.write_all(&len.to_le_bytes())?;
        self.stream.write_all(data)?;
        Ok(())
    }

    pub fn decode(&mut self, data: &[u8]) -> ResultType<bool> {
        if !self.csd_sent {
            if let Some(csd) = extract_param_sets(data, self.hevc) {
                self.send(&csd)?;
                log::info!("goke bridge CSD: {} bytes", csd.len());
            }
            self.csd_sent = true;
        }
        self.send(data)?;
        // The render thread presents decoded frames independently. Keeping
        // the stream receiver moving avoids blocking before the first IDR.
        Ok(true)
    }
}

impl Drop for GokeDecoder {
    fn drop(&mut self) {
        let _ = self.stream.shutdown(Shutdown::Both);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

fn render_loop(mut reader: UnixStream, window_ptr: usize) {
    let window = window_ptr as *mut c_void;
    unsafe { ANativeWindow_acquire(window); }
    // PATCH(nv21): the daemon sends raw NV21 planes (Y + interleaved VU); the
    // conversion to RGBA happens here with NEON libyuv, directly into the
    // Surface buffer. 2.8MB/frame over the socket instead of 7.5MB RGBA.
    let mut yuv = Vec::<u8>::new();
    let mut argb = Vec::<u8>::new();
    let mut size = (0u32, 0u32);
    let mut frames = 0u32;
    loop {
        let mut header = [0u8; 24];
        if reader.read_exact(&mut header).is_err() { break; }
        let words: Vec<u32> = header.chunks_exact(4)
            .map(|chunk| u32::from_le_bytes(chunk.try_into().unwrap())).collect();
        let (magic, width, height, ylen, vulen) =
            (words[0], words[1], words[2], words[3], words[4]);
        if magic != OUTPUT_NV21 || width == 0 || height == 0 || width > 4096 || height > 2304
            || ylen != width * height || vulen != width * height / 2
            || (ylen + vulen) as usize > MAX_YUV {
            log::error!("goke bridge: bad output header {words:?}"); break;
        }
        yuv.resize((ylen + vulen) as usize, 0);
        if reader.read_exact(&mut yuv).is_err() { break; }
        let (y, vu) = yuv.split_at(ylen as usize);
        unsafe {
            if size != (width, height) {
                // WINDOW_FORMAT_RGBA_8888 = 1.
                if ANativeWindow_setBuffersGeometry(window, width as i32, height as i32, 1) != 0 {
                    log::error!("goke bridge: setBuffersGeometry failed"); break;
                }
                size = (width, height);
            }
            let mut buffer = NativeWindowBuffer {
                width: 0, height: 0, stride: 0, format: 0,
                bits: ptr::null_mut(), reserved: [0; 6],
            };
            if ANativeWindow_lock(window, &mut buffer, ptr::null_mut()) != 0 || buffer.bits.is_null() {
                log::error!("goke bridge: ANativeWindow_lock failed"); break;
            }
            if buffer.stride < width as i32 || buffer.height < height as i32 {
                let _ = ANativeWindow_unlockAndPost(window);
                log::error!("goke bridge: unexpected surface geometry"); break;
            }
            argb.resize(width as usize * height as usize * 4, 0);
            // NV21 (Y + interleaved VU) -> ARGB -> ABGR (R,G,B,A bytes).
            if NV21ToARGB(
                y.as_ptr(), width as i32,
                vu.as_ptr(), width as i32,
                argb.as_mut_ptr(), (width * 4) as i32,
                width as i32, height as i32,
            ) != 0 {
                let _ = ANativeWindow_unlockAndPost(window);
                log::error!("goke bridge: NV21ToARGB failed"); break;
            }
            ARGBToABGR(
                argb.as_ptr(), (width * 4) as i32,
                buffer.bits as *mut u8, buffer.stride * 4,
                width as i32, height as i32,
            );
            if ANativeWindow_unlockAndPost(window) != 0 { break; }
        }
        frames += 1;
        if frames == 1 || frames % 60 == 0 {
            log::info!("goke bridge displayed {frames} frames at {width}x{height}");
        }
    }
    unsafe { ANativeWindow_release(window); }
    log::info!("goke bridge render thread ended after {frames} frames");
}
