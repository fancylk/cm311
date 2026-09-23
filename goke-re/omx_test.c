// Direct-OMX goke decoder experiment (root, buffer mode).
// usage: omx_test <video/hevc|video/avc> <params.bin> <frames.bin> <index.bin> <attrs 0|1>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include "omx_mini.h"

#define LOG(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); fflush(stderr); } while (0)

static unsigned char *read_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) { LOG("cannot open %s", path); exit(1); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(*len);
    if (fread(buf, 1, *len, f) != (size_t)*len) exit(1);
    fclose(f);
    return buf;
}
static long now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000 + ts.tv_nsec / 1000000; }

#define MAXQ 64
static OMX_BUFFERHEADERTYPE *out_q[MAXQ];
static int out_qn = 0;
static OMX_BUFFERHEADERTYPE *in_q[MAXQ];
static int in_qn = 0;
static pthread_mutex_t q_mtx = PTHREAD_MUTEX_INITIALIZER;

static OMX_ERRORTYPE event_handler(OMX_HANDLETYPE h, OMX_PTR app, OMX_EVENTTYPE e, OMX_U32 d1, OMX_U32 d2, OMX_PTR ed) {
    (void)h; (void)app; (void)ed;
    if (e == OMX_EventError) LOG("!! OMX_EventError 0x%lx state-ev %ld", (unsigned long)d1, (long)(int32_t)d2);
    else if (e == OMX_EventCmdComplete) LOG("cmd complete type %lu d2 %lu", (unsigned long)d1, (unsigned long)d2);
    else if (e == OMX_EventPortSettingsChanged) LOG("port settings changed port %lu", (unsigned long)d1);
    else LOG("event %d", (int)e);
    return OMX_ErrorNone;
}
static OMX_ERRORTYPE empty_done(OMX_HANDLETYPE h, OMX_PTR app, OMX_BUFFERHEADERTYPE *b) {
    (void)h; (void)app;
    pthread_mutex_lock(&q_mtx);
    if (in_qn < MAXQ) in_q[in_qn++] = b;
    pthread_mutex_unlock(&q_mtx);
    return OMX_ErrorNone;
}
static OMX_ERRORTYPE fill_done(OMX_HANDLETYPE h, OMX_PTR app, OMX_BUFFERHEADERTYPE *b) {
    (void)h; (void)app;
    pthread_mutex_lock(&q_mtx);
    if (out_qn < MAXQ) out_q[out_qn++] = b;
    pthread_mutex_unlock(&q_mtx);
    return OMX_ErrorNone;
}

int main(int argc, char **argv) {
    if (argc < 6) { LOG("usage: %s mime params frames index attrs01", argv[0]); return 1; }
    const char *mime = argv[1];
    long plen, flen, ilen;
    unsigned char *params = read_file(argv[2], &plen);
    unsigned char *frames = read_file(argv[3], &flen);
    unsigned char *index = read_file(argv[4], &ilen);
    int use_attrs = atoi(argv[5]);
    int naus = ilen / 4;
    int hevc = strstr(mime, "hevc") != NULL;

    void *core = dlopen("/vendor/lib/libOMX_Core.so", RTLD_NOW);
    if (!core) { LOG("dlopen failed: %s", dlerror()); return 1; }
    fn_OMX_Init omx_init = dlsym(core, "OMX_Init");
    fn_OMX_Deinit omx_deinit = dlsym(core, "OMX_Deinit");
    fn_OMX_GetHandle get_handle = dlsym(core, "OMX_GetHandle");
    fn_OMX_FreeHandle free_handle = dlsym(core, "OMX_FreeHandle");
    if (!get_handle || !omx_init) { LOG("dlsym failed %p %p", get_handle, omx_init); return 1; }
    omx_init();
    LOG("core ok");

    OMX_CALLBACKTYPE cbs = {event_handler, empty_done, fill_done};
    OMX_HANDLETYPE h = NULL;
    OMX_ERRORTYPE err = get_handle(&h, (OMX_STRING)"OMX.goke.video.decoder", NULL, &cbs);
    LOG("get_handle: 0x%x", err);
    if (err || !h) return 1;
    OMX_COMPONENTTYPE *c = (OMX_COMPONENTTYPE *)h;
    LOG("component nSize %u ver %x", c->nSize, c->nVersion.nVersion);
    fn_OMX_SendCommand send_cmd = c->SendCommand;
    fn_OMX_GetParameter get_param = c->GetParameter;
    fn_OMX_SetParameter set_param = c->SetParameter;
    fn_OMX_GetExtensionIndex get_ext = c->GetExtensionIndex;
    fn_OMX_AllocateBuffer alloc_buf = c->AllocateBuffer;
    fn_OMX_EmptyThisBuffer etb = c->EmptyThisBuffer;
    fn_OMX_FillThisBuffer ftb = c->FillThisBuffer;
    fn_OMX_GetState get_state = c->GetState;

    OMX_PARAM_COMPONENTROLETYPE role;
    memset(&role, 0, sizeof(role));
    role.nSize = sizeof(role); role.nVersion.s.nVersionMajor = 1; role.nVersion.s.nVersionMinor = 1;
    strcpy((char *)role.cRole, hevc ? "video_decoder.hevc" : "video_decoder.avc");
    err = set_param(h, 0x01000017, &role);
    LOG("set role %s: 0x%x", role.cRole, err);

    // probe: GET with generous buffer to discover the component's native struct
    {
        unsigned char big[256];
        memset(big, 0, sizeof(big));
        OMX_PARAM_PORTDEFINITIONTYPE *p = (OMX_PARAM_PORTDEFINITIONTYPE *)big;
        p->nSize = 256; p->nVersion.s.nVersionMajor = 1; p->nVersion.s.nVersionMinor = 1; p->nPortIndex = 0;
        err = get_param(h, OMX_IndexParamPortDefinition, p);
        LOG("probe get 0x%x nSize-now %u domain %u w %u h %u cnt %u bufsize %u", err, p->nSize, p->eDomain,
            p->video.nFrameWidth, p->video.nFrameHeight, p->nBufferCountMin, p->nBufferSize);
        LOG("raw words: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
            ((unsigned int*)big)[0],((unsigned int*)big)[1],((unsigned int*)big)[2],((unsigned int*)big)[3],
            ((unsigned int*)big)[4],((unsigned int*)big)[5],((unsigned int*)big)[6],((unsigned int*)big)[7],
            ((unsigned int*)big)[8],((unsigned int*)big)[9],((unsigned int*)big)[10],((unsigned int*)big)[11],
            ((unsigned int*)big)[12],((unsigned int*)big)[13],((unsigned int*)big)[14],((unsigned int*)big)[15],
            ((unsigned int*)big)[16],((unsigned int*)big)[17]);
    }
    OMX_U32 in_size = 5898240, out_size = 5898240;
    OMX_U32 in_cnt = 8, out_cnt = 8;
    // goke uses a trimmed PortDefinition (68 bytes): video struct without
    // bFlagErrorConcealment + counts directly after xFramerate.
    struct GkPortDef {
        OMX_U32 nSize;          // 0
        OMX_VERSIONTYPE nVersion; // 4
        OMX_U32 nPortIndex;     // 8
        OMX_BOOL bEnabled;      // 12
        OMX_BOOL bPopulated;    // 16
        OMX_U32 eDomain;        // 20
        struct {                // 24
            OMX_U32 nFrameWidth, nFrameHeight;   // 24,28
            OMX_S32 nStride;                     // 32
            OMX_U32 nSliceHeight;                // 36
            OMX_U32 eCompressionFormat;          // 40
            OMX_U32 eColorFormat;                // 44
            OMX_U32 xFramerate;                  // 48
        } video;                // ends 52
        OMX_U32 nBufferCountActual; // 52
        OMX_U32 nBufferCountMin;    // 56
        OMX_U32 nBufferSize;        // 60
        OMX_BOOL bBuffersContiguous;// 64
    } __attribute__((packed_size_check)); 
    typedef struct GkPortDef GkPortDef2;
    for (int port = 0; port <= 1; port++) {
        unsigned char buf[128];
        memset(buf, 0, sizeof(buf));
        GkPortDef2 *p = (GkPortDef2 *)buf;
        p->nSize = 68; p->nVersion.s.nVersionMajor = 1; p->nVersion.s.nVersionMinor = 1; p->nPortIndex = port;
        p->eDomain = 1; // video
        if (port == 0) {
            p->video.nFrameWidth = 1280; p->video.nFrameHeight = 720;
            p->video.eCompressionFormat = hevc ? OMX_VIDEO_CodingHEVC : OMX_VIDEO_CodingAVC;
            p->video.xFramerate = 30 << 16;
            p->video.nStride = 1280; p->video.nSliceHeight = 720;
            p->nBufferSize = 5898240; p->nBufferCountMin = 4; p->nBufferCountActual = 6;
            in_size = p->nBufferSize; in_cnt = p->nBufferCountActual;
        } else {
            p->video.nFrameWidth = 1280; p->video.nFrameHeight = 720;
            p->video.eColorFormat = 19; // YUV420Planar
            p->video.nStride = 1280; p->video.nSliceHeight = 720;
            p->nBufferSize = 5898240; p->nBufferCountMin = 4; p->nBufferCountActual = 6;
            out_size = p->nBufferSize; out_cnt = p->nBufferCountActual;
        }
        err = set_param(h, OMX_IndexParamPortDefinition, p);
        LOG("port %d set 0x%x w %u h %u", port, err, p->video.nFrameWidth, p->video.nFrameHeight);
    }

    if (use_attrs) {
        OMX_U32 attr[10]; // nSize, nVersion, +8 fields
        memset(attr, 0, sizeof(attr));
        attr[0] = sizeof(attr);
        attr[1] = 0x00010101; // 1.1.0.0
        attr[2] = 1; attr[3] = 1; attr[4] = 1; attr[5] = 1;
        attr[6] = 1; attr[7] = 1; attr[8] = 1; attr[9] = 1;
        OMX_INDEXTYPE idx = 0;
        err = get_ext(h, (OMX_STRING)"OMX.Goke.Param.Index.ChannelAttributes", &idx);
        LOG("getext ChannelAttributes: 0x%x idx 0x%x", err, idx);
        if (!err) { err = set_param(h, idx, attr); LOG("set attrs: 0x%x", err); }
    }

    OMX_BUFFERHEADERTYPE *in_b[24], *out_b[24];
    int n_in = 0, n_out = 0;
    LOG("allocating buffers before Idle (vendor order)");
    for (int i = 0; i < (int)in_cnt; i++) {
        if ((err = alloc_buf(h, &in_b[n_in], 0, NULL, in_size))) { LOG("alloc in %d: 0x%x", i, err); return 1; }
        n_in++;
    }
    for (int i = 0; i < (int)out_cnt; i++) {
        if ((err = alloc_buf(h, &out_b[n_out], 1, NULL, out_size))) { LOG("alloc out %d: 0x%x", i, err); return 1; }
        n_out++;
    }
    LOG("allocated in %d out %d", n_in, n_out);
    usleep(200000);
    err = send_cmd(h, OMX_CommandStateSet, OMX_StateIdle, NULL);
    LOG("to idle: 0x%x", err);
    usleep(500000);
    OMX_STATETYPE st = 0; get_state(h, &st);
    LOG("state=%u", st);
    err = send_cmd(h, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    LOG("to exec: 0x%x", err);
    usleep(300000);

    // prime output port
    for (int i = 0; i < n_out; i++) { out_b[i]->nFilledLen = 0; ftb(h, out_b[i]); }
    // feed CSD
    in_b[0]->nFilledLen = plen; in_b[0]->nOffset = 0;
    in_b[0]->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    in_b[0]->nTimeStamp = 0;
    memcpy(in_b[0]->pBuffer, params, plen);
    etb(h, in_b[0]);
    LOG("csd fed %ld", plen);

    long t0 = now_ms();
    int au = 0, out_count = 0, in_flight = 1;
    long first_out = -1;
    while (now_ms() - t0 < 8000) {
        // feed
        while (au < naus && in_flight < n_in) {
            long off = 0;
            for (int k = 0; k < au; k++) { long l; memcpy(&l, index + off, 4); off += 4 + l; }
            long l; memcpy(&l, index + off, 4);
            OMX_BUFFERHEADERTYPE *ib;
            pthread_mutex_lock(&q_mtx);
            if (in_qn > 0) { ib = in_q[--in_qn]; } else { pthread_mutex_unlock(&q_mtx); break; }
            pthread_mutex_unlock(&q_mtx);
            if ((OMX_U32)l > ib->nAllocLen) { au = naus; break; }
            memcpy(ib->pBuffer, frames + off + 4, l);
            ib->nFilledLen = l; ib->nOffset = 0;
            ib->nFlags = OMX_BUFFERFLAG_ENDOFFRAME;
            ib->nTimeStamp = (OMX_TICKS)au * 33333;
            etb(h, ib);
            au++; in_flight++;
            usleep(25000);
        }
        // drain
        pthread_mutex_lock(&q_mtx);
        int qn = out_qn;
        out_qn = 0;
        pthread_mutex_unlock(&q_mtx);
        for (int i = 0; i < qn; i++) {
            OMX_BUFFERHEADERTYPE *b = out_q[i];
            if (first_out < 0) { first_out = now_ms() - t0; LOG("first frame at %ld ms", first_out); }
            out_count++;
            b->nFilledLen = 0;
            ftb(h, b);
        }
        usleep(10000);
    }
    LOG("RESULT attrs=%d outputs=%d first_out=%ldms fed=%d", use_attrs, out_count, first_out, au);
    free_handle(h);
    omx_deinit();
    return 0;
}
