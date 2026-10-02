#include "native_api.h"
#include <stdlib.h>
#include <unordered_map>

using namespace bridge;

class Session
{
    NativeApi &api;
    std::unordered_map<DWORD, PVOID> handles;

  public:
    explicit Session(NativeApi &native) : api(native)
    {
    }
    ~Session()
    {
        for (const auto &entry : handles)
            api.FT_Close(entry.second);
    }

    Response dispatch(const Request &request, std::vector<BYTE> &input, std::vector<BYTE> &output)
    {
        Response response;
        response.status = invalidParameter;
        const auto operation = request.operation;
        const DWORD *argument = request.argument;
        PVOID device = nullptr;
        auto found = handles.find(request.token);
        if (needsHandle(operation))
        {
            if (found == handles.end())
            {
                response.status = invalidHandle;
                return response;
            }
            device = found->second;
        }
        if (!input.empty() && operation != Operation::write && operation != Operation::openEx)
            return response;
        if (isOpen(operation) && (!request.token || found != handles.end()))
            return response;

        switch (operation)
        {
        case Operation::list:
            response.status = api.FT_CreateDeviceInfoList(&response.value[0]);
            break;
        case Operation::detail: {
            output.resize(80);
            PVOID nativeHandle = nullptr;
            response.status = api.FT_GetDeviceInfoDetail(
                argument[0], &response.value[0], &response.value[1], &response.value[2],
                &response.value[3], output.data(), output.data() + 16, &nativeHandle);
            for (const auto &entry : handles)
                if (entry.second == nativeHandle)
                    response.token = entry.first;
            break;
        }
        case Operation::deviceInfo:
            output.resize(80);
            response.status = api.FT_GetDeviceInfo(device, &response.value[0], &response.value[1],
                                                   output.data(), output.data() + 16, nullptr);
            break;
        case Operation::open:
        case Operation::openEx: {
            // Reserve the map slot before opening hardware: an allocation
            // failure must not leak an already-open native handle.
            if (operation == Operation::openEx && argument[0] != 4 &&
                ((argument[0] != 1 && argument[0] != 2) || input.empty() || input.size() > 256 ||
                 input.back() != 0))
                break;
            handles.emplace(request.token, nullptr);
            if (operation == Operation::open)
                response.status = api.FT_Open(static_cast<int>(argument[0]), &device);
            else
            {
                PVOID selector = argument[0] == 4
                                     ? reinterpret_cast<PVOID>(static_cast<uintptr_t>(argument[1]))
                                     : input.data();
                response.status = api.FT_OpenEx(selector, argument[0], &device);
            }
            if (response.status == ok)
            {
                handles[request.token] = device;
                response.token = request.token;
            }
            else
                handles.erase(request.token);
            break;
        }
        case Operation::close:
            response.status = api.FT_Close(device);
            if (response.status == ok)
                handles.erase(request.token);
            break;
        case Operation::read:
            if (argument[0] > maxPayload)
                break;
            output.resize(argument[0]);
            // Exactly one native read, with the timeout requested by NSP.
            // Do not replace partial reads with retry loops or a cached queue.
            response.status =
                argument[0] ? api.FT_Read(device, output.data(), argument[0], &response.value[0])
                            : ok;
            if (response.value[0] > argument[0])
            {
                response.status = ioError;
                response.value[0] = 0;
            }
            output.resize(response.value[0]);
            break;
        case Operation::write:
            if (input.size() != argument[0])
                break;
            response.status =
                argument[0] ? api.FT_Write(device, input.data(), argument[0], &response.value[0])
                            : ok;
            if (response.value[0] > argument[0])
            {
                response.status = ioError;
                response.value[0] = 0;
            }
            break;
        case Operation::queue:
            response.status = api.FT_GetQueueStatus(device, &response.value[0]);
            break;
        case Operation::status:
            response.status = api.FT_GetStatus(device, &response.value[0], &response.value[1],
                                               &response.value[2]);
            break;
        case Operation::purge:
            response.status = api.FT_Purge(device, argument[0]);
            break;
        case Operation::baud:
            response.status = api.FT_SetBaudRate(device, argument[0]);
            break;
        case Operation::data:
            response.status = api.FT_SetDataCharacteristics(device, static_cast<UCHAR>(argument[0]),
                                                            static_cast<UCHAR>(argument[1]),
                                                            static_cast<UCHAR>(argument[2]));
            break;
        case Operation::flow:
            response.status = api.FT_SetFlowControl(device, static_cast<USHORT>(argument[0]),
                                                    static_cast<UCHAR>(argument[1]),
                                                    static_cast<UCHAR>(argument[2]));
            break;
        case Operation::timeouts:
            response.status = api.FT_SetTimeouts(device, argument[0], argument[1]);
            break;
        case Operation::bitMode:
            response.status = api.FT_SetBitMode(device, static_cast<UCHAR>(argument[0]),
                                                static_cast<UCHAR>(argument[1]));
            break;
        case Operation::latency:
            response.status = api.FT_SetLatencyTimer(device, static_cast<UCHAR>(argument[0]));
            break;
        case Operation::getLatency:
        case Operation::getBitMode: {
            UCHAR value = 0;
            response.status = operation == Operation::getLatency
                                  ? api.FT_GetLatencyTimer(device, &value)
                                  : api.FT_GetBitMode(device, &value);
            response.value[0] = value;
            break;
        }
        case Operation::usb:
            response.status = api.FT_SetUSBParameters(device, argument[0], argument[1]);
            break;
        case Operation::chars:
            response.status = api.FT_SetChars(
                device, static_cast<UCHAR>(argument[0]), static_cast<UCHAR>(argument[1]),
                static_cast<UCHAR>(argument[2]), static_cast<UCHAR>(argument[3]));
            break;
        case Operation::reset:
            response.status = api.FT_ResetDevice(device);
            break;
        case Operation::libraryVersion:
            response.status = api.FT_GetLibraryVersion(&response.value[0]);
            break;
        case Operation::driverVersion:
            response.status = api.FT_GetDriverVersion(device, &response.value[0]);
            break;
        default:
            break;
        }
        response.payloadBytes = static_cast<DWORD>(output.size());
        return response;
    }
};

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
        return 2;
    wchar_t *end = nullptr;
    auto requestPipe =
        reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(argv[1], &end, 10)));
    if (!requestPipe || !end || *end)
        return 2;
    auto responsePipe =
        reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(argv[2], &end, 10)));
    if (!responsePipe || !end || *end)
        return 2;

    NativeApi api;
    Response ready;
    ready.status = api.load() ? ok : deviceNotFound;
    ready.win32Error = ready.status == ok ? 0 : GetLastError();
    if (!writeAll(responsePipe, &ready, sizeof(ready)) || ready.status != ok)
        return 3;

    Session session(api);
    try
    {
        Request request;
        while (readAll(requestPipe, &request, sizeof(request)))
        {
            if (request.signature != magic || request.protocolVersion != version ||
                request.payloadBytes > maxPayload)
                break;
            std::vector<BYTE> input(request.payloadBytes), output;
            if (!readAll(requestPipe, input.data(), request.payloadBytes))
                break;
            Response response = session.dispatch(request, input, output);
            std::vector<BYTE> packet(sizeof(response) + output.size());
            memcpy(packet.data(), &response, sizeof(response));
            if (!output.empty())
                memcpy(packet.data() + sizeof(response), output.data(), output.size());
            if (!writeAll(responsePipe, packet.data(), static_cast<DWORD>(packet.size())))
                break;
        }
    }
    catch (...)
    {
        // Disconnect on resource/unexpected failure. A half-completed ECU
        // write must never be replayed automatically by either side.
    }
    CloseHandle(requestPipe);
    CloseHandle(responsePipe);
    return 0;
}
