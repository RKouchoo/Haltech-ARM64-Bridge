#include "diagnostics.h"
#include "native_protocol.h"
#include <new>
#include <string>

using namespace bridge;

namespace
{
HMODULE thisModule;
SRWLOCK transportLock = SRWLOCK_INIT;
HANDLE requestPipe = INVALID_HANDLE_VALUE;
HANDLE responsePipe = INVALID_HANDLE_VALUE;
HANDLE helperJob = nullptr;
// Never reset this counter on a helper restart. An old FT_HANDLE must not
// accidentally identify a newly-opened ECU after a transport failure.
DWORD nextToken = 1;

struct Lock
{
    Lock()
    {
        AcquireSRWLockExclusive(&transportLock);
    }
    ~Lock()
    {
        ReleaseSRWLockExclusive(&transportLock);
    }
};

struct Handle
{
    HANDLE value = nullptr;
    ~Handle()
    {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    HANDLE release()
    {
        HANDLE result = value;
        value = nullptr;
        return result;
    }
};

void disconnect()
{
    if (requestPipe != INVALID_HANDLE_VALUE)
        CloseHandle(requestPipe);
    if (responsePipe != INVALID_HANDLE_VALUE)
        CloseHandle(responsePipe);
    requestPipe = responsePipe = INVALID_HANDLE_VALUE;
    // Closing the job also handles an orphaned helper blocked inside FT_Read.
    if (helperJob)
        CloseHandle(helperJob);
    helperJob = nullptr;
    diagnostics::close();
}

bool launch()
{
    wchar_t modulePath[32768];
    DWORD length = GetModuleFileNameW(thisModule, modulePath, ARRAYSIZE(modulePath));
    if (!length || length >= ARRAYSIZE(modulePath))
        return false;
    std::wstring helperPath(modulePath);
    size_t separator = helperPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return false;
    helperPath.resize(separator + 1);
    helperPath += helperName;

    SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
    Handle childRequest, parentRequest, parentResponse, childResponse;
    if (!CreatePipe(&childRequest.value, &parentRequest.value, &security, 131072) ||
        !CreatePipe(&parentResponse.value, &childResponse.value, &security, 131072) ||
        !SetHandleInformation(parentRequest.value, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parentResponse.value, HANDLE_FLAG_INHERIT, 0))
        return false;

    std::wstring command = L"\"" + helperPath + L"\" " +
                           std::to_wstring(reinterpret_cast<uintptr_t>(childRequest.value)) + L" " +
                           std::to_wstring(reinterpret_cast<uintptr_t>(childResponse.value));

    // Give the helper only its two anonymous pipe endpoints. There is no
    // globally named pipe to collide with, and no unrelated handles inherited.
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<BYTE> storage(attributeBytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes))
        return false;
    HANDLE inherited[] = {childRequest.value, childResponse.value};
    BOOL attributesReady =
        UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                  sizeof(inherited), nullptr, nullptr);
    if (!attributesReady)
    {
        DeleteProcThreadAttributeList(attributes);
        return false;
    }

    Handle job;
    job.value = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                               &limits, sizeof(limits)))
    {
        DeleteProcThreadAttributeList(attributes);
        return false;
    }
    STARTUPINFOEXW startup = {};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process = {};
    BOOL created =
        CreateProcessW(helperPath.c_str(), &command[0], nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                       nullptr, &startup.StartupInfo, &process);
    DWORD launchError = created ? 0 : GetLastError();
    DeleteProcThreadAttributeList(attributes);
    if (!created)
    {
        SetLastError(launchError);
        return false;
    }
    Handle processHandle, threadHandle;
    processHandle.value = process.hProcess;
    threadHandle.value = process.hThread;
    if (!AssignProcessToJobObject(job.value, process.hProcess) ||
        ResumeThread(process.hThread) == static_cast<DWORD>(-1))
    {
        launchError = GetLastError();
        TerminateProcess(process.hProcess, 1);
        SetLastError(launchError);
        return false;
    }
    // Close our copies now: otherwise EOF would not arrive if the helper died.
    CloseHandle(childRequest.release());
    CloseHandle(childResponse.release());
    Response ready;
    if (!readAll(parentResponse.value, &ready, sizeof(ready)) || ready.signature != magic ||
        ready.protocolVersion != version || ready.status != ok || ready.payloadBytes)
    {
        SetLastError(ready.win32Error ? ready.win32Error : ERROR_BROKEN_PIPE);
        return false;
    }
    requestPipe = parentRequest.release();
    responsePipe = parentResponse.release();
    helperJob = job.release();
    diagnostics::open();
    diagnostics::record("native-helper", 0, 0, ok, 0, 0);
    return true;
}

struct Result
{
    Response response;
    std::vector<BYTE> payload;
};

const char *operationName(Operation operation)
{
    switch (operation)
    {
    case Operation::read:
        return "read";
    case Operation::write:
        return "write";
    case Operation::open:
        return "open";
    case Operation::openEx:
        return "open-ex";
    case Operation::close:
        return "close";
    case Operation::timeouts:
        return "timeouts";
    case Operation::latency:
        return "latency";
    case Operation::usb:
        return "usb-transfers";
    case Operation::purge:
        return "purge";
    case Operation::reset:
        return "reset";
    default:
        return "control";
    }
}

Result call(Operation operation, PVOID handle = nullptr, DWORD first = 0, DWORD second = 0,
            DWORD third = 0, DWORD fourth = 0, const void *payload = nullptr,
            DWORD bytes = 0) noexcept
{
    Result result;
    Lock guard;
    if (needsHandle(operation) && (!handle || requestPipe == INVALID_HANDLE_VALUE))
    {
        result.response.status = invalidHandle;
        return result;
    }
    if (bytes > maxPayload || (bytes && !payload))
    {
        result.response.status = invalidParameter;
        return result;
    }
    try
    {
        if (requestPipe == INVALID_HANDLE_VALUE && !launch())
        {
            result.response.status = deviceNotFound;
            result.response.win32Error = GetLastError();
            return result;
        }
        Request request;
        request.operation = operation;
        request.token = static_cast<DWORD>(reinterpret_cast<uintptr_t>(handle));
        if (isOpen(operation))
        {
            if (nextToken == MAXDWORD)
            {
                result.response.status = insufficientResources;
                return result;
            }
            request.token = nextToken++;
        }
        request.argument[0] = first;
        request.argument[1] = second;
        request.argument[2] = third;
        request.argument[3] = fourth;
        request.payloadBytes = bytes;
        std::vector<BYTE> packet(sizeof(request) + bytes);
        memcpy(packet.data(), &request, sizeof(request));
        if (bytes)
            memcpy(packet.data() + sizeof(request), payload, bytes);
        LONGLONG started = diagnostics::start();
        bool exchanged = writeAll(requestPipe, packet.data(), static_cast<DWORD>(packet.size())) &&
                         readAll(responsePipe, &result.response, sizeof(result.response));
        if (exchanged)
        {
            const Response &reply = result.response;
            exchanged = reply.signature == magic && reply.protocolVersion == version &&
                        reply.payloadBytes <= maxPayload;
            if (exchanged && operation == Operation::read)
                exchanged = reply.value[0] <= first && reply.payloadBytes == reply.value[0];
            else if (exchanged &&
                     (operation == Operation::detail || operation == Operation::deviceInfo))
                exchanged = reply.payloadBytes == (reply.status == ok ? 80u : 0u) ||
                            (reply.status != ok && reply.payloadBytes == 80);
            else if (exchanged)
                exchanged = reply.payloadBytes == 0;
            if (exchanged && operation == Operation::write)
                exchanged = reply.value[0] <= first;
            if (exchanged && isOpen(operation) && reply.status == ok)
                exchanged = reply.token == request.token;
        }
        if (exchanged && result.response.payloadBytes)
        {
            result.payload.resize(result.response.payloadBytes);
            exchanged = readAll(responsePipe, result.payload.data(), result.response.payloadBytes);
        }
        if (!exchanged)
        {
            DWORD error = GetLastError();
            diagnostics::record("pipe-failure", first, 0, ioError, error, started);
            disconnect();
            result.response = Response{};
            result.response.win32Error = error;
            result.payload.clear();
            // In particular, do NOT restart the helper and repeat an ECU write.
            return result;
        }
        if (operation == Operation::timeouts && result.response.status == ok)
        {
            diagnostics::readTimeoutMs = first;
            diagnostics::writeTimeoutMs = second;
        }
        if (operation == Operation::queue || operation == Operation::status)
        {
            diagnostics::queuePoll(started);
            if (result.response.status != ok)
                diagnostics::record("queue-error", 0, 0, result.response.status, 0, started);
        }
        else if (operation != Operation::list && operation != Operation::detail &&
                 operation != Operation::libraryVersion)
            diagnostics::record(operationName(operation), first, result.response.value[0],
                                result.response.status, 0, started);
        if (operation == Operation::close)
            diagnostics::flush();
    }
    catch (...)
    {
        disconnect();
        result.response = Response{};
        result.response.status = insufficientResources;
        result.payload.clear();
    }
    return result;
}

void copyIdentity(const Result &result, LPVOID serial, LPVOID description)
{
    if (serial)
        ZeroMemory(serial, 16);
    if (description)
        ZeroMemory(description, 64);
    if (result.response.status == ok && result.payload.size() == 80)
    {
        if (serial)
            memcpy(serial, result.payload.data(), 16);
        if (description)
            memcpy(description, result.payload.data() + 16, 64);
    }
}
} // namespace

#define EXP extern "C" __declspec(dllexport) DWORD WINAPI

EXP FT_CreateDeviceInfoList(LPDWORD count)
{
    if (!count)
        return invalidParameter;
    auto result = call(Operation::list);
    *count = result.response.value[0];
    return result.response.status;
}

EXP FT_GetDeviceInfoDetail(DWORD index, LPDWORD flags, LPDWORD type, LPDWORD id, LPDWORD location,
                           LPVOID serial, LPVOID description, PVOID *handle)
{
    auto result = call(Operation::detail, nullptr, index);
    if (flags)
        *flags = result.response.value[0];
    if (type)
        *type = result.response.value[1];
    if (id)
        *id = result.response.value[2];
    if (location)
        *location = result.response.value[3];
    if (handle)
        *handle = reinterpret_cast<PVOID>(static_cast<uintptr_t>(result.response.token));
    copyIdentity(result, serial, description);
    return result.response.status;
}

EXP FT_GetDeviceInfo(PVOID handle, LPDWORD type, LPDWORD id, LPVOID serial, LPVOID description,
                     LPVOID)
{
    auto result = call(Operation::deviceInfo, handle);
    if (type)
        *type = result.response.value[0];
    if (id)
        *id = result.response.value[1];
    copyIdentity(result, serial, description);
    return result.response.status;
}

EXP FT_Open(int index, PVOID *handle)
{
    if (!handle)
        return invalidParameter;
    *handle = nullptr;
    if (index < 0)
        return deviceNotFound;
    auto result = call(Operation::open, nullptr, static_cast<DWORD>(index));
    if (result.response.status == ok)
        *handle = reinterpret_cast<PVOID>(static_cast<uintptr_t>(result.response.token));
    return result.response.status;
}

EXP FT_OpenEx(PVOID selector, DWORD flags, PVOID *handle)
{
    if (!handle)
        return invalidParameter;
    *handle = nullptr;
    if (!selector || (flags != 1 && flags != 2 && flags != 4))
        return invalidParameter;
    DWORD bytes = 0;
    if (flags != 4)
    {
        size_t length = strnlen_s(static_cast<const char *>(selector), 256);
        if (length == 256)
            return invalidParameter;
        bytes = static_cast<DWORD>(length + 1);
    }
    auto result = call(Operation::openEx, nullptr, flags,
                       flags == 4 ? static_cast<DWORD>(reinterpret_cast<uintptr_t>(selector)) : 0,
                       0, 0, flags == 4 ? nullptr : selector, bytes);
    if (result.response.status == ok)
        *handle = reinterpret_cast<PVOID>(static_cast<uintptr_t>(result.response.token));
    return result.response.status;
}

EXP FT_Close(PVOID handle)
{
    return call(Operation::close, handle).response.status;
}

EXP FT_Read(PVOID handle, LPVOID buffer, DWORD requested, LPDWORD received)
{
    if (!received)
        return invalidParameter;
    *received = 0;
    if ((!buffer && requested) || requested > maxPayload)
        return invalidParameter;
    auto result = call(Operation::read, handle, requested);
    *received = result.response.value[0];
    if (*received)
        memcpy(buffer, result.payload.data(), *received);
    return result.response.status;
}

EXP FT_Write(PVOID handle, LPVOID buffer, DWORD requested, LPDWORD written)
{
    if (!written)
        return invalidParameter;
    *written = 0;
    if ((!buffer && requested) || requested > maxPayload)
        return invalidParameter;
    auto result = call(Operation::write, handle, requested, 0, 0, 0, buffer, requested);
    *written = result.response.value[0];
    return result.response.status;
}

EXP FT_GetQueueStatus(PVOID handle, LPDWORD received)
{
    if (!received)
        return invalidParameter;
    auto result = call(Operation::queue, handle);
    *received = result.response.value[0];
    return result.response.status;
}

EXP FT_GetStatus(PVOID handle, LPDWORD received, LPDWORD written, LPDWORD events)
{
    auto result = call(Operation::status, handle);
    if (received)
        *received = result.response.value[0];
    if (written)
        *written = result.response.value[1];
    if (events)
        *events = result.response.value[2];
    return result.response.status;
}

EXP FT_Purge(PVOID h, DWORD mask)
{
    return call(Operation::purge, h, mask).response.status;
}
EXP FT_SetBaudRate(PVOID h, DWORD baud)
{
    return call(Operation::baud, h, baud).response.status;
}
EXP FT_SetDataCharacteristics(PVOID h, UCHAR bits, UCHAR stop, UCHAR parity)
{
    return call(Operation::data, h, bits, stop, parity).response.status;
}
EXP FT_SetFlowControl(PVOID h, USHORT flow, UCHAR xon, UCHAR xoff)
{
    return call(Operation::flow, h, flow, xon, xoff).response.status;
}
EXP FT_SetTimeouts(PVOID h, DWORD readMs, DWORD writeMs)
{
    return call(Operation::timeouts, h, readMs, writeMs).response.status;
}
EXP FT_SetBitMode(PVOID h, UCHAR mask, UCHAR mode)
{
    return call(Operation::bitMode, h, mask, mode).response.status;
}
EXP FT_SetLatencyTimer(PVOID h, UCHAR latency)
{
    return call(Operation::latency, h, latency).response.status;
}
EXP FT_SetUSBParameters(PVOID h, DWORD inBytes, DWORD outBytes)
{
    return call(Operation::usb, h, inBytes, outBytes).response.status;
}
EXP FT_SetChars(PVOID h, UCHAR eventChar, UCHAR eventEnabled, UCHAR errorChar, UCHAR errorEnabled)
{
    return call(Operation::chars, h, eventChar, eventEnabled, errorChar, errorEnabled)
        .response.status;
}
EXP FT_ResetDevice(PVOID h)
{
    return call(Operation::reset, h).response.status;
}

EXP FT_GetLatencyTimer(PVOID handle, PUCHAR latency)
{
    if (!latency)
        return invalidParameter;
    auto result = call(Operation::getLatency, handle);
    *latency = static_cast<UCHAR>(result.response.value[0]);
    return result.response.status;
}
EXP FT_GetBitMode(PVOID handle, PUCHAR mode)
{
    if (!mode)
        return invalidParameter;
    auto result = call(Operation::getBitMode, handle);
    *mode = static_cast<UCHAR>(result.response.value[0]);
    return result.response.status;
}
EXP FT_GetLibraryVersion(LPDWORD versionNumber)
{
    if (!versionNumber)
        return invalidParameter;
    auto result = call(Operation::libraryVersion);
    *versionNumber = result.response.value[0];
    return result.response.status;
}
EXP FT_GetDriverVersion(PVOID handle, LPDWORD versionNumber)
{
    if (!versionNumber)
        return invalidParameter;
    auto result = call(Operation::driverVersion, handle);
    *versionNumber = result.response.value[0];
    return result.response.status;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        thisModule = instance;
    else if (reason == DLL_PROCESS_DETACH)
        disconnect(); // Kernel handles only; never wait for another thread here.
    return TRUE;
}
