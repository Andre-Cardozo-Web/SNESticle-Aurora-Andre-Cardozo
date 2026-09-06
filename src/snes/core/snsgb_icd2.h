#ifndef _SNSGB_ICD2_H
#define _SNSGB_ICD2_H

#include "types.h"

/* AURORA_SGB_ICD2_V0_2_20260904
 * Isolated ICD2 hardware bridge. V0.2 does not map it into SnesSystem yet.
 */
class SNSGBICD2
{
public:
    enum ModelE { MODEL_NONE = 0, MODEL_SGB1 = 1, MODEL_SGB2 = 2 };

    enum {
        PACKET_BYTES = 16,
        LCD_BANKS = 4,
        LCD_BANK_BYTES = 512,
        LCD_VISIBLE_BYTES = 320,
        LCD_WIDTH = 160,
        LCD_VISIBLE_LINES = 144,
        LCD_TOTAL_LINES = 154
    };

    struct StateT {
        Uint32 magic, version, model, icdRevision;
        Uint32 control, packetReady, readBank, readAddress, writeBank;
        Int32 vcounter;
        Uint32 joypID, previousP15, pulseLock, strobeLock, packetLock;
        Uint32 packetOffset, bitData, bitOffset, resetRequested;
        Uint8 controller[4];
        Uint8 packet[PACKET_BYTES];
        Uint8 output[LCD_BANKS][LCD_BANK_BYTES];
        Uint64 clockAccumulator;
    };

    SNSGBICD2();
    void Reset(ModelE eModel);

    ModelE GetModel() const { return m_eModel; }
    Bool IsEnabled() const { return m_eModel != MODEL_NONE; }
    Bool IsRunning() const { return (m_uControl & 0x80U) ? TRUE : FALSE; }
    Bool ConsumeResetRequest();

    Uint8 Read(Uint32 uAddr);
    void Write(Uint32 uAddr, Uint8 uData);

    /* Feed raw GB JOYP P14/P15 levels; returns active-low ICD input nibble. */
    Uint8 JoypRead(Bool bP14, Bool bP15) const;
    Uint8 JoypWrite(Bool bP14, Bool bP15);
    void SubmitPacket(const Uint8 *pPacket);

    /* EndLCDLine must also be called for vblank lines 144..153. */
    void PushLCDScanline(Int32 nLine, const Uint8 *pShade2Bit);
    void EndLCDLine(Int32 nLine);

    Uint8 GetControllerByte(Int32 iController) const;
    Uint8 GetControllerCount() const;
    Uint8 GetControl() const { return m_uControl; }
    Uint8 GetICDRevision() const { return m_uIcdRevision; }
    void SetICDRevision(Uint8 uRevision) { m_uIcdRevision = uRevision; }

    Uint32 GetClockDivider() const;
    Uint32 AdvanceMasterClocks(Uint32 uSnesMasterClocks, Uint32 uSnesMasterHz);

    Bool SaveState(StateT *pState) const;
    Bool RestoreState(const StateT *pState);

    static Bool IsMappedAddress(Uint32 uAddr);

private:
    static const Uint32 STATE_MAGIC = 0x32424753U; /* "SGB2" LE */
    static const Uint32 STATE_VERSION = 2U;
    static const Uint32 SGB2_OSC_HZ = 20971520U;

    ModelE m_eModel;
    Uint8 m_uIcdRevision;
    Uint8 m_uControl;
    Bool m_bPacketReady;
    Uint8 m_uReadBank;
    Uint16 m_uReadAddress;
    Uint8 m_uWriteBank;
    Int32 m_nVCounter;

    Uint8 m_uJoypID;
    Bool m_bPreviousP15, m_bPulseLock, m_bStrobeLock, m_bPacketLock;
    Uint8 m_uPacketOffset, m_uBitData, m_uBitOffset;
    Bool m_bResetRequested;

    Uint8 m_uController[4];
    Uint8 m_uPacket[PACKET_BYTES];
    Uint8 m_uOutput[LCD_BANKS][LCD_BANK_BYTES];
    Uint64 m_uClockAccumulator;

    Uint8 ReadDecoded(Uint32 uDecoded);
    void WriteDecoded(Uint32 uDecoded, Uint8 uData);
    Uint8 ControllerMask() const;
    Uint8 JoypInput(Bool bP14, Bool bP15) const;
};

#endif
