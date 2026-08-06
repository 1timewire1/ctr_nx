/* Unity 2022.3.67f2 entry points used by Angry Birds Journey 3.8.4. */
#ifndef JOURNEY_UNITY_ENTRYPOINTS_H
#define JOURNEY_UNITY_ENTRYPOINTS_H

#include <stdint.h>
#include "so_util.h"

#define OFF_JNI_OnLoad                     0x72b104
#define OFF_initJni                        0x72a300
#define OFF_nativeDone                     0x72a30c
#define OFF_nativeResume                   0x72a400
#define OFF_nativeApplicationUnload        0x72a490
#define OFF_nativeFocusChanged             0x72a4e0
#define OFF_nativeRecreateGfxState         0x72a534
#define OFF_nativeSendSurfaceChangedEvent  0x72a59c
#define OFF_nativeRender                   0x72a5f4
#define OFF_nativeInjectEvent              0x72a654

#define OFF_TimeManager_Update_entry       0x56fd30
#define OFF_TimeManager_Update_body        0x56fd54
#define OFF_TM_frameCount_u64               0x00c8
#define OFF_TM_aux_u32                      0x00d0
#define OFF_TM_startupRef_double            0x00e8
#define OFF_TM_pause_u8                     0x00f8

#define OFF_Choreographer_wait_site         0x721f28
#define JOURNEY_CHOREO_FROM                 0xb50000a8u
#define JOURNEY_CHOREO_TO                   0x14000005u
#define OFF_WaitVSync_wait_site             0x71f550
#define JOURNEY_WAITVSYNC_FROM              0x540000aau
#define JOURNEY_WAITVSYNC_TO                0x14000005u

typedef void    (*fn_initJni)(void *, void *, void *);
typedef void    (*fn_gfxstate)(void *, void *, int32_t, void *);
typedef void    (*fn_v)(void *, void *);
typedef uint8_t (*fn_z)(void *, void *);
typedef void    (*fn_vz)(void *, void *, int32_t);
typedef uint8_t (*fn_inject)(void *, void *, void *, int32_t);

#define UNITY_RESOLVE(mod, off) ((void *)((uintptr_t)(mod).load_virtbase + (off)))

#endif
