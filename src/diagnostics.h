#pragma once

#include <windows.h>
#include <stdio.h>
#include <string.h>

// Opt-in transport metadata only: no ECU payloads, calibration values, or log
// contents. The launcher sets HALTECH_BRIDGE_TRACE to a new local CSV path.
// Calls are serialized by the transport lock. Queue polling is aggregated instead of
// writing a row per poll, and writes are buffered/capped to limit perturbation.
namespace diagnostics
{
static HANDLE file = INVALID_HANDLE_VALUE;
static LARGE_INTEGER frequency = {};
static LARGE_INTEGER origin = {};
static char buffer[8192];
static DWORD used;
static DWORD written;
static constexpr DWORD limit = 4 * 1024 * 1024;
static ULONGLONG queueCalls;
static ULONGLONG queueTimeUs;
static DWORD readTimeoutMs;
static DWORD writeTimeoutMs;

static LONGLONG now()
{
    LARGE_INTEGER value = {};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

static ULONGLONG microseconds(LONGLONG ticks)
{
    return static_cast<ULONGLONG>(ticks * 1000000 / frequency.QuadPart);
}

static LONGLONG start()
{
    return file == INVALID_HANDLE_VALUE ? 0 : now();
}

static void flush()
{
    if (file == INVALID_HANDLE_VALUE || !used)
        return;
    DWORD count = 0;
    if (!WriteFile(file, buffer, used, &count, nullptr) || count != used)
    {
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }
    written += count;
    used = 0;
}

static void close()
{
    flush();
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
}

static void append(const char *text)
{
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD length = static_cast<DWORD>(strlen(text));
    if (written + used + length > limit)
    {
        close();
        return;
    }
    if (used + length > sizeof(buffer))
        flush();
    if (file == INVALID_HANDLE_VALUE || length > sizeof(buffer))
        return;
    memcpy(buffer + used, text, length);
    used += length;
}

static void open()
{
    close();
    wchar_t path[1024];
    DWORD length = GetEnvironmentVariableW(L"HALTECH_BRIDGE_TRACE", path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path))
        return;
    // A reconnect appends to the same diagnostic session. Normal launches do
    // not set this variable; the launcher chooses a unique file each time.
    file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart >= limit ||
        !QueryPerformanceFrequency(&frequency))
    {
        close();
        return;
    }
    written = static_cast<DWORD>(size.QuadPart);
    used = 0;
    queueCalls = queueTimeUs = 0;
    readTimeoutMs = 0;
    writeTimeoutMs = 5000;
    QueryPerformanceCounter(&origin);
    append("# bridge transport diagnostics; build " __DATE__ " " __TIME__ "\n");
    append("time_us,operation,requested,transferred,status,win32_error,duration_us,"
           "read_timeout_ms,write_timeout_ms,queue_calls,queue_time_us,comm_errors\n");
    flush();
}

static void record(const char *operation, DWORD requested, DWORD transferred, DWORD status,
                   DWORD error, LONGLONG started, DWORD commErrors = 0)
{
    if (file == INVALID_HANDLE_VALUE)
        return;
    LONGLONG ended = now();
    char line[320];
    snprintf(line, sizeof(line), "%llu,%s,%lu,%lu,%lu,%lu,%llu,%lu,%lu,%llu,%llu,%lu\n",
             microseconds(ended - origin.QuadPart), operation, requested, transferred,
             status, error, started ? microseconds(ended - started) : 0,
             readTimeoutMs, writeTimeoutMs, queueCalls, queueTimeUs, commErrors);
    append(line);
    queueCalls = queueTimeUs = 0;
}

static void queuePoll(LONGLONG started)
{
    if (file != INVALID_HANDLE_VALUE && started)
    {
        ++queueCalls;
        queueTimeUs += microseconds(now() - started);
    }
}
} // namespace diagnostics
