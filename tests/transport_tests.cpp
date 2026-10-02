#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "../src/diagnostics.h"

// Exercise the real exports against a deterministic serial-driver model. No
// device is opened and no ECU commands are sent. This checks the API contract,
// not USB/VM performance; that still needs a hardware test.
struct Fragment
{
    DWORD arrivalMs;
    std::vector<BYTE> bytes;
};

static COMMTIMEOUTS serialTimeouts = {};
static DCB serialState = {};
static std::vector<Fragment> fragments;
static DWORD elapsedMs;
static bool failTimeouts;
static bool failRead;
static bool failSetup;
static bool failWrite;
static bool failPurge;
static std::vector<BYTE> writtenBytes;

static BOOL WINAPI fakeGetCommState(HANDLE, LPDCB state)
{
    *state = serialState;
    return TRUE;
}

static BOOL WINAPI fakeSetCommState(HANDLE, LPDCB state)
{
    serialState = *state;
    return TRUE;
}

static BOOL WINAPI fakeSetCommTimeouts(HANDLE, LPCOMMTIMEOUTS timeouts)
{
    if (failTimeouts)
        return FALSE;
    serialTimeouts = *timeouts;
    return TRUE;
}

static BOOL WINAPI fakeSetupComm(HANDLE, DWORD, DWORD)
{
    return !failSetup;
}

static BOOL WINAPI fakeReadFile(HANDLE, LPVOID buffer, DWORD requested, LPDWORD received,
                              LPOVERLAPPED)
{
    *received = 0;
    if (failRead || (!buffer && requested))
        return FALSE;
    if (!requested)
        return TRUE;

    // Model total-timeout reads plus the documented MAXDWORD/0/0 immediate
    // mode. Do not guess driver-specific behavior for other interval settings.
    const bool immediate = serialTimeouts.ReadIntervalTimeout == MAXDWORD &&
                           serialTimeouts.ReadTotalTimeoutConstant == 0;
    const bool unlimited = !immediate && serialTimeouts.ReadTotalTimeoutConstant == 0;
    const DWORD deadline = immediate ? elapsedMs :
                           unlimited ? MAXDWORD : elapsedMs + serialTimeouts.ReadTotalTimeoutConstant;
    for (Fragment &fragment : fragments)
    {
        if (fragment.bytes.empty() || fragment.arrivalMs > deadline)
            continue;
        if (fragment.arrivalMs > elapsedMs)
            elapsedMs = fragment.arrivalMs;
        DWORD count = static_cast<DWORD>(fragment.bytes.size());
        if (count > requested - *received)
            count = requested - *received;
        memcpy(static_cast<BYTE *>(buffer) + *received, fragment.bytes.data(), count);
        fragment.bytes.erase(fragment.bytes.begin(), fragment.bytes.begin() + count);
        *received += count;
        if (*received == requested)
            return TRUE;
    }
    if (!unlimited)
        elapsedMs = deadline;
    return TRUE;
}

static BOOL WINAPI fakeWriteFile(HANDLE, LPCVOID buffer, DWORD requested, LPDWORD transferred,
                               LPOVERLAPPED)
{
    *transferred = 0;
    if (failWrite)
        return FALSE;
    const BYTE *bytes = static_cast<const BYTE *>(buffer);
    writtenBytes.assign(bytes, bytes + requested);
    *transferred = requested;
    return TRUE;
}

static BOOL WINAPI fakePurgeComm(HANDLE, DWORD)
{
    return !failPurge;
}

// Include after windows.h so only the bridge's calls, not SDK declarations,
// are redirected. Test binaries retain the same FT_Read/FT_SetTimeouts code.
#define GetCommState fakeGetCommState
#define SetCommState fakeSetCommState
#define SetCommTimeouts fakeSetCommTimeouts
#define SetupComm fakeSetupComm
#define ReadFile fakeReadFile
#define WriteFile fakeWriteFile
#define PurgeComm fakePurgeComm
#include "../src/ftd2xx.cpp"
#undef GetCommState
#undef SetCommState
#undef SetCommTimeouts
#undef SetupComm
#undef ReadFile
#undef WriteFile
#undef PurgeComm

static int failures;
static int checks;

static void check(bool condition, const char *name)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        printf("FAIL: %s\n", name);
    }
}

static void reset()
{
    g_port = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x1234));
    serialTimeouts = {};
    serialState = {};
    fragments.clear();
    elapsedMs = 0;
    failTimeouts = failRead = failSetup = false;
    failWrite = failPurge = false;
    writtenBytes.clear();
}

static void testTimeoutContract()
{
    reset();
    check(FT_SetTimeouts(g_port, 50, 80) == FT_OK, "set finite timeouts");
    check(serialTimeouts.ReadIntervalTimeout == 0, "disable inter-byte read timeout");
    check(serialTimeouts.ReadTotalTimeoutMultiplier == 0, "no per-byte read delay");
    check(serialTimeouts.ReadTotalTimeoutConstant == 50, "honor requested read timeout");
    check(serialTimeouts.WriteTotalTimeoutMultiplier == 0 &&
              serialTimeouts.WriteTotalTimeoutConstant == 80, "honor write timeout");

    BYTE buffer[8] = {};
    DWORD received = 0;
    fragments = {{0, {0, 1}}, {2, {2, 3}}, {8, {4, 5, 6, 7}}};
    check(FT_Read(g_port, buffer, sizeof(buffer), &received) == FT_OK && received == 8,
          "collect fragmented reply");
    check(elapsedMs == 8, "return when complete, not at timeout");
    for (BYTE i = 0; i < sizeof(buffer); ++i)
        check(buffer[i] == i, "preserve binary bytes and ordering");

    fragments = {{elapsedMs, {42, 43}}};
    DWORD started = elapsedMs;
    check(FT_Read(g_port, buffer, sizeof(buffer), &received) == FT_OK && received == 2,
          "partial read at timeout is success with actual byte count");
    check(elapsedMs - started == 50, "partial read consumes one total timeout");
    started = elapsedMs;
    check(FT_Read(g_port, buffer, sizeof(buffer), &received) == FT_OK && received == 0,
          "empty timeout is not a transport error");
    check(elapsedMs - started == 50, "empty read honors timeout");

    fragments = {{elapsedMs, {9, 8, 7, 6, 5, 4, 3, 2}}};
    started = elapsedMs;
    check(FT_Read(g_port, buffer, sizeof(buffer), &received) == FT_OK && received == 8,
          "read queued live-value reply");
    check(elapsedMs == started, "no added wait for queued reply");

    check(FT_SetTimeouts(g_port, 0, 0) == FT_OK, "set unlimited timeouts");
    check(serialTimeouts.ReadIntervalTimeout == 0, "zero timeout must not mean nonblocking");
    fragments = {{elapsedMs + 5, {1, 2}}, {elapsedMs + 100, {3, 4, 5, 6, 7, 8}}};
    check(FT_Read(g_port, buffer, sizeof(buffer), &received) == FT_OK && received == 8,
          "zero timeout waits for all fragments");
    check(FT_SetTimeouts(g_port, MAXDWORD, MAXDWORD) == FT_OK &&
              serialTimeouts.ReadIntervalTimeout == 0,
          "largest finite timeout avoids invalid MAXDWORD interval combination");

    failTimeouts = true;
    check(FT_SetTimeouts(g_port, 5, 6) == FT_IO_ERROR, "propagate timeout configuration failure");
}

static void testLogBlock()
{
    reset();
    FT_SetTimeouts(g_port, 50, 50);
    std::vector<BYTE> expected(65536);
    for (size_t offset = 0; offset < expected.size(); ++offset)
        expected[offset] = static_cast<BYTE>(offset);
    // 510-byte fragments deliberately do not align with NSP's 64 KiB reads.
    for (size_t offset = 0; offset < expected.size(); offset += 510)
    {
        size_t end = offset + 510;
        if (end > expected.size())
            end = expected.size();
        fragments.push_back({static_cast<DWORD>(offset / 4096),
                             std::vector<BYTE>(expected.begin() + offset, expected.begin() + end)});
    }
    std::vector<BYTE> actual(expected.size());
    DWORD received = 0;
    check(FT_Read(g_port, actual.data(), static_cast<DWORD>(actual.size()), &received) == FT_OK &&
              received == actual.size(), "read complete 64 KiB log block");
    check(actual == expected, "64 KiB log block is byte-for-byte intact");
    check(elapsedMs == 15, "log block returns as soon as last fragment arrives");
}

static void testBinaryConfiguration()
{
    reset();
    serialState.fNull = TRUE;
    serialState.fErrorChar = TRUE;
    serialState.fDsrSensitivity = TRUE;
    serialState.fOutxDsrFlow = TRUE;
    serialState.fAbortOnError = TRUE;
    serialState.fParity = TRUE;
    check(configure() == FT_OK, "configure serial transport");
    check(!serialState.fNull && !serialState.fErrorChar, "never discard or replace binary bytes");
    check(!serialState.fDsrSensitivity && !serialState.fOutxDsrFlow,
          "no inherited DSR gating");
    check(!serialState.fAbortOnError && !serialState.fParity, "clear inherited error/parity policy");
    check(!serialState.fOutX && !serialState.fInX, "binary bytes are not XON/XOFF commands");
    check(serialState.fOutxCtsFlow && serialState.fRtsControl == RTS_CONTROL_HANDSHAKE,
          "keep NSP's hardware flow control");
    check(serialTimeouts.ReadIntervalTimeout == 0, "default reads wait for requested bytes");
    failSetup = true;
    check(configure() == FT_IO_ERROR, "propagate queue setup failure on open");
    check(FT_SetUSBParameters(g_port, 65536, 65536) == FT_IO_ERROR,
          "do not claim success when buffer setup fails");
}

static void testErrors()
{
    reset();
    BYTE buffer[8] = {};
    DWORD received = 123;
    check(FT_Read(nullptr, buffer, sizeof(buffer), &received) == FT_INVALID_HANDLE && received == 0,
          "reject invalid handle and clear byte count");
    check(FT_Read(g_port, buffer, sizeof(buffer), nullptr) == FT_INVALID_PARAMETER,
          "reject missing count pointer");
    check(FT_Read(g_port, nullptr, 1, &received) == FT_INVALID_PARAMETER && received == 0,
          "reject missing data buffer");
    check(FT_Read(g_port, nullptr, 0, &received) == FT_OK && received == 0,
          "zero-byte read is a no-op");
    failRead = true;
    check(FT_Read(g_port, buffer, sizeof(buffer), &received) == FT_IO_ERROR && received == 0,
          "report driver read failure");
    failPurge = true;
    check(FT_ResetDevice(g_port) == FT_IO_ERROR, "report reset/purge failure");
    check(FT_SetUSBParameters(nullptr, 65536, 65536) == FT_INVALID_HANDLE,
          "reject buffer setup with invalid handle");
}

static void testWritesAndIdentity()
{
    reset();
    BYTE bytes[] = {0, 0x11, 0x13, 0x80, 0xff};
    DWORD transferred = 0;
    check(FT_Write(g_port, bytes, sizeof(bytes), &transferred) == FT_OK &&
              transferred == sizeof(bytes), "write reports actual count");
    check(writtenBytes == std::vector<BYTE>(bytes, bytes + sizeof(bytes)),
          "write preserves binary data");
    check(FT_Write(g_port, nullptr, 1, &transferred) == FT_INVALID_PARAMETER && transferred == 0,
          "reject missing write buffer");
    check(FT_Write(g_port, nullptr, 0, &transferred) == FT_OK && transferred == 0,
          "zero-byte write is a no-op");
    failWrite = true;
    check(FT_Write(g_port, bytes, sizeof(bytes), &transferred) == FT_IO_ERROR && transferred == 0,
          "report write failure");

    DWORD type = 0, id = 0;
    char serial[16] = {}, description[64] = {};
    check(FT_GetDeviceInfo(g_port, &type, &id, serial, description, nullptr) == FT_OK,
          "managed wrapper can query the open device");
    check(type == kFt232hDeviceType && id == kEliteDeviceId &&
              strcmp(serial, kEliteSerialNumber) == 0 &&
              strcmp(description, kEliteDescription) == 0, "open-device identity matches enumeration");
    check(FT_GetDeviceInfo(nullptr, &type, &id, serial, description, nullptr) == FT_INVALID_HANDLE,
          "device information requires a valid open handle");
}

static void testDiagnostics()
{
    reset();
    wchar_t temporaryFolder[MAX_PATH], temporaryFile[MAX_PATH];
    if (!GetTempPathW(ARRAYSIZE(temporaryFolder), temporaryFolder) ||
        !GetTempFileNameW(temporaryFolder, L"hbt", 0, temporaryFile))
    {
        check(false, "create diagnostic test file");
        return;
    }
    SetEnvironmentVariableW(L"HALTECH_BRIDGE_TRACE", temporaryFile);
    diagnostics::open();
    check(diagnostics::file != INVALID_HANDLE_VALUE, "opt-in diagnostic file opens");
    FT_SetTimeouts(g_port, 50, 80);
    fragments = {{0, {0, 0x11, 0x13, 0xff}}};
    BYTE bytes[4];
    DWORD received = 0;
    FT_Read(g_port, bytes, sizeof(bytes), &received);
    diagnostics::close();
    HANDLE trace = CreateFileW(temporaryFile, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    char contents[4096] = {};
    DWORD length = 0;
    check(trace != INVALID_HANDLE_VALUE &&
              ReadFile(trace, contents, sizeof(contents) - 1, &length, nullptr), "read diagnostic output");
    check(strstr(contents, "read_timeout_ms,write_timeout_ms") != nullptr &&
              strstr(contents, ",read,4,4,0,0,") != nullptr,
          "diagnostics contain timeout and transfer metadata");
    if (trace != INVALID_HANDLE_VALUE)
        CloseHandle(trace);
    // Reconnecting must retain the first connection's trace rather than erase it.
    diagnostics::open();
    check(diagnostics::written > length, "reconnect appends a new header after the existing trace");
    diagnostics::close();
    SetEnvironmentVariableW(L"HALTECH_BRIDGE_TRACE", nullptr);
    diagnostics::open();
    check(diagnostics::file == INVALID_HANDLE_VALUE, "diagnostics disabled by default");
    DeleteFileW(temporaryFile);
}

int main()
{
    testTimeoutContract();
    testLogBlock();
    testBinaryConfiguration();
    testErrors();
    testWritesAndIdentity();
    testDiagnostics();
    g_port = INVALID_HANDLE_VALUE;
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
