#pragma once
#include <windows.h>

// Test-only protocol understood by fake-ftd2xx.dll, never by the production
// helper. It lets x86 integration tests inspect settings in the ARM64 process.
constexpr char snapshotCommand[] = "HALTECH_BRIDGE_TEST_SNAPSHOT";
constexpr char crashCommand[] = "HALTECH_BRIDGE_TEST_CRASH";
constexpr char readErrorCommand[] = "HALTECH_BRIDGE_TEST_READ_ERROR";
constexpr char overreportCommand[] = "HALTECH_BRIDGE_TEST_OVERREPORT";
constexpr DWORD fakeVersion = 0x05001234;
struct FakeState
{
    DWORD readTimeout = 5000, writeTimeout = 5000;
    DWORD usbIn = 4096, usbOut = 4096;
    DWORD latency = 16, mask = 0, mode = 0;
    DWORD baud = 9600, bits = 8, stop = 0, parity = 0;
    DWORD flow = 0, xon = 0, xoff = 0;
    DWORD eventChar = 0, eventEnabled = 0, errorChar = 0, errorEnabled = 0;
    DWORD writes = 0, processId = 0;
};
