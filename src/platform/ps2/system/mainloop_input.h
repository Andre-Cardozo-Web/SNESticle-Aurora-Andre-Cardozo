#pragma once

#include "types.h"

/* AURORA_CONTROLLER_OPTIONS_V2
 * User-facing Max is the historical one-frame ON / one-frame OFF cadence. */
enum MainLoopTurboSpeedE
{
	MAINLOOP_TURBO_SPEED_NORMAL = 0,
	MAINLOOP_TURBO_SPEED_HALF,
	MAINLOOP_TURBO_SPEED_QUARTER,
	MAINLOOP_TURBO_SPEED_NUM
};

void MainLoopTurboSetSpeed(MainLoopTurboSpeedE eSpeed);
MainLoopTurboSpeedE MainLoopTurboGetSpeed(void);
void MainLoopTurboCycleSpeedDir(Int32 dir);
const char *MainLoopTurboGetSpeedName(void);
void MainLoopTurboAdvanceHostFrame(void);
void MainLoopTurboRearmHostPhase(void);

/* AURORA_SGB_INVERT_INPUT_V1_20260905
 * Preference is global/persistent, but its EFFECT is hard-gated in
 * _MainLoopSnesInput() to the currently executing Super Game Boy only. */
void MainLoopSgbInvertSetEnabled(Bool enabled);
Bool MainLoopSgbInvertGetEnabled(void);

/* AURORA_MD_PAD_LAYOUT_V1
 * Ordem dos três botões frontais do DualShock: Square / Cross / Circle.
 * ABC = A/B/C. BCA = B/C/A. SMS/GG usam somente os lógicos B/C. */
enum MainLoopMdPadLayoutE
{
	MAINLOOP_MD_PAD_ABC = 0,
	MAINLOOP_MD_PAD_BCA,
	MAINLOOP_MD_PAD_NUM
};

void MainLoopMdPadSetLayout(MainLoopMdPadLayoutE eLayout);
MainLoopMdPadLayoutE MainLoopMdPadGetLayout(void);
void MainLoopMdPadCycleLayoutDir(Int32 dir);
const char *MainLoopMdPadGetLayoutName(void);

Uint16 _MainLoopInput(Uint32 pad);
void _MainLoopInputProcess(Uint32 buttons);
void _MainLoopInputSuppressUntilRelease();
void _MainLoopQuickStateExecuteConfirmed(Bool bSave);
