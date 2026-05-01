#pragma once

typedef struct _HOOK_CONTEXT {
    void* OriginalFunction;
    void* HookFunction;
    void* Trampoline;
} HOOK_CONTEXT, *PHOOK_CONTEXT;

long InitializeHookManager();
long InstallHook(void* targetFunction, void* hookFunction, PHOOK_CONTEXT* outContext);
long RemoveHook(PHOOK_CONTEXT context);
void CleanupHookManager();
