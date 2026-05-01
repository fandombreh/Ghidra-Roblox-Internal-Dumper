#include <ntddk.h>

#define DRIVER_NAME L"\\Device\\RobloxDumper"
#define DOS_DEVICE_NAME L"\\DosDevices\\RobloxDumper"

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

PDEVICE_OBJECT g_DeviceObject = NULL;
UNICODE_STRING g_DriverName, g_DosDeviceName;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    
    DbgPrint("[RobloxDumper] Driver loading...\n");
    
    RtlInitUnicodeString(&g_DriverName, DRIVER_NAME);
    RtlInitUnicodeString(&g_DosDeviceName, DOS_DEVICE_NAME);
    
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &g_DriverName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_DeviceObject
    );
    
    if (!NT_SUCCESS(status)) {
        DbgPrint("[RobloxDumper] Failed to create device: 0x%X\n", status);
        return status;
    }
    
    status = IoCreateSymbolicLink(&g_DosDeviceName, &g_DriverName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[RobloxDumper] Failed to create symbolic link: 0x%X\n", status);
        IoDeleteDevice(g_DeviceObject);
        return status;
    }
    
    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    
    for (ULONG i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = DefaultDispatch;
    }
    
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControlDispatch;
    DriverObject->DriverUnload = DriverUnload;
    
    DbgPrint("[RobloxDumper] Driver loaded successfully\n");
    return STATUS_SUCCESS;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    
    DbgPrint("[RobloxDumper] Driver unloading...\n");
    
    IoDeleteSymbolicLink(&g_DosDeviceName);
    IoDeleteDevice(g_DeviceObject);
    
    DbgPrint("[RobloxDumper] Driver unloaded\n");
}

NTSTATUS DefaultDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return STATUS_SUCCESS;
}

NTSTATUS DeviceControlDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    ULONG controlCode = ioStack->Parameters.DeviceIoControl.IoControlCode;
    
    NTSTATUS status = STATUS_SUCCESS;
    ULONG bytesReturned = 0;
    
    switch (controlCode) {
        case IOCTL_READ_MEMORY:
            status = HandleReadMemory(Irp, &bytesReturned);
            break;
        case IOCTL_GET_MODULE_BASE:
            status = HandleGetModuleBase(Irp, &bytesReturned);
            break;
        case IOCTL_ENUM_MODULES:
            status = HandleEnumModules(Irp, &bytesReturned);
            break;
        case IOCTL_SCAN_MEMORY:
            status = HandleScanMemory(Irp, &bytesReturned);
            break;
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return status;
}

PEPROCESS GetProcessById(ULONG pid) {
    PEPROCESS process;
    if (PsLookupProcessByProcessId((HANDLE)pid, &process) == STATUS_SUCCESS) {
        return process;
    }
    return NULL;
}

NTSTATUS HandleReadMemory(PIRP Irp, PULONG bytesReturned) {
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PMEMORY_READ_REQUEST request = (PMEMORY_READ_REQUEST)Irp->AssociatedIrp.SystemBuffer;
    PVOID outputBuffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG outputLength = ioStack->Parameters.DeviceIoControl.OutputBufferLength;
    
    if (!request || request->Size > outputLength) {
        *bytesReturned = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    PEPROCESS process = GetProcessById(request->ProcessId);
    if (!process) {
        *bytesReturned = 0;
        return STATUS_NOT_FOUND;
    }
    
    KAPC_STATE apcState;
    KeStackAttachProcess(process, &apcState);
    
    __try {
        RtlCopyMemory(outputBuffer, (PVOID)request->Address, request->Size);
        *bytesReturned = request->Size;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *bytesReturned = 0;
        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(process);
        return STATUS_ACCESS_VIOLATION;
    }
    
    KeUnstackDetachProcess(&apcState);
    ObDereferenceObject(process);
    return STATUS_SUCCESS;
}

NTSTATUS HandleGetModuleBase(PIRP Irp, PULONG bytesReturned) {
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PMEMORY_READ_REQUEST request = (PMEMORY_READ_REQUEST)Irp->AssociatedIrp.SystemBuffer;
    ULONG64* outputBuffer = (ULONG64*)Irp->AssociatedIrp.SystemBuffer;
    
    if (!request || ioStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG64)) {
        *bytesReturned = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    PEPROCESS process = GetProcessById(request->ProcessId);
    if (!process) {
        *bytesReturned = 0;
        return STATUS_NOT_FOUND;
    }
    
    KAPC_STATE apcState;
    KeStackAttachProcess(process, &apcState);
    
    __try {
        PPEB peb = PsGetProcessPeb(process);
        if (peb && peb->Ldr) {
            PLIST_ENTRY head = &peb->Ldr->InLoadOrderModuleList;
            PLIST_ENTRY entry = head->Flink;
            
            while (entry != head) {
                PLDR_DATA_TABLE_ENTRY module = CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                
                if (module->BaseAddress) {
                    *outputBuffer = (ULONG64)module->BaseAddress;
                    *bytesReturned = sizeof(ULONG64);
                    KeUnstackDetachProcess(&apcState);
                    ObDereferenceObject(process);
                    return STATUS_SUCCESS;
                }
                
                entry = entry->Flink;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *bytesReturned = 0;
        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(process);
        return STATUS_ACCESS_VIOLATION;
    }
    
    KeUnstackDetachProcess(&apcState);
    ObDereferenceObject(process);
    *bytesReturned = 0;
    return STATUS_NOT_FOUND;
}

NTSTATUS HandleEnumModules(PIRP Irp, PULONG bytesReturned) {
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PMEMORY_READ_REQUEST request = (PMEMORY_READ_REQUEST)Irp->AssociatedIrp.SystemBuffer;
    PMODULE_INFO outputBuffer = (PMODULE_INFO)Irp->AssociatedIrp.SystemBuffer;
    ULONG outputLength = ioStack->Parameters.DeviceIoControl.OutputBufferLength;
    
    PEPROCESS process = GetProcessById(request->ProcessId);
    if (!process) {
        *bytesReturned = 0;
        return STATUS_NOT_FOUND;
    }
    
    KAPC_STATE apcState;
    KeStackAttachProcess(process, &apcState);
    
    ULONG moduleCount = 0;
    ULONG maxModules = outputLength / sizeof(MODULE_INFO);
    
    __try {
        PPEB peb = PsGetProcessPeb(process);
        if (peb && peb->Ldr) {
            PLIST_ENTRY head = &peb->Ldr->InLoadOrderModuleList;
            PLIST_ENTRY entry = head->Flink;
            
            while (entry != head && moduleCount < maxModules) {
                PLDR_DATA_TABLE_ENTRY module = CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                
                if (module->BaseAddress) {
                    outputBuffer[moduleCount].BaseAddress = (ULONG64)module->BaseAddress;
                    outputBuffer[moduleCount].Size = module->SizeOfImage;
                    
                    if (module->BaseDllName.Buffer && module->BaseDllName.Length < 260) {
                        RtlCopyMemory(outputBuffer[moduleCount].Name, 
                                     module->BaseDllName.Buffer, 
                                     module->BaseDllName.Length);
                        outputBuffer[moduleCount].Name[module->BaseDllName.Length / sizeof(WCHAR)] = L'\0';
                    }
                    
                    moduleCount++;
                }
                
                entry = entry->Flink;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(process);
        return STATUS_ACCESS_VIOLATION;
    }
    
    KeUnstackDetachProcess(&apcState);
    ObDereferenceObject(process);
    
    *bytesReturned = moduleCount * sizeof(MODULE_INFO);
    return STATUS_SUCCESS;
}

NTSTATUS HandleScanMemory(PIRP Irp, PULONG bytesReturned) {
    PIO_STACK_LOCATION ioStack = IoGetCurrentIrpStackLocation(Irp);
    PSCAN_REQUEST request = (PSCAN_REQUEST)Irp->AssociatedIrp.SystemBuffer;
    ULONG64* outputBuffer = (ULONG64*)Irp->AssociatedIrp.SystemBuffer;
    
    if (!request || ioStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(ULONG64)) {
        *bytesReturned = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    PEPROCESS process = GetProcessById(request->ProcessId);
    if (!process) {
        *bytesReturned = 0;
        return STATUS_NOT_FOUND;
    }
    
    KAPC_STATE apcState;
    KeStackAttachProcess(process, &apcState);
    
    __try {
        for (ULONG64 addr = request->StartAddress; addr < request->EndAddress; addr++) {
            BOOLEAN match = TRUE;
            
            for (ULONG i = 0; i < request->PatternLength; i++) {
                if (request->Pattern[i] == '?') continue;
                
                PUCHAR byte = (PUCHAR)addr + i;
                if (*byte != (UCHAR)request->Pattern[i]) {
                    match = FALSE;
                    break;
                }
            }
            
            if (match) {
                *outputBuffer = addr;
                *bytesReturned = sizeof(ULONG64);
                KeUnstackDetachProcess(&apcState);
                ObDereferenceObject(process);
                return STATUS_SUCCESS;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        *bytesReturned = 0;
        KeUnstackDetachProcess(&apcState);
        ObDereferenceObject(process);
        return STATUS_ACCESS_VIOLATION;
    }
    
    KeUnstackDetachProcess(&apcState);
    ObDereferenceObject(process);
    *bytesReturned = 0;
    return STATUS_NOT_FOUND;
}
