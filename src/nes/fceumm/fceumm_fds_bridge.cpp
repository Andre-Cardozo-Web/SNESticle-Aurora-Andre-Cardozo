#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fceumm_fds_bridge.h"
#include "types.h"
#include "rendersurface.h"
extern "C" {
#include "gs.h"
#include "gpprim.h"
}
static bool s_GameLoaded = true;
static bool s_DirectReady = true;
static bool s_DirectClutResident = false;
static Uint32 s_DirectFrameSerial = 1;
static Uint32 s_DirectUploadSerial = 0;
static Uint8 s_VideoBuffer[256 * 240];
static Uint32 s_GsPalette[256];
bool FceummFdsBridge_DrawDirectGs(Uint32 auroraOutBaseTBP, Int32 logicalY, Float32 intensity) {
    if (!auroraOutBaseTBP || !s_GameLoaded || !s_DirectReady) return false;
    Uint32 texTBP = auroraOutBaseTBP + 0x400;
    Uint32 clutTBP = auroraOutBaseTBP + 0x580;
    const Uint8 *uploadPixels = s_VideoBuffer;
    if (s_DirectUploadSerial != s_DirectFrameSerial) {
        GPPrimUploadTexture((int)texTBP, 320, 0, 0, GS_PSMT8, uploadPixels, 256, 240);
        s_DirectUploadSerial = s_DirectFrameSerial;
    }
    if (!s_DirectClutResident) {
        GPPrimUploadTexture((int)clutTBP, 64, 0, 0, GS_PSMCT32, s_GsPalette, 16, 16);
        s_DirectClutResident = true;
    }
    GPPrimSetTex(texTBP, 320, 9, 8, GS_PSMT8, clutTBP, 64, GS_PSMCT32, 0);
    Uint32 mod = (Uint32)(128.0f * intensity + 0.5f);
    if (mod > 128u) mod = 128u;
    Uint32 modColor = 0x80000000u | (mod << 16) | (mod << 8) | mod;
    Uint32 startY = (Uint32)logicalY + 16u;
    GPPrimTexRect(0, startY << 4, 8, 8, 1280u << 4, (startY + 480u) << 4, (256u << 4) + 8u, 248u << 4, 10u << 4, modColor, 0);
    return true;
}
