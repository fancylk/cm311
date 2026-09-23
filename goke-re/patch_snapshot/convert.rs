#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]
#![allow(improper_ctypes)]
#![allow(dead_code)]

include!(concat!(env!("OUT_DIR"), "/yuv_ffi.rs"));

#[cfg(not(target_os = "ios"))]
use crate::PixelBuffer;
use crate::{generate_call_macro, EncodeYuvFormat, TraitPixelBuffer};

fn src_width_check<T: TraitPixelBuffer>(captured: &T, w: usize, h: usize) -> bool {
    captured.width() <= w && captured.height() <= h
}

fn fit_dims(w: usize, h: usize, max_w: usize, max_h: usize) -> (usize, usize) {
    let mut dw = max_w;
    let mut dh = ((h as u64 * max_w as u64 + w as u64 / 2) / w as u64) as usize;
    if dh > max_h {
        dh = max_h;
        dw = ((w as u64 * max_h as u64 + h as u64 / 2) / h as u64) as usize;
    }
    (dw & !1, dh & !1)
}
use hbb_common::{bail, log, ResultType};

generate_call_macro!(call_yuv, false);

#[cfg(not(target_os = "ios"))]
pub fn convert_to_yuv<T: TraitPixelBuffer>(
    captured: &T,
    dst_fmt: EncodeYuvFormat,
    dst: &mut Vec<u8>,
    mid_data: &mut Vec<u8>,
) -> ResultType<()> {
    // PATCH(mac-1080p): when the captured framebuffer is larger than the
    // encoder (macOS HiDPI: 3840x2160 framebuffer for a 1080p UI), area-
    // downscale into a temporary buffer instead of failing. Keeps OS display
    // settings untouched while halving encode/decode cost on the peers.
    let mut scaled_buffer: Option<crate::common::ScaledPixelBuffer> = None;
    let captured_dyn: &dyn TraitPixelBuffer = if src_width_check(captured, dst_fmt.w, dst_fmt.h) {
        captured
    } else {
        let (dw, dh) = fit_dims(captured.width(), captured.height(), dst_fmt.w, dst_fmt.h);
        log::info!(
            "capture {}x{} > encoder {}x{}, downscaling",
            captured.width(),
            captured.height(),
            dst_fmt.w,
            dst_fmt.h
        );
        scaled_buffer = Some(crate::common::ScaledPixelBuffer::downscale_from(captured, dw, dh));
        scaled_buffer.as_ref().unwrap()
    };
    let src = captured_dyn.data();
    let src_stride = captured_dyn.stride();
    let src_pixfmt = captured_dyn.pixfmt();
    let src_width = captured_dyn.width();
    let src_height = captured_dyn.height();
    if src_pixfmt == crate::Pixfmt::BGRA
        || src_pixfmt == crate::Pixfmt::RGBA
        || src_pixfmt == crate::Pixfmt::RGB565LE
    {
        // stride is calculated, not real, so we need to check it
        if src_stride[0] < src_width * src_pixfmt.bytes_per_pixel() {
            bail!(
                "src_stride too small: {} < {}",
                src_stride[0],
                src_width * src_pixfmt.bytes_per_pixel()
            );
        }
        if src.len() < src_stride[0] * src_height {
            bail!(
                "wrong src len, {} < {} * {}",
                src.len(),
                src_stride[0],
                src_height
            );
        }
    }
    let align = |x: usize| (x + 63) / 64 * 64;
    let unsupported = format!(
        "unsupported pixfmt conversion: {src_pixfmt:?} -> {:?}",
        dst_fmt.pixfmt
    );

    match (src_pixfmt, dst_fmt.pixfmt) {
        (crate::Pixfmt::BGRA, crate::Pixfmt::I420)
        | (crate::Pixfmt::RGBA, crate::Pixfmt::I420)
        | (crate::Pixfmt::RGB565LE, crate::Pixfmt::I420) => {
            let dst_stride_y = dst_fmt.stride[0];
            let dst_stride_uv = dst_fmt.stride[1];
            dst.resize(dst_fmt.h * dst_stride_y * 2, 0); // waste some memory to ensure memory safety
            let dst_y = dst.as_mut_ptr();
            let dst_u = dst[dst_fmt.u..].as_mut_ptr();
            let dst_v = dst[dst_fmt.v..].as_mut_ptr();
            let f = match src_pixfmt {
                crate::Pixfmt::BGRA => ARGBToI420,
                crate::Pixfmt::RGBA => ABGRToI420,
                crate::Pixfmt::RGB565LE => RGB565ToI420,
                _ => bail!(unsupported),
            };
            call_yuv!(f(
                src.as_ptr(),
                src_stride[0] as _,
                dst_y,
                dst_stride_y as _,
                dst_u,
                dst_stride_uv as _,
                dst_v,
                dst_stride_uv as _,
                src_width as _,
                src_height as _,
            ));
        }
        (crate::Pixfmt::BGRA, crate::Pixfmt::NV12)
        | (crate::Pixfmt::RGBA, crate::Pixfmt::NV12)
        | (crate::Pixfmt::RGB565LE, crate::Pixfmt::NV12) => {
            let dst_stride_y = dst_fmt.stride[0];
            let dst_stride_uv = dst_fmt.stride[1];
            dst.resize(
                align(dst_fmt.h) * (align(dst_stride_y) + align(dst_stride_uv / 2)),
                0,
            );
            let dst_y = dst.as_mut_ptr();
            let dst_uv = dst[dst_fmt.u..].as_mut_ptr();
            let (input, input_stride) = match src_pixfmt {
                crate::Pixfmt::BGRA => (src.as_ptr(), src_stride[0]),
                crate::Pixfmt::RGBA => (src.as_ptr(), src_stride[0]),
                crate::Pixfmt::RGB565LE => {
                    let mid_stride = src_width * 4;
                    mid_data.resize(mid_stride * src_height, 0);
                    call_yuv!(RGB565ToARGB(
                        src.as_ptr(),
                        src_stride[0] as _,
                        mid_data.as_mut_ptr(),
                        mid_stride as _,
                        src_width as _,
                        src_height as _,
                    ));
                    (mid_data.as_ptr(), mid_stride)
                }
                _ => bail!(unsupported),
            };
            let f = match src_pixfmt {
                crate::Pixfmt::BGRA => ARGBToNV12,
                crate::Pixfmt::RGBA => ABGRToNV12,
                crate::Pixfmt::RGB565LE => ARGBToNV12,
                _ => bail!(unsupported),
            };
            call_yuv!(f(
                input,
                input_stride as _,
                dst_y,
                dst_stride_y as _,
                dst_uv,
                dst_stride_uv as _,
                src_width as _,
                src_height as _,
            ));
        }
        (crate::Pixfmt::BGRA, crate::Pixfmt::I444)
        | (crate::Pixfmt::RGBA, crate::Pixfmt::I444)
        | (crate::Pixfmt::RGB565LE, crate::Pixfmt::I444) => {
            let dst_stride_y = dst_fmt.stride[0];
            let dst_stride_u = dst_fmt.stride[1];
            let dst_stride_v = dst_fmt.stride[2];
            dst.resize(
                align(dst_fmt.h)
                    * (align(dst_stride_y) + align(dst_stride_u) + align(dst_stride_v)),
                0,
            );
            let dst_y = dst.as_mut_ptr();
            let dst_u = dst[dst_fmt.u..].as_mut_ptr();
            let dst_v = dst[dst_fmt.v..].as_mut_ptr();
            let (input, input_stride) = match src_pixfmt {
                crate::Pixfmt::BGRA => (src.as_ptr(), src_stride[0]),
                crate::Pixfmt::RGBA => {
                    mid_data.resize(src.len(), 0);
                    call_yuv!(ABGRToARGB(
                        src.as_ptr(),
                        src_stride[0] as _,
                        mid_data.as_mut_ptr(),
                        src_stride[0] as _,
                        src_width as _,
                        src_height as _,
                    ));
                    (mid_data.as_ptr(), src_stride[0])
                }
                crate::Pixfmt::RGB565LE => {
                    let mid_stride = src_width * 4;
                    mid_data.resize(mid_stride * src_height, 0);
                    call_yuv!(RGB565ToARGB(
                        src.as_ptr(),
                        src_stride[0] as _,
                        mid_data.as_mut_ptr(),
                        mid_stride as _,
                        src_width as _,
                        src_height as _,
                    ));
                    (mid_data.as_ptr(), mid_stride)
                }
                _ => bail!(unsupported),
            };

            call_yuv!(ARGBToI444(
                input,
                input_stride as _,
                dst_y,
                dst_stride_y as _,
                dst_u,
                dst_stride_u as _,
                dst_v,
                dst_stride_v as _,
                src_width as _,
                src_height as _,
            ));
        }
        _ => {
            bail!(unsupported);
        }
    }
    Ok(())
}

#[cfg(not(target_os = "ios"))]
pub fn convert(captured: &PixelBuffer, pixfmt: crate::Pixfmt, dst: &mut Vec<u8>) -> ResultType<()> {
    if captured.pixfmt() == pixfmt {
        dst.extend_from_slice(captured.data());
        return Ok(());
    }

    let src = captured.data();
    let src_stride = captured.stride();
    let src_pixfmt = captured.pixfmt();
    let src_width = captured.width();
    let src_height = captured.height();

    let unsupported = format!(
        "unsupported pixfmt conversion: {src_pixfmt:?} -> {:?}",
        pixfmt
    );

    match (src_pixfmt, pixfmt) {
        (crate::Pixfmt::BGRA, crate::Pixfmt::RGBA) | (crate::Pixfmt::RGBA, crate::Pixfmt::BGRA) => {
            dst.resize(src.len(), 0);
            call_yuv!(ABGRToARGB(
                src.as_ptr(),
                src_stride[0] as _,
                dst.as_mut_ptr(),
                src_stride[0] as _,
                src_width as _,
                src_height as _,
            ));
        }
        _ => {
            bail!(unsupported);
        }
    }
    Ok(())
}
