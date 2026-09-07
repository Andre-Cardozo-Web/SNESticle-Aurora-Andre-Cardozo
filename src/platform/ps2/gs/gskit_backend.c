/* gskit_backend.c
 *
 * gsKit-based replacement for the original direct-GS pipeline.
 * See gskit_backend.h for the public API.
 *
 * Fase 1 GS->gsKit migration.
 * Otimizado para Ultra-Nitidez 1280x512 @ 16-bit (QuickNES 5x Integer Scaling).
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <gsKit.h>
#include <dmaKit.h>
#include <gsInline.h>
#include <gsToolkit.h>

#include "types.h"
#include "ps2dma.h"
#include "gs.h"
#include "gskit_backend.h"
#include "gpprim.h"

/* Legacy logical coordinate space the entire UI was written in. Both
   supported outputs use a 640x480 physical framebuffer; 1080i is scaled by
   the PCRTC into a centred 1280x960 4:3 window. */
#define GSK_LOGICAL_W   256
#define GSK_LOGICAL_H   240

/* The original headers use these constants for mode / interlace. They
   live in gs.h but we want this TU to compile without dragging the
   register-level header in, so re-declare the values that match. */
#ifndef GS_NTSC
#define GS_NTSC          2
#define GS_PAL           3
#define GS_INTERLACE     1
#define GS_NONINTERLACED  0
#endif

static GSGLOBAL *_pGsGlobal = NULL;
static int       _gsk_initialised = 0;
static int       _gsk_invalidate_pending = 0;

/* AURORA_GS_LATENCY_V1
 * Off by default: every caller that does not explicitly opt into the gameplay
 * fast-clear path retains the exact historical full-frame clear. */
static Bool      _gsk_gameplay_fast_clear = FALSE;
/* AURORA_PD_DIRECT_MD_SKIP_CLEAR_V3_C_20260821 */
static Bool      _gsk_gameplay_skip_clear = FALSE;

/* Video mode + display offset (selectable in the Settings screen).
   480i is the safe default and 1080i is the only alternate output. */
int g_GskVideoMode = GSK_VIDMODE_480I;
int g_GskDispOffX  = 0;
int g_GskDispOffY  = 0;
int g_GskOverscan  = 0;   /* 0..100 shrink of display area */
int g_GskWidescreen = 0;  /* 0 = 4:3, 1 = safe 16:9 presentation */
static int _gsk_vck         = 1;   /* display-offset VCK units (Ajustado para 1 em 1080p) */
static int _gsk_fb_width    = 1280; /* active FB width (Modificado para 1280) */
static int _gsk_fb_height   = 512;  /* active FB height (Modificado para 512) */
static int _gsk_active_mode = GSK_VIDMODE_1080I; /* Força modo de inicialização ativa */
static int _gsk_native240p_par = 0;
static int _gsk_240p_fb_width = 256;
/* AURORA_PCE_FIXED512_DBX0_CUMULATIVE_V8_20260830
 * Optional horizontal scanout window inside the 240p framebuffer.
 * -1 means normal full-framebuffer presentation. */
static int _gsk_240p_window_x = -1;
static int _gsk_240p_window_w = 0;
/* AURORA_MD_UI256_320FB_V1_20260823
 * Keep MD's physical 320-wide framebuffer while its cartridge is alive,
 * but draw/scan Aurora's UI as exactly 256 uniform source pixels. */
static int _gsk_ui256_on_320fb = 0;
static int _gsk_game_y_bias = 0;

/* gsKit's computed DISPLAY params, captured after gsKit_init_screen so
   overscan/widescreen can be recomputed from a clean baseline. */
static int _gsk_base_dw, _gsk_base_dh, _gsk_base_magh, _gsk_base_magv;
static int _gsk_base_startx, _gsk_base_starty;

static void _GskApplyDisplay(void);   /* offset + overscan + widescreen */
static void _GskApplyRenderTransform(void);

/* Saved GSK_Init arguments so GSK_ReinitVideo() can replay them. */
static int _gsk_arg_w, _gsk_arg_h, _gsk_arg_dispx, _gsk_arg_dispy;
static int _gsk_arg_psm, _gsk_arg_psmz, _gsk_arg_mode, _gsk_arg_interlace;

GSGLOBAL *GSK_GetGlobal(void)
{
    return _pGsGlobal;
}

/* AURORA_MEGA_V2_GS_REFRESH_IMPL
 * PS2 NTSC VBlank is the 59.94-family clock, not exactly 60.000 Hz. Feeding
 * this rational to the audio mixer prevents a slow ring-buffer phase drift.
 */
void GSK_GetRefreshRate(Uint32 *pNumerator, Uint32 *pDenominator)
{
    if (!pNumerator || !pDenominator) return;
    if (_pGsGlobal && _pGsGlobal->Mode == GS_MODE_PAL)
    {
        *pNumerator = 50;
        *pDenominator = 1;
    }
    else
    {
        *pNumerator = 60000;
        *pDenominator = 1001;
    }
}

/* Detect the console's TV region from the BIOS ROMVER region byte. */
static int _gsk_DetectTvMode(void)
{
    volatile char region = *(volatile char *)0x1FC7FF52;
    return (region == 'E') ? GS_MODE_PAL : GS_MODE_NTSC;
}

void GSK_Init(int width, int height,
              int dispx, int dispy,
              int psm, int psmz,
              int mode, int interlace)
{
    if (_gsk_initialised) {
        return;
    }

    _gsk_arg_w     = width;  _gsk_arg_h        = height;
    _gsk_arg_dispx = dispx;  _gsk_arg_dispy    = dispy;
    _gsk_arg_psm   = GS_PSM_CT16; // Alocação segura em 16-bits para evitar estouro de VRAM
    _gsk_arg_psmz  = psmz;
    _gsk_arg_mode  = mode;   _gsk_arg_interlace = interlace;

    _pGsGlobal = gsKit_init_global();
    if (!_pGsGlobal) {
        return;
    }

    /* Injeção de registros de controle estável para monitores modernos e adaptadores HDMI */
    _pGsGlobal->Mode      = GS_MODE_DTV_1080I; 
    _pGsGlobal->Interlace = GS_NONINTERLACED;   // Força Varredura Progressiva Real (1080p)
    _pGsGlobal->Field     = GS_FRAME;          // Renderiza frames cheios (Evita tela escura/esmagada)
    
    _gsk_fb_width         = 1280; // Largura pixel-perfect para QuickNES (256x5)
    _gsk_fb_height        = 512;  // Altura expandida para evitar compressão vertical
    _gsk_vck              = 1;    
    g_GskVideoMode        = GSK_VIDMODE_1080I; 
    _gsk_active_mode      = g_GskVideoMode;

    _pGsGlobal->Width  = _gsk_fb_width;
    _pGsGlobal->Height = _gsk_fb_height;
    _pGsGlobal->PSM    = GS_PSM_CT16; 
    _pGsGlobal->PSMZ   = psmz;

    _pGsGlobal->ZBuffering      = GS_SETTING_OFF;
    _pGsGlobal->DoubleBuffering = GS_SETTING_ON;
    _pGsGlobal->PrimAAEnable    = GS_SETTING_OFF;
    _pGsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    _pGsGlobal->Dithering       = GS_SETTING_OFF;
    _pGsGlobal->DrawOrder       = GS_PER_OS;

    (void)interlace;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_vram_clear(_pGsGlobal);
    gsKit_init_screen(_pGsGlobal);

    if (_gsk_active_mode == GSK_VIDMODE_1080I)
    {
        const int aspect_dw = 1280; // Mantém a proporção estável 4:3 centralizada em displays 16:9
        _pGsGlobal->StartX += (_pGsGlobal->DW - aspect_dw) / 2;
        _pGsGlobal->MagH = 1;
        _pGsGlobal->DW   = aspect_dw;
    }

    _gsk_base_dw     = _pGsGlobal->DW;
    _gsk_base_dh     = _pGsGlobal->DH;
    _gsk_base_magh   = _pGsGlobal->MagH;
    _gsk_base_magv   = _pGsGlobal->MagV;
    _gsk_base_startx = _pGsGlobal->StartX;
    _gsk_base_starty = _pGsGlobal->StartY;

    _gsk_initialised = 1;
    _GskApplyDisplay();

    (void)dispx; (void)dispy; (void)width; (void)height;

    gsKit_set_test (_pGsGlobal, GS_ZTEST_OFF);
    gsKit_set_clamp(_pGsGlobal, GS_CMODE_REPEAT);
    gsKit_set_primalpha(_pGsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0x80), 0);

    gsKit_TexManager_init(_pGsGlobal);
    gsKit_mode_switch(_pGsGlobal, GS_ONESHOT);

    gsKit_clear(_pGsGlobal, 0);
    gsKit_queue_exec(_pGsGlobal);
    gsKit_finish();
    gsKit_sync_flip(_pGsGlobal);
    gsKit_clear(_pGsGlobal, 0);
    gsKit_queue_exec(_pGsGlobal);
    gsKit_finish();
    gsKit_sync_flip(_pGsGlobal);
}
/* Map the application canvas onto the common framebuffer. */
static void _GskApplyRenderTransform(void)
{
    float sx = (float)_gsk_fb_width / (float)GSK_LOGICAL_W;
    float sy = (float)_gsk_fb_height / (float)GSK_LOGICAL_H;

    if (_gsk_ui256_on_320fb &&
        _gsk_active_mode == GSK_VIDMODE_240P &&
        _gsk_fb_width == 320)
    {
        sx = 1.0f;
    }

    GPPrimSetTransform(sx, sy, 0.0f, 0.0f);
}

static void _GskApplyDisplay(void)
{
    GSGLOBAL *gs = _pGsGlobal;
    int dw, dh, magh, startx, starty;

    if (!_gsk_initialised || !gs) {
        return;
    }

    dw     = _gsk_base_dw;
    dh     = _gsk_base_dh;
    magh   = _gsk_base_magh;
    startx = _gsk_base_startx;
    starty = _gsk_base_starty;

    if (g_GskOverscan > 0)
    {
        int sx = (_gsk_base_dw * g_GskOverscan) / 1300;
        int sy = (_gsk_base_dh * g_GskOverscan) / 1300;
        dw     = _gsk_base_dw - sx * 2;
        dh     = _gsk_base_dh - sy * 2;
        startx = _gsk_base_startx + sx;
        starty = _gsk_base_starty + sy;
    }

    if (_gsk_active_mode == GSK_VIDMODE_240P &&
        _gsk_240p_window_x >= 0 &&
        _gsk_240p_window_w > 0 &&
        g_GskOverscan == 0)
    {
        const int winw = _gsk_240p_window_w;

        if (winw == 256)
        {
            const int contentMagH1 = 10;
            const int contentDw = winw * contentMagH1;
            startx += (dw - contentDw) / 2;
            dw = contentDw;
            magh = contentMagH1 - 1;
        }
        else if (winw == 352)
        {
            const int contentMagH1 = 7;
            const int contentDw = winw * contentMagH1;
            startx += (dw - contentDw) / 2;
            dw = contentDw;
            magh = contentMagH1 - 1;
        }
        else if (winw == 512)
        {
            const int contentMagH1 = 5;
            const int contentDw = winw * contentMagH1;
            startx += (dw - contentDw) / 2;
            dw = contentDw;
            magh = contentMagH1 - 1;
        }

        GS_SET_DISPFB2(
            _pGsGlobal->ScreenBuffer[(_pGsGlobal->ActiveBuffer ^ 1) & 1] / 8192,
            _gsk_fb_width / 64,
            _pGsGlobal->PSM,
            _gsk_240p_window_x,
            0);
    }

    if (_gsk_native240p_par &&
        _gsk_active_mode == GSK_VIDMODE_240P &&
        g_GskOverscan == 0 &&
        _gsk_base_magh > 0 &&
        _gsk_240p_window_w <= 0)
    {
        int old_magh1 = _gsk_base_magh + 1;
        int new_magh1 = old_magh1 - 1;
        int srcpix    = _gsk_base_dw / old_magh1;
        int new_dw    = srcpix * new_magh1;

        startx += (dw - new_dw) / 2 - ((_gsk_fb_width == 256 ? 2 : 0) * new_magh1);
        dw      = new_dw;
        magh    = new_magh1 - 1;
        starty += 1;
    }

    if (_gsk_ui256_on_320fb &&
        _gsk_active_mode == GSK_VIDMODE_240P &&
        _gsk_fb_width == 320)
    {
        int new_magh1 = (dw + (GSK_LOGICAL_W / 2)) / GSK_LOGICAL_W;
        int new_dw;

        if (new_magh1 < 1)  new_magh1 = 1;
        if (new_magh1 > 16) new_magh1 = 16;

        new_dw = GSK_LOGICAL_W * new_magh1;
        startx += (dw - new_dw) / 2;
        dw      = new_dw;
        magh    = new_magh1 - 1;
    }

    if (g_GskWidescreen)
    {
        int magh1  = magh + 1;
        int srcpix = magh1 ? dw / magh1 : dw;
        int new_magh1 = (magh1 * 4 + 1) / 3;
        int new_dw1;

        if (new_magh1 > 16) new_magh1 = 16;
        if (new_magh1 < 1)  new_magh1 = 1;
        new_dw1 = new_magh1 * srcpix;

        startx -= (new_dw1 - dw) / 2;
        dw   = new_dw1;
        magh = new_magh1 - 1;
    }

    gs->DW     = dw;
    gs->DH     = dh;
    gs->MagH   = magh;
    gs->MagV   = _gsk_base_magv;
    gs->StartX = startx;
    gs->StartY = starty;

    gsKit_set_display_offset(gs, g_GskDispOffX * _gsk_vck,
                             g_GskDispOffY + _gsk_game_y_bias);
    _GskApplyRenderTransform();
}

void GSK_Set240pVisibleWindow(int x, int width)
{
    if (x < 0 || width <= 0 || x + width > _gsk_fb_width)
    {
        GSK_Clear240pVisibleWindow();
        return;
    }

    if (_gsk_240p_window_x == x && _gsk_240p_window_w == width)
        return;

    _gsk_240p_window_x = x;
    _gsk_240p_window_w = width;
    _GskApplyDisplay();
}

void GSK_Clear240pVisibleWindow(void)
{
    if (_gsk_240p_window_x < 0 && _gsk_240p_window_w == 0)
        return;

    _gsk_240p_window_x = -1;
    _gsk_240p_window_w = 0;
    _GskApplyDisplay();
}

void GSK_SetDisplayOffset(int x, int y)
{
    g_GskDispOffX = x;
    g_GskDispOffY = y;
    _GskApplyDisplay();
}

void GSK_SetGameplayYOffsetBias(int y)
{
    if (_gsk_game_y_bias == y)
        return;

    _gsk_game_y_bias = y;
    _GskApplyDisplay();
}

void GSK_SetUi256On320Framebuffer(int on)
{
    on = on ? 1 : 0;
    if (_gsk_ui256_on_320fb == on)
        return;

    _gsk_ui256_on_320fb = on;
    _GskApplyDisplay();
}

void GSK_Set240pFramebufferWidth(int width)
{
    if (width != 256 && width != 320 && width != 512)
        width = 256;

    _gsk_240p_fb_width = width;
}

int GSK_Get240pFramebufferWidth(void)
{
    return _gsk_240p_fb_width;
}

int GSK_GetActiveFramebufferWidth(void)
{
    if (!_gsk_initialised || !_pGsGlobal)
        return 0;
    return _pGsGlobal->Width;
}

void GSK_GetPceDebugState(int *fbw, int *winw, int *dw, int *magh,
                          int *startx, int *overscan, int *wide)
{
    if (fbw)      *fbw = _gsk_fb_width;
    if (winw)     *winw = _gsk_240p_window_w;
    if (dw)       *dw = _pGsGlobal ? _pGsGlobal->DW : -1;
    if (magh)     *magh = _pGsGlobal ? _pGsGlobal->MagH : -1;
    if (startx)   *startx = _pGsGlobal ? _pGsGlobal->StartX : -1;
    if (overscan) *overscan = g_GskOverscan;
    if (wide)     *wide = g_GskWidescreen;
}

void GSK_SetOverscan(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    g_GskOverscan = percent;
    _GskApplyDisplay();
}

void GSK_SetWidescreen(int on)
{
    g_GskWidescreen = on ? 1 : 0;
    _GskApplyDisplay();
}

void GSK_SetNative240pPar(int on)
{
    on = on ? 1 : 0;
    if (_gsk_native240p_par == on)
        return;

    _gsk_native240p_par = on;
    _GskApplyDisplay();
}

void GSK_ReinitVideo(void)
{
    GSGLOBAL *oldGlobal;

    if (!_gsk_initialised || !_pGsGlobal) {
        return;
    }

    GSK_DrainAndWait();
    oldGlobal = _pGsGlobal;

    _gsk_initialised = 0;
    _pGsGlobal = NULL;
    gsKit_deinit_global(oldGlobal);

    GSK_Init(_gsk_arg_w, _gsk_arg_h, _gsk_arg_dispx, _gsk_arg_dispy,
             _gsk_arg_psm, _gsk_arg_psmz, _gsk_arg_mode, _gsk_arg_interlace);
}

int GSK_GetActiveVideoMode(void)
{
    return _gsk_active_mode;
}

Uint32 GSK_VramAllocTBP(Uint32 nBytes)
{
    u32 addr;

    if (!_gsk_initialised || !_pGsGlobal) {
        return 0;
    }

    addr = gsKit_vram_alloc(_pGsGlobal, nBytes, GSKIT_ALLOC_USERBUFFER);
    if (addr == GSKIT_ALLOC_ERROR) {
        return 0;
    }
    return addr / 256;
}

void GSK_DrainAndWait(void)
{
    if (!_gsk_initialised) {
        return;
    }

    gsKit_queue_exec(_pGsGlobal);
    gsKit_finish();
    DmaSyncGIF();
}

void GSK_DrainForRawGif(void)
{
    if (!_gsk_initialised) {
        return;
    }

    gsKit_queue_exec(_pGsGlobal);
    DmaSyncGIF();
}

void GSK_SetGameplayFastClear(Bool enabled)
{
    _gsk_gameplay_fast_clear = enabled ? TRUE : FALSE;
}

void GSK_SetGameplaySkipClear(Bool enabled)
{
    _gsk_gameplay_skip_clear = enabled ? TRUE : FALSE;
}

void GSK_FlushFrame(void)
{
    if (!_gsk_initialised) {
        return;
    }

    gsKit_queue_exec(_pGsGlobal);
    gsKit_finish();
}

void GSK_SyncFlip(void)
{
    if (!_gsk_initialised) {
        return;
    }

    gsKit_sync_flip(_pGsGlobal);
    if (_gsk_240p_window_x >= 0)
        _GskApplyDisplay();
}

void GSK_ResetFrame(void)
{
    GSGLOBAL *gs;
    u64 *p_data;

    if (!_gsk_initialised || !_pGsGlobal) {
        return;
    }

    gs = _pGsGlobal;
    p_data = (u64 *)gsKit_heap_alloc(gs, 4, 64, GIF_AD);
    if (!p_data) {
        return;
    }

    *p_data++ = GIF_TAG_AD(4);
    *p_data++ = GIF_AD;
    *p_data++ = GS_SETREG_FRAME_1(
        gs->ScreenBuffer[gs->ActiveBuffer & 1] / 8192,
        gs->Width / 64,
        gs->PSM,
        0);
    *p_data++ = GS_REG_FRAME_1;
    *p_data++ = GS_SETREG_XYOFFSET_1(gs->OffsetX, gs->OffsetY);
    *p_data++ = GS_XYOFFSET_1;
    *p_data++ = GS_SETREG_ALPHA(0, 1, 0, 1, 0x80);
    *p_data++ = GS_REG_ALPHA_1;
    *p_data++ = (u64)1;
    *p_data++ = (u64)GS_REG_COLCLAMP;

    {
        u8 previous_alpha = gs->PrimAlphaEnable;
        gs->PrimAlphaEnable = GS_SETTING_OFF;

        if (_gsk_gameplay_skip_clear)
        {
        }
        else if (_gsk_gameplay_fast_clear && gs->Width > 0 && gs->Height > 0)
        {
            int wanted_rows =
                (_gsk_active_mode == GSK_VIDMODE_240P) ? 8 : 16;
            int clear_rows =
                (gs->Height < wanted_rows) ? gs->Height : wanted_rows;
            u64 top_scissor = GS_SETREG_SCISSOR(
                0, gs->Width - 1, 0, clear_rows - 1);
            u64 full_scissor = GS_SETREG_SCISSOR(
                0, gs->Width - 1, 0, gs->Height - 1);

            gsKit_set_scissor(gs, top_scissor);
            gsKit_clear(gs, 0);
            gsKit_set_scissor(gs, full_scissor);
        }
        else
        {
            gsKit_clear(gs, 0);
        }

        gs->PrimAlphaEnable = previous_alpha;
    }
}

void GSK_InvalidateTextureCache(void)
{
    _gsk_invalidate_pending = 1;
}

int GSK_TakeInvalidatePending(void)
{
    int p = _gsk_invalidate_pending;
    _gsk_invalidate_pending = 0;
    return p;
}

void *GSK_AsUncached(void *ptr)
{
    Uint32 addr = (Uint32)ptr;

    if (!addr) {
        return ptr;
    }

    assert((addr & 0xF0000000) == 0 &&
           "GSK_AsUncached: pointer is outside physical RAM (<256MB)");

    return (void *)(addr | 0x20000000);
}
