#include "kernel_client.h"
#include <iostream>

KernelClient::KernelClient() : hDevice(INVALID_HANDLE_VALUE) {}

KernelClient::~KernelClient() {
    Disconnect();
}

bool KernelClient::Connect() {
    hDevice = CreateFileA(
        "\\\\.\\RobloxDumper",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to connect to kernel driver: " << GetLastError() << "\n";
        return false;
    }

    std::cout << "Connected to kernel driver\n";
    return true;
}

void KernelClient::Disconnect() {
    if (hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice);
        hDevice = INVALID_HANDLE_VALUE;
    }
}

bool KernelClient::IsConnected() {
    return hDevice != INVALID_HANDLE_VALUE;
}

bool KernelClient::ReadMemory(ULONG pid, ULONG64 address, PVOID buffer, ULONG size) {
    if (!IsConnected()) return false;

    MEMORY_READ_REQUEST request;
    request.ProcessId = pid;
    request.Address = address;
    request.Size = size;

    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_READ_MEMORY,
        &request,
        sizeof(request),
        buffer,
        size,
        &bytesReturned,
        NULL
    );

    return result && bytesReturned == size;
}

bool KernelClient::GetModuleBase(ULONG pid, ULONG64* baseAddress) {
    if (!IsConnected()) return false;

    MEMORY_READ_REQUEST request;
    request.ProcessId = pid;
    request.Address = 0;
    request.Size = 0;

    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_GET_MODULE_BASE,
        &request,
        sizeof(request),
        baseAddress,
        sizeof(ULONG64),
        &bytesReturned,
        NULL
    );

    return result && bytesReturned == sizeof(ULONG64);
}

bool KernelClient::EnumModules(ULONG pid, PMODULE_INFO modules, ULONG maxModules, ULONG* modulesReturned) {
    if (!IsConnected()) return false;

    MEMORY_READ_REQUEST request;
    request.ProcessId = pid;
    request.Address = 0;
    request.Size = 0;

    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_ENUM_MODULES,
        &request,
        sizeof(request),
        modules,
        maxModules * sizeof(MODULE_INFO),
        &bytesReturned,
        NULL
    );

    if (result) {
        *modulesReturned = bytesReturned / sizeof(MODULE_INFO);
    }

    return result;
}

bool KernelClient::ScanMemory(ULONG pid, ULONG64 start, ULONG64 end, const char* pattern, ULONG patternLength, ULONG64* result) {
    if (!IsConnected()) return false;

    SCAN_REQUEST request;
    request.ProcessId = pid;
    request.StartAddress = start;
    request.EndAddress = end;
    request.PatternLength = patternLength;
    
    if (patternLength > sizeof(request.Pattern)) {
        return false;
    }
    
    RtlCopyMemory(request.Pattern, pattern, patternLength);

    DWORD bytesReturned;
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_SCAN_MEMORY,
        &request,
        sizeof(request),
        result,
        sizeof(ULONG64),
        &bytesReturned,
        NULL
    );

    return success && bytesReturned == sizeof(ULONG64);
}
