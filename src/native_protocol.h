#pragma once

#include <stdint.h>
#include <vector>
#include <windows.h>

namespace bridge
{
using Status = DWORD;
constexpr Status ok = 0;
constexpr Status invalidHandle = 1;
constexpr Status deviceNotFound = 2;
constexpr Status ioError = 4;
constexpr Status insufficientResources = 5;
constexpr Status invalidParameter = 6;
constexpr DWORD magic = 0x48465435; // HFT5: native-D2XX transport, not the old V3 pipe protocol.
constexpr DWORD version = 1;
constexpr DWORD maxPayload = 16 * 1024 * 1024;
constexpr wchar_t helperName[] = L"haltech-ftdi-arm64.exe";

enum class Operation : DWORD
{
    list = 1,
    detail,
    open,
    openEx,
    close,
    read,
    write,
    queue,
    status,
    purge,
    baud,
    data,
    flow,
    timeouts,
    bitMode,
    latency,
    usb,
    reset,
    libraryVersion,
    driverVersion,
    deviceInfo,
    getLatency,
    getBitMode,
    chars
};

// Only fixed-width values cross the x86/ARM64 boundary. Handles below are
// session tokens, never truncated ARM64 pointers or Windows file handles.
struct Request
{
    DWORD signature = magic;
    DWORD protocolVersion = version;
    Operation operation = Operation::list;
    DWORD token = 0;
    DWORD argument[4] = {};
    DWORD payloadBytes = 0;
};

struct Response
{
    DWORD signature = magic;
    DWORD protocolVersion = version;
    Status status = ioError;
    DWORD token = 0;
    DWORD value[4] = {};
    DWORD payloadBytes = 0;
    DWORD win32Error = 0;
};

static_assert(sizeof(Request) == 36, "Protocol layout must match on x86 and ARM64");
static_assert(sizeof(Response) == 40, "Protocol layout must match on x86 and ARM64");

inline bool readAll(HANDLE pipe, void *destination, DWORD bytes)
{
    auto cursor = static_cast<BYTE *>(destination);
    while (bytes)
    {
        DWORD received = 0;
        if (!ReadFile(pipe, cursor, bytes, &received, nullptr) || !received)
            return false;
        cursor += received;
        bytes -= received;
    }
    return true;
}

inline bool writeAll(HANDLE pipe, const void *source, DWORD bytes)
{
    auto cursor = static_cast<const BYTE *>(source);
    while (bytes)
    {
        DWORD sent = 0;
        if (!WriteFile(pipe, cursor, bytes, &sent, nullptr) || !sent)
            return false;
        cursor += sent;
        bytes -= sent;
    }
    return true;
}

inline bool isOpen(Operation operation)
{
    return operation == Operation::open || operation == Operation::openEx;
}

inline bool needsHandle(Operation operation)
{
    return operation != Operation::list && operation != Operation::detail &&
           operation != Operation::libraryVersion && !isOpen(operation);
}
} // namespace bridge
