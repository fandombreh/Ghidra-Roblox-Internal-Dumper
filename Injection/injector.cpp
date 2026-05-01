#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>

class Injector {
private:
    HANDLE hProcess;
    std::string dllPath;
public:
    DWORD processId;

public:
    Injector(const std::string& dll) : dllPath(dll), hProcess(NULL), processId(0) {}

    ~Injector() {
        if (hProcess) {
            CloseHandle(hProcess);
        }
    }

    bool LoadKernelDriver(const std::string& driverPath, const std::string& serviceName) {
        SC_HANDLE hSCManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
        if (!hSCManager) {
            std::cerr << "Failed to open service manager: " << GetLastError() << "\n";
            return false;
        }

        // Create service
        SC_HANDLE hService = CreateServiceA(
            hSCManager,
            serviceName.c_str(),
            serviceName.c_str(),
            SERVICE_START | DELETE | SERVICE_STOP,
            SERVICE_KERNEL_DRIVER,
            SERVICE_DEMAND_START,
            SERVICE_ERROR_IGNORE,
            driverPath.c_str(),
            NULL, NULL, NULL, NULL, NULL
        );

        if (!hService) {
            DWORD error = GetLastError();
            if (error == ERROR_SERVICE_EXISTS) {
                hService = OpenServiceA(hSCManager, serviceName.c_str(), SERVICE_START | DELETE | SERVICE_STOP);
                if (!hService) {
                    CloseServiceHandle(hSCManager);
                    return false;
                }
            } else {
                std::cerr << "Failed to create service: " << error << "\n";
                CloseServiceHandle(hSCManager);
                return false;
            }
        }

        // Start service
        if (!StartServiceA(hService, 0, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_SERVICE_ALREADY_RUNNING) {
                std::cerr << "Failed to start service: " << error << "\n";
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCManager);
                return false;
            }
        }

        std::cout << "Kernel driver loaded successfully\n";
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
        return true;
    }

    bool UnloadKernelDriver(const std::string& serviceName) {
        SC_HANDLE hSCManager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
        if (!hSCManager) {
            return false;
        }

        SC_HANDLE hService = OpenServiceA(hSCManager, serviceName.c_str(), SERVICE_STOP | DELETE);
        if (!hService) {
            CloseServiceHandle(hSCManager);
            return false;
        }

        SERVICE_STATUS status;
        ControlService(hService, SERVICE_CONTROL_STOP, &status);
        DeleteService(hService);

        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
        return true;
    }

    bool RunDumper(const std::string& dumperPath, DWORD pid, const std::string& outputFile = "roblox_dump.txt") {
        std::string cmd = dumperPath + " " + std::to_string(pid) + " " + outputFile;
        
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        if (!CreateProcessA(
            NULL,
            const_cast<char*>(cmd.c_str()),
            NULL, NULL, FALSE,
            0, NULL, NULL, &si, &pi
        )) {
            std::cerr << "Failed to run dumper: " << GetLastError() << "\n";
            return false;
        }

        // Wait for dumper to complete
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        std::cout << "Dumper completed with exit code: " << exitCode << "\n";
        return exitCode == 0;
    }

    bool FindProcessByName(const std::string& processName) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create process snapshot\n";
            return false;
        }

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        if (!Process32First(hSnapshot, &pe32)) {
            CloseHandle(hSnapshot);
            return false;
        }

        do {
            if (processName == pe32.szExeFile) {
                processId = pe32.th32ProcessID;
                CloseHandle(hSnapshot);
                return true;
            }
        } while (Process32Next(hSnapshot, &pe32));

        CloseHandle(hSnapshot);
        return false;
    }

    bool AttachToProcess(DWORD pid) {
        processId = pid;
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
        if (!hProcess) {
            std::cerr << "Failed to open process: " << GetLastError() << "\n";
            return false;
        }
        return true;
    }

    bool InjectDLL() {
        if (!hProcess) {
            std::cerr << "Not attached to any process\n";
            return false;
        }

        // Allocate memory in target process
        SIZE_T pathLen = dllPath.length() + 1;
        LPVOID pRemoteMemory = VirtualAllocEx(hProcess, NULL, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pRemoteMemory) {
            std::cerr << "Failed to allocate memory in target process\n";
            return false;
        }

        // Write DLL path to target process
        if (!WriteProcessMemory(hProcess, pRemoteMemory, dllPath.c_str(), pathLen, NULL)) {
            std::cerr << "Failed to write DLL path to target process\n";
            VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
            return false;
        }

        // Get LoadLibraryA address
        HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
        LPVOID pLoadLibrary = (LPVOID)GetProcAddress(hKernel32, "LoadLibraryA");
        if (!pLoadLibrary) {
            std::cerr << "Failed to get LoadLibraryA address\n";
            VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
            return false;
        }

        // Create remote thread to load DLL
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibrary, pRemoteMemory, 0, NULL);
        if (!hThread) {
            std::cerr << "Failed to create remote thread\n";
            VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);
            return false;
        }

        // Wait for thread to complete
        WaitForSingleObject(hThread, INFINITE);
        
        // Cleanup
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, pRemoteMemory, 0, MEM_RELEASE);

        std::cout << "DLL injected successfully\n";
        return true;
    }

    bool ManualMapInject() {
        if (!hProcess) {
            std::cerr << "Not attached to any process\n";
            return false;
        }

        HANDLE hFile = CreateFileA(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to open DLL file\n";
            return false;
        }

        DWORD fileSize = GetFileSize(hFile, NULL);
        LPVOID pFileData = HeapAlloc(GetProcessHeap(), 0, fileSize);
        DWORD bytesRead;
        ReadFile(hFile, pFileData, fileSize, &bytesRead, NULL);
        CloseHandle(hFile);

        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)pFileData;
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            std::cerr << "Invalid DOS signature\n";
            HeapFree(GetProcessHeap(), 0, pFileData);
            return false;
        }

        PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)pFileData + pDosHeader->e_lfanew);
        if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
            std::cerr << "Invalid NT signature\n";
            HeapFree(GetProcessHeap(), 0, pFileData);
            return false;
        }

        // Allocate memory in target process
        LPVOID pRemoteImage = VirtualAllocEx(hProcess, NULL, pNtHeaders->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pRemoteImage) {
            std::cerr << "Failed to allocate memory for image\n";
            HeapFree(GetProcessHeap(), 0, pFileData);
            return false;
        }

        // Write headers
        WriteProcessMemory(hProcess, pRemoteImage, pFileData, pNtHeaders->OptionalHeader.SizeOfHeaders, NULL);

        // Write sections
        PIMAGE_SECTION_HEADER pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
        for (WORD i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++) {
            LPVOID pSectionDest = (LPVOID)((BYTE*)pRemoteImage + pSectionHeader[i].VirtualAddress);
            LPVOID pSectionSrc = (LPVOID)((BYTE*)pFileData + pSectionHeader[i].PointerToRawData);
            WriteProcessMemory(hProcess, pSectionDest, pSectionSrc, pSectionHeader[i].SizeOfRawData, NULL);
        }

        // Perform relocation
        IMAGE_DATA_DIRECTORY relocDir = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (relocDir.Size) {
            PIMAGE_BASE_RELOCATION pReloc = (PIMAGE_BASE_RELOCATION)((BYTE*)pFileData + relocDir.VirtualAddress);
            while (pReloc->SizeOfBlock) {
                UINT relocCount = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                PWORD pRelocEntries = (PWORD)((BYTE*)pReloc + sizeof(IMAGE_BASE_RELOCATION));
                
                for (UINT i = 0; i < relocCount; i++) {
                    if (pRelocEntries[i] >> 12 == IMAGE_REL_BASED_DIR64) {
                        UINT64* pPatch = (UINT64*)((BYTE*)pRemoteImage + pReloc->VirtualAddress + (pRelocEntries[i] & 0xFFF));
                        UINT64 delta = (UINT64)pRemoteImage - pNtHeaders->OptionalHeader.ImageBase;
                        UINT64 originalValue;
                        ReadProcessMemory(hProcess, pPatch, &originalValue, sizeof(UINT64), NULL);
                        UINT64 patchedValue = originalValue + delta;
                        WriteProcessMemory(hProcess, pPatch, &patchedValue, sizeof(UINT64), NULL);
                    }
                }
                pReloc = (PIMAGE_BASE_RELOCATION)((BYTE*)pReloc + pReloc->SizeOfBlock);
            }
        }

        // Resolve imports (simplified)
        IMAGE_DATA_DIRECTORY importDir = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.Size) {
            PIMAGE_IMPORT_DESCRIPTOR pImport = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)pFileData + importDir.VirtualAddress);
            while (pImport->Name) {
                char dllName[MAX_PATH];
                ReadProcessMemory(hProcess, (LPVOID)((BYTE*)pRemoteImage + pImport->Name), dllName, MAX_PATH, NULL);
                HMODULE hRemoteModule = LoadLibraryA(dllName);
                
                PIMAGE_THUNK_DATA pOriginalThunk = (PIMAGE_THUNK_DATA)((BYTE*)pFileData + pImport->OriginalFirstThunk);
                PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((BYTE*)pFileData + pImport->FirstThunk);
                
                while (pOriginalThunk->u1.AddressOfData) {
                    if (pOriginalThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) {
                        // Import by ordinal
                    } else {
                        PIMAGE_IMPORT_BY_NAME pImportName = (PIMAGE_IMPORT_BY_NAME)((BYTE*)pFileData + pOriginalThunk->u1.AddressOfData);
                        FARPROC pFunc = GetProcAddress(hRemoteModule, pImportName->Name);
                        UINT64 funcAddr = (UINT64)pFunc;
                        LPVOID pThunkRemote = (LPVOID)((BYTE*)pRemoteImage + (pThunk - (PIMAGE_THUNK_DATA)pFileData));
                        WriteProcessMemory(hProcess, pThunkRemote, &funcAddr, sizeof(UINT64), NULL);
                    }
                    pOriginalThunk++;
                    pThunk++;
                }
                pImport++;
            }
        }

        HeapFree(GetProcessHeap(), 0, pFileData);
        std::cout << "Manual map injection successful\n";
        return true;
    }
};

int main(int argc, char* argv[]) {
    std::cout << "Roblox Dumper Injector v1.0\n";
    std::cout << "=============================\n\n";

    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <process_name_or_pid> <dll_path> [manual]\n";
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::string target = argv[1];
    std::string dllPath = argv[2];
    bool useManualMap = (argc > 3 && std::string(argv[3]) == "manual");

    Injector injector(dllPath);

    // Try to parse as PID first
    DWORD pid = 0;
    try {
        pid = std::stoul(target);
    } catch (...) {
        pid = 0;
    }

    if (pid > 0) {
        if (!injector.AttachToProcess(pid)) {
            return 1;
        }
    } else {
        if (!injector.FindProcessByName(target)) {
            std::cerr << "Process not found: " << target << "\n";
            return 1;
        }
        if (!injector.AttachToProcess(injector.processId)) {
            return 1;
        }
    }

    // Load kernel driver if available
    std::string driverPath = "RobloxDumperKernel.sys";
    std::string serviceName = "RobloxDumper";
    
    // Check if driver file exists
    DWORD attrib = GetFileAttributesA(driverPath.c_str());
    if (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        std::cout << "Loading kernel driver...\n";
        injector.LoadKernelDriver(driverPath, serviceName);
    } else {
        std::cout << "Kernel driver not found, skipping...\n";
    }

    // Run dumper
    std::string dumperPath = "Dumper.exe";
    attrib = GetFileAttributesA(dumperPath.c_str());
    if (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
        std::cout << "Running dumper...\n";
        injector.RunDumper(dumperPath, injector.processId, "Offsets.hpp");
    } else {
        std::cout << "Dumper not found, skipping...\n";
    }

    // Inject DLL
    if (useManualMap) {
        injector.ManualMapInject();
    } else {
        injector.InjectDLL();
    }

    std::cout << "\nPress any key to exit...";
    system("pause");

    return 0;
}
