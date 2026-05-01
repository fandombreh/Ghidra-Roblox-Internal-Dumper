#include <ntddk.h>
#include "hook_manager.h"

PHOOK_CONTEXT g_Hooks[256];
ULONG g_HookCount = 0;

NTSTATUS InitializeHookManager() {
    RtlZeroMemory(g_Hooks, sizeof(g_Hooks));
    g_HookCount = 0;
    return STATUS_SUCCESS;
}

NTSTATUS InstallHook(void* targetFunction, void* hookFunction, PHOOK_CONTEXT* outContext) {
    if (g_HookCount >= 256) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    PHOOK_CONTEXT context = (PHOOK_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(HOOK_CONTEXT), 'HOOK');
    if (!context) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    context->OriginalFunction = targetFunction;
    context->HookFunction = hookFunction;
    
    
    context->Trampoline = ExAllocatePoolWithTag(NonPagedPool, 16, 'TRAM');
    if (!context->Trampoline) {
        ExFreePoolWithTag(context, 'HOOK');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    
    RtlCopyMemory(context->Trampoline, targetFunction, 16);
    
    
    UCHAR* patch = (UCHAR*)targetFunction;
    
    
    patch[0] = 0x48;
    patch[1] = 0xB8;
    *(UINT64*)&patch[2] = (UINT64)hookFunction;
    
    
    patch[10] = 0xFF;
    patch[11] = 0xE0;
    
    g_Hooks[g_HookCount++] = context;
    *outContext = context;
    
    DbgPrint("[HookManager] Hook installed at 0x%p\n", targetFunction);
    return STATUS_SUCCESS;
}

NTSTATUS RemoveHook(PHOOK_CONTEXT context) {
    if (!context) {
        return STATUS_INVALID_PARAMETER;
    }
    
    
    RtlCopyMemory(context->OriginalFunction, context->Trampoline, 16);
    
    
    ExFreePoolWithTag(context->Trampoline, 'TRAM');
    ExFreePoolWithTag(context, 'HOOK');
    
    DbgPrint("[HookManager] Hook removed\n");
    return STATUS_SUCCESS;
}

void CleanupHookManager() {
    for (ULONG i = 0; i < g_HookCount; i++) {
        if (g_Hooks[i]) {
            RemoveHook(g_Hooks[i]);
        }
    }
    g_HookCount = 0;
}
