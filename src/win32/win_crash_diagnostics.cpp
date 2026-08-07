#include "win_crash_diagnostics.h"

#include <DbgHelp.h>
#include <Psapi.h>
#include <TlHelp32.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr char kCrashBuildMarker[] =
    "KISAK_SP_VR_CRASH_DIAGNOSTICS_V48";

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(
    HANDLE,
    DWORD,
    HANDLE,
    MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);

using GetProcessMemoryInfoFn = BOOL(WINAPI*)(
    HANDLE,
    PPROCESS_MEMORY_COUNTERS,
    DWORD);

HMODULE g_dbgHelpModule = nullptr;
MiniDumpWriteDumpFn g_miniDumpWriteDump = nullptr;

volatile LONG g_installed = 0;
volatile LONG g_reportStarted = 0;
volatile LONG g_recordedExceptionCode = 0;

ULONGLONG g_processStartTick = 0u;
char g_crashDirectory[MAX_PATH] = {};
char g_sessionPath[MAX_PATH] = {};
char g_commandLine[2048] = {};

std::atomic<const char*> g_stage{
    "crash recorder: before engine startup"
};
std::atomic<unsigned int> g_frameNumber{0u};
std::atomic<unsigned int> g_vrInitialized{0u};
std::atomic<unsigned int> g_vrSessionRunning{0u};
std::atomic<int> g_vrSessionState{0};
std::atomic<unsigned int> g_uploadedCaptureSerialLow{0u};
std::atomic<unsigned int> g_capturedWidth{0u};
std::atomic<unsigned int> g_capturedHeight{0u};

thread_local const char* g_currentThreadName = "unregistered";

bool AppendPath(
    char* destination,
    const std::size_t destinationSize,
    const char* directory,
    const char* leaf)
{
    if (destination == nullptr ||
        destinationSize == 0u ||
        directory == nullptr ||
        leaf == nullptr)
    {
        return false;
    }

    const std::size_t directoryLength =
        std::strlen(directory);

    const bool needsSeparator =
        directoryLength > 0u &&
        directory[directoryLength - 1u] != '\\' &&
        directory[directoryLength - 1u] != '/';

    const int result =
        _snprintf_s(
            destination,
            destinationSize,
            _TRUNCATE,
            needsSeparator ? "%s\\%s" : "%s%s",
            directory,
            leaf);

    return result >= 0;
}

bool EnsureDirectory(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return false;
    }

    if (CreateDirectoryA(path, nullptr))
    {
        return true;
    }

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ResolveDefaultCrashDirectory(
    char* destination,
    const std::size_t destinationSize)
{
    char modulePath[MAX_PATH] = {};

    const DWORD moduleLength =
        GetModuleFileNameA(
            nullptr,
            modulePath,
            static_cast<DWORD>(sizeof(modulePath)));

    if (moduleLength == 0u ||
        moduleLength >= sizeof(modulePath))
    {
        return false;
    }

    char* lastSeparator = std::strrchr(modulePath, '\\');

    if (lastSeparator == nullptr)
    {
        lastSeparator = std::strrchr(modulePath, '/');
    }

    if (lastSeparator == nullptr)
    {
        return false;
    }

    *lastSeparator = '\0';

    return AppendPath(
        destination,
        destinationSize,
        modulePath,
        "CrashDumps");
}

bool ResolveTemporaryCrashDirectory(
    char* destination,
    const std::size_t destinationSize)
{
    char temporaryPath[MAX_PATH] = {};

    const DWORD temporaryLength =
        GetTempPathA(
            static_cast<DWORD>(sizeof(temporaryPath)),
            temporaryPath);

    if (temporaryLength == 0u ||
        temporaryLength >= sizeof(temporaryPath))
    {
        return false;
    }

    return AppendPath(
        destination,
        destinationSize,
        temporaryPath,
        "KisakCOD-VR-CrashDumps");
}

void ResolveCrashDirectory()
{
    char environmentPath[MAX_PATH] = {};

    const DWORD environmentLength =
        GetEnvironmentVariableA(
            "KISAK_VR_CRASH_DIR",
            environmentPath,
            static_cast<DWORD>(sizeof(environmentPath)));

    if (environmentLength > 0u &&
        environmentLength < sizeof(environmentPath))
    {
        lstrcpynA(
            g_crashDirectory,
            environmentPath,
            static_cast<int>(sizeof(g_crashDirectory)));
    }
    else
    {
        ResolveDefaultCrashDirectory(
            g_crashDirectory,
            sizeof(g_crashDirectory));
    }

    if (!EnsureDirectory(g_crashDirectory))
    {
        g_crashDirectory[0] = '\0';
        ResolveTemporaryCrashDirectory(
            g_crashDirectory,
            sizeof(g_crashDirectory));
        EnsureDirectory(g_crashDirectory);
    }

    AppendPath(
        g_sessionPath,
        sizeof(g_sessionPath),
        g_crashDirectory,
        "KisakCOD-VR-Last-Session.txt");
}

HMODULE LoadSystemLibrary(const wchar_t* libraryName)
{
    wchar_t systemDirectory[MAX_PATH] = {};

    const UINT systemDirectoryLength =
        GetSystemDirectoryW(
            systemDirectory,
            static_cast<UINT>(sizeof(systemDirectory) /
                              sizeof(systemDirectory[0])));

    if (systemDirectoryLength == 0u ||
        systemDirectoryLength >=
            sizeof(systemDirectory) /
                sizeof(systemDirectory[0]))
    {
        return nullptr;
    }

    wchar_t libraryPath[MAX_PATH] = {};

    const int result =
        _snwprintf_s(
            libraryPath,
            sizeof(libraryPath) / sizeof(libraryPath[0]),
            _TRUNCATE,
            L"%ls\\%ls",
            systemDirectory,
            libraryName);

    if (result < 0)
    {
        return nullptr;
    }

    return LoadLibraryW(libraryPath);
}

void WriteRaw(HANDLE file, const char* text)
{
    if (file == INVALID_HANDLE_VALUE || text == nullptr)
    {
        return;
    }

    const DWORD length =
        static_cast<DWORD>(std::strlen(text));

    DWORD written = 0u;
    WriteFile(file, text, length, &written, nullptr);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void WriteFormat(HANDLE file, const char* format, ...)
{
    char buffer[4096] = {};

    va_list arguments;
    va_start(arguments, format);
    _vsnprintf_s(
        buffer,
        sizeof(buffer),
        _TRUNCATE,
        format,
        arguments);
    va_end(arguments);

    WriteRaw(file, buffer);
}

const char* ExceptionName(const DWORD exceptionCode)
{
    switch (exceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION:
            return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_BREAKPOINT:
            return "EXCEPTION_BREAKPOINT";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION:
            return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:
            return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_STACK_OVERFLOW:
            return "EXCEPTION_STACK_OVERFLOW";
        default:
            return "UNKNOWN_EXCEPTION";
    }
}

void WriteEnvironmentValue(
    HANDLE report,
    const char* variableName)
{
    char value[2048] = {};

    const DWORD length =
        GetEnvironmentVariableA(
            variableName,
            value,
            static_cast<DWORD>(sizeof(value)));

    WriteFormat(
        report,
        "%s=%s\r\n",
        variableName,
        length > 0u && length < sizeof(value)
            ? value
            : "<unset>");
}

void WriteExceptionModule(
    HANDLE report,
    const void* exceptionAddress)
{
    if (exceptionAddress == nullptr)
    {
        WriteRaw(
            report,
            "exception_module=<unknown>\r\n");
        return;
    }

    HMODULE module = nullptr;

    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(exceptionAddress),
            &module))
    {
        WriteRaw(
            report,
            "exception_module=<not inside a loaded module>\r\n");
        return;
    }

    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA(
        module,
        modulePath,
        static_cast<DWORD>(sizeof(modulePath)));

    const std::uintptr_t moduleBase =
        reinterpret_cast<std::uintptr_t>(module);
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(exceptionAddress);

    DWORD imageTimestamp = 0u;
    DWORD imageSize = 0u;

    const auto* dosHeader =
        reinterpret_cast<const IMAGE_DOS_HEADER*>(module);

    if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE)
    {
        const auto* ntHeader =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(
                moduleBase + dosHeader->e_lfanew);

        if (ntHeader->Signature == IMAGE_NT_SIGNATURE)
        {
            imageTimestamp =
                ntHeader->FileHeader.TimeDateStamp;
            imageSize =
                ntHeader->OptionalHeader.SizeOfImage;
        }
    }

    WriteFormat(
        report,
        "exception_module=%s\r\n"
        "exception_module_base=0x%p\r\n"
        "exception_module_offset=0x%llX\r\n"
        "exception_module_pe_timestamp=0x%08lX\r\n"
        "exception_module_image_size=%lu\r\n",
        modulePath[0] != '\0' ? modulePath : "<unknown>",
        reinterpret_cast<void*>(module),
        static_cast<unsigned long long>(address - moduleBase),
        static_cast<unsigned long>(imageTimestamp),
        static_cast<unsigned long>(imageSize));
}

void WriteExceptionDetails(
    HANDLE report,
    EXCEPTION_POINTERS* exceptionPointers)
{
    if (exceptionPointers == nullptr ||
        exceptionPointers->ExceptionRecord == nullptr)
    {
        WriteRaw(
            report,
            "exception_context=unavailable (fatal engine exit)\r\n");
        return;
    }

    const EXCEPTION_RECORD* exceptionRecord =
        exceptionPointers->ExceptionRecord;

    WriteFormat(
        report,
        "exception_code=0x%08lX\r\n"
        "exception_name=%s\r\n"
        "exception_flags=0x%08lX\r\n"
        "exception_address=0x%p\r\n"
        "exception_parameter_count=%lu\r\n",
        static_cast<unsigned long>(exceptionRecord->ExceptionCode),
        ExceptionName(exceptionRecord->ExceptionCode),
        static_cast<unsigned long>(exceptionRecord->ExceptionFlags),
        exceptionRecord->ExceptionAddress,
        static_cast<unsigned long>(
            exceptionRecord->NumberParameters));

    if ((exceptionRecord->ExceptionCode ==
             EXCEPTION_ACCESS_VIOLATION ||
         exceptionRecord->ExceptionCode ==
             EXCEPTION_IN_PAGE_ERROR) &&
        exceptionRecord->NumberParameters >= 2u)
    {
        const ULONG_PTR operation =
            exceptionRecord->ExceptionInformation[0];

        const char* operationName = "unknown";

        if (operation == 0u)
        {
            operationName = "read";
        }
        else if (operation == 1u)
        {
            operationName = "write";
        }
        else if (operation == 8u)
        {
            operationName = "execute";
        }

        WriteFormat(
            report,
            "access_operation=%s\r\n"
            "access_target=0x%p\r\n",
            operationName,
            reinterpret_cast<void*>(
                exceptionRecord->ExceptionInformation[1]));

        if (exceptionRecord->ExceptionCode ==
                EXCEPTION_IN_PAGE_ERROR &&
            exceptionRecord->NumberParameters >= 3u)
        {
            WriteFormat(
                report,
                "in_page_ntstatus=0x%p\r\n",
                reinterpret_cast<void*>(
                    exceptionRecord->ExceptionInformation[2]));
        }
    }

    WriteExceptionModule(
        report,
        exceptionRecord->ExceptionAddress);

    const CONTEXT* context =
        exceptionPointers->ContextRecord;

    if (context == nullptr)
    {
        WriteRaw(report, "registers=<unavailable>\r\n");
        return;
    }

#if defined(_M_IX86)
    WriteFormat(
        report,
        "register_eip=0x%08lX\r\n"
        "register_esp=0x%08lX\r\n"
        "register_ebp=0x%08lX\r\n"
        "register_eax=0x%08lX\r\n"
        "register_ebx=0x%08lX\r\n"
        "register_ecx=0x%08lX\r\n"
        "register_edx=0x%08lX\r\n"
        "register_esi=0x%08lX\r\n"
        "register_edi=0x%08lX\r\n",
        context->Eip,
        context->Esp,
        context->Ebp,
        context->Eax,
        context->Ebx,
        context->Ecx,
        context->Edx,
        context->Esi,
        context->Edi);
#elif defined(_M_X64)
    WriteFormat(
        report,
        "register_rip=0x%016llX\r\n"
        "register_rsp=0x%016llX\r\n"
        "register_rbp=0x%016llX\r\n",
        static_cast<unsigned long long>(context->Rip),
        static_cast<unsigned long long>(context->Rsp),
        static_cast<unsigned long long>(context->Rbp));
#endif
}

void WriteMemoryDetails(HANDLE report)
{
    MEMORYSTATUSEX memoryStatus = {};
    memoryStatus.dwLength = sizeof(memoryStatus);

    if (GlobalMemoryStatusEx(&memoryStatus))
    {
        WriteFormat(
            report,
            "system_memory_load_percent=%lu\r\n"
            "system_physical_total_bytes=%llu\r\n"
            "system_physical_available_bytes=%llu\r\n"
            "system_pagefile_total_bytes=%llu\r\n"
            "system_pagefile_available_bytes=%llu\r\n"
            "process_address_space_available_bytes=%llu\r\n",
            static_cast<unsigned long>(memoryStatus.dwMemoryLoad),
            static_cast<unsigned long long>(
                memoryStatus.ullTotalPhys),
            static_cast<unsigned long long>(
                memoryStatus.ullAvailPhys),
            static_cast<unsigned long long>(
                memoryStatus.ullTotalPageFile),
            static_cast<unsigned long long>(
                memoryStatus.ullAvailPageFile),
            static_cast<unsigned long long>(
                memoryStatus.ullAvailVirtual));
    }

    HMODULE kernelModule = GetModuleHandleA("kernel32.dll");

    GetProcessMemoryInfoFn getProcessMemoryInfo =
        kernelModule != nullptr
            ? reinterpret_cast<GetProcessMemoryInfoFn>(
                  GetProcAddress(
                      kernelModule,
                      "K32GetProcessMemoryInfo"))
            : nullptr;

    HMODULE psapiModule = nullptr;

    if (getProcessMemoryInfo == nullptr)
    {
        psapiModule = LoadSystemLibrary(L"psapi.dll");

        if (psapiModule != nullptr)
        {
            getProcessMemoryInfo =
                reinterpret_cast<GetProcessMemoryInfoFn>(
                    GetProcAddress(
                        psapiModule,
                        "GetProcessMemoryInfo"));
        }
    }

    if (getProcessMemoryInfo != nullptr)
    {
        PROCESS_MEMORY_COUNTERS_EX counters = {};
        counters.cb = sizeof(counters);

        if (getProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(
                    &counters),
                sizeof(counters)))
        {
            WriteFormat(
                report,
                "process_working_set_bytes=%llu\r\n"
                "process_peak_working_set_bytes=%llu\r\n"
                "process_private_bytes=%llu\r\n"
                "process_pagefile_bytes=%llu\r\n"
                "process_peak_pagefile_bytes=%llu\r\n",
                static_cast<unsigned long long>(
                    counters.WorkingSetSize),
                static_cast<unsigned long long>(
                    counters.PeakWorkingSetSize),
                static_cast<unsigned long long>(
                    counters.PrivateUsage),
                static_cast<unsigned long long>(
                    counters.PagefileUsage),
                static_cast<unsigned long long>(
                    counters.PeakPagefileUsage));
        }
    }

    if (psapiModule != nullptr)
    {
        FreeLibrary(psapiModule);
    }

    DWORD handleCount = 0u;

    if (GetProcessHandleCount(
            GetCurrentProcess(),
            &handleCount))
    {
        WriteFormat(
            report,
            "process_handle_count=%lu\r\n",
            static_cast<unsigned long>(handleCount));
    }

    WriteFormat(
        report,
        "process_gdi_object_count=%lu\r\n"
        "process_user_object_count=%lu\r\n",
        static_cast<unsigned long>(
            GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)),
        static_cast<unsigned long>(
            GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)));

    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);

    std::uintptr_t cursor =
        reinterpret_cast<std::uintptr_t>(
            systemInfo.lpMinimumApplicationAddress);
    const std::uintptr_t maximumAddress =
        reinterpret_cast<std::uintptr_t>(
            systemInfo.lpMaximumApplicationAddress);

    unsigned long long freeBytes = 0u;
    unsigned long long largestFreeRegion = 0u;
    unsigned long long committedBytes = 0u;

    while (cursor < maximumAddress)
    {
        MEMORY_BASIC_INFORMATION region = {};

        if (VirtualQuery(
                reinterpret_cast<const void*>(cursor),
                &region,
                sizeof(region)) == 0u)
        {
            break;
        }

        const unsigned long long regionSize =
            static_cast<unsigned long long>(region.RegionSize);

        if (region.State == MEM_FREE)
        {
            freeBytes += regionSize;

            if (regionSize > largestFreeRegion)
            {
                largestFreeRegion = regionSize;
            }
        }
        else if (region.State == MEM_COMMIT)
        {
            committedBytes += regionSize;
        }

        const std::uintptr_t next =
            reinterpret_cast<std::uintptr_t>(region.BaseAddress) +
            region.RegionSize;

        if (next <= cursor)
        {
            break;
        }

        cursor = next;
    }

    WriteFormat(
        report,
        "process_virtual_free_bytes=%llu\r\n"
        "process_virtual_largest_free_region_bytes=%llu\r\n"
        "process_virtual_committed_bytes=%llu\r\n",
        freeBytes,
        largestFreeRegion,
        committedBytes);
}

void WriteLoadedModules(HANDLE report)
{
    WriteRaw(report, "\r\n[loaded_modules]\r\n");

    HANDLE snapshot =
        CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
            GetCurrentProcessId());

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        WriteFormat(
            report,
            "module_snapshot_error=%lu\r\n",
            static_cast<unsigned long>(GetLastError()));
        return;
    }

    MODULEENTRY32 moduleEntry = {};
    moduleEntry.dwSize = sizeof(moduleEntry);

    if (Module32First(snapshot, &moduleEntry))
    {
        do
        {
            WriteFormat(
                report,
                "base=0x%p size=%lu name=%s path=%s\r\n",
                static_cast<void*>(moduleEntry.modBaseAddr),
                static_cast<unsigned long>(moduleEntry.modBaseSize),
                moduleEntry.szModule,
                moduleEntry.szExePath);
        }
        while (Module32Next(snapshot, &moduleEntry));
    }
    else
    {
        WriteFormat(
            report,
            "module_enumeration_error=%lu\r\n",
            static_cast<unsigned long>(GetLastError()));
    }

    CloseHandle(snapshot);
}

bool WriteMiniDump(
    const char* dumpPath,
    EXCEPTION_POINTERS* exceptionPointers,
    DWORD* errorCode)
{
    if (errorCode != nullptr)
    {
        *errorCode = ERROR_SUCCESS;
    }

    if (g_miniDumpWriteDump == nullptr)
    {
        if (errorCode != nullptr)
        {
            *errorCode = ERROR_PROC_NOT_FOUND;
        }

        return false;
    }

    HANDLE dumpFile =
        CreateFileA(
            dumpPath,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    if (dumpFile == INVALID_HANDLE_VALUE)
    {
        if (errorCode != nullptr)
        {
            *errorCode = GetLastError();
        }

        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION exceptionInformation = {};
    PMINIDUMP_EXCEPTION_INFORMATION exceptionInformationPointer = nullptr;

    if (exceptionPointers != nullptr)
    {
        exceptionInformation.ThreadId = GetCurrentThreadId();
        exceptionInformation.ExceptionPointers = exceptionPointers;
        exceptionInformation.ClientPointers = FALSE;
        exceptionInformationPointer = &exceptionInformation;
    }

    const MINIDUMP_TYPE diagnosticType =
        static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal |
            MiniDumpWithDataSegs |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpWithUnloadedModules |
            MiniDumpWithThreadInfo |
            MiniDumpIgnoreInaccessibleMemory);

    BOOL succeeded =
        g_miniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            dumpFile,
            diagnosticType,
            exceptionInformationPointer,
            nullptr,
            nullptr);

    DWORD dumpError =
        succeeded ? ERROR_SUCCESS : GetLastError();

    // Older system DbgHelp builds can reject newer flag combinations. Keep a
    // MiniDumpNormal fallback so a useful artifact is still produced.
    if (!succeeded && dumpError == ERROR_INVALID_PARAMETER)
    {
        SetFilePointer(dumpFile, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(dumpFile);

        succeeded =
            g_miniDumpWriteDump(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                dumpFile,
                MiniDumpNormal,
                exceptionInformationPointer,
                nullptr,
                nullptr);

        dumpError =
            succeeded ? ERROR_SUCCESS : GetLastError();
    }

    FlushFileBuffers(dumpFile);
    CloseHandle(dumpFile);

    if (!succeeded)
    {
        DeleteFileA(dumpPath);
    }

    if (errorCode != nullptr)
    {
        *errorCode = dumpError;
    }

    return succeeded != FALSE;
}

void WriteSessionStatus(
    const char* status,
    const char* reason,
    const int exitCode,
    const char* reportPath,
    const char* dumpPath)
{
    if (g_sessionPath[0] == '\0')
    {
        return;
    }

    HANDLE sessionFile =
        CreateFileA(
            g_sessionPath,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    if (sessionFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    SYSTEMTIME utcTime = {};
    GetSystemTime(&utcTime);

    WriteFormat(
        sessionFile,
        "%s\r\n"
        "diagnostic_version=V48\r\n"
        "status=%s\r\n"
        "utc=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\r\n"
        "pid=%lu\r\n"
        "exit_code=%d\r\n"
        "reason=%s\r\n"
        "report=%s\r\n"
        "dump=%s\r\n",
        kCrashBuildMarker,
        status != nullptr ? status : "UNKNOWN",
        utcTime.wYear,
        utcTime.wMonth,
        utcTime.wDay,
        utcTime.wHour,
        utcTime.wMinute,
        utcTime.wSecond,
        utcTime.wMilliseconds,
        static_cast<unsigned long>(GetCurrentProcessId()),
        exitCode,
        reason != nullptr ? reason : "<none>",
        reportPath != nullptr ? reportPath : "<none>",
        dumpPath != nullptr ? dumpPath : "<none>");

    FlushFileBuffers(sessionFile);
    CloseHandle(sessionFile);
}

void WriteLatestPointer(
    const char* reportPath,
    const char* dumpPath)
{
    char latestPath[MAX_PATH] = {};

    if (!AppendPath(
            latestPath,
            sizeof(latestPath),
            g_crashDirectory,
            "LATEST.txt"))
    {
        return;
    }

    HANDLE latestFile =
        CreateFileA(
            latestPath,
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    if (latestFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    WriteFormat(
        latestFile,
        "report=%s\r\n"
        "dump=%s\r\n",
        reportPath != nullptr ? reportPath : "<none>",
        dumpPath != nullptr ? dumpPath : "<none>");

    FlushFileBuffers(latestFile);
    CloseHandle(latestFile);
}

void WriteCrashReport(
    EXCEPTION_POINTERS* exceptionPointers,
    const char* kind,
    const char* boundary,
    const char* message)
{
    SYSTEMTIME utcTime = {};
    GetSystemTime(&utcTime);

    char stem[160] = {};
    _snprintf_s(
        stem,
        sizeof(stem),
        _TRUNCATE,
        "KisakCOD-VR-%s-%04u%02u%02uT%02u%02u%02u-%03uZ-pid%lu",
        kind != nullptr ? kind : "crash",
        utcTime.wYear,
        utcTime.wMonth,
        utcTime.wDay,
        utcTime.wHour,
        utcTime.wMinute,
        utcTime.wSecond,
        utcTime.wMilliseconds,
        static_cast<unsigned long>(GetCurrentProcessId()));

    char reportLeaf[192] = {};
    char dumpLeaf[192] = {};
    _snprintf_s(
        reportLeaf,
        sizeof(reportLeaf),
        _TRUNCATE,
        "%s.txt",
        stem);
    _snprintf_s(
        dumpLeaf,
        sizeof(dumpLeaf),
        _TRUNCATE,
        "%s.dmp",
        stem);

    char reportPath[MAX_PATH] = {};
    char dumpPath[MAX_PATH] = {};
    AppendPath(
        reportPath,
        sizeof(reportPath),
        g_crashDirectory,
        reportLeaf);
    AppendPath(
        dumpPath,
        sizeof(dumpPath),
        g_crashDirectory,
        dumpLeaf);

    HANDLE report =
        CreateFileA(
            reportPath,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

    if (report == INVALID_HANDLE_VALUE)
    {
        return;
    }

    const DWORD exceptionCode =
        exceptionPointers != nullptr &&
                exceptionPointers->ExceptionRecord != nullptr
            ? exceptionPointers->ExceptionRecord->ExceptionCode
            : 0u;

    WriteFormat(
        report,
        "%s\r\n"
        "diagnostic_version=V48\r\n"
        "kind=%s\r\n"
        "utc=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\r\n"
        "pid=%lu\r\n"
        "thread_id=%lu\r\n"
        "thread_name=%s\r\n"
        "boundary=%s\r\n"
        "process_uptime_ms=%llu\r\n"
        "frame_number=%u\r\n"
        "stage=%s\r\n"
        "message=%s\r\n"
        "command_line=%s\r\n",
        kCrashBuildMarker,
        kind != nullptr ? kind : "crash",
        utcTime.wYear,
        utcTime.wMonth,
        utcTime.wDay,
        utcTime.wHour,
        utcTime.wMinute,
        utcTime.wSecond,
        utcTime.wMilliseconds,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        g_currentThreadName != nullptr
            ? g_currentThreadName
            : "unregistered",
        boundary != nullptr ? boundary : "unhandled",
        static_cast<unsigned long long>(
            GetTickCount64() - g_processStartTick),
        g_frameNumber.load(std::memory_order_relaxed),
        g_stage.load(std::memory_order_relaxed),
        message != nullptr ? message : "<none>",
        g_commandLine[0] != '\0'
            ? g_commandLine
            : "<unavailable>");

    WriteRaw(report, "\r\n[vr_snapshot]\r\n");
    WriteFormat(
        report,
        "vr_initialized=%u\r\n"
        "vr_session_running=%u\r\n"
        "vr_session_state=%d\r\n"
        "uploaded_capture_serial_low=%u\r\n"
        "captured_width=%u\r\n"
        "captured_height=%u\r\n",
        g_vrInitialized.load(std::memory_order_relaxed),
        g_vrSessionRunning.load(std::memory_order_relaxed),
        g_vrSessionState.load(std::memory_order_relaxed),
        g_uploadedCaptureSerialLow.load(
            std::memory_order_relaxed),
        g_capturedWidth.load(std::memory_order_relaxed),
        g_capturedHeight.load(std::memory_order_relaxed));

    WriteRaw(report, "\r\n[exception]\r\n");
    WriteExceptionDetails(report, exceptionPointers);

    DWORD dumpError = ERROR_SUCCESS;
    const bool dumpWritten =
        WriteMiniDump(
            dumpPath,
            exceptionPointers,
            &dumpError);

    WriteRaw(report, "\r\n[minidump]\r\n");
    WriteFormat(
        report,
        "dump_written=%s\r\n"
        "dump_path=%s\r\n"
        "dump_error=%lu\r\n",
        dumpWritten ? "YES" : "NO",
        dumpWritten ? dumpPath : "<none>",
        static_cast<unsigned long>(dumpError));

    WriteRaw(report, "\r\n[process_memory]\r\n");
    WriteMemoryDetails(report);

    WriteRaw(report, "\r\n[environment]\r\n");
    WriteEnvironmentValue(report, "XR_RUNTIME_JSON");
    WriteEnvironmentValue(report, "XR_API_LAYER_PATH");
    WriteEnvironmentValue(report, "XR_ENABLE_API_LAYERS");
    WriteEnvironmentValue(report, "KISAK_VR_OUTPUT_SCALE");
    WriteEnvironmentValue(report, "KISAK_VR_CRASH_DIR");

    char currentDirectory[MAX_PATH] = {};
    GetCurrentDirectoryA(
        static_cast<DWORD>(sizeof(currentDirectory)),
        currentDirectory);
    WriteFormat(
        report,
        "working_directory=%s\r\n",
        currentDirectory[0] != '\0'
            ? currentDirectory
            : "<unavailable>");

    WriteLoadedModules(report);

    FlushFileBuffers(report);
    CloseHandle(report);

    WriteLatestPointer(
        reportPath,
        dumpWritten ? dumpPath : nullptr);

    const int signedExitCode =
        exceptionCode != 0u
            ? static_cast<int>(exceptionCode)
            : 48;

    WriteSessionStatus(
        exceptionPointers != nullptr
            ? "CRASHED"
            : "FATAL_ENGINE_ERROR",
        message != nullptr
            ? message
            : ExceptionName(exceptionCode),
        signedExitCode,
        reportPath,
        dumpWritten ? dumpPath : nullptr);
}
}

void KisakCrash_Install(const char* commandLine)
{
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
    {
        KisakCrash_ReinstallUnhandledExceptionFilter();
        return;
    }

    g_processStartTick = GetTickCount64();

    const char* completeCommandLine = GetCommandLineA();

    lstrcpynA(
        g_commandLine,
        completeCommandLine != nullptr
            ? completeCommandLine
            : commandLine != nullptr
                ? commandLine
                : "",
        static_cast<int>(sizeof(g_commandLine)));

    ResolveCrashDirectory();

    g_dbgHelpModule = LoadSystemLibrary(L"dbghelp.dll");

    if (g_dbgHelpModule != nullptr)
    {
        g_miniDumpWriteDump =
            reinterpret_cast<MiniDumpWriteDumpFn>(
                GetProcAddress(
                    g_dbgHelpModule,
                    "MiniDumpWriteDump"));
    }

    KisakCrash_PrepareCurrentThread("MAIN");
    KisakCrash_ReinstallUnhandledExceptionFilter();

    WriteSessionStatus(
        "RUNNING",
        g_miniDumpWriteDump != nullptr
            ? "crash recorder installed; system DbgHelp ready"
            : "crash recorder installed; system DbgHelp unavailable",
        0,
        nullptr,
        nullptr);

    char explicitTest[64] = {};
    const DWORD explicitTestLength =
        GetEnvironmentVariableA(
            "KISAK_VR_CRASH_TEST",
            explicitTest,
            static_cast<DWORD>(sizeof(explicitTest)));

    if (explicitTestLength > 0u &&
        explicitTestLength < sizeof(explicitTest) &&
        std::strcmp(
            explicitTest,
            "V48-EXPLICIT-TEST") == 0)
    {
        KisakCrash_SetStage(
            "explicit V48 pre-engine crash-recorder test");

        RaiseException(
            0xE0480001u,
            EXCEPTION_NONCONTINUABLE,
            0u,
            nullptr);
    }
}

void KisakCrash_ReinstallUnhandledExceptionFilter()
{
    SetUnhandledExceptionFilter(
        KisakCrash_UnhandledExceptionFilter);
}

void KisakCrash_PrepareCurrentThread(const char* threadName)
{
    g_currentThreadName =
        threadName != nullptr && threadName[0] != '\0'
            ? threadName
            : "unregistered";

    ULONG stackGuarantee = 64u * 1024u;
    SetThreadStackGuarantee(&stackGuarantee);
}

void KisakCrash_SetStage(const char* stage)
{
    if (stage != nullptr && stage[0] != '\0')
    {
        g_stage.store(stage, std::memory_order_release);
    }
}

void KisakCrash_SetFrameNumber(const unsigned int frameNumber)
{
    g_frameNumber.store(
        frameNumber,
        std::memory_order_release);
}

void KisakCrash_SetVrState(
    const bool initialized,
    const bool sessionRunning,
    const int sessionState,
    const unsigned int uploadedCaptureSerialLow,
    const unsigned int capturedWidth,
    const unsigned int capturedHeight)
{
    g_vrInitialized.store(
        initialized ? 1u : 0u,
        std::memory_order_relaxed);
    g_vrSessionRunning.store(
        sessionRunning ? 1u : 0u,
        std::memory_order_relaxed);
    g_vrSessionState.store(
        sessionState,
        std::memory_order_relaxed);
    g_uploadedCaptureSerialLow.store(
        uploadedCaptureSerialLow,
        std::memory_order_relaxed);
    g_capturedWidth.store(
        capturedWidth,
        std::memory_order_relaxed);
    g_capturedHeight.store(
        capturedHeight,
        std::memory_order_release);
}

LONG KisakCrash_ExceptionFilter(
    EXCEPTION_POINTERS* exceptionPointers,
    const char* boundary)
{
    const DWORD exceptionCode =
        exceptionPointers != nullptr &&
                exceptionPointers->ExceptionRecord != nullptr
            ? exceptionPointers->ExceptionRecord->ExceptionCode
            : EXCEPTION_NONCONTINUABLE_EXCEPTION;

    InterlockedExchange(
        &g_recordedExceptionCode,
        static_cast<LONG>(exceptionCode));

    if (InterlockedCompareExchange(&g_reportStarted, 1, 0) == 0)
    {
        WriteCrashReport(
            exceptionPointers,
            "crash",
            boundary,
            ExceptionName(exceptionCode));
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI KisakCrash_UnhandledExceptionFilter(
    EXCEPTION_POINTERS* exceptionPointers)
{
    return KisakCrash_ExceptionFilter(
        exceptionPointers,
        "process unhandled-exception filter");
}

DWORD KisakCrash_GetRecordedExceptionCode()
{
    return static_cast<DWORD>(
        InterlockedCompareExchange(
            &g_recordedExceptionCode,
            0,
            0));
}

void KisakCrash_RecordFatalError(const char* message)
{
    InterlockedExchange(&g_recordedExceptionCode, 48);

    if (InterlockedCompareExchange(&g_reportStarted, 1, 0) == 0)
    {
        WriteCrashReport(
            nullptr,
            "fatal",
            "Sys_Error",
            message != nullptr ? message : "fatal engine error");
    }
}

void KisakCrash_MarkExpectedExit(
    const char* reason,
    const int exitCode)
{
    WriteSessionStatus(
        "EXPECTED_EXIT",
        reason != nullptr ? reason : "normal exit",
        exitCode,
        nullptr,
        nullptr);
}
