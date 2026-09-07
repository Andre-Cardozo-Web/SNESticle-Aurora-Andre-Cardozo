    if (s_DirectReady) { 
        if (++s_DirectFrameSerial == 0) { 
            s_DirectFrameSerial = 1; 
            s_DirectUploadSerial = 0; 
        } 
    } else {
        qRenderFrame(pTarget);
    }
    qDrainAudio(pMixBuf);
}

int QuicknesBridge_GetStateSize(void) 
{ 
    return s_GameLoaded ? QUICKNES_STATE_CAPACITY : 0; 
}

int QuicknesBridge_SaveState(void *p, int n) 
{ 
    if (!s_GameLoaded || !s_pEmu || !p || n <= 0) return 0; 
    Mem_Writer w(p, (long)(n > QUICKNES_STATE_CAPACITY ? QUICKNES_STATE_CAPACITY : n)); 
    return s_pEmu->save_state(w) ? 0 : (int)w.size(); 
}

bool QuicknesBridge_LoadState(const void *p, int n) 
{ 
    if (!s_GameLoaded || !p || n <= 0) return false; 
    Mem_File_Reader r(p, (long)n); 
    if (s_pEmu->load_state(r)) return false; 
    s_PendingCount = 0; s_PaletteValid = false; 
    qResetDirectVideo(); quicknes_snesticle_ext_reset_bus(); 
    return true; 
}

int QuicknesBridge_GetSRAMBytes(void) 
{ 
    return (s_GameLoaded && s_pEmu->cart() && s_pEmu->has_battery_ram()) ? (int)s_pEmu->battery_ram_size() : 0; 
}

uint8_t *QuicknesBridge_GetSRAMData(void) 
{ 
    return QuicknesBridge_GetSRAMBytes() <= 0 ? NULL : s_pEmu->high_mem(); 
}

bool QuicknesBridge_IsArkanoidVaus(void) 
{ 
    return s_ArkanoidVaus; 
}

bool QuicknesBridge_TurboFileEnabled(void) 
{ 
    return s_TurboFileEnabled; 
}

int QuicknesBridge_GetTurboFileBytes(void) 
{ 
    return 0x2000; 
}

uint8_t *QuicknesBridge_GetTurboFileData(void) 
{ 
    return quicknes_snesticle_ext_turbofile_data(); 
}

bool QuicknesBridge_TurboFileDirty(void) 
{ 
    return quicknes_snesticle_ext_turbofile_dirty() != 0; 
}

void QuicknesBridge_ClearTurboFileDirty(void) 
{ 
    quicknes_snesticle_ext_turbofile_clear_dirty(); 
}

void QuicknesBridge_SetLightGunEnabled(bool e) 
{ 
    s_LightGunEnabled = e; 
    int m = e ? s_LightGunDetectedMode : 0; 
    if (m != s_LightGunMode) { 
        s_LightGunMode = m; 
        quicknes_snesticle_ext_set_lightgun(s_LightGunMode); 
        quicknes_snesticle_ext_reset_bus(); 
        if (s_LightGunMode != 0) qResetLightGunAim(); 
    } 
}

bool QuicknesBridge_GetLightGunEnabled(void) 
{ 
    return s_LightGunEnabled; 
}

bool QuicknesBridge_LightGunActive(void) 
{ 
    return s_GameLoaded && s_LightGunMode != 0; 
}

void QuicknesBridge_GetLightGunCursor(Int32 *x, Int32 *y) 
{ 
    if (x) *x = s_GunX >> 8; 
    if (y) *y = s_GunY >> 8; 
}

void QuicknesBridge_DrawLightGunCursor(Int32 lY) 
{
    if (!QuicknesBridge_LightGunActive()) return; 
    Int32 x = s_GunX >> 8, y = (s_GunY >> 8) + lY;
    GPPrimRect((Uint32)(x - 6) << 4, (Uint32)y << 4, 0x80000000u, (Uint32)(x + 7) << 4, (Uint32)(y + 1) << 4, 0x80000000u, 9u << 4, 0);
    GPPrimRect((Uint32)x << 4, (Uint32)(y - 6) << 4, 0x80000000u, (Uint32)(x + 1) << 4, (Uint32)(y + 7) << 4, 0x80000000u, 9u << 4, 0);
    GPPrimRect((Uint32)(x - 5) << 4, (Uint32)y << 4, 0x80FFFFFFu, (Uint32)(x + 6) << 4, (Uint32)(y + 1) << 4, 0x80FFFFFFu, 9u << 4, 0);
    GPPrimRect((Uint32)x << 4, (Uint32)(y - 5) << 4, 0x80FFFFFFu, (Uint32)(x + 1) << 4, (Uint32)(y + 6) << 4, 0x80FFFFFFu, 9u << 4, 0);
}

bool QuicknesBridge_BattleBoxEnabled(void) 
{ 
    return s_BattleBoxEnabled; 
}

int QuicknesBridge_GetBattleBoxBytes(void) 
{ 
    return 0x0200; 
}

uint8_t *QuicknesBridge_GetBattleBoxData(void) 
{ 
    return quicknes_snesticle_ext_battlebox_data(); 
}

bool QuicknesBridge_BattleBoxDirty(void) 
{ 
    return quicknes_snesticle_ext_battlebox_dirty() != 0; 
}

void QuicknesBridge_ClearBattleBoxDirty(void) 
{ 
    quicknes_snesticle_ext_battlebox_clear_dirty(); 
}

unsigned QuicknesBridge_GetSampleRate(void) 
{ 
    return 32000; 
}
