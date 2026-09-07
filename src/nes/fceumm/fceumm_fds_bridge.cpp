    GPPrimSetTex(texTBP, 320, 9, 8, GS_PSMT8, clutTBP, 64, GS_PSMCT32, 0);

    Uint32 mod = (Uint32)(128.0f * intensity + 0.5f);
    if (mod > 128u) mod = 128u;
    Uint32 modColor = 0x80000000u | (mod << 16) | (mod << 8) | mod;
    Uint32 startY = (Uint32)logicalY + 16u;

    GPPrimTexRect(0, startY << 4, 8, 8, 1280u << 4, (startY + 480u) << 4, (256u << 4) + 8u, 248u << 4, 10u << 4, modColor, 0);
    return true;
}
    GPPrimSetTex(texTBP, 320, 9, 8, GS_PSMT8, clutTBP, 64, GS_PSMCT32, 0);

    Uint32 mod = (Uint32)(128.0f * intensity + 0.5f);
    if (mod > 128u) mod = 128u;
    Uint32 modColor = 0x80000000u | (mod << 16) | (mod << 8) | mod;
    Uint32 startY = (Uint32)logicalY + 16u;

    GPPrimTexRect(0, startY << 4, 8, 8, 1280u << 4, (startY + 480u) << 4, (256u << 4) + 8u, 248u << 4, 10u << 4, modColor, 0);
    return true;
}
