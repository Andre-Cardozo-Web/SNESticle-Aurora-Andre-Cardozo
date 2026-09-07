/* SNESTICLE_QUICKNES_BRIDGE
 * Direct QuickNES Nes_Emu integration for SNESticle/PS2.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <libpad.h>

#include "quicknes_bridge.h"
#include "types.h"
#include "emuinput.h"
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"
#include "snio.h"
#include "snppurender.h"
#include "input.h"

extern "C" {
#include "gs.h"
#include "gpprim.h"
}

#ifndef NO_UNALIGNED_ACCESS
#define NO_UNALIGNED_ACCESS 1
#endif

#include "Nes_Emu.h"
#include "Nes_Buffer.h"
#include "Data_Reader.h"
#include "abstract_file.h"
#include "nes_ntsc.h"

extern "C" void quicknes_snesticle_set_duty_swap(int enable);
extern "C" void quicknes_snesticle_set_microphone(int enable);
extern "C" void quicknes_snesticle_ext_set_arkanoid(int enable);
extern "C" void quicknes_snesticle_ext_set_turbofile(int enable);
extern "C" void quicknes_snesticle_ext_set_battlebox(int enable);
extern "C" void quicknes_snesticle_ext_set_lightgun(int mode);
extern "C" void quicknes_snesticle_ext_set_lightgun_state(int x, int y, int trigger, int offscreen);
extern "C" void quicknes_snesticle_ext_set_arkanoid_state(unsigned int paddle, int fire);
extern "C" void quicknes_snesticle_ext_reset_bus(void);
extern "C" unsigned char *quicknes_snesticle_ext_turbofile_data(void);
extern "C" int quicknes_snesticle_ext_turbofile_dirty(void);
extern "C" void quicknes_snesticle_ext_turbofile_clear_dirty(void);
extern "C" unsigned char *quicknes_snesticle_ext_battlebox_data(void);
extern "C" int quicknes_snesticle_ext_battlebox_dirty(void);
extern "C" void quicknes_snesticle_ext_battlebox_clear_dirty(void);

static bool s_Initialized = false;
static bool s_GameLoaded  = false;
static bool s_DutySwap    = false;
static bool s_TurboPhase  = false;
static bool s_SkipVideoNext = false;
static bool s_ArkanoidVaus = false;
static bool s_TurboFileEnabled = false;
static bool s_BattleBoxEnabled = false;

static bool s_LightGunEnabled = true;
static int s_LightGunDetectedMode = 0;
static int s_LightGunMode = 0;
static Int32 s_GunX = 128 << 8;
static Int32 s_GunY = 120 << 8;
static Int32 s_GunVX = 0;
static Int32 s_GunVY = 0;
static unsigned s_TurboSpeedShift = 0;
static uint32_t s_TurboFrame = 0;

static int s_LastSpriteScanlineLimit = -1;
static int s_LastSpriteScreenLimit = -1;

static Nes_Emu    *s_pEmu = NULL;
static Nes_Buffer *s_pAudioBuffer = NULL;

enum {
    QN_VIDEO_W = Nes_Emu::buffer_width,
    QN_VIDEO_H = Nes_Emu::image_height + 2,
    QN_AUDIO_MAX = 4096
};

static Uint8 s_Video[QN_VIDEO_W * QN_VIDEO_H + 16] __attribute__((aligned(64)));
static Uint32 s_RgbaPalette[256];
static short  s_LastFramePalette[Nes_Emu::max_palette_size];
static bool   s_PaletteValid = false;

static bool s_CustomPaletteValid = false;
static Uint8 s_CustomBasePalette[64 * 3];
static Uint8 s_CustomExpandedPalette[Nes_Emu::color_table_size * 3];

static void qGetRgb(unsigned ci, Uint8 *r, Uint8 *g, Uint8 *b) {
    if (ci >= (unsigned)Nes_Emu::color_table_size) ci = 0;
    if (s_CustomPaletteValid) {
        const Uint8 *p = s_CustomExpandedPalette + ci * 3;
        *r = *(p + 0); *g = *(p + 1); *b = *(p + 2);
    } else {
        const Nes_Emu::rgb_t &rgb = Nes_Emu::nes_colors[ci];
        *r = rgb.red; *g = rgb.green; *b = rgb.blue;
    }
}
static Uint32 s_GsPalette[256] __attribute__((aligned(64)));
static short s_DirectLastPalette[Nes_Emu::max_palette_size];
static bool s_DirectPaletteValid = false;
static bool s_DirectClutResident = false;
static bool s_DirectReady = false;
static Uint32 s_DirectFrameSerial = 0;
static Uint32 s_DirectUploadSerial = 0;

static Int16 s_AudioOut[QN_AUDIO_MAX + 4];
static Int16 s_Pending[QN_AUDIO_MAX + 4];
static int   s_PendingCount = 0;

static Uint32 qCrc32(const Uint8 *pData, size_t nBytes) {
    static const Uint32 table[16] = {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
        0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
        0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU
    };
    Uint32 crc = 0xFFFFFFFFU;
    while (nBytes--) {
        crc ^= *pData++;
        crc = (crc >> 4) ^ table[crc & 0x0FU];
        crc = (crc >> 4) ^ table[crc & 0x0FU];
    }
    return crc ^ 0xFFFFFFFFU;
}

static Uint32 qNesPayloadCrc32(const void *pData, size_t nBytes) {
    const Uint8 *rom = (const Uint8 *)pData;
    if (!rom || nBytes < 16 || rom[0] != 'N' || rom[1] != 'E' || rom[2] != 'S' || rom[3] != 0x1A) return 0;
    size_t offset = 16U + ((rom[6] & 0x04U) ? 512U : 0U);
    size_t payload = (size_t)rom[4] * 16384U + (size_t)rom[5] * 8192U;
    if (payload == 0 || offset > nBytes || payload > nBytes - offset) return 0;
    return qCrc32(rom + offset, payload);
}

static bool qFamicomLightGunCrc(Uint32 crc) { return (crc == 0x0AFB395EU || crc == 0x24598791U || crc == 0x2A6559A1U); }
static bool qTwoNesZapperCrc(Uint32 crc) { return (crc == 0x231BC76EU || crc == 0xB79F2651U || crc == 0xD15009CCU); }
static bool qNesZapperCrc(Uint32 crc) { return (crc == 0x01B87025U || crc == 0x04A6B46DU || crc == 0x051E60C6U); }

static int qLightGunModeForCrc(Uint32 crc) {
    if (qFamicomLightGunCrc(crc)) return 2;
    if (qTwoNesZapperCrc(crc)) return 3;
    if (qNesZapperCrc(crc)) return 1;
    return 0;
}

static void qResetLightGunAim(void) { s_GunX = 128 << 8; s_GunY = 120 << 8; s_GunVX = s_GunVY = 0; }
static Int32 qGunTargetVelocity(unsigned axis) { Int32 d = (Int32)axis - 128; if (d < 0 ? -d : d <= 20) return 0; Int32 speed = 160 + ((d < 0 ? -d : d - 20) * 1056 + 53) / 107; return (d < 0 ? -1 : 1) * (speed > 1216 ? 1216 : speed); }
static Int32 qGunApproach(Int32 v, Int32 t, Int32 s) { if (v < t) return (v + s > t) ? t : v + s; if (v > t) return (v - s < t) ? t : v - s; return v; }
static Int32 qGunVelocityStep(Int32 c, Int32 t) { if (!t) return 320; if ((c < 0 && t > 0) || (c > 0 && t < 0)) return 256; return 80; }

static void qUpdateLightGunAim(Emu::SysInputT *pInput) {
    unsigned ax = 0x80U, ay = 0x80U; bool trigger = false, offscreen = false;
    if (InputIsPadConnected(0)) { Uint32 packed = InputGetPadAnalog(0); ax = (packed >> 16) & 0xFFU; ay = (packed >> 24) & 0xFFU; offscreen = (InputGetPadData(0) & (PAD_L2 | PAD_SQUARE)) == (PAD_L2 | PAD_SQUARE); }
    if (pInput && pInput->uPad != EMUSYS_DEVICE_DISCONNECTED) trigger = (pInput->uPad & SNESIO_JOY_B) != 0;
    if (offscreen) trigger = true;
    s_GunVX = qGunApproach(s_GunVX, qGunTargetVelocity(ax), qGunVelocityStep(s_GunVX, qGunTargetVelocity(ax)));
    s_GunVY = qGunApproach(s_GunVY, qGunTargetVelocity(ay), qGunVelocityStep(s_GunVY, qGunTargetVelocity(ay)));
    s_GunX += s_GunVX; s_GunY += s_GunVY;
    if (s_GunX < 1536) s_GunX = 1536; if (s_GunX > 63744) s_GunX = 63744; if (s_GunY < 1536) s_GunY = 1536; if (s_GunY > 59648) s_GunY = 59648;
    quicknes_snesticle_ext_set_lightgun_state((int)(s_GunX >> 8), (int)(s_GunY >> 8), trigger ? 1 : 0, offscreen ? 1 : 0);
}

static void qUpdateArkanoidVaus(Emu::SysInputT *pInput) { unsigned axis = 0x80U; if (!s_ArkanoidVaus) return; if (InputIsPadConnected(0)) axis = (InputGetPadAnalog(0) >> 16) & 0xFFU; int fire = (pInput && pInput->uPad != EMUSYS_DEVICE_DISCONNECTED && (pInput->uPad & SNESIO_JOY_B)) ? 1 : 0; quicknes_snesticle_ext_set_arkanoid_state(0x54U + (axis * 160U + 127U) / 255U, fire); }
static void qResetDirectVideo(void) { memset(s_DirectLastPalette, 0, sizeof(s_DirectLastPalette)); s_DirectPaletteValid = s_DirectClutResident = s_DirectReady = false; s_DirectFrameSerial = s_DirectUploadSerial = 0; }
static void qResetTransient(void) { memset(s_Video, 0, sizeof(s_Video)); memset(s_LastFramePalette, 0, sizeof(s_LastFramePalette)); memset(s_Pending, 0, sizeof(s_Pending)); s_PendingCount = 0; s_PaletteValid = false; qResetDirectVideo(); s_TurboPhase = false; s_TurboFrame = s_TurboSpeedShift = 0; s_SkipVideoNext = false; s_LastSpriteScanlineLimit = s_LastSpriteScreenLimit = -1; }
static Uint8 qMapPad(Uint16 pad) { if (pad == EMUSYS_DEVICE_DISCONNECTED) return 0; Uint8 nes = 0; if (pad & SNESIO_JOY_B) nes |= 0x01; if (pad & SNESIO_JOY_Y) nes |= 0x02; if ((pad & SNESIO_JOY_A) && s_TurboPhase) nes |= 0x01; if ((pad & SNESIO_JOY_X) && s_TurboPhase) nes |= 0x02; if (pad & SNESIO_JOY_SELECT) nes |= 0x04; if (pad & SNESIO_JOY_START) nes |= 0x08; if (pad & SNESIO_JOY_UP) nes |= 0x10; if (pad & SNESIO_JOY_DOWN) nes |= 0x20; if (pad & SNESIO_JOY_LEFT) nes |= 0x40; if (pad & SNESIO_JOY_RIGHT) nes |= 0x80; return nes; }

static void qRenderFrame(CRenderSurface *pTarget) {
    if (!pTarget || !pTarget->GetFormat() || pTarget->GetFormat()->uBitDepth != 32) return; const Nes_Emu::frame_t &frame = s_pEmu->frame(); if (!frame.pixels || frame.pitch <= 0) return;
    if (!s_PaletteValid || memcmp(s_LastFramePalette, frame.palette, sizeof(s_LastFramePalette)) != 0) { for (unsigned i = 0; i < 256; ++i) { unsigned ci = (unsigned)(unsigned short)frame.palette[i]; Uint8 r, g, b; qGetRgb(ci >= (unsigned)Nes_Emu::color_table_size ? 0 : ci, &r, &g, &b); s_RgbaPalette[i] = 0xff000000u | ((Uint32)b << 16) | ((Uint32)g << 8) | r; } memcpy(s_LastFramePalette, frame.palette, sizeof(s_LastFramePalette)); s_PaletteValid = true; }
    for (int y = 0; y < Nes_Emu::image_height; ++y) { Uint32 *dst = (Uint32 *)pTarget->GetLinePtr(y); if (dst) { const Uint8 *src = frame.pixels + (long)y * frame.pitch; for (int x = 0; x < Nes_Emu::image_width; ++x) dst[x] = s_RgbaPalette[src[x]]; } }
}

enum { QN_GS_TEX_TBP_OFFSET = 0x400, QN_GS_CLUT_TBP_OFFSET = 0x580, QN_GS_T8_TBW = 320 };
void QuicknesBridge_InvalidateGsResources(void) { s_DirectUploadSerial = 0; s_DirectClutResident = false; }
bool QuicknesBridge_CanDirectGsVideo(void) { return s_GameLoaded && s_DirectReady && s_DirectFrameSerial != 0; }

static void qRefreshGsPalette(const Nes_Emu::frame_t &frame) {
    if (s_DirectPaletteValid && memcmp(s_DirectLastPalette, frame.palette, sizeof(s_DirectLastPalette)) == 0) return;
    for (unsigned i = 0; i < 256; ++i) { unsigned ci = (unsigned)(unsigned short)frame.palette[i]; unsigned dest = i; unsigned block = i & 0x3fU; if ((block & 0x18U) == 0x08U) dest += 8U; else if ((block & 0x18U) == 0x10U) dest -= 8U; Uint8 r, g, b; qGetRgb(ci >= (unsigned)Nes_Emu::color_table_size ? 0 : ci, &r, &g, &b); s_GsPalette[dest] = 0x80000000u | ((Uint32)b << 16) | ((Uint32)g << 8) | r; }
    memcpy(s_DirectLastPalette, frame.palette, sizeof(s_DirectLastPalette)); s_DirectPaletteValid = true; s_DirectClutResident = false;
}

bool QuicknesBridge_DrawDirectGs(Uint32 auroraOutBaseTBP, Int32 logicalY, Float32 intensity) {
    const Nes_Emu::frame_t &frame = s_pEmu->frame(); if (!auroraOutBaseTBP || !QuicknesBridge_CanDirectGsVideo() || !frame.pixels || frame.pitch != QN_VIDEO_W) return false;
    Uint32 texTBP = auroraOutBaseTBP + QN_GS_TEX_TBP_OFFSET, clutTBP = auroraOutBaseTBP + QN_GS_CLUT_TBP_OFFSET;
    if (s_DirectUploadSerial != s_DirectFrameSerial) { GPPrimUploadTexture((int)texTBP, QN_GS_T8_TBW, 0, 0, GS_PSMT8, frame.pixels, QN_VIDEO_W, Nes_Emu::image_height); s_DirectUploadSerial = s_DirectFrameSerial; }
    qRefreshGsPalette(frame); if (!s_DirectClutResident) { GPPrimUploadTexture((int)clutTBP, 64, 0, 0, GS_PSMCT32, s_GsPalette, 16, 16); s_DirectClutResident = true; }
    GPPrimSetTex(texTBP, QN_GS_T8_TBW, 9, 8, GS_PSMT8, clutTBP, 64, GS_PSMCT32, 0);
    Uint32 mod = (Uint32)(128.0f * intensity + 0.5f); if (mod > 128u) mod = 128u; Uint32 modColor = 0x80000000u | (mod << 16) | (mod << 8) | mod;
    Uint32 startY = (Uint32)logicalY + 16u;
    GPPrimTexRect(0, startY << 4, 8, 8, 1280u << 4, (startY + 480u) << 4, (256u << 4) + 8u, (Uint32)(Nes_Emu::image_height << 4) + 8u, 10u << 4, modColor, 0);
    return true;
}
static void qDrainAudio(CMixBuffer *pMix) {
    if (!pMix) { s_pEmu->read_samples(NULL, QN_AUDIO_MAX); s_PendingCount = 0; return; }
    if (s_PendingCount > 0) memcpy(s_AudioOut, s_Pending, (size_t)s_PendingCount * sizeof(short));
    long count = s_pEmu->read_samples((short *)(s_AudioOut + s_PendingCount), QN_AUDIO_MAX); if (count <= 0) { pMix->Flush(); return; }
    int n = s_PendingCount + (int)(count > QN_AUDIO_MAX ? QN_AUDIO_MAX : count); int flush = n & ~3; s_PendingCount = n - flush;
    if (s_PendingCount > 0) memcpy(s_Pending, s_AudioOut + flush, (size_t)s_PendingCount * sizeof(short));
    if (flush > 0) pMix->OutputSamplesMono(s_AudioOut, flush); pMix->Flush();
}

bool QuicknesBridge_Init(void) {
    if (s_Initialized) return true; s_pEmu = new Nes_Emu(); if (!s_pEmu) return false; s_pAudioBuffer = new Nes_Buffer(); if (!s_pAudioBuffer) { delete s_pEmu; return false; }
    if (s_pEmu->set_sample_rate(32000, s_pAudioBuffer)) { delete s_pAudioBuffer; delete s_pEmu; return false; }
    s_pEmu->set_palette_range(0); s_pEmu->set_sprite_mode(Nes_Emu::sprites_visible); s_pEmu->set_pixels(s_Video + 8, QN_VIDEO_W); quicknes_snesticle_set_duty_swap(s_DutySwap ? 1 : 0); qResetTransient(); s_Initialized = true; return true;
}

void QuicknesBridge_Shutdown(void) { if (s_pEmu) s_pEmu->close(); delete s_pAudioBuffer; delete s_pEmu; s_pEmu = NULL; s_GameLoaded = s_Initialized = false; qResetTransient(); }

bool QuicknesBridge_LoadGame(const void *pData, size_t nBytes, const char *pName) {
    (void)pName;
    if (!pData || nBytes < 16 || !QuicknesBridge_Init()) return false; if (s_GameLoaded) QuicknesBridge_UnloadGame(); qResetTransient(); s_pEmu->set_pixels(s_Video + 8, QN_VIDEO_W);
    Mem_File_Reader reader(pData, (long)nBytes); if (s_pEmu->load_ines(reader)) { s_pEmu->close(); return false; }
    Uint32 payloadCrc = qNesPayloadCrc32(pData, nBytes); s_LightGunDetectedMode = qLightGunModeForCrc(payloadCrc); s_LightGunMode = s_LightGunEnabled ? s_LightGunDetectedMode : 0; s_ArkanoidVaus = (payloadCrc == 0xD89E5A67U);
    quicknes_snesticle_ext_set_turbofile(0); quicknes_snesticle_ext_set_arkanoid(s_ArkanoidVaus ? 1 : 0); quicknes_snesticle_ext_set_battlebox(0); quicknes_snesticle_ext_set_lightgun(s_LightGunMode); quicknes_snesticle_ext_reset_bus(); s_GameLoaded = true; return true;
}

void QuicknesBridge_UnloadGame(void) { if (s_Initialized && s_pEmu) s_pEmu->close(); s_GameLoaded = s_ArkanoidVaus = s_TurboFileEnabled = s_BattleBoxEnabled = false; s_LightGunDetectedMode = s_LightGunMode = 0; quicknes_snesticle_ext_set_turbofile(0); quicknes_snesticle_ext_set_arkanoid(0); quicknes_snesticle_ext_set_battlebox(0); quicknes_snesticle_ext_set_lightgun(0); qResetLightGunAim(); qResetTransient(); }
void QuicknesBridge_Reset(void) { if (s_GameLoaded) s_pEmu->reset(true, false); s_PendingCount = 0; s_PaletteValid = false; qResetDirectVideo(); quicknes_snesticle_ext_reset_bus(); }
void QuicknesBridge_SoftReset(void) { if (s_GameLoaded) s_pEmu->reset(false, false); s_PendingCount = 0; s_PaletteValid = false; qResetDirectVideo(); quicknes_snesticle_ext_reset_bus(); }
void QuicknesBridge_SetDutySwap(bool enabled) { s_DutySwap = enabled; quicknes_snesticle_set_duty_swap(enabled ? 1 : 0); }
bool QuicknesBridge_SetPalette(const Uint8 *rgb192) { nes_ntsc_setup_t setup; if (!rgb192) return false; memcpy(s_CustomBasePalette, rgb192, sizeof(s_CustomBasePalette)); setup = nes_ntsc_rgb; setup.palette = NULL; setup.base_palette = s_CustomBasePalette; setup.palette_out = s_CustomExpandedPalette; nes_ntsc_init(NULL, &setup); s_CustomPaletteValid = true; s_PaletteValid = s_DirectPaletteValid = s_DirectClutResident = false; return true; }
void QuicknesBridge_SetTurboSpeed(unsigned s) { if (s_TurboSpeedShift != (s <= 2U ? s : 0U)) { s_TurboSpeedShift = (s <= 2U ? s : 0U); s_TurboFrame = 0; s_TurboPhase = true; } }
void QuicknesBridge_SetSkipVideo(bool skip) { s_SkipVideoNext = skip; }

void QuicknesBridge_RunFrame(Emu::SysInputT *pInput, CRenderSurface *pTarget, CMixBuffer *pMixBuf) {
    if (!s_GameLoaded || !s_pEmu) return; s_TurboPhase = (((s_TurboFrame >> s_TurboSpeedShift) & 1U) == 0U); ++s_TurboFrame; quicknes_snesticle_set_microphone((pInput && (pInput->uPad[0] & SNESIO_JOY_L)) ? 1 : 0);
    Uint8 p1 = pInput ? qMapPad(pInput->uPad[0]) : 0; Uint8 p2 = pInput ? qMapPad(pInput->uPad[1]) : 0; if (s_LightGunMode != 0) { qUpdateLightGunAim(pInput); if (s_LightGunMode == 1) p2 = 0; else if (s_LightGunMode == 3) p1 = p2 = 0; }
    if (s_ArkanoidVaus) { p1 &= (Uint8)~0x01U; qUpdateArkanoidVaus(pInput); } if (s_pEmu->emulate_frame((int)p1, (int)p2)) { qDrainAudio(pMixBuf); return; }
    const Nes_Emu::frame_t &frame = s_pEmu->frame(); s_DirectReady = frame.pixels && frame.pitch == QN_VIDEO_W && (((uintptr_t)frame.pixels & 15u) == 0u); if (s_DirectReady) { if (++s_DirectFrameSerial == 0) { s_DirectFrameSerial = 1; s_DirectUploadSerial = 0; } } else qRenderFrame(pTarget); qDrainAudio(pMixBuf);
}

int QuicknesBridge_GetStateSize(void) { return s_GameLoaded ? QUICKNES_STATE_CAPACITY : 0; }
int QuicknesBridge_SaveState(void *p, int n) { if (!s_GameLoaded || !s_pEmu || !p || n <= 0) return 0; Mem_Writer w(p, (long)(n > QUICKNES_STATE_CAPACITY ? QUICKNES_STATE_CAPACITY : n)); return s_pEmu->save_state(w) ? 0 : (int)w.size(); }
bool QuicknesBridge_LoadState(const void *p, int n) { if (!s_GameLoaded || !p || n <= 0) return false; Mem_File_Reader r(p, (long)n); if (s_pEmu->load_state(r)) return false; s_PendingCount = 0; s_PaletteValid = false; qResetDirectVideo(); quicknes_snesticle_ext_reset_bus(); return true; }
int QuicknesBridge_GetSRAMBytes(void) { return (s_GameLoaded && s_pEmu->cart() && s_pEmu->has_battery_ram()) ? (int)s_pEmu->battery_ram_size() : 0; }
uint8_t *QuicknesBridge_GetSRAMData(void) { return QuicknesBridge_GetSRAMBytes() <= 0 ? NULL : s_pEmu->high_mem(); }
bool QuicknesBridge_IsArkanoidVaus(void) { return s_ArkanoidVaus; }
bool QuicknesBridge_TurboFileEnabled(void) { return s_TurboFileEnabled; }
int QuicknesBridge_GetTurboFileBytes(void) { return 0x2000; }
uint8_t *QuicknesBridge_GetTurboFileData(void) { return quicknes_snesticle_ext_turbofile_data(); }
bool QuicknesBridge_TurboFileDirty(void) { return quicknes_snesticle_ext_turbofile_dirty() != 0; }
void QuicknesBridge_ClearTurboFileDirty(void) { quicknes_snesticle_ext_turbofile_clear_dirty(); }
void QuicknesBridge_SetLightGunEnabled(bool e) { s_LightGunEnabled = e; int m = e ? s_LightGunDetectedMode : 0; if (m != s_LightGunMode) { s_LightGunMode = m; quicknes_snesticle_ext_set_lightgun(s_LightGunMode); quicknes_snesticle_ext_reset_bus(); if (s_LightGunMode != 0) qResetLightGunAim(); } }
bool QuicknesBridge_GetLightGunEnabled(void) { return s_LightGunEnabled; }
bool QuicknesBridge_LightGunActive(void) { return s_GameLoaded && s_LightGunMode != 0; }
void QuicknesBridge_GetLightGunCursor(Int32 *x, Int32 *y) { if (x) *x = s_GunX >> 8; if (y) *y = s_GunY >> 8; }

void QuicknesBridge_DrawLightGunCursor(Int32 lY) {
    if (!QuicknesBridge_LightGunActive()) return; Int32 x = s_GunX >> 8, y = (s_GunY >> 8) + lY;
    GPPrimRect((Uint32)(x - 6) << 4, (Uint32)y << 4, 0x80000000u, (Uint32)(x + 7) << 4, (Uint32)(y + 1) << 4, 0x80000000u, 9u << 4, 0); GPPrimRect((Uint32)x << 4, (Uint32)(y - 6) << 4, 0x80000000u, (Uint32)(x + 1) << 4, (Uint32)(y + 7) << 4, 0x80000000u, 9u << 4, 0);
    GPPrimRect((Uint32)(x - 5) << 4, (Uint32)y << 4, 0x80FFFFFFu, (Uint32)(x + 6) << 4, (Uint32)(y + 1) << 4, 0x80FFFFFFu, 9u << 4, 0); GPPrimRect((Uint32)x << 4, (Uint32)(y - 5) << 4, 0x80FFFFFFu, (Uint32)(x + 1) << 4, (Uint32)(y + 6) << 4, 0x80FFFFFFu, 9u << 4, 0);
}
bool QuicknesBridge_BattleBoxEnabled(void) { return s_BattleBoxEnabled; }
int QuicknesBridge_GetBattleBoxBytes(void) { return 0x0200; }
uint8_t *QuicknesBridge_GetBattleBoxData(void) { return quicknes_snesticle_ext_battlebox_data(); }
bool QuicknesBridge_BattleBoxDirty(void) { return quicknes_snesticle_ext_battlebox_dirty() != 0; }
void QuicknesBridge_ClearBattleBoxDirty(void) { quicknes_snesticle_ext_battlebox_clear_dirty(); }
unsigned QuicknesBridge_GetSampleRate(void) { return 32000; }
