#include <stdio.h>
#include <windows.h>

// Load the built x86 DLL as NSP does, but never open hardware. This catches
// missing undecorated exports and dependency/architecture mistakes that an
// include-based transport test cannot detect.
int wmain(int argc, wchar_t **argv)
{
    if (argc != 2)
        return 2;
    HMODULE library = LoadLibraryW(argv[1]);
    if (!library)
    {
        printf("DLL load failed: %lu\n", GetLastError());
        return 1;
    }
    const char *required[] = {"FT_CreateDeviceInfoList",
                              "FT_GetDeviceInfoDetail",
                              "FT_GetDeviceInfo",
                              "FT_Open",
                              "FT_OpenEx",
                              "FT_Close",
                              "FT_Read",
                              "FT_Write",
                              "FT_GetQueueStatus",
                              "FT_GetStatus",
                              "FT_Purge",
                              "FT_SetBaudRate",
                              "FT_SetDataCharacteristics",
                              "FT_SetFlowControl",
                              "FT_SetTimeouts",
                              "FT_SetBitMode",
                              "FT_SetLatencyTimer",
                              "FT_SetUSBParameters",
                              "FT_ResetDevice",
                              "FT_GetLibraryVersion",
                              "FT_GetDriverVersion",
                              "FT_GetLatencyTimer",
                              "FT_GetBitMode",
                              "FT_SetChars"};
    int failures = 0;
    for (const char *name : required)
    {
        if (!GetProcAddress(library, name))
        {
            printf("Missing export: %s\n", name);
            ++failures;
        }
    }
    FreeLibrary(library);
    printf("%zu DLL exports checked, %d failures (no hardware opened)\n", ARRAYSIZE(required),
           failures);
    return failures ? 1 : 0;
}
