// goke vdec CSD feeding experiment: NDK MediaCodec, buffer mode.
// usage: vdec_test <video/avc|video/hevc> <A|B|C> <params.bin> <frames.bin> <index.bin>
//   A: params as CODEC_CONFIG, then all AUs as normal frames
//   B: entire first AU as CODEC_CONFIG (rustdesk current behavior), rest normal
//   C: no CODEC_CONFIG at all, everything as normal frames
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/log.h>
#include <time.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "vdectest", __VA_ARGS__)

static unsigned char *read_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { LOG("cannot open %s", path); return NULL; }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(*len + 1);
    fread(buf, 1, *len, f); fclose(f);
    return buf;
}

static long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv) {
    if (argc < 6) { LOG("usage: %s <mime> <A|B|C|D> <params> <frames> <index>", argv[0]); return 1; }
    const char *mime = argv[1];
    char variant = argv[2][0];
    long plen, flen, ilen;
    unsigned char *params = read_file(argv[3], &plen);
    unsigned char *frames = read_file(argv[4], &flen);
    unsigned char *index = read_file(argv[5], &ilen);
    if (!params || !frames || !index) return 1;
    int naus = ilen / 4;
    LOG("mime=%s variant=%c params=%ldB naus=%d frames=%ldB", mime, variant, plen, naus, flen);

    AMediaCodec *codec = AMediaCodec_createDecoderByType(mime);
    if (!codec) { LOG("createDecoderByType failed"); return 1; }
    AMediaFormat *fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, 1280);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, 720);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, 19); // YUV420Planar
    if (argc > 6) {
        const char *fm = argv[6];
        if (!strcmp(fm, "s1")) { AMediaFormat_setString(fmt, "fast-output-mode", "1"); LOG("vendor key fast-output-mode=\"1\" (string)"); }
        else if (!strcmp(fm, "i1")) { AMediaFormat_setInt32(fmt, "fast-output-mode", 1); LOG("vendor key fast-output-mode=1 (int32)"); }
        else if (!strcmp(fm, "st")) { AMediaFormat_setString(fmt, "fast-output-mode", "true"); LOG("vendor key fast-output-mode=\"true\" (string)"); }
        else if (!strcmp(fm, "gk")) { AMediaFormat_setString(fmt, "vendor.app.compatibility.enhance", "true"); LOG("vendor key vendor.app.compatibility.enhance=true"); }
        else if (!strcmp(fm, "omxi")) { AMediaFormat_setInt32(fmt, "OMX.Goke.Param.Index.FastOutputMode", 1); LOG("vendor key OMX.Goke.Param.Index.FastOutputMode=1 (int32)"); }
        else if (!strcmp(fm, "omxs")) { AMediaFormat_setString(fmt, "OMX.Goke.Param.Index.FastOutputMode", "1"); LOG("vendor key OMX.Goke.Param.Index.FastOutputMode=\"1\" (string)"); }
        else if (!strcmp(fm, "vf")) { AMediaFormat_setInt32(fmt, "vendor.goke.fast-output-mode", 1); LOG("vendor key vendor.goke.fast-output-mode=1 (int32)"); }
        else if (!strcmp(fm, "vfs")) { AMediaFormat_setString(fmt, "vendor.goke.fast-output-mode", "true"); LOG("vendor key vendor.goke.fast-output-mode=true (string)"); }
    }
    media_status_t st = AMediaCodec_configure(codec, fmt, NULL, NULL, 0);
    LOG("configure: %d", st);
    if (st != 0) { AMediaFormat_delete(fmt); return 1; }
    st = AMediaCodec_start(codec);
    LOG("start: %d", st);
    if (st != 0) return 1;

    long t0 = now_ms();
    int in_au = 0;        // which AU we are feeding
    int fed_csd = 0;
    int out_count = 0;
    int in_eos = 0;
    long first_out_ms = -1;
    unsigned long checksum = 0;

    while (!in_eos || out_count == 0) {
        if (now_ms() - t0 > 8000) { LOG("timeout"); break; }
        // feed input
        if (!in_eos) {
            ssize_t ibuf = AMediaCodec_dequeueInputBuffer(codec, 20000);
            if (ibuf >= 0) {
                size_t cap;
                uint8_t *buf = AMediaCodec_getInputBuffer(codec, ibuf, &cap);
                const unsigned char *data = NULL;
                size_t dlen = 0;
                int flags = 0;
                if (variant == 'D' && in_au == 0 && fed_csd == 0) {
                    long off = 0, alen = 0;
                    long l; memcpy(&l, index, 4); alen = l;
                    data = frames; dlen = alen; flags = 2; fed_csd = 1;
                    LOG("queue AU0 as CSD %zu B (flags=2)", dlen);
                } else if (variant == 'R' && !fed_csd) {
                    data = params; dlen = plen; flags = 2; fed_csd = 1;
                    LOG("queue CSD params %zu B (flags=2)", dlen);
                } else if (variant == 'A' && !fed_csd) {
                    data = params; dlen = plen; flags = 2; fed_csd = 1;
                    LOG("queue CSD params %zu B (flags=2)", dlen);
                } else if (variant == 'B' && in_au == 0 && fed_csd == 0) {
                    long off = 0, alen = 0;
                    long l; memcpy(&l, index, 4); alen = l;
                    data = frames; dlen = alen; flags = 1; fed_csd = 1;
                    LOG("queue AU0 as CSD %zu B (flags=1)", dlen);
                } else {
                    int au = in_au;
                    if (variant == 'B') au = in_au + 1; // AU0 was consumed as CSD(flag=1)
                    if (variant == 'D') au = in_au;     // AU0 fully fed again as normal frame
                    if (au >= naus) {
                        if (variant == 'R') { in_eos = 1; LOG("all AUs queued, no EOS"); continue; }
                        flags = 4; dlen = 0; in_eos = 1; // EOS
                        LOG("queue EOS");
                    } else {
                        long off = 0;
                        for (int k = 0; k < au; k++) { long l; memcpy(&l, index + off, 4); off += 4 + l; }
                        long l; memcpy(&l, index + off, 4);
                        data = frames + off + 4; dlen = l;
                    }
                }
                if (dlen > cap) { LOG("input too big %zu>%zu", dlen, cap); return 1; }
                if (dlen) memcpy(buf, data, dlen);
                long ts = in_eos ? 0 : (1000000L * in_au / 30);
                AMediaCodec_queueInputBuffer(codec, ibuf, 0, dlen, ts, flags);
                if (!in_eos) { in_au++; if (variant == 'R') usleep(33000); }
            }
        }
        // drain output
        AMediaCodecBufferInfo info;
        ssize_t obuf = AMediaCodec_dequeueOutputBuffer(codec, &info, 20000);
        if (obuf >= 0) {
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                LOG("output codec-config buffer %u B", info.size);
            } else {
                if (first_out_ms < 0) { first_out_ms = now_ms() - t0; LOG("first frame at %ld ms", first_out_ms); }
                if (out_count == 0) {
                    AMediaFormat *of = AMediaCodec_getOutputFormat(codec);
                    int32_t w = 0, h = 0, cf = 0, stride = 0, sh = 0;
                    AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_WIDTH, &w);
                    AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_HEIGHT, &h);
                    AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_COLOR_FORMAT, &cf);
                    AMediaFormat_getInt32(of, "stride", &stride);
                    AMediaFormat_getInt32(of, "slice-height", &sh);
                    LOG("output format: %dx%d color=%d stride=%d slice=%d", w, h, cf, stride, sh);
                    AMediaFormat_delete(of);
                }
                if (obuf > 0 && info.size > 0) {
                    size_t cap; uint8_t *b = AMediaCodec_getOutputBuffer(codec, obuf, &cap);
                    if (b) for (int i = 0; i < 64 && i < info.size; i++) checksum += b[i];
                }
                out_count++;
            }
            AMediaCodec_releaseOutputBuffer(codec, obuf, false);
        } else if (obuf == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *of = AMediaCodec_getOutputFormat(codec);
            int32_t w = 0, h = 0, cf = 0, stride = 0;
            AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_WIDTH, &w);
            AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_HEIGHT, &h);
            AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_COLOR_FORMAT, &cf);
            AMediaFormat_getInt32(of, "stride", &stride);
            LOG("FORMAT_CHANGED: %dx%d color=%d stride=%d", w, h, cf, stride);
            AMediaFormat_delete(of);
        }
    }
    LOG("RESULT variant=%c outputs=%d checksum=%lu first_out=%ldms", variant, out_count, checksum, first_out_ms);
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaFormat_delete(fmt);
    return 0;
}
