// Minimal OpenMAX IL 1.1 ABI subset for the goke direct-OMX experiment.
// Hand-rolled to match the standard OMX Core.h/IL struct layouts (32-bit ARM).
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint32_t OMX_U32;
typedef int32_t OMX_S32;
typedef uint16_t OMX_U16;
typedef int16_t OMX_S16;
typedef uint8_t OMX_U8;
typedef int8_t OMX_S8;
typedef unsigned char OMX_BOOL;
typedef void *OMX_HANDLETYPE;
typedef void *OMX_PTR;
typedef char *OMX_STRING;
typedef long long OMX_TICKS;
typedef int OMX_ERRORTYPE;
typedef uint32_t OMX_INDEXTYPE;
typedef uint32_t OMX_COMMANDTYPE;
typedef uint32_t OMX_STATETYPE;
typedef uint32_t OMX_EVENTTYPE;

#define OMX_TRUE 1
#define OMX_FALSE 0

typedef union OMX_VERSIONTYPE {
    OMX_U32 nVersion;
    struct { OMX_U8 nVersionMajor, nVersionMinor, nRevision, nStep; } s;
} OMX_VERSIONTYPE;

typedef enum {
    OMX_ErrorNone = 0,
    OMX_ErrorInsufficientResources = 1,
    OMX_ErrorUndefined = (OMX_S32)0x80001000,
    OMX_ErrorInvalidComponentName = (OMX_S32)0x80001001,
    OMX_ErrorNotImplemented = (OMX_S32)0x80001006,
    OMX_ErrorUnsupportedIndex = (OMX_S32)0x8000101A,
    OMX_ErrorBadParameter = (OMX_S32)0x80001021,
} OMX_ERR_BASE;

typedef enum {
    OMX_CommandStateSet = 0,
    OMX_CommandFlush,
    OMX_CommandMarkBuffer,
    OMX_CommandPortDisable,
    OMX_CommandPortEnable,
} OMX_CMD;

typedef enum {
    OMX_StateInvalid = 0,
    OMX_StateLoaded = 1,
    OMX_StateIdle = 2,
    OMX_StateExecuting = 3,
    OMX_StatePause = 4,
    OMX_StateWaitForResources = 5,
} OMX_STATE;

// buffer flags
#define OMX_BUFFERFLAG_EOS 0x00000001
#define OMX_BUFFERFLAG_DECODEONLY 0x00000004
#define OMX_BUFFERFLAG_ENDOFFRAME 0x00000010
#define OMX_BUFFERFLAG_SYNCFRAME 0x00000020
#define OMX_BUFFERFLAG_CODECCONFIG 0x00000040

// standard param indexes
#define OMX_IndexParamPortDefinition 0x03000001
#define OMX_IndexParamStandardComponentRole 0x01000017
// event
#define OMX_EventCmdComplete 0
#define OMX_EventError 1
#define OMX_EventPortSettingsChanged 3

typedef struct OMX_BUFFERHEADERTYPE {
    OMX_U32 nSize;              // 0
    OMX_VERSIONTYPE nVersion;   // 4
    OMX_U8 *pBuffer;            // 8
    OMX_U32 nAllocLen;          // 12
    OMX_U32 nFilledLen;         // 16
    OMX_U32 nOffset;            // 20
    OMX_PTR pAppPrivate;        // 24
    OMX_PTR pPlatformPrivate;   // 28
    OMX_PTR pInputPortPrivate;  // 32
    OMX_PTR pOutputPortPrivate; // 36
    OMX_HANDLETYPE hMarkTargetComponent; // 40
    OMX_PTR pMarkData;          // 44
    OMX_U32 nTickCount;         // 48
    OMX_TICKS nTimeStamp;       // 56 (aligned)
    OMX_U32 nFlags;             // 64
    OMX_U32 nOutputPortIndex;   // 68
    OMX_U32 nInputPortIndex;    // 72
} OMX_BUFFERHEADERTYPE;

typedef struct OMX_PARAM_PORTDEFINITIONTYPE {
    OMX_U32 nSize;              // 0
    OMX_VERSIONTYPE nVersion;   // 4
    OMX_U32 nPortIndex;         // 8
    OMX_BOOL bEnabled;          // 12
    OMX_BOOL bPopulated;        // 16
    OMX_U32 eDomain;            // 20 (1 = video)
    struct {                    // OMX_VIDEO_PORTDEFINITIONTYPE
        OMX_U32 nFrameWidth;            // 24
        OMX_U32 nFrameHeight;           // 28
        OMX_S32 nStride;                // 32
        OMX_U32 nSliceHeight;           // 36
        OMX_U32 eCompressionFormat;     // 40
        OMX_U32 eColorFormat;           // 44
        OMX_U32 xFramerate;             // 48 (Q16)
        OMX_BOOL bFlagErrorConcealment; // 52
    } video;                    // 24..56
    OMX_U32 nBufferCountActual; // 56
    OMX_U32 nBufferCountMin;    // 60
    OMX_U32 nBufferSize;        // 64
    OMX_BOOL bBuffersContiguous;// 68
} OMX_PARAM_PORTDEFINITIONTYPE; // 72 (OMX IL 1.1.1 — no nBufferAlignment)

typedef struct OMX_PARAM_COMPONENTROLETYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U8 cRole[128];
} OMX_PARAM_COMPONENTROLETYPE;

typedef OMX_ERRORTYPE (*OMXEventHandler)(OMX_HANDLETYPE, OMX_PTR, OMX_EVENTTYPE, OMX_U32, OMX_U32, OMX_PTR);
typedef OMX_ERRORTYPE (*OMXBufferDone)(OMX_HANDLETYPE, OMX_PTR, OMX_BUFFERHEADERTYPE *);
typedef struct OMX_CALLBACKTYPE {
    OMXEventHandler EventHandler;
    OMXBufferDone EmptyBufferDone;
    OMXBufferDone FullBufferDone;
} OMX_CALLBACKTYPE;

// core API (dlsym'ed from libOMX_Core.so)
typedef OMX_ERRORTYPE (*fn_OMX_Init)(void);
typedef OMX_ERRORTYPE (*fn_OMX_Deinit)(void);
typedef OMX_ERRORTYPE (*fn_OMX_GetHandle)(OMX_HANDLETYPE *, OMX_STRING, OMX_PTR, OMX_CALLBACKTYPE *);
typedef OMX_ERRORTYPE (*fn_OMX_FreeHandle)(OMX_HANDLETYPE);

typedef OMX_ERRORTYPE (*fn_OMX_SendCommand)(OMX_HANDLETYPE, OMX_COMMANDTYPE, OMX_U32, OMX_PTR);
typedef OMX_ERRORTYPE (*fn_OMX_GetParameter)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR);
typedef OMX_ERRORTYPE (*fn_OMX_SetParameter)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR);
typedef OMX_ERRORTYPE (*fn_OMX_GetExtensionIndex)(OMX_HANDLETYPE, OMX_STRING, OMX_INDEXTYPE *);
typedef OMX_ERRORTYPE (*fn_OMX_AllocateBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE **, OMX_U32, OMX_PTR, OMX_U32);
typedef OMX_ERRORTYPE (*fn_OMX_EmptyThisBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE *);
typedef OMX_ERRORTYPE (*fn_OMX_FillThisBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE *);
typedef OMX_ERRORTYPE (*fn_OMX_GetState)(OMX_HANDLETYPE, OMX_STATETYPE *);

// component function table (standard OMX_COMPONENTTYPE layout, IL1.1)
typedef struct OMX_TUNNELSETUPTYPE OMX_TUNNELSETUPTYPE;
typedef struct OMX_COMPONENTTYPE {
    OMX_U32 nSize;                  // 0
    OMX_VERSIONTYPE nVersion;       // 4
    OMX_ERRORTYPE (*GetComponentVersion)(OMX_HANDLETYPE, OMX_STRING, OMX_VERSIONTYPE *, OMX_VERSIONTYPE *, OMX_VERSIONTYPE *); // 8
    OMX_ERRORTYPE (*SendCommand)(OMX_HANDLETYPE, OMX_COMMANDTYPE, OMX_U32, OMX_PTR); // 12
    OMX_ERRORTYPE (*GetParameter)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR); // 16
    OMX_ERRORTYPE (*SetParameter)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR); // 20
    OMX_ERRORTYPE (*GetConfig)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR); // 24
    OMX_ERRORTYPE (*SetConfig)(OMX_HANDLETYPE, OMX_INDEXTYPE, OMX_PTR); // 28
    OMX_ERRORTYPE (*GetExtensionIndex)(OMX_HANDLETYPE, OMX_STRING, OMX_INDEXTYPE *); // 32
    OMX_ERRORTYPE (*GetState)(OMX_HANDLETYPE, OMX_STATETYPE *); // 36
    OMX_ERRORTYPE (*BufferTunnelRequest)(OMX_HANDLETYPE, OMX_U32, OMX_TUNNELSETUPTYPE *); // 40
    OMX_ERRORTYPE (*SetCallbacks)(OMX_HANDLETYPE, OMX_CALLBACKTYPE *, OMX_PTR); // 44
    OMX_ERRORTYPE (*AllocateBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE **, OMX_U32, OMX_PTR, OMX_U32); // 48
    OMX_ERRORTYPE (*UseBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE **, OMX_U32, OMX_PTR, OMX_U32, OMX_U8 *); // 52
    OMX_ERRORTYPE (*FreeBuffer)(OMX_HANDLETYPE, OMX_U32, OMX_BUFFERHEADERTYPE *); // 56
    OMX_ERRORTYPE (*EmptyThisBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE *); // 60
    OMX_ERRORTYPE (*FillThisBuffer)(OMX_HANDLETYPE, OMX_BUFFERHEADERTYPE *); // 64
} OMX_COMPONENTTYPE;

#define OMX_VIDEO_CodingAVC 4
#define OMX_VIDEO_CodingHEVC 11
