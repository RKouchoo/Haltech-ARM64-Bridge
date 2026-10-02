#pragma once

#include "native_protocol.h"

namespace bridge
{
// The helper loads only the installed, native ARM64 FTDI library. No driver
// binaries or Haltech code are bundled with this project.
struct NativeApi
{
    HMODULE module = nullptr;
#define FTDI_FUNCTION(name, ...)                                                                   \
    using name##Function = Status(WINAPI *)(__VA_ARGS__);                                          \
    name##Function name = nullptr;
    FTDI_FUNCTION(FT_CreateDeviceInfoList, LPDWORD)
    FTDI_FUNCTION(FT_GetDeviceInfoDetail, DWORD, LPDWORD, LPDWORD, LPDWORD, LPDWORD, LPVOID, LPVOID,
                  PVOID *)
    FTDI_FUNCTION(FT_GetDeviceInfo, PVOID, LPDWORD, LPDWORD, LPVOID, LPVOID, LPVOID)
    FTDI_FUNCTION(FT_Open, int, PVOID *)
    FTDI_FUNCTION(FT_OpenEx, PVOID, DWORD, PVOID *)
    FTDI_FUNCTION(FT_Close, PVOID)
    FTDI_FUNCTION(FT_Read, PVOID, LPVOID, DWORD, LPDWORD)
    FTDI_FUNCTION(FT_Write, PVOID, LPVOID, DWORD, LPDWORD)
    FTDI_FUNCTION(FT_GetQueueStatus, PVOID, LPDWORD)
    FTDI_FUNCTION(FT_GetStatus, PVOID, LPDWORD, LPDWORD, LPDWORD)
    FTDI_FUNCTION(FT_Purge, PVOID, DWORD)
    FTDI_FUNCTION(FT_SetBaudRate, PVOID, DWORD)
    FTDI_FUNCTION(FT_SetDataCharacteristics, PVOID, UCHAR, UCHAR, UCHAR)
    FTDI_FUNCTION(FT_SetFlowControl, PVOID, USHORT, UCHAR, UCHAR)
    FTDI_FUNCTION(FT_SetTimeouts, PVOID, DWORD, DWORD)
    FTDI_FUNCTION(FT_SetBitMode, PVOID, UCHAR, UCHAR)
    FTDI_FUNCTION(FT_SetLatencyTimer, PVOID, UCHAR)
    FTDI_FUNCTION(FT_GetLatencyTimer, PVOID, PUCHAR)
    FTDI_FUNCTION(FT_GetBitMode, PVOID, PUCHAR)
    FTDI_FUNCTION(FT_SetUSBParameters, PVOID, DWORD, DWORD)
    FTDI_FUNCTION(FT_SetChars, PVOID, UCHAR, UCHAR, UCHAR, UCHAR)
    FTDI_FUNCTION(FT_ResetDevice, PVOID)
    FTDI_FUNCTION(FT_GetLibraryVersion, LPDWORD)
    FTDI_FUNCTION(FT_GetDriverVersion, PVOID, LPDWORD)
#undef FTDI_FUNCTION

    bool load()
    {
#ifdef BRIDGE_TEST_BACKEND
        // Test helpers are built into an isolated test directory. Production
        // helpers cannot select this DLL through an environment variable.
        module = LoadLibraryExW(L"fake-ftd2xx.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
#else
        module = LoadLibraryExW(L"ftd2xx.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
        if (!module)
            return false;
#define LOAD_FTDI(name)                                                                            \
    do                                                                                             \
    {                                                                                              \
        FARPROC address = GetProcAddress(module, #name);                                           \
        static_assert(sizeof(address) == sizeof(name), "Function pointer size");                   \
        memcpy(&name, &address, sizeof(name));                                                     \
        if (!name)                                                                                 \
        {                                                                                          \
            SetLastError(ERROR_PROC_NOT_FOUND);                                                    \
            return false;                                                                          \
        }                                                                                          \
    } while (false)
        LOAD_FTDI(FT_CreateDeviceInfoList);
        LOAD_FTDI(FT_GetDeviceInfoDetail);
        LOAD_FTDI(FT_GetDeviceInfo);
        LOAD_FTDI(FT_Open);
        LOAD_FTDI(FT_OpenEx);
        LOAD_FTDI(FT_Close);
        LOAD_FTDI(FT_Read);
        LOAD_FTDI(FT_Write);
        LOAD_FTDI(FT_GetQueueStatus);
        LOAD_FTDI(FT_GetStatus);
        LOAD_FTDI(FT_Purge);
        LOAD_FTDI(FT_SetBaudRate);
        LOAD_FTDI(FT_SetDataCharacteristics);
        LOAD_FTDI(FT_SetFlowControl);
        LOAD_FTDI(FT_SetTimeouts);
        LOAD_FTDI(FT_SetBitMode);
        LOAD_FTDI(FT_SetLatencyTimer);
        LOAD_FTDI(FT_GetLatencyTimer);
        LOAD_FTDI(FT_GetBitMode);
        LOAD_FTDI(FT_SetUSBParameters);
        LOAD_FTDI(FT_SetChars);
        LOAD_FTDI(FT_ResetDevice);
        LOAD_FTDI(FT_GetLibraryVersion);
        LOAD_FTDI(FT_GetDriverVersion);
#undef LOAD_FTDI
        return true;
    }

    ~NativeApi()
    {
        if (module)
            FreeLibrary(module);
    }
};
} // namespace bridge
