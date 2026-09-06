
#ifndef _SNES_H
#define _SNES_H

#include "emusys.h"

extern "C" {
#include "sndisasm.h"
#include "sncpu.h"
#include "snspcdisasm.h"
#include "snspc.h"
}
#include "snspcio.h"
#include "snspcdsp.h"
#include "snspcmix.h"
#include "snio.h"
#include "snppu.h"
#include "sndma.h"
#include "snrom.h"
#include "snppurender.h"
#include "sndebug.h"

#include "sndsp1.h"
#include "sndsp2.h"
#include "sndsp4.h"
#include "snobc1.h"
#include "sncx4.h"
#include "sngsu.h"
#include "snsdd1.h"
#include "snsrtc.h"
#include "snswc.h" /* AURORA_SWC_FLOPPY_V1_20260831 */
#include "snsa1.h" /* AURORA_SA1_V1_REFERENCE_LOGIC_20260902 */
#include "snsgb.h" /* AURORA_SGB_RUNTIME_V0_4_20260904 */

#define SNES_RAMSIZE  0x20000
#define SNES_SRAMSIZE (256 * 1024)

#define SNES_DSP1 1

/* AURORA_SNES_NATIVE_32K_V1_20260822
 * SNES S-DSP synthesis is fixed to its native 32 kHz domain. The setter is
 * retained only so existing config/call sites remain source-compatible. */
void SnesAudioSetRate(Uint32 hz);
Uint32 SnesAudioGetRate(void);

class SnesSystem : public Emu::System
{
public:
    SnesSystem();
    ~SnesSystem();
    SNCpuT *GetCpu() {return &m_Cpu;}
    SNSpcT *GetSpc() {return &m_Spc;}
    SnesPPU *GetPPU() {return &m_PPU;}

    Uint32	GetFrame() {return m_uFrame;}
    Uint8	*GetSRAM() {return m_SRam;}

    void 	SetRom(class Emu::Rom *pRom);
    void	SetSnesRom(SnesRom *pRom);
    /* AURORA_SWC_FLOPPY_V1_20260831 -- isolated copier mode. */
    Bool    LoadSuperWildCard(const Char *pFirmwarePath, const Char *pDiskPath);
    Bool    LoadSuperMagicom(const Char *pFirmwarePath, const Char *pDiskPath); /* AURORA_V6_MAGICOM_FRONT_FAREAST_20260831 */
    Bool    SwapSuperWildCardDisk(const Char *pDiskPath);
    void    ShutdownSuperWildCard();
    /* AURORA_FRONT_COPIER_POWER_CYCLE_V2_20260902
     * Host-requested copier power cycle: keep inserted media/topology but
     * return the Front copier and the SNES to their boot/BIOS state. */
    void    PowerCycleFrontCopier();
    /* Legacy predicate intentionally means active classic Front copier. */
    /* AURORA_SGB_RUNTIME_V0_4_20260904 */
    Bool AttachSuperGameBoyGame(const Uint8 *pData, Uint32 nBytes, Bool bSgb2);
    void DetachSuperGameBoyGame();
    Bool IsSuperGameBoy() const { return m_SGB.IsActive(); }
    Uint32 GetSuperGameBoyGameBytes() const { return m_SGB.GetGameBytes(); }
    Uint32 GetSuperGameBoyGameCRC() const { return m_SGB.GetGameCRC(); }
    Uint32 GetSuperGameBoySavedataBytes() { return m_SGB.GetSavedataBytes(); }
    Bool LoadSuperGameBoySavedata(const Uint8 *pData, Uint32 nBytes) { return m_SGB.AttachSavedata(pData, nBytes); }
    Bool ExportSuperGameBoySavedata(Uint8 *pData, Uint32 nCap, Uint32 *pActual) { return m_SGB.ExportSavedata(pData, nCap, pActual); }
    Bool IsSuperGameBoySavedataDirty() const { return m_SGB.SavedataDirty(); }
    void ClearSuperGameBoySavedataDirty() { m_SGB.ClearSavedataDirty(); }

    Bool    IsSuperWildCard() const { return m_bSuperWildCard; }
    Bool    IsSuperWildCardFirmwareMode() const
        { return m_bSuperWildCard && m_SWC.IsFirmwareMode(); }
    Bool    IsSuperMagicom() const { return m_bSuperWildCard && m_SWC.IsSuperMagicom(); }
    const Char *GetFrontCopierName() const { return m_SWC.GetModelName(); }
    const Char *GetSuperWildCardDiskPath() const { return m_SWC.GetDiskPath(); }
    const Char *GetSuperWildCardError() const { return m_SWC.GetLastError(); }
    /* AURORA_FRONT_TRACE_V10_8_20260831 */
    Bool ConsumeFrontCopierDebugTransition(
        SNSuperWildCard::DebugTransitionT *pOut)
    {
        return m_bSuperWildCard ? m_SWC.ConsumeDebugTransition(pOut) : FALSE;
    }
    /* AURORA_SWC_FLOPPY_V5_20260831 */
    Bool    InsertSuperWildCardCartridge(SnesRom *pRom);
    void    EjectSuperWildCardCartridge();
    Bool    HasSuperWildCardCartridge() const { return m_SWC.HasExternalCartridge(); }

    /* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901 */
    Int32   GetSuperWildCardCartridgeSRAMBytes() const
        { return (Int32)m_SWC.GetExternalCartridgeSRAMBytes(); }
    Uint8  *GetSuperWildCardCartridgeSRAMData()
        { return m_SWC.GetExternalCartridgeSRAMData(); }
    Bool    HasSuperWildCardCartridgeBatterySRAM() const
        { return m_SWC.HasExternalCartridgeBatterySRAM(); }
    Bool    IsSuperWildCardCartridgeSRAMDirty() const
        { return m_SWC.IsExternalCartridgeSRAMDirty(); }
    void    ClearSuperWildCardCartridgeSRAMDirty()
        { m_SWC.ClearExternalCartridgeSRAMDirty(); }

    Bool    HasSuperWildCardDisk() const { return m_SWC.HasDisk(); }
    /* AURORA_SWC_MEDIA_PROBE_V10_2_20260831 */
    Bool    IsSuperWildCardDiskWritable() const { return m_SWC.IsDiskWritable(); }
    void	Reset();
	void	SoftReset();
	void	ExecuteFrame(Emu::SysInputT *pInput, class CRenderSurface *pTarget, class CMixBuffer *pSound, ModeE eMode);

	/* Host-only peripheral injection; SysInputT/save-state layout stays fixed. */
	void	SetMouseInput(Bool bConnected, Int32 nDeltaX, Int32 nDeltaY, Uint32 uButtons)
	{
		m_IO.SetMouseInput(bConnected, nDeltaX, nDeltaY, uButtons);
	}

    void	SaveState(struct SnesStateT *pState);
    Bool	RestoreState(struct SnesStateT *pState);

    void    SaveState(void *pState, Int32 nStateBytes);
    void    RestoreState(void *pState, Int32 nStateBytes);
    Int32   GetStateSize();
    /* AURORA_SWC_FLOPPY_V4_20260831 */
    Bool    SaveStateChecked(void *pState, Int32 nStateBytes);
    Bool    RestoreStateChecked(void *pState, Int32 nStateBytes);

    /* AURORA_CX4_STATE_V7
     * CX4 data is packed into unused tail bytes of the legacy 256 KiB SRAM
     * field, preserving sizeof(SnesStateT) and normal SNES state files. */
    Bool    CanSerializeCX4State();

    /* AURORA_SPECIAL_CHIP_STATE_V1 */
    Bool    CanSerializeSpecialChipState();

    Int32   GetSRAMBytes();
    Uint8   *GetSRAMData();

	virtual const char *GetString(StringE eString);
    virtual Uint32 GetSampleRate() {return SNSPCDSP_SAMPLERATE;}

    static const Char *GetRegName(Uint32 uAddr);


private:
	SNCpuT		m_Cpu;
	SnesPPU		m_PPU;
	SnesDMAC	m_DMAC;
	SnesIO		m_IO;
	SNSpcIO		m_SpcIO;
	SNSpcT		m_Spc;
	SNSpcDsp	m_SpcDsp;

	// extra hardware
	ISNDSP		*m_pDsp;

#if SNES_DSP1
	SNDSP1		m_DSP1;
	SNDSP2		m_DSP2;
	// DSP-4 (Top Gear 3000): HLE self-contained, sem firmware.
	SNDSP4		m_DSP4;
#endif

	SNOBC1		m_OBC1;

	SNCX4		m_CX4;

	// SuperFX / GSU (Star Fox, Yoshi's Island, etc.) -- core experimental
	SNGSU		m_GSU;

	SNSDD1		m_SDD1;
	Bool		m_bSDD1;

	SNSRTC		m_SRTC;
	Bool		m_bSRTC;
	Bool		m_bSuperFX;   // cartucho usa SuperFX/GSU -> rotear $3000-34FF

	SNSA1       m_SA1;      /* AURORA_SA1_V1_REFERENCE_LOGIC_20260902 */
	Bool        m_bSA1IRQ;

	/* AURORA_SWC_FLOPPY_V1_20260831 */
	SNSuperWildCard m_SWC;
    SNSuperGameBoy m_SGB;
    Uint32 m_uSGBSyncClock;
	Bool            m_bSuperWildCard;

	SnesRom		*m_pRom;
Bool            m_bRegionLocked;
	SnesPPURender	m_PPURender;
	SNSpcDspMixFull		m_SpcDspMixer;		    // non-deterministic mixer
	SNSpcDspMixSilent	m_SpcDspSilentMixer;    // deterministic mixer

	Uint32		m_uSramSize;
#if SNES_HVIRQ_RESCHEDULE
	/* AURORA_HVIRQ_RESCHEDULE_V4 -- transient per-scanline scheduler state. */
	Bool		m_bLineIRQActive;
	Bool		m_bLineIRQReschedule;
	Bool		m_bLineIRQFired;
	Bool		m_bLineIRQInstant;
	Int32		m_nLineIRQCycle;
	Int32		m_nLineIRQClock;
#endif
	Uint8		m_Ram[SNES_RAMSIZE] _ALIGN(16);
	Uint8		m_SRam[SNES_SRAMSIZE] _ALIGN(16);


private:
	static Uint8 SNCPU_TRAPFUNC ReadMem(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteMem(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC Read2000(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  Write2000(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC Read4000(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  Write4000(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadSRAM(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteSRAM(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadDSP1(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteDSP1(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadOBC1(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteOBC1(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadCX4(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteCX4(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadGSU(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteGSU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadSWC(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteSWC(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
    static Uint8 SNCPU_TRAPFUNC ReadSGB(SNCpuT *pCpu, Uint32 uAddr);
    static void SNCPU_TRAPFUNC WriteSGB(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
    void MapSuperGameBoy();
    void SyncSuperGameBoy();
	static Uint8 SNCPU_TRAPFUNC ReadSA1BWRAM(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteSA1BWRAM(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 SNCPU_TRAPFUNC ReadSA1ROM(SNCpuT *pCpu, Uint32 uAddr);
	static void SNCPU_TRAPFUNC  WriteSA1ROM(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
	static Uint8 CX4ReadMem(void *pCtx, Uint32 uAddr);

    static Uint8 SNCPU_TRAPFUNC Read2000Debug(SNCpuT *pCpu, Uint32 uAddr);
    static Uint8 SNCPU_TRAPFUNC Read4000Debug(SNCpuT *pCpu, Uint32 uAddr);
    static void SNCPU_TRAPFUNC  Write2000Debug(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);
    static void SNCPU_TRAPFUNC  Write4000Debug(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);



	void	MapLoRom();
	void	MapHiRom();
	void	MapMem(struct SnesMemMapT *pMemMap);
	void	MapMem(SNRomMappingE eRomMapping, Uint32 uFlags);
	void	MapMemExLoRom(void);
	void	MapSuperWildCard(void); /* AURORA_SWC_FLOPPY_V1_20260831 */
	void	MapSuperWildCardDevice(struct SnesMemMapT *pMemMap);
	void	MapSuperWildCardCoprocessor(void);
	void    SetSA1IRQ(Bool bPending);
	void    UpdateMainIRQLine(void);
	void    MarkSA1BWRAMDirty(void);
	friend class SNSA1;
	void	RemapSuperWildCardMode0Dram(void); /* AURORA_SWC_V11_MENU_FASTPATH_20260831 */
	void	RemapSDD1(void);   // (re)mapeia $C0-$FF conforme $4804-$4807
	void	DumpMemMap();

	void	SetFastRom();
	void	SetSlowRom();

	void	SyncSPC(Int32 uExtra = 0);
	void	SyncPPU();
#if SNES_HVIRQ_RESCHEDULE
	Int32	CalculateLineIRQCycle();
	void	RescheduleLineIRQ(Bool bAllowImmediate);
#endif
	void	ExecuteLine();
    void    ExecuteWithIRQ(Int32 nCycles, Int32 &nIRQCycles);
    void    ExecuteCPU(Int32 nExecCycles);
};

void SnesDebugBegin(SnesSystem *pSnes, const char *pFileName);
void SnesDebugEnd();

#endif

