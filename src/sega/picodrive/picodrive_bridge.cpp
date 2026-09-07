/* SNESTICLE_PICODRIVE_BRIDGE
 * Direct PicoDrive core integration for SNESticle/PS2.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "picodrive_bridge.h"
#include "types.h"
#include "emuinput.h"
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"
#include "snio.h"
#include "input.h"

extern "C" {
#include "gs.h"
#include "gpprim.h"
}

extern "C" {
    bool retro_load_game(const void *info);
    void retro_unload_game(void);
    void retro_run(void);
    void retro_reset(void);
    size_t core_retro_serialize_size(void);
    bool retro_serialize(void *data, size_t size);
    bool retro_unserialize(const void *data, size_t size);
    int PicoDriveAurora_GetSRamSize(void);
    uint8_t *PicoDriveAurora_GetSRamPtr(void);
    int PicoDriveAurora_SmdSaveState(void *data, int size);
    void PicoDriveAurora_SmdLoadState(const void *data, int size);
}

static bool s_Initialized = false;
static bool s_GameLoaded = false;
static bool s_DirectReady = false;
static Uint32 s_DirectFrameSerial = 0;
static Uint32 s_DirectUploadSerial = 0;

static int s_PadData[2] = {0, 0};

enum {
    MD_VIDEO_W = 320,
    MD_VIDEO_H = 240,
    MD_AUDIO_MAX = 4096
};

static Uint16 s_VideoBuffer[MD_VIDEO_W * MD_VIDEO_H] __attribute__((aligned(64)));
static Uint32 s_GsPalette[256] __attribute__((aligned(64)));
static Int16 s_AudioBuffer[MD_AUDIO_MAX * 2];
static int s_AudioSamplesPending = 0;

static bool pdPadHas(int port, uint32_t mask) {
    if (port < 0 || port > 1) return false;
    return (s_PadData[port] & mask) != 0;
}
extern "C" void retro_video_refresh_cb(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (!data || width > MD_VIDEO_W || height > MD_VIDEO_H) return;
    const Uint16 *src = (const Uint16 *)data;
    Uint16 *dst = s_VideoBuffer;
    for (unsigned y = 0; y < height; ++y) {
        memcpy(dst + y * MD_VIDEO_W, src + y * (pitch >> 1), width * 2);
    }
    s_DirectReady = true;
    if (++s_DirectFrameSerial == 0) {
        s_DirectFrameSerial = 1;
        s_DirectUploadSerial = 0;
    }
}

extern "C" void retro_audio_sample_batch_cb(const int16_t *data, size_t frames) {
    if (!data || frames == 0) return;
    size_t count = frames * 2;
    if (s_AudioSamplesPending + count > MD_AUDIO_MAX * 2) count = (MD_AUDIO_MAX * 2) - s_AudioSamplesPending;
    if (count > 0) {
        memcpy(s_AudioBuffer + s_AudioSamplesPending, data, count * sizeof(Int16));
        s_AudioSamplesPending += count;
    }
}

extern "C" void retro_audio_sample_cb(int16_t left, int16_t right) {
    int16_t frame[2] = {left, right};
    retro_audio_sample_batch_cb(frame, 1);
}

extern "C" int16_t retro_input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (device != 1 || index != 0 || port > 1) return 0;
    int p = (int)port;
    switch (id) {
        case 0: return pdPadHas(p, SNESIO_JOY_B) ? 1 : 0;
        case 1: return pdPadHas(p, SNESIO_JOY_Y) ? 1 : 0;
        case 2: return pdPadHas(p, SNESIO_JOY_SELECT) ? 1 : 0;
        case 3: return pdPadHas(p, SNESIO_JOY_START) ? 1 : 0;
        case 4: return pdPadHas(p, SNESIO_JOY_UP) ? 1 : 0;
        case 5: return pdPadHas(p, SNESIO_JOY_DOWN) ? 1 : 0;
        case 6: return pdPadHas(p, SNESIO_JOY_LEFT) ? 1 : 0;
        case 7: return pdPadHas(p, SNESIO_JOY_RIGHT) ? 1 : 0;
        case 8: return pdPadHas(p, SNESIO_JOY_A) ? 1 : 0;
        case 9: return pdPadHas(p, SNESIO_JOY_X) ? 1 : 0;
        case 10: return pdPadHas(p, SNESIO_JOY_L) ? 1 : 0;
        case 11: return pdPadHas(p, SNESIO_JOY_R) ? 1 : 0;
        case 12: return pdPadHas(p, SNESIO_JOY_X) ? 1 : 0;
        case 13: return pdPadHas(p, SNESIO_JOY_R) ? 1 : 0;
        case 14: return pdPadHas(p, SNESIO_JOY_SELECT) ? 1 : 0;
        case 15: return pdPadHas(p, SNESIO_JOY_START) ? 1 : 0;
        default: return 0;
    }
}

extern "C" void retro_input_poll_cb(void) {}
static void qResetTransientAudio(void) {
    memset(s_AudioBuffer, 0, sizeof(s_AudioBuffer));
    s_AudioSamplesPending = 0;
}

static void qResetTransient(void) {
    memset(s_VideoBuffer, 0, sizeof(s_VideoBuffer));
    memset(s_GsPalette, 0, sizeof(s_GsPalette));
    qResetTransientAudio();
    s_DirectReady = false;
    s_DirectFrameSerial = 0;
    s_DirectUploadSerial = 0;
    s_PadData[0] = s_PadData[1] = 0;
}

void PicoDriveBridge_InvalidateGsResources(void) {
    s_DirectUploadSerial = 0;
}

bool PicoDriveBridge_CanDirectGsVideo(void) {
    return s_GameLoaded && s_DirectReady && s_DirectFrameSerial != 0;
}

bool PicoDriveBridge_DrawDirectGs(Uint32 auroraOutBaseTBP, Int32 logicalY, Float32 intensity) {
    if (!auroraOutBaseTBP || !PicoDriveBridge_CanDirectGsVideo()) return false;
    Uint32 texTBP = auroraOutBaseTBP + 0x400;
    if (s_DirectUploadSerial != s_DirectFrameSerial) {
        GPPrimUploadTexture((int)texTBP, 320, 0, 0, GS_PSMCT16, s_VideoBuffer, MD_VIDEO_W, MD_VIDEO_H);
        s_DirectUploadSerial = s_DirectFrameSerial;
    }
    GPPrimSetTex(texTBP, 320, 4, 0, GS_PSMCT16, 0, 0, 0, 0);
    Uint32 mod = (Uint32)(128.0f * intensity + 0.5f);
    if (mod > 128u) mod = 128u;
    Uint32 modColor = 0x80000000u | (mod << 16) | (mod << 8) | mod;
    Uint32 startY = (Uint32)logicalY;
    GPPrimTexRect(0, startY << 4, 8, 8, 1280u << 4, (startY + 480u) << 4, (Uint32)(MD_VIDEO_W << 4) + 8u, (Uint32)(MD_VIDEO_H << 4) + 8u, 10u << 4, modColor, 0);
    return true;
}
bool PicoDriveBridge_Init(void) {
    if (s_Initialized) return true;
    qResetTransient();
    s_Initialized = true;
    return true;
}

void PicoDriveBridge_Shutdown(void) {
    if (s_GameLoaded) PicoDriveBridge_UnloadGame();
    s_Initialized = false;
}

bool PicoDriveBridge_LoadGame(const void *pData, size_t nBytes, const char *pName) {
    (void)pName;
    if (!pData || nBytes < 64 || !PicoDriveBridge_Init()) return false;
    struct { const void *data; size_t size; const char *path; void *meta; } info = { pData, nBytes, "game.md", NULL };
    if (!retro_load_game(&info)) return false;
    s_GameLoaded = true;
    return true;
}

void PicoDriveBridge_UnloadGame(void) {
    if (s_GameLoaded) retro_unload_game();
    s_GameLoaded = false;
    qResetTransient();
}

void PicoDriveBridge_Reset(void) {
    if (s_GameLoaded) retro_reset();
    qResetTransientAudio();
}

void PicoDriveBridge_RunFrame(Emu::SysInputT *pInput, CRenderSurface *pTarget, CMixBuffer *pMixBuf) {
    (void)pTarget;
    if (!s_GameLoaded) return;
    if (pInput) {
        s_PadData[0] = (int)pInput->uPad;
        s_PadData[1] = (int)pInput->uPad;
    } else {
        s_PadData[0] = s_PadData[1] = 0;
    }
    s_AudioSamplesPending = 0;
    retro_run();
    if (pMixBuf && s_AudioSamplesPending > 0) {
        pMixBuf->OutputSamplesMono(s_AudioBuffer, s_AudioSamplesPending);
        pMixBuf->Flush();
    }
}

int PicoDriveBridge_GetStateSize(void) {
    if (!s_GameLoaded) return 0;
    return (int)core_retro_serialize_size();
}

int PicoDriveBridge_SaveState(void *sd, int extra) {
    if (!s_GameLoaded || !sd || extra <= 0) return 0;
    int nBytes = (int)core_retro_serialize_size();
    if (nBytes <= 0 || nBytes > extra) return 0;
    if (!retro_serialize(sd, (size_t)nBytes)) return 0;
    if (PicoDriveAurora_SmdSaveState(sd, extra) != extra) return 0;
    return nBytes;
}

bool PicoDriveBridge_LoadState(const void *sd, int extra) {
    if (!s_GameLoaded || !sd || extra <= 0) return false;
    int nBytes = (int)core_retro_serialize_size();
    if (nBytes <= 0) return false;
    if (!retro_unserialize(sd, (size_t)nBytes)) return false;
    PicoDriveAurora_SmdLoadState(sd, extra);
    qResetTransientAudio();
    return true;
}

int PicoDriveBridge_GetSRAMBytes(void) {
    if (!s_GameLoaded) return 0;
    return PicoDriveAurora_GetSRamSize();
}

uint8_t *PicoDriveBridge_GetSRAMData(void) {
    if (!s_GameLoaded) return NULL;
    return PicoDriveAurora_GetSRamPtr();
}

unsigned PicoDriveBridge_GetSampleRate(void) {
    return 44100;
}
