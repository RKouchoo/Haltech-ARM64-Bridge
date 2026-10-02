#include "../src/native_api.h"
#include "fake_state.h"
#include <atomic>
#include <stdio.h>
#include <string.h>
#include <thread>

static int checks, failures;
static void check(bool success, const char *description)
{
    ++checks;
    if (!success)
    {
        ++failures;
        printf("FAIL: %s\n", description);
    }
}
static FakeState snapshot(bridge::NativeApi &api, PVOID handle)
{
    FakeState state;
    DWORD count = 0;
    check(api.FT_Write(handle, const_cast<char *>(snapshotCommand), sizeof(snapshotCommand),
                       &count) == 0,
          "send test-only snapshot command");
    check(api.FT_Read(handle, &state, sizeof(state), &count) == 0 && count == sizeof(state),
          "receive ARM64 settings snapshot");
    return state;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2)
        return 2;
    bridge::NativeApi api;
    api.module = LoadLibraryW(argv[1]);
    if (!api.module)
    {
        printf("DLL load failed: %lu\n", GetLastError());
        return 1;
    }
#define BIND(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        FARPROC address = GetProcAddress(api.module, #name);                                       \
        memcpy(&api.name, &address, sizeof(address));                                              \
        if (!api.name)                                                                             \
        {                                                                                          \
            printf("Missing %s\n", #name);                                                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
    BIND(FT_CreateDeviceInfoList);
    BIND(FT_GetDeviceInfoDetail);
    BIND(FT_GetDeviceInfo);
    BIND(FT_Open);
    BIND(FT_OpenEx);
    BIND(FT_Close);
    BIND(FT_Read);
    BIND(FT_Write);
    BIND(FT_GetQueueStatus);
    BIND(FT_GetStatus);
    BIND(FT_Purge);
    BIND(FT_ResetDevice);
    BIND(FT_SetTimeouts);
    BIND(FT_SetLatencyTimer);
    BIND(FT_GetLatencyTimer);
    BIND(FT_SetBitMode);
    BIND(FT_GetBitMode);
    BIND(FT_SetUSBParameters);
    BIND(FT_SetBaudRate);
    BIND(FT_SetDataCharacteristics);
    BIND(FT_SetFlowControl);
    BIND(FT_SetChars);
    BIND(FT_GetLibraryVersion);
    BIND(FT_GetDriverVersion);
#undef BIND
    DWORD version = 0;
    if (argc == 3 && !wcscmp(argv[2], L"--expect-startup-failure"))
    {
        DWORD count = 123;
        DWORD status = api.FT_CreateDeviceInfoList(&count);
        check(status != 0 && count == 0, "startup failure returns a clean error and zero count");
        check(api.FT_GetLibraryVersion(&version) != 0 && version == 0,
              "repeated startup failure does not leave broken handles");
        printf("Startup failure tests: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    if (api.FT_GetLibraryVersion(&version) != 0)
    {
        printf("Native helper startup/library call failed\n");
        return 1;
    }
    // Explicit smoke mode never calls FT_Open/FT_OpenEx, even if an ECU is
    // connected. It measures IPC/control-call overhead, not USB throughput.
    if (argc == 3 && !wcscmp(argv[2], L"--smoke"))
    {
        LARGE_INTEGER frequency, started, ended;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&started);
        DWORD status = 0;
        constexpr int calls = 10000;
        for (int index = 0; index < calls; ++index)
            status |= api.FT_GetLibraryVersion(&version);
        QueryPerformanceCounter(&ended);
        double totalMs = (ended.QuadPart - started.QuadPart) * 1000.0 / frequency.QuadPart;
        printf(
            "Native version=%08lx status=%lu calls=%d total_ms=%.3f microseconds_per_call=%.3f\n",
            version, status, calls, totalMs, totalMs * 1000 / calls);
        printf("No hardware opened. This is not a USB throughput benchmark.\n");
        return status ? 1 : 0;
    }
    if (version != fakeVersion)
    {
        printf("Refusing device tests: this is not the test-only ARM64 backend\n");
        return 1;
    }

    DWORD count = 0;
    check(api.FT_CreateDeviceInfoList(&count) == 0 && count == 2, "native device enumeration");
    PVOID first = nullptr, second = nullptr;
    check(api.FT_Open(0, &first) == 0 && first, "open first fake device");
    check(api.FT_OpenEx(const_cast<char *>("TEST1"), 1, &second) == 0 && second && second != first,
          "distinct tokens for two ARM64 handles");
    if (!first || !second)
        return 1;
    DWORD flags = 0, type = 0, id = 0, location = 0;
    char serial[16] = {}, description[64] = {};
    PVOID listedHandle = nullptr;
    check(api.FT_GetDeviceInfoDetail(1, &flags, &type, &id, &location, serial, description,
                                     &listedHandle) == 0 &&
              listedHandle == second && flags == 3 && type == 8 && id == 0x04036014 &&
              location == 0x14 && !strcmp(serial, "TEST1"),
          "native identity and open-handle mapping");
    check(api.FT_GetDeviceInfo(first, &type, &id, serial, description, nullptr) == 0 &&
              !strcmp(serial, "TEST0"),
          "open-device identity is not synthetic");

    check(api.FT_SetTimeouts(first, 50, 70) == 0, "forward timeouts");
    check(api.FT_SetUSBParameters(first, 65536, 32768) == 0, "forward USB transfer sizes");
    check(api.FT_SetLatencyTimer(first, 2) == 0, "forward hardware latency");
    check(api.FT_SetBitMode(first, 0xab, 0x40) == 0, "forward bit mode and mask");
    check(api.FT_SetBaudRate(first, 115200) == 0, "forward baud");
    check(api.FT_SetDataCharacteristics(first, 7, 2, 1) == 0, "forward data format");
    check(api.FT_SetFlowControl(first, 0x100, 0x11, 0x13) == 0,
          "forward flow and control characters");
    check(api.FT_SetChars(first, 0xaa, 1, 0xbb, 1) == 0, "forward event/error characters");
    FakeState state = snapshot(api, first);
    check(state.readTimeout == 50 && state.writeTimeout == 70,
          "timeouts reached ARM64 backend unchanged");
    check(state.usbIn == 65536 && state.usbOut == 32768, "USB sizes reached backend unchanged");
    check(state.latency == 2 && state.mode == 0x40 && state.mask == 0xab,
          "latency and mode reached backend");
    check(state.baud == 115200 && state.bits == 7 && state.stop == 2 && state.parity == 1,
          "data format reached backend");
    check(state.flow == 0x100 && state.xon == 0x11 && state.xoff == 0x13,
          "flow parameters reached backend");
    check(state.eventChar == 0xaa && state.eventEnabled == 1 && state.errorChar == 0xbb &&
              state.errorEnabled == 1,
          "character parameters reached backend");
    UCHAR byte = 0;
    check(api.FT_GetLatencyTimer(first, &byte) == 0 && byte == 2, "native latency readback");
    check(api.FT_GetBitMode(first, &byte) == 0 && byte == 0x40, "native mode readback");
    check(api.FT_SetLatencyTimer(first, 0) == 6, "native configuration failure is not hidden");
    check(api.FT_SetUSBParameters(first, 0, 0) == 6,
          "native USB configuration failure is not hidden");
    FakeState secondState = snapshot(api, second);
    check(secondState.readTimeout == 5000 && secondState.latency == 16,
          "device settings are isolated");

    api.FT_SetTimeouts(first, 0, 0);
    std::vector<BYTE> sent(65536), received(sent.size());
    for (size_t index = 0; index < sent.size(); ++index)
        sent[index] = static_cast<BYTE>(index);
    check(api.FT_Write(first, sent.data(), static_cast<DWORD>(sent.size()), &count) == 0 &&
              count == sent.size(),
          "write full 64 KiB block");
    check(api.FT_GetQueueStatus(first, &count) == 0 && count == sent.size(),
          "queue count immediately reflects write");
    check(api.FT_Read(first, received.data(), static_cast<DWORD>(received.size()), &count) == 0 &&
              count == received.size() && sent == received,
          "64 KiB binary block survives x86/ARM64 round trip");
    check(api.FT_GetQueueStatus(first, &count) == 0 && count == 0,
          "queue count immediately reflects read");
    api.FT_Write(first, sent.data(), 100, &count);
    api.FT_SetTimeouts(first, 3, 4);
    check(api.FT_Read(first, received.data(), 100, &count) == 0 && count == 30,
          "partial timed-out read returned once, not retried");
    api.FT_Purge(first, 3);
    check(api.FT_GetQueueStatus(first, &count) == 0 && count == 0, "no cached bytes after purge");
    check(api.FT_Write(first, sent.data(), 100, &count) == 0 && count == 40,
          "partial timed-out write returned once, not replayed");
    api.FT_ResetDevice(first);
    check(api.FT_GetQueueStatus(first, &count) == 0 && count == 0, "native reset forwarded");
    api.FT_SetTimeouts(first, 0, 0);
    api.FT_Write(first, const_cast<char *>(readErrorCommand), sizeof(readErrorCommand), &count);
    check(api.FT_Read(first, received.data(), 100, &count) == 4 && count == 7 &&
              received[0] == 0xa5,
          "preserve partial data on native read error");
    api.FT_Write(first, const_cast<char *>(overreportCommand), sizeof(overreportCommand), &count);
    check(api.FT_Read(first, received.data(), 100, &count) == 4 && count == 0,
          "reject impossible native byte count");
    check(api.FT_Read(first, nullptr, 1, &count) == 6 && count == 0, "reject null read buffer");
    check(api.FT_Write(first, nullptr, 1, &count) == 6 && count == 0, "reject null write buffer");
    check(api.FT_Read(first, nullptr, 0, &count) == 0 && count == 0, "zero-byte read");
    check(api.FT_Write(first, nullptr, 0, &count) == 0 && count == 0, "zero-byte write");
    check(api.FT_Read(first, received.data(), bridge::maxPayload + 1, &count) == 6 && count == 0,
          "oversized request rejected before allocating/copying");
    check(api.FT_OpenEx(const_cast<char *>("TEST1"), 8, &listedHandle) == 6 && !listedHandle,
          "invalid OpenEx flags rejected");

    std::atomic<int> threadErrors{0};
    std::vector<std::thread> workers;
    for (int thread = 0; thread < 4; ++thread)
        workers.emplace_back([&] {
            for (int index = 0; index < 500; ++index)
            {
                DWORD libraryVersion = 0;
                if (api.FT_GetLibraryVersion(&libraryVersion) != 0 || libraryVersion != fakeVersion)
                    ++threadErrors;
            }
        });
    for (auto &worker : workers)
        worker.join();
    check(threadErrors == 0, "four concurrent callers cannot interleave protocol frames");

    check(api.FT_Close(second) == 0, "close second handle");
    check(api.FT_GetQueueStatus(second, &count) == 1, "closed token rejected");
    check(api.FT_OpenEx(reinterpret_cast<PVOID>(0x14), 4, &second) == 0,
          "OpenEx by location uses a number, not strlen on a pointer");
    api.FT_Close(second);
    check(api.FT_OpenEx(const_cast<char *>("Bridge test 1"), 2, &second) == 0,
          "OpenEx by description");
    api.FT_Close(second);
    check(api.FT_Write(first, const_cast<char *>(crashCommand), sizeof(crashCommand), &count) ==
                  4 &&
              count == 0,
          "helper crash reports transport error without replay");
    check(api.FT_GetQueueStatus(first, &count) == 1, "handle calls do not restart a dead session");
    check(api.FT_GetLibraryVersion(&version) == 0 && version == fakeVersion,
          "enumeration/control can start a fresh session");
    PVOID reopened = nullptr;
    check(api.FT_Open(0, &reopened) == 0 && reopened != first, "reconnect allocates a fresh token");
    check(api.FT_GetQueueStatus(first, &count) == 1, "stale token cannot alias new device");
    state = snapshot(api, reopened);
    check(state.writes == 0, "failed write was not replayed into new session");
    api.FT_Close(reopened);
    HANDLE helperProcess = OpenProcess(SYNCHRONIZE, FALSE, state.processId);
    FreeLibrary(api.module);
    api.module = nullptr;
    check(helperProcess && WaitForSingleObject(helperProcess, 3000) == WAIT_OBJECT_0,
          "DLL unload terminates its helper without leaving an orphan");
    if (helperProcess)
        CloseHandle(helperProcess);
    printf("Native integration: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
