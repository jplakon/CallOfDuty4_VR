#pragma once

#include <Windows.h>

// KISAK_SP_VR_CRASH_DIAGNOSTICS_V48
// Installs the crash recorder before engine, Steam, Direct3D, or OpenXR
// initialization. The recorder deliberately uses Win32 file I/O and a
// preloaded system DbgHelp entry point so it remains useful when the CRT or a
// third-party runtime is part of the failure.
void KisakCrash_Install(const char* commandLine);
void KisakCrash_ReinstallUnhandledExceptionFilter();

// Give each engine-created thread a stable label and a small emergency stack
// reserve before it begins work.
void KisakCrash_PrepareCurrentThread(const char* threadName);

// These setters only publish fixed strings and integer snapshots. They do not
// allocate memory or write files, so frame-stage breadcrumbs are cheap enough
// to keep enabled in Release builds.
void KisakCrash_SetStage(const char* stage);
void KisakCrash_SetFrameNumber(unsigned int frameNumber);
void KisakCrash_SetVrState(
    bool initialized,
    bool sessionRunning,
    int sessionState,
    unsigned int uploadedCaptureSerialLow,
    unsigned int capturedWidth,
    unsigned int capturedHeight);

// Use this function directly in an SEH __except expression. It writes at most
// one report/dump per process and returns EXCEPTION_EXECUTE_HANDLER.
LONG KisakCrash_ExceptionFilter(
    EXCEPTION_POINTERS* exceptionPointers,
    const char* boundary);

LONG WINAPI KisakCrash_UnhandledExceptionFilter(
    EXCEPTION_POINTERS* exceptionPointers);

DWORD KisakCrash_GetRecordedExceptionCode();

// Fatal engine exits do not necessarily raise a Windows exception. Record a
// text report and a context-free minidump for those paths as well.
void KisakCrash_RecordFatalError(const char* message);
void KisakCrash_MarkExpectedExit(const char* reason, int exitCode);

