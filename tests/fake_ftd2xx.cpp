#include "fake_state.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>

struct Device
{
    FakeState state;
    std::vector<BYTE> queue;
    unsigned index;
    bool readError = false;
    bool overreport = false;
};
static Device *devices[2] = {};
static Device *lookup(PVOID handle)
{
    for (auto device : devices)
        if (device && device == handle)
            return device;
    return nullptr;
}
static bool command(LPVOID data, DWORD size, const char *expected, size_t expectedSize)
{
    return size == expectedSize && !memcmp(data, expected, size);
}
#define EXP extern "C" __declspec(dllexport) DWORD WINAPI
#define DEVICE_OR_FAIL                                                                             \
    auto device = lookup(handle);                                                                  \
    if (!device)                                                                                   \
    return 1

EXP FT_CreateDeviceInfoList(LPDWORD count)
{
    *count = 2;
    return 0;
}
EXP FT_GetDeviceInfoDetail(DWORD index, LPDWORD flags, LPDWORD type, LPDWORD id, LPDWORD location,
                           LPVOID serial, LPVOID description, PVOID *handle)
{
    if (index >= 2)
        return 2;
    if (flags)
        *flags = 2 | (devices[index] ? 1 : 0);
    if (type)
        *type = 8;
    if (id)
        *id = 0x04036014;
    if (location)
        *location = 0x13 + index;
    if (serial)
        snprintf(static_cast<char *>(serial), 16, "TEST%u", index);
    if (description)
        snprintf(static_cast<char *>(description), 64, "Bridge test %u", index);
    if (handle)
        *handle = devices[index];
    return 0;
}
EXP FT_GetDeviceInfo(PVOID handle, LPDWORD type, LPDWORD id, LPVOID serial, LPVOID description,
                     LPVOID)
{
    DEVICE_OR_FAIL;
    return FT_GetDeviceInfoDetail(device->index, nullptr, type, id, nullptr, serial, description,
                                  nullptr);
}
EXP FT_Open(int index, PVOID *handle)
{
    if (index < 0 || index >= 2)
        return 2;
    if (devices[index])
        return 3;
    auto device = new Device;
    device->index = static_cast<unsigned>(index);
    device->state.processId = GetCurrentProcessId();
    devices[index] = device;
    *handle = device;
    return 0;
}
EXP FT_OpenEx(PVOID selector, DWORD flags, PVOID *handle)
{
    for (int index = 0; index < 2; ++index)
    {
        char serial[16], description[64];
        FT_GetDeviceInfoDetail(index, nullptr, nullptr, nullptr, nullptr, serial, description,
                               nullptr);
        bool match =
            flags == 4
                ? reinterpret_cast<uintptr_t>(selector) == static_cast<unsigned>(0x13 + index)
            : flags == 1 ? !strcmp(static_cast<char *>(selector), serial)
                         : flags == 2 && !strcmp(static_cast<char *>(selector), description);
        if (match)
            return FT_Open(index, handle);
    }
    return 2;
}
EXP FT_Close(PVOID handle)
{
    DEVICE_OR_FAIL;
    devices[device->index] = nullptr;
    delete device;
    return 0;
}
EXP FT_Write(PVOID handle, LPVOID data, DWORD size, LPDWORD written)
{
    DEVICE_OR_FAIL;
    *written = size;
    if (command(data, size, crashCommand, sizeof(crashCommand)))
        ExitProcess(66);
    if (command(data, size, snapshotCommand, sizeof(snapshotCommand)))
    {
        auto first = reinterpret_cast<const BYTE *>(&device->state);
        device->queue.assign(first, first + sizeof(device->state));
        return 0;
    }
    if (command(data, size, readErrorCommand, sizeof(readErrorCommand)))
    {
        device->readError = true;
        device->queue.assign(7, 0xa5);
        return 0;
    }
    if (command(data, size, overreportCommand, sizeof(overreportCommand)))
    {
        device->overreport = true;
        return 0;
    }
    // Deterministic partial-transfer model: 10 bytes arrive per virtual ms.
    // This is an argument/byte-integrity test, not a USB timing simulation.
    if (device->state.writeTimeout)
        *written = (std::min)(size, device->state.writeTimeout * 10);
    auto first = static_cast<const BYTE *>(data);
    device->queue.insert(device->queue.end(), first, first + *written);
    ++device->state.writes;
    return 0;
}
EXP FT_Read(PVOID handle, LPVOID data, DWORD size, LPDWORD received)
{
    DEVICE_OR_FAIL;
    if (device->overreport)
    {
        *received = size + 1;
        device->overreport = false;
        return 0;
    }
    *received = (std::min)(size, static_cast<DWORD>(device->queue.size()));
    if (device->state.readTimeout)
        *received = (std::min)(*received, device->state.readTimeout * 10);
    if (*received)
        memcpy(data, device->queue.data(), *received);
    device->queue.erase(device->queue.begin(), device->queue.begin() + *received);
    if (device->readError)
    {
        device->readError = false;
        return 4;
    }
    return 0;
}
EXP FT_GetQueueStatus(PVOID handle, LPDWORD count)
{
    DEVICE_OR_FAIL;
    *count = static_cast<DWORD>(device->queue.size());
    return 0;
}
EXP FT_GetStatus(PVOID handle, LPDWORD rx, LPDWORD tx, LPDWORD events)
{
    DEVICE_OR_FAIL;
    *rx = static_cast<DWORD>(device->queue.size());
    *tx = 0;
    *events = 0;
    return 0;
}
EXP FT_Purge(PVOID handle, DWORD mask)
{
    DEVICE_OR_FAIL;
    if (mask & 1)
        device->queue.clear();
    return 0;
}
EXP FT_ResetDevice(PVOID handle)
{
    DEVICE_OR_FAIL;
    device->queue.clear();
    return 0;
}
EXP FT_SetTimeouts(PVOID handle, DWORD readMs, DWORD writeMs)
{
    DEVICE_OR_FAIL;
    device->state.readTimeout = readMs;
    device->state.writeTimeout = writeMs;
    return 0;
}
EXP FT_SetLatencyTimer(PVOID handle, UCHAR latency)
{
    DEVICE_OR_FAIL;
    if (!latency)
        return 6;
    device->state.latency = latency;
    return 0;
}
EXP FT_GetLatencyTimer(PVOID handle, PUCHAR latency)
{
    DEVICE_OR_FAIL;
    *latency = static_cast<UCHAR>(device->state.latency);
    return 0;
}
EXP FT_SetBitMode(PVOID handle, UCHAR mask, UCHAR mode)
{
    DEVICE_OR_FAIL;
    device->state.mask = mask;
    device->state.mode = mode;
    return 0;
}
EXP FT_GetBitMode(PVOID handle, PUCHAR mode)
{
    DEVICE_OR_FAIL;
    *mode = static_cast<UCHAR>(device->state.mode);
    return 0;
}
EXP FT_SetUSBParameters(PVOID handle, DWORD inBytes, DWORD outBytes)
{
    DEVICE_OR_FAIL;
    if (!inBytes || !outBytes)
        return 6;
    device->state.usbIn = inBytes;
    device->state.usbOut = outBytes;
    return 0;
}
EXP FT_SetBaudRate(PVOID handle, DWORD baud)
{
    DEVICE_OR_FAIL;
    device->state.baud = baud;
    return 0;
}
EXP FT_SetDataCharacteristics(PVOID handle, UCHAR bits, UCHAR stop, UCHAR parity)
{
    DEVICE_OR_FAIL;
    device->state.bits = bits;
    device->state.stop = stop;
    device->state.parity = parity;
    return 0;
}
EXP FT_SetFlowControl(PVOID handle, USHORT flow, UCHAR xon, UCHAR xoff)
{
    DEVICE_OR_FAIL;
    device->state.flow = flow;
    device->state.xon = xon;
    device->state.xoff = xoff;
    return 0;
}
EXP FT_SetChars(PVOID handle, UCHAR eventChar, UCHAR eventEnabled, UCHAR errorChar,
                UCHAR errorEnabled)
{
    DEVICE_OR_FAIL;
    device->state.eventChar = eventChar;
    device->state.eventEnabled = eventEnabled;
    device->state.errorChar = errorChar;
    device->state.errorEnabled = errorEnabled;
    return 0;
}
EXP FT_GetLibraryVersion(LPDWORD value)
{
    *value = fakeVersion;
    return 0;
}
EXP FT_GetDriverVersion(PVOID handle, LPDWORD value)
{
    DEVICE_OR_FAIL;
    *value = fakeVersion;
    return 0;
}
