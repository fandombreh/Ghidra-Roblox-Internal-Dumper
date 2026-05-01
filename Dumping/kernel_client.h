#pragma once

#include <windows.h>

#define IOCTL_READ_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_MODULE_BASE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ENUM_MODULES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SCAN_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _MEMORY_READ_REQUEST {
    ULONG ProcessId;
    ULONG64 Address;
    ULONG Size;
} MEMORY_READ_REQUEST, *PMEMORY_READ_REQUEST;

typedef struct _MODULE_INFO {
    ULONG64 BaseAddress;
    ULONG Size;
    WCHAR Name[260];
} MODULE_INFO, *PMODULE_INFO;

typedef struct _SCAN_REQUEST {
    ULONG ProcessId;
    ULONG64 StartAddress;
    ULONG64 EndAddress;
    CHAR Pattern[256];
    ULONG PatternLength;
} SCAN_REQUEST, *PSCAN_REQUEST;

class KernelClient {
private:
    HANDLE hDevice;

public:
    KernelClient();
    ~KernelClient();

    bool Connect();
    void Disconnect();
    bool IsConnected();

    bool ReadMemory(ULONG pid, ULONG64 address, PVOID buffer, ULONG size);
    bool GetModuleBase(ULONG pid, ULONG64* baseAddress);
    bool EnumModules(ULONG pid, PMODULE_INFO modules, ULONG maxModules, ULONG* modulesReturned);
    bool ScanMemory(ULONG pid, ULONG64 start, ULONG64 end, const char* pattern, ULONG patternLength, ULONG64* result);
};
