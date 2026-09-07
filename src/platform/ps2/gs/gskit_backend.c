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
    int new_dw1 = 0; // REINCLUÍDA: Correção vital para evitar o erro de compilação (Exit Code 2)

    if (!_gsk_initialised || !gs) {
        return;
    }

    /* Força o alinhamento de display do sinal de alta definição de forma
       perfeita para impedir cortes de texto na interface e nos jogos */
    dw     = 1280; // Força a largura de saída digital estável
    dh     = 720;  // Altura padrão do sinal progressivo
    magh   = 1;    // Multiplicador horizontal limpo do PCRTC
    startx = 232;  // Centraliza o início da varredura horizontal
    starty = 42;   // Centraliza o início da varredura vertical

    if (g_GskOverscan > 0)
    {
        int sx = (dw * g_GskOverscan) / 1300;
        int sy = (dh * g_GskOverscan) / 1300;
        dw     = dw - sx * 2;
        dh     = dh - sy * 2;
        startx = startx + sx;
        starty = starty + sy;
    }

    if (g_GskWidescreen)
    {
        int magh1  = magh + 1;
        int srcpix = magh1 ? dw / magh1 : dw;
        int new_magh1 = (magh1 * 4 + 1) / 3;

        if (new_magh1 > 16) new_magh1 = 16;
        if (new_magh1 < 1)  new_magh1 = 1;
        new_dw1 = new_magh1 * srcpix;

        startx -= (new_dw1 - dw) / 2;
        dw   = new_dw1;
        magh = new_magh1 - 1;
    }

    /* Trava os parâmetros de enquadramento anamórfico e impede que chamadores
       antigos rebaixem a resolução horizontal do emulador */
    gs->DW     = dw;
    gs->DH     = dh;
    gs->MagH   = magh;
    gs->MagV   = 1; // Força magnificação vertical estável
    gs->StartX = startx;
    gs->StartY = starty;

    /* Re-emite o offset de tela sincronizado com o novo espaço 1280x512 */
    gsKit_set_display_offset(gs, g_GskDispOffX * _gsk_vck, g_GskDispOffY + _gsk_game_y_bias);
    _GskApplyRenderTransform();
}

void GSK_Set240pVisibleWindow(int x, int width)
...
(o resto do código da Caixa 2 continua igual abaixo)
