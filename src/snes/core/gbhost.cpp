#include <string.h>
#include <new>
#include "gbhost.h"

/* AURORA_SGB_ATTACH_TRACE_V0_6_3_20260905 */
extern "C" void AuroraSgbBootTrace(const char *pText);

/* AURORA_SGB_RUNTIME_V0_4_20260904 */

extern "C" {
#include <mgba/core/cpu.h>
#include <mgba/core/timing.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/mbc.h>
#include <mgba/internal/gb/serialize.h>
#include <mgba/internal/gb/video.h>
#include <mgba/internal/gb/renderers/software.h>
#include <mgba/internal/sm83/sm83.h>
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>
#include "aurora-hooks.h"
}

typedef char GBHostSerializedSizeCheck[
    sizeof(struct GBSerializedState) == GBHost::SERIALIZED_BYTES ? 1 : -1
];

struct GBHost::Impl
{
    struct GB gb;
    struct SM83Core cpu;
    struct GBVideoSoftwareRenderer renderer;
    struct mCPUComponent *components[CPU_COMPONENT_MAX];
    mColor frame[GB_VIDEO_HORIZONTAL_PIXELS * GB_VIDEO_VERTICAL_PIXELS];
    Uint8 keys;
    ModelE model;
    Int64 clockCredit;
    Bool initialized;
    Bool loaded;
    JoypHookT joypHook;
    ScanlineHookT scanlineHook;
    LineHookT lineHook;
    void *hookContext;
};

GBHost::GBHost() : m_p(NULL) {}
GBHost::~GBHost() { Shutdown(); }

Bool GBHost::Init()
{
    if (m_p && m_p->initialized) return TRUE;
    if (m_p) Shutdown();

    AuroraSgbBootTrace("SGB 4C1: alloc GBHost");
    m_p = new (std::nothrow) Impl;
    if (!m_p) return FALSE;
    memset(m_p, 0, sizeof(*m_p));

    AuroraSgbBootTrace("SGB 4C2: GBCreate");
    GBCreate(&m_p->gb);

    AuroraSgbBootTrace("SGB 4C3: SM83 setup");
    SM83SetComponents(&m_p->cpu, &m_p->gb.d, CPU_COMPONENT_MAX, m_p->components);

    AuroraSgbBootTrace("SGB 4C4: SM83 init");
    SM83Init(&m_p->cpu);

    AuroraSgbBootTrace("SGB 4C5: renderer");
    GBVideoSoftwareRendererCreate(&m_p->renderer);
    m_p->renderer.outputBuffer = m_p->frame;
    m_p->renderer.outputBufferStride = GB_VIDEO_HORIZONTAL_PIXELS;

    /* AURORA_SGB_DISABLE_MGBA_BORDER_V0_6_8_20260905
     * mGBA is only the embedded 160x144 GB engine here. The real SGB border
     * belongs to the SNES firmware/ICD2 path. Leaving mGBA sgbBorders enabled
     * lets its software renderer regenerate a 256x224 border into our 160x144
     * output buffer when LCDC/palette 0 is initialized. */
    m_p->gb.video.sgbBorders = false;

    GBVideoAssociateRenderer(&m_p->gb.video, &m_p->renderer.d);

    m_p->keys = 0;
    m_p->gb.keySource = &m_p->keys;
    m_p->model = MODEL_SGB1;
    m_p->initialized = TRUE;
    mGBAAuroraSetHooks(NULL, NULL, NULL, NULL);
    AuroraSgbBootTrace("SGB 4C6: init ready");
    return TRUE;
}

void GBHost::Shutdown()
{
    if (!m_p) return;
    mGBAAuroraSetHooks(NULL, NULL, NULL, NULL);
    if (m_p->initialized) {
        SM83Deinit(&m_p->cpu);
        GBDestroy(&m_p->gb);
    }
    delete m_p;
    m_p = NULL;
}

Bool GBHost::IsInitialized() const { return m_p && m_p->initialized ? TRUE : FALSE; }
Bool GBHost::IsLoaded() const { return m_p && m_p->loaded ? TRUE : FALSE; }

void GBHost::UnloadROM()
{
    if (!m_p || !m_p->initialized || !m_p->loaded) return;
    GBUnloadROM(&m_p->gb);
    m_p->loaded = FALSE;
    m_p->clockCredit = 0;
}

/* AURORA_SGB_RESET_ONCE_V0_6_1_20260904: SM83Reset already dispatches GBReset. */
Bool GBHost::LoadROM(const Uint8 *pData, Uint32 nBytes, ModelE eModel)
{
    struct VFile *pRom;
    if (!pData || nBytes < 0x150U) return FALSE;

    AuroraSgbBootTrace("SGB 4D1: ensure init");
    if (!Init()) return FALSE;

    if (m_p->loaded) {
        AuroraSgbBootTrace("SGB 4D2: unload old ROM");
        UnloadROM();
    }

    AuroraSgbBootTrace("SGB 4D3: copy ROM VFile");
    pRom = VFileMemChunk(pData, (size_t)nBytes);
    if (!pRom) return FALSE;

    AuroraSgbBootTrace("SGB 4D4: GBLoadROM");
    if (!GBLoadROM(&m_p->gb, pRom)) {
        GBUnloadROM(&m_p->gb);
        return FALSE;
    }

    AuroraSgbBootTrace("SGB 4D5: ROM loaded");
    m_p->model = (eModel == MODEL_SGB2) ? MODEL_SGB2 : MODEL_SGB1;
    m_p->gb.model = (m_p->model == MODEL_SGB2) ? GB_MODEL_SGB2 : GB_MODEL_SGB;
    m_p->clockCredit = 0;
    m_p->loaded = TRUE;

    AuroraSgbBootTrace("SGB 4D6: SM83 reset");
    /* SM83Reset dispatches GBReset through irqh.reset; do it once. */
    SM83Reset(&m_p->cpu);
    /* AURORA_SGB_SCHEDULER_PRIME_V0_6_18_20260905
     * SM83Reset sets nextEvent=0 before GBReset rebuilds mTiming.
     * Publish the actual first queued deadline afterwards. This
     * executes/skips no event; it only repairs the stale deadline. */
    /* AURORA_SGB_ABSOLUTE_DEADLINE_PRIME_V0_6_19_20260905
     * mTimingNextEvent() returns a DELTA from the current relative
     * cycle position. cpu.nextEvent is an absolute threshold in that
     * same relative-cycle coordinate, so add cpu.cycles here.
     * When GBProcessEvents calls mTimingNextEvent internally it has
     * already zeroed cpu.cycles, which is why the raw delta is valid
     * there but was wrong in our post-reset prime. */
    {
        Int32 delta = mTimingNextEvent(&m_p->gb.timing);
        m_p->cpu.nextEvent = (delta == INT_MAX)
            ? INT_MAX
            : (m_p->cpu.cycles + delta);
    }

    AuroraSgbBootTrace("SGB 4D7: reset returned");
    return TRUE;
}

void GBHost::Reset(ModelE eModel)
{
    if (!m_p || !m_p->initialized || !m_p->loaded) return;
    m_p->model = (eModel == MODEL_SGB2) ? MODEL_SGB2 : MODEL_SGB1;
    m_p->gb.model = (m_p->model == MODEL_SGB2) ? GB_MODEL_SGB2 : GB_MODEL_SGB;
    m_p->clockCredit = 0;
    SM83Reset(&m_p->cpu);
    /* AURORA_SGB_SCHEDULER_PRIME_V0_6_18_20260905
     * SM83Reset sets nextEvent=0 before GBReset rebuilds mTiming.
     * Publish the actual first queued deadline afterwards. This
     * executes/skips no event; it only repairs the stale deadline. */
    /* AURORA_SGB_ABSOLUTE_DEADLINE_PRIME_V0_6_19_20260905
     * mTimingNextEvent() returns a DELTA from the current relative
     * cycle position. cpu.nextEvent is an absolute threshold in that
     * same relative-cycle coordinate, so add cpu.cycles here.
     * When GBProcessEvents calls mTimingNextEvent internally it has
     * already zeroed cpu.cycles, which is why the raw delta is valid
     * there but was wrong in our post-reset prime. */
    {
        Int32 delta = mTimingNextEvent(&m_p->gb.timing);
        m_p->cpu.nextEvent = (delta == INT_MAX)
            ? INT_MAX
            : (m_p->cpu.cycles + delta);
    }
}

Bool GBHost::AttachSavedata(const Uint8 *pData, Uint32 nBytes)
{
    struct VFile *pSave;
    if (!m_p || !m_p->initialized || !m_p->loaded || (nBytes && !pData)) return FALSE;
    pSave = VFileMemChunk(pData, (size_t)nBytes);
    if (!pSave) return FALSE;
    if (!GBLoadSave(&m_p->gb, pSave)) {
        pSave->close(pSave);
        return FALSE;
    }
    return TRUE;
}

void GBHost::SyncSavedataFooter()
{
    if (!m_p || !m_p->loaded || !m_p->gb.sramVf) return;
    switch (m_p->gb.memory.mbcType) {
        case GB_MBC3_RTC: GBMBCRTCWrite(&m_p->gb); break;
        case GB_HuC3:     GBMBCHuC3Write(&m_p->gb); break;
        case GB_TAMA5:    GBMBCTAMA5Write(&m_p->gb); break;
        default: break;
    }
    if (m_p->gb.memory.sram && m_p->gb.sramSize)
        (void)m_p->gb.sramVf->sync(m_p->gb.sramVf, m_p->gb.memory.sram, m_p->gb.sramSize);
}

Uint32 GBHost::GetSavedataBytes()
{
    ssize_t n;
    if (!m_p || !m_p->loaded || !m_p->gb.sramVf) return 0;
    SyncSavedataFooter();
    n = m_p->gb.sramVf->size(m_p->gb.sramVf);
    return n > 0 && (Uint64)n <= 0xffffffffULL ? (Uint32)n : 0;
}

Bool GBHost::ExportSavedata(Uint8 *pData, Uint32 nCapacity, Uint32 *pActualBytes)
{
    ssize_t n, got;
    if (pActualBytes) *pActualBytes = 0;
    if (!m_p || !m_p->loaded || !m_p->gb.sramVf) return FALSE;
    SyncSavedataFooter();
    n = m_p->gb.sramVf->size(m_p->gb.sramVf);
    if (n < 0 || (Uint64)n > 0xffffffffULL) return FALSE;
    if (pActualBytes) *pActualBytes = (Uint32)n;
    if (!n) return TRUE;
    if (!pData || nCapacity < (Uint32)n) return FALSE;
    if (m_p->gb.sramVf->seek(m_p->gb.sramVf, 0, SEEK_SET) < 0) return FALSE;
    got = m_p->gb.sramVf->read(m_p->gb.sramVf, pData, (size_t)n);
    return got == n ? TRUE : FALSE;
}

Bool GBHost::SavedataDirty() const { return m_p && m_p->loaded && m_p->gb.sramDirty ? TRUE : FALSE; }
void GBHost::ClearSavedataDirty() { if (m_p) { m_p->gb.sramDirty = 0; m_p->gb.sramDirtAge = 0; } }

/* AURORA_SGB_PRE_EVENT_HARD_PROBE_V0_6_17_20260905
 * Snapshot immediately before the public SM83Tick event pump.
 * Bits:
 *  0: cpu.cycles >= cpu.nextEvent (SM83Tick enters processEvents first)
 *  1: GB cpuBlocked already set
 *  2: GB earlyExit already set
 *  3: timing queue has no root/reroot event
 *  4: cpu.nextEvent is negative
 * 31: host invalid/unloaded
 */
/* AURORA_SGB_DUE_EVENT_IDENTITY_PROBE_V0_6_20_20260905
 * Read-only scheduler introspection. No callback is executed here. */
const char *GBHost::DebugPreEventName() const
{
    const struct mTimingEvent *ev;
    if (!m_p || !m_p->loaded)
        return NULL;

    ev = m_p->gb.timing.root;
    if (!ev)
        ev = m_p->gb.timing.reroot;

    if (!ev || !ev->name)
        return NULL;
    return ev->name;
}

Int32 GBHost::DebugPreEventDelta() const
{
    const struct mTimingEvent *ev;

    if (!m_p || !m_p->loaded)
        return INT_MAX;

    ev = m_p->gb.timing.root;
    if (!ev)
        ev = m_p->gb.timing.reroot;

    if (!ev)
        return INT_MAX;

    return (Int32)(
        (Int64)ev->when
        - (Int64)m_p->gb.timing.masterCycles
        - (Int64)m_p->cpu.cycles
    );
}

/* AURORA_SGB_SKIP_FIRST_AUDIO_EVENT_PROBE_V0_6_21_20260905
 * Diagnostic only:
 * - accept ONLY the exact GB Audio Sample timing event
 * - accept it ONLY while currently due
 * - do NOT execute its callback
 * - rearm it at the same normal interval used by mGBA _sample()
 * - republish cpu.nextEvent in the absolute cpu.cycles coordinate
 *
 * This skips one callback, not the audio subsystem. */
Bool GBHost::DebugSkipDueAudioSample()
{
    struct mTimingEvent *ev;
    Int64 due;
    Int64 delay;
    Int32 delta;

    if (!m_p || !m_p->loaded)
        return FALSE;

    ev = m_p->gb.timing.root;
    if (!ev)
        ev = m_p->gb.timing.reroot;

    if (ev != &m_p->gb.audio.sampleEvent)
        return FALSE;

    due =
        (Int64)ev->when
        - (Int64)m_p->gb.timing.masterCycles
        - (Int64)m_p->cpu.cycles;

    if (due > 0)
        return FALSE;

    delay =
        (Int64)m_p->gb.audio.sampleInterval
        * (Int64)m_p->gb.audio.timingFactor;

    if (delay <= 0 || delay > 0x7fffffffLL)
        return FALSE;

    mTimingDeschedule(&m_p->gb.timing, &m_p->gb.audio.sampleEvent);
    mTimingSchedule(
        &m_p->gb.timing,
        &m_p->gb.audio.sampleEvent,
        (Int32)delay
    );

    delta = mTimingNextEvent(&m_p->gb.timing);
    m_p->cpu.nextEvent = (delta == INT_MAX)
        ? INT_MAX
        : (m_p->cpu.cycles + delta);

    return TRUE;
}

Uint32 GBHost::DebugPreTickState() const
{
    Uint32 state = 0;
    if (!m_p || !m_p->loaded)
        return 0x80000000U;
    if (m_p->cpu.cycles >= m_p->cpu.nextEvent) state |= 0x01U;
    if (m_p->gb.cpuBlocked) state |= 0x02U;
    if (m_p->gb.earlyExit) state |= 0x04U;
    if (!m_p->gb.timing.root && !m_p->gb.timing.reroot) state |= 0x08U;
    if (m_p->cpu.nextEvent < 0) state |= 0x10U;
    return state;
}

Uint32 GBHost::RunClocks(Uint32 nTargetClocks)
{
    /* AURORA_SGB_NATIVE_RUNTIME_BOOT_ATTEMPT_V0_6_29_20260906
     *
     * Restore mGBA's native event-sized SM83 execution after the SGB HLE
     * handshake. V0.6.15 never entered this path (H96 held before RunClocks),
     * while V0.6.16 replaced it with per-unit SM83Tick stepping before runtime
     * was actually released. Let SM83Run own GBProcessEvents as upstream does.
     *
     * Aurora still budgets logical GB clocks. mGBA timing uses two timing ticks
     * per logical GB clock here, and positive event overshoot is carried to the
     * following call. Any old negative cooperative debt is discarded because
     * it belonged only to the retired V0.6.16 stepping scheme.
     */
    Uint64 need;
    Uint64 advanced = 0;
    Uint64 credit = 0;
    Uint32 guard = 0;
    Int32 before, after;

    if (!m_p || !m_p->loaded || !nTargetClocks)
        return 0;

    need = (Uint64)nTargetClocks * 2ULL;

    if (m_p->clockCredit > 0)
        credit = (Uint64)m_p->clockCredit;

    if (credit >= need)
    {
        m_p->clockCredit = (Int64)(credit - need);
        return nTargetClocks;
    }

    need -= credit;
    m_p->clockCredit = 0;

    /* SM83Run returns at an mGBA event boundary. Usually one call is enough
     * for Aurora's tiny SGB slices; the guard only bounds repeated boundaries,
     * not SM83Run itself. */
    while (advanced < need && guard++ < 64U)
    {
        Uint32 delta;

        before = mTimingCurrentTime(&m_p->gb.timing);
        SM83Run(&m_p->cpu);
        after = mTimingCurrentTime(&m_p->gb.timing);

        delta = (Uint32)after - (Uint32)before;

        /* Upstream normally advances in SM83Run. Keep one native public Tick
         * fallback only for a zero-progress boundary, then give control back. */
        if (!delta)
        {
            SM83Tick(&m_p->cpu);
            after = mTimingCurrentTime(&m_p->gb.timing);
            delta = (Uint32)after - (Uint32)before;
            if (!delta)
                break;
        }

        advanced += (Uint64)delta;
    }

    if (advanced > need)
        m_p->clockCredit = (Int64)(advanced - need);

    if (advanced >= need)
        return nTargetClocks;

    /* Partial progress is reported in logical GB clocks. */
    advanced += credit;
    if (advanced > (Uint64)nTargetClocks * 2ULL)
        advanced = (Uint64)nTargetClocks * 2ULL;
    return (Uint32)(advanced >> 1);
}

Uint32 GBHost::GetClockHz() const
{
    if (!m_p) return 0;
    return m_p->model == MODEL_SGB2 ? 4194304U : (Uint32)SGB_SM83_FREQUENCY;
}
Int64 GBHost::GetClockCredit() const { return m_p ? m_p->clockCredit : 0; }
Uint32 GBHost::GetROMBytes() const
{
    return (m_p && m_p->loaded && m_p->gb.pristineRomSize <= 0xffffffffULL)
        ? (Uint32)m_p->gb.pristineRomSize : 0;
}
Uint32 GBHost::GetROMCRC() const
{
    return (m_p && m_p->loaded) ? (Uint32)m_p->gb.romCrc32 : 0;
}

/* AURORA_SGB_AUDIO_V0_5_20260904
 * mGBA produces one stereo PSG sample per 32 logical GB clocks. Keep the
 * FIFO owned by GBHost; SNSuperGameBoy performs the cartridge-side resample.
 */
Uint32 GBHost::ReadAudioFrames(Int16 *pStereoInterleaved, Uint32 nFrames)
{
    size_t got;
    if (!m_p || !m_p->initialized || !m_p->loaded || !pStereoInterleaved || !nFrames)
        return 0;
    got = mAudioBufferRead(&m_p->gb.audio.buffer,
                           (int16_t *)pStereoInterleaved, (size_t)nFrames);
    return got <= 0xffffffffULL ? (Uint32)got : 0;
}

void GBHost::ClearAudio()
{
    if (m_p && m_p->initialized)
        mAudioBufferClear(&m_p->gb.audio.buffer);
}

void GBHost::SetHooks(JoypHookT pJoyp, ScanlineHookT pScanline, LineHookT pLine, void *pContext)
{
    if (!m_p) return;
    m_p->joypHook = pJoyp;
    m_p->scanlineHook = pScanline;
    m_p->lineHook = pLine;
    m_p->hookContext = pContext;
    mGBAAuroraSetHooks((pJoyp || pScanline || pLine) ? this : NULL,
                       pJoyp ? &GBHost::JoypThunk : NULL,
                       pScanline ? &GBHost::ScanlineThunk : NULL,
                       pLine ? &GBHost::LineThunk : NULL);
}

Uint8 GBHost::JoypThunk(void *pContext, int p14, int p15, int isWrite)
{
    GBHost *p = (GBHost *)pContext;
    if (!p || !p->m_p || !p->m_p->joypHook) return 0x0f;
    return p->m_p->joypHook(p->m_p->hookContext, p14 ? TRUE : FALSE, p15 ? TRUE : FALSE,
                            isWrite ? TRUE : FALSE) & 0x0fU;
}

void GBHost::ScanlineThunk(void *pContext, int y, const unsigned short *pRow)
{
    GBHost *p = (GBHost *)pContext;
    Uint8 shades[160];
    Int32 x;
    if (!p || !p->m_p || !p->m_p->scanlineHook || !pRow || y < 0 || y >= 144) return;
    for (x = 0; x < 160; ++x) shades[x] = (Uint8)(pRow[x] & 3U);
    p->m_p->scanlineHook(p->m_p->hookContext, y, shades);
}

void GBHost::LineThunk(void *pContext, int y)
{
    GBHost *p = (GBHost *)pContext;
    if (!p || !p->m_p || !p->m_p->lineHook || y < 0 || y >= 154) return;
    p->m_p->lineHook(p->m_p->hookContext, y);
}

Bool GBHost::SaveState(StateT *pState) const
{
    if (!pState || !m_p || !m_p->loaded) return FALSE;
    memset(pState, 0, sizeof(*pState));
    pState->Magic = 0x48424741U;
    pState->Version = 1;
    pState->Model = (Uint32)m_p->model;
    pState->ClockCredit = m_p->clockCredit;
    GBSerialize((struct GB *)&m_p->gb, (struct GBSerializedState *)pState->Serialized);
    return TRUE;
}

Bool GBHost::RestoreState(const StateT *pState)
{
    if (!pState || !m_p || !m_p->loaded || pState->Magic != 0x48424741U ||
        pState->Version != 1 || (pState->Model != MODEL_SGB1 && pState->Model != MODEL_SGB2))
        return FALSE;
    if (!GBDeserialize(&m_p->gb, (const struct GBSerializedState *)pState->Serialized)) return FALSE;
    /* The host-consumption FIFO is not part of the emulated machine state. */
    mAudioBufferClear(&m_p->gb.audio.buffer);
    m_p->model = (ModelE)pState->Model;
    /* AURORA_SGB_COOPERATIVE_GB_RUNTIME_V0_6_16_20260905: negative values are valid cooperative debt. */
    m_p->clockCredit = pState->ClockCredit;
    return TRUE;
}
