#ifndef _AURORA_GBHOST_H
#define _AURORA_GBHOST_H

#include "types.h"

/* AURORA_SGB_GBHOST_V0_3_20260904
 * AURORA_SGB_RUNTIME_V0_4_20260904
 * Neutral mGBA GB/SM83 adapter. This unit has no SNES/browser knowledge.
 * The SGB client supplies JOYP/scanline hooks; a future standalone GB client
 * can use the same host with different hooks without changing mGBA itself.
 */
class GBHost
{
public:
    enum ModelE { MODEL_SGB1 = 1, MODEL_SGB2 = 2 };
    enum { SERIALIZED_BYTES = 0x11800 };

    typedef Uint8 (*JoypHookT)(void *pContext, Bool bP14, Bool bP15, Bool bWrite);
    typedef void (*ScanlineHookT)(void *pContext, Int32 y, const Uint8 *pShade160);
    typedef void (*LineHookT)(void *pContext, Int32 y);

    struct StateT
    {
        Uint32 Magic;
        Uint32 Version;
        Uint32 Model;
        Uint32 Reserved;
        Int64 ClockCredit;
        Uint8 Serialized[SERIALIZED_BYTES];
    };

    GBHost();
    ~GBHost();
    Bool Init();
    void Shutdown();
    Bool IsInitialized() const;
    Bool IsLoaded() const;
    Bool LoadROM(const Uint8 *pData, Uint32 nBytes, ModelE eModel);
    void UnloadROM();
    void Reset(ModelE eModel);

    /* Full mGBA savedata image: SRAM plus mapper footer/RTC bytes. */
    Bool AttachSavedata(const Uint8 *pData, Uint32 nBytes);
    Uint32 GetSavedataBytes();
    Bool ExportSavedata(Uint8 *pData, Uint32 nCapacity, Uint32 *pActualBytes);
    Bool SavedataDirty() const;
    void ClearSavedataDirty();

    /* AURORA_SGB_COOPERATIVE_GB_RUNTIME_V0_6_16_20260905
       Cooperative SM83 stepping. ClockCredit is a signed timing balance:
       positive = overshoot credit, negative = unfinished timing debt. */
    Uint32 RunClocks(Uint32 nTargetClocks);
    /* AURORA_SGB_PRE_EVENT_HARD_PROBE_V0_6_17_20260905: diagnostic-only snapshot; no scheduler mutation. */
    Uint32 DebugPreTickState() const;
    const char *DebugPreEventName() const; /* AURORA_SGB_DUE_EVENT_IDENTITY_PROBE_V0_6_20_20260905 */
    Int32 DebugPreEventDelta() const;
    Bool DebugSkipDueAudioSample(); /* AURORA_SGB_SKIP_FIRST_AUDIO_EVENT_PROBE_V0_6_21_20260905 */
    Uint32 GetClockHz() const;
    Int64 GetClockCredit() const;
    Uint32 GetROMBytes() const;
    Uint32 GetROMCRC() const;
    /* AURORA_SGB_AUDIO_V0_5_20260904 -- raw mGBA PSG FIFO. */
    Uint32 ReadAudioFrames(Int16 *pStereoInterleaved, Uint32 nFrames);
    void ClearAudio();
    void SetHooks(JoypHookT pJoyp, ScanlineHookT pScanline, LineHookT pLine, void *pContext);
    Bool SaveState(StateT *pState) const;
    Bool RestoreState(const StateT *pState);

private:
    struct Impl;
    Impl *m_p;
    static Uint8 JoypThunk(void *pContext, int p14, int p15, int isWrite);
    static void ScanlineThunk(void *pContext, int y, const unsigned short *pRow);
    static void LineThunk(void *pContext, int y);
    void SyncSavedataFooter();
};

#endif
