#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include "kernel_client.h"

#pragma comment(lib, "psapi.lib")

struct RobloxClass {
    std::string name;
    uintptr_t address;
    std::vector<std::pair<std::string, uintptr_t>> properties;
    std::vector<std::pair<std::string, uintptr_t>> methods;
};

struct RobloxInstance {
    std::string className;
    std::string name;
    uintptr_t address;
    uintptr_t parent;
    std::vector<RobloxInstance> children;
};

class RobloxDumper {
private:
    HANDLE hProcess;
    DWORD processId;
    uintptr_t robloxBase;
    uintptr_t dataModel;
    std::string robloxVersion;
    std::map<std::string, RobloxClass> classes;
    KernelClient kernel;
    bool useKernel;

public:
    RobloxDumper() : hProcess(NULL), processId(0), robloxBase(0), dataModel(0), useKernel(false), robloxVersion("Unknown") {}

    ~RobloxDumper() {
        if (hProcess) {
            CloseHandle(hProcess);
        }
        kernel.Disconnect();
    }

    bool ConnectKernel() {
        if (kernel.Connect()) {
            useKernel = true;
            return true;
        }
        useKernel = false;
        return false;
    }

    bool FindVersion() {
        if (!hProcess) return false;

        
        std::string pattern = "version-";
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t addr = robloxBase;

        while (VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
                std::vector<uint8_t> buffer(mbi.RegionSize);
                if (ReadProcessMemory(hProcess, mbi.BaseAddress, buffer.data(), mbi.RegionSize, NULL)) {
                    for (size_t i = 0; i < buffer.size() - 64; ++i) {
                        if (memcmp(&buffer[i], pattern.c_str(), pattern.length()) == 0) {
                            
                            char versionBuf[64];
                            memcpy(versionBuf, &buffer[i], 64);
                            versionBuf[63] = '\0';
                            
                            
                            std::string fullVersion = versionBuf;
                            size_t endPos = fullVersion.find_first_of(" \\\n\r\t\0");
                            if (endPos != std::string::npos) {
                                fullVersion = fullVersion.substr(0, endPos);
                            }
                            
                            if (fullVersion.length() > 8) {
                                robloxVersion = "LIVE-WindowsPlayer-" + fullVersion;
                                std::cout << "Detected Roblox version: " << robloxVersion << "\n";
                                return true;
                            }
                        }
                    }
                }
            }
            addr += mbi.RegionSize;
            if (addr >= robloxBase + 0x10000000) break; 
        }
        return false;
    }

    std::string GetVersion() const {
        return robloxVersion;
    }

    bool AttachToProcess(DWORD pid) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) {
            std::cerr << "Failed to open process: " << GetLastError() << "\n";
            return false;
        }
        return true;
    }

    DWORD FindProcessByName(const std::string& processName) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create process snapshot\n";
            return 0;
        }

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        if (!Process32First(hSnapshot, &pe32)) {
            CloseHandle(hSnapshot);
            return 0;
        }

        do {
            if (processName == pe32.szExeFile) {
                CloseHandle(hSnapshot);
                return pe32.th32ProcessID;
            }
        } while (Process32Next(hSnapshot, &pe32));

        CloseHandle(hSnapshot);
        return 0;
    }

    DWORD FindRobloxProcess() {
        std::vector<std::string> robloxNames = {
            "RobloxPlayerBeta.exe",
            "RobloxPlayerLauncher.exe",
            "Roblox.exe",
            "RobloxStudioBeta.exe",
            "RobloxStudioLauncher.exe"
        };

        for (const auto& name : robloxNames) {
            DWORD pid = FindProcessByName(name);
            if (pid != 0) {
                std::cout << "Found Roblox process: " << name << " (PID: " << pid << ")\n";
                return pid;
            }
        }

        return 0;
    }

    bool FindRobloxBase() {
        if (useKernel) {
            ULONG64 base = 0;
            if (kernel.GetModuleBase(processId, &base)) {
                robloxBase = base;
                std::cout << "Found Roblox base via kernel: 0x" << std::hex << robloxBase << "\n";
                return true;
            }
        }

        HMODULE hMods[1024];
        DWORD cbNeeded;

        if (!EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
            std::cerr << "EnumProcessModules failed: " << GetLastError() << "\n";
            return false;
        }

        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            if (!hMods[i]) continue;
            
            char szModName[MAX_PATH];
            if (GetModuleFileNameExA(hProcess, hMods[i], szModName, sizeof(szModName))) {
                std::string modName(szModName);
                if (modName.find("RobloxPlayerBeta.exe") != std::string::npos ||
                    modName.find("RobloxPlayerLauncher.exe") != std::string::npos ||
                    modName.find("Roblox.exe") != std::string::npos) {
                    robloxBase = (uintptr_t)hMods[i];
                    std::cout << "Found Roblox base: 0x" << std::hex << robloxBase << "\n";
                    return true;
                }
            }
        }
        return false;
    }

    uintptr_t ScanPattern(uintptr_t start, size_t size, const std::string& pattern) {
        std::vector<uint8_t> buffer(size);
        SIZE_T bytesRead;
        
        if (!ReadProcessMemory(hProcess, (LPCVOID)start, buffer.data(), size, &bytesRead)) {
            return 0;
        }

        std::vector<uint8_t> patternBytes;
        std::vector<bool> wildcards;

        for (size_t i = 0; i < pattern.length(); i += 3) {
            if (pattern[i] == '?') {
                patternBytes.push_back(0);
                wildcards.push_back(true);
            } else {
                std::string byteStr = pattern.substr(i, 2);
                patternBytes.push_back((uint8_t)strtol(byteStr.c_str(), NULL, 16));
                wildcards.push_back(false);
            }
        }

        for (size_t i = 0; i < bytesRead - patternBytes.size(); i++) {
            bool match = true;
            for (size_t j = 0; j < patternBytes.size(); j++) {
                if (!wildcards[j] && buffer[i + j] != patternBytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return start + i;
            }
        }

        return 0;
    }

    bool FindDataModel() {
        
        std::string dataModelStr = "DataModel";
        uintptr_t scanStart = robloxBase;
        uintptr_t scanEnd = robloxBase + 0x5000000;

        for (uintptr_t addr = scanStart; addr < scanEnd && addr >= scanStart; addr += 0x1000) {
            char buffer[4096];
            SIZE_T bytesRead;

            if (ReadProcessMemory(hProcess, (LPCVOID)addr, buffer, 4096, &bytesRead)) {
                for (size_t i = 0; i < bytesRead - dataModelStr.length(); i++) {
                    if (memcmp(buffer + i, dataModelStr.c_str(), dataModelStr.length()) == 0) {
                        dataModel = addr + i;
                        std::cout << "Found DataModel string at: 0x" << std::hex << dataModel << "\n";
                        return true;
                    }
                }
            }
        }

        
        dataModel = robloxBase;
        std::cout << "Using Roblox base as DataModel reference: 0x" << std::hex << dataModel << "\n";
        return true;
    }

    bool DumpClasses() {
        std::cout << "Scanning for meaningful offsets...\n";

        uintptr_t scanStart = robloxBase;
        uintptr_t scanEnd = robloxBase + 0x20000000; 

        
        std::vector<std::string> targetStrings = {
            
            "Workspace", "Players", "Lighting", "ReplicatedStorage", "ReplicatedFirst",
            "StarterGui", "StarterPlayer", "StarterPlayerScripts", "StarterCharacterScripts",
            "Game", "Instance", "Part", "Model", "Humanoid", "Camera",
            
            "Script", "LocalScript", "ModuleScript",
            
            "TextLabel", "TextButton", "Frame", "ScreenGui", "ScrollingFrame",
            "TextBox", "ImageButton", "ViewportFrame",
            
            "lua_State", "luaL_", "lua_", "luau_", "Luau",
            "bytecode", "proto", "closure", "upvalue", "env",
            "getglobal", "setglobal", "getfield", "setfield",
            "call", "pcall", "xpcall", "loadstring", "load",
            "VM", "Execution", "Interpreter", "JIT",
            "GC", "garbage", "collect", "memory",
            "table", "array", "metatable", "__index", "__newindex",
            "thread", "coroutine", "resume", "yield",
            
            "FindFirstChild", "WaitForChild", "GetService", "Children",
            "Destroy", "Clone", "Parent", "Name", "ClassName",
            "AncestryChanged", "ChildAdded", "ChildRemoved",
            
            "Velocity", "Position", "CFrame", "Anchored", "CanCollide",
            "Mass", "Size", "BrickColor",
            
            "BasePart", "BaseScript", "BaseGui", "BasePlayerGui",
            "CharacterAppearance", "CharacterMesh", "Character",
            "ContentProvider", "ContextActionService",
            "DataModel", "DataStoreService", "Debris",
            "EffectService", "Explosion", "FaceInstance",
            "Fire", "FlyweightService", "Folder",
            "ForceField", "GamePassService", "Geometry",
            "GuiObject", "GuiService", "GuiRoot",
            "HopperBin", "HttpService", "InsertService",
            "JointInstance", "Keyframe", "KeyframeSequence",
            "Light", "Lighting", "LineConstraint",
            "LogService", "LuaSourceContainer",
            "MarkerService", "ModuleScript",
            "Motor", "Motor6D", "NetworkReplicator",
            "ObjectValue", "Output", "ParticleEmitter",
            "PathfindingService", "PhysicsService",
            "Player", "Players", "PointsService",
            "Pose", "PrismaticConstraint", "ProximityPrompt",
            "Ray", "Raycast", "RemoteEvent", "RemoteFunction",
            "ReplicatedStorage", "ReplicatedFirst",
            "RunService", "Script", "Selection",
            "ServiceProvider", "Sound", "SoundService",
            "SpawnLocation", "StarterGui", "StarterPlayer",
            "Studio", "TeleportService", "Terrain",
            "TouchInterest", "TouchTransmitter",
            "Trail", "TweenService", "UI",
            "UserInputService", "ValueBase", "VehicleSeat",
            "VehicleSimulation", "Weld", "WeldConstraint",
            "Workspace", "WrapService",
            
            "luaV_execute", "luaV_concat", "luaV_equal",
            "luaV_lessthan", "luaV_gettable", "luaV_settable",
            "luaV_objlen", "luaV_mod", "luaV_pow",
            "luaV_div", "luaV_idiv", "luaV_band",
            "luaV_bor", "luaV_bxor", "luaV_shl",
            "luaV_shr", "luaV_unm", "luaV_add",
            "luaV_sub", "luaV_mul", "luaV_tonumber",
            "luaV_tostring", "luaV_tointeger",
            "luaF_close", "luaF_newproto", "luaF_newLclosure",
            "luaC_newobj", "luaC_newthread", "luaC_newuserdata",
            "luaC_freeobj", "luaC_step", "luaC_fullgc",
            "luaH_new", "luaH_next", "luaH_get",
            "luaH_set", "luaH_getn", "luaH_resize",
            "luaH_resizearray", "luaH_setnum",
            "luaT_typenames", "luaT_eventnames",
            "luaO_nilobject", "luaO_tostring",
            "luaX_init", "luaX_setinput", "luaX_token2str",
            "luaX_lexerror", "luaX_syntaxerror",
            "luaY_parser", "luaY_newbytestring",
            "luaZ_init", "luaZ_fill", "luaZ_read",
            "luaZ_lookahead", "luaZ_skip", "luaZ_read",
            "luaZ_buffer", "luaZ_resetbuffer",
            "luaopen_base", "luaopen_coroutine", "luaopen_table",
            "luaopen_io", "luaopen_os", "luaopen_string",
            "luaopen_math", "luaopen_debug", "luaopen_bit32",
            "luaopen_package", "luaopen_utf8",
            "lua_pushnil", "lua_pushnumber", "lua_pushinteger",
            "lua_pushlstring", "lua_pushstring", "lua_pushboolean",
            "lua_pushcclosure", "lua_pushlightuserdata",
            "lua_pushthread", "lua_pushvalue",
            "lua_pushglobaltable", "lua_gettop", "lua_settop",
            "lua_getfield", "lua_gettable", "lua_geti",
            "lua_rawget", "lua_rawgeti", "lua_rawgetp",
            "lua_setfield", "lua_settable", "lua_seti",
            "lua_rawset", "lua_rawseti", "lua_rawsetp",
            "lua_getglobal", "lua_setglobal",
            "lua_call", "lua_pcall", "lua_callk", "lua_pcallk",
            "lua_load", "lua_loadx", "lua_dump",
            "lua_resume", "lua_yield", "lua_yieldk",
            "lua_status", "lua_isyieldable",
            "lua_error", "lua_newthread", "lua_newuserdata",
            "lua_newtable", "lua_createtable",
            "lua_getmetatable", "lua_setmetatable",
            "lua_touserdata", "lua_tothread", "lua_topointer",
            "lua_type", "lua_typename", "lua_isnumber",
            "lua_isstring", "lua_iscfunction", "lua_isinteger",
            "lua_isuserdata", "lua_islightuserdata", "lua_istable",
            "lua_isfunction", "lua_isthread", "lua_isnone",
            "lua_isnoneornil", "lua_isnil", "lua_isboolean",
            "lua_len", "lua_rawlen", "lua_objlen",
            "lua_stringtonumber", "lua_tolstring", "lua_tonumberx",
            "lua_tointegerx", "lua_toboolean", "lua_compare",
            "lua_equal", "lua_lessthan", "lua_rawequal",
            "lua_getallocf", "lua_setallocf",
            "lua_getstack", "lua_getinfo", "lua_getlocal",
            "lua_setlocal", "lua_getupvalue", "lua_setupvalue",
            "lua_upvalueid", "lua_upvaluejoin",
            "lua_sethook", "lua_gethook", "lua_gethookmask",
            "lua_gethookcount", "lua_gc",
            
            "NetworkClient", "NetworkServer", "NetworkReplicator", "PlayerReplicator",
            "RemoteEvent", "RemoteFunction", "UnreliableRemoteEvent",
            
            "RunService", "LogService", "Stats", "GuiService", "UserInputService",
            "ContextActionService", "HttpService", "AssetService", "TweenService",
            "ContentProvider", "PhysicsService", "Chat", "TextChatService",
            "VoiceChatService", "VRService", "ControllerService",
            
            "archivable", "name", "parent", "className", "children",
            "position", "size", "cframe", "velocity", "transparency",
            "reflectance", "canCollide", "anchored", "locked",
            "health", "maxHealth", "jump", "walkSpeed", "userId",
            
            "AssemblyLinearVelocity", "AssemblyAngularVelocity", "Mass", "CenterOfMass",
            "Friction", "Elasticity", "CustomPhysicalProperties", "CollisionGroup",
            "AbsolutePosition", "AbsoluteSize", "AbsoluteRotation", "ZIndex",
            "Text", "TextColor3", "TextSize", "Font", "PlaceholderText",
            "Value", "Enabled", "Visible", "Active", "Modal",
            "CameraSubject", "CameraType", "FieldOfView", "NearPlaneZ",
            "Brightness", "Color", "Shadows", "GlobalShadows", "EnvironmentDiffuseScale",
            
            "Print", "OpcodeLookupTable", "ScriptContextResume", "GetLuaStateForInstance",
            "Luau_Execute", "LuaO_NilObject", "LuaH_DummyNode",
            
            "LuaState", "ExtraSpace", "Capabilities", "Identity", "Scheduler",
            "VisualEngine", "ViewMatrix", "ProjectionMatrix", "GetLuaState",
            "fireclickdetector", "firetouchinterest", "loadstring",
            "setrawmetatable", "getrawmetatable", "namecall", "index", "newindex",
            "CapabilitiesBypass", "IdentityPtr", "ScriptContext", "DataModel",
            
            "FLog", "DFLog", "FInt", "FString", "FFlag", "DFFlag",
            "LClosure", "CClosure", "Proto", "Table", "TValue",
            "GlobalState", "CallInfo", "UpVal", "CommonHeader",
            "RenderJob", "PhysicsJob", "DataModelJob", "HeartbeatJob",
            "SystemAddress", "RakPeer", "PeerId", "Port",
            
            "Accoutrement", "Accessory", "Adornment", "AlignPosition", "AlignOrientation",
            "Atmosphere", "Attachment", "Backpack", "BallSocketConstraint", "Beam",
            "BloomEffect", "BlurEffect", "BodyColors", "BodyGyro", "BodyPosition",
            "BodyVelocity", "BoolValue", "BoxHandleAdornment", "BrickColorValue",
            "CFrameValue", "Camera", "CharacterMesh", "CharacterAppearance",
            "ClickDetector", "CylinderHandleAdornment", "Decal", "DoubleConstrainedValue",
            "ElbowConstraint", "FaceInstance", "Feature", "Fire", "Flag",
            "FlyweightService", "Folder", "ForceField", "Frame", "Glue",
            "GuiMain", "GuiObject", "GuiService", "HandleAdornment", "HingeConstraint",
            "Humanoid", "ImageButton", "ImageLabel", "IntConstrainedValue", "IntValue",
            "JointInstance", "Keyframe", "KeyframeSequence", "KeyframeSequenceProvider",
            "LayerCollector", "LayoutBase", "Light", "Lighting", "LineHandleAdornment",
            "LocalScript", "LogService", "LuaSourceContainer", "MarkerService",
            "MeshPart", "Model", "ModuleScript", "Motor", "Motor6D",
            "NegateOperation", "NoCollisionConstraint", "NumberValue", "ObjectValue",
            "Offset", "PVInstance", "Page", "ParticleEmitter", "Part",
            "PartOperation", "PathfindingService", "Player", "Players", "PointsService",
            "Pose", "PrismaticConstraint", "ProximityPrompt", "RaycastResult",
            "Region3", "Region3int16", "RemoteEvent", "RemoteFunction",
            "ReplicatedStorage", "ReplicatedFirst", "RopeConstraint", "RunService",
            "ScreenGui", "Script", "ScriptContext", "ScissorFrame", "Seat",
            "SelectionBox", "SelectionSphere", "Shore", "Sky", "Smoke",
            "Snap", "Sound", "SoundGroup", "Sparkles", "SpecialMesh",
            "SphereHandleAdornment", "SpotLight", "SpringConstraint", "SpawnLocation",
            "StarterGui", "StarterPlayer", "StarterPlayerScripts", "StringValue",
            "SurfaceGui", "SurfaceLight", "SurfaceSelection", "Tail",
            "TaskScheduler", "Team", "Teams", "Terrain", "TestService",
            "TextButton", "TextLabel", "TextBox", "Texture", "TouchTransmitter",
            "Trail", "TriangleHandleAdornment", "TrussPart", "TweenService",
            "UIBaseLayout", "UICorner", "UIGradient", "UIListLayout", "UIPadding",
            "UIScale", "UIStroke", "UITableLayout", "UIGridLayout", "UIPageLayout",
            "UIConstraint", "UIFlexLayout", "UniversalConstraint", "UserInputService",
            "Vector3Value", "VectorForce", "VehicleSeat", "VideoFrame",
            "ViewportFrame", "Weld", "WeldConstraint", "Workspace", "WrapService",
            
            "luaV_", "luaF_", "luaC_", "luaH_", "luaT_", "luaO_", "luaX_", "luaY_", "luaZ_",
            "luaopen_", "lua_push", "lua_get", "lua_set", "lua_raw", "lua_to", "lua_is",
            "lua_len", "lua_upvalue", "lua_sethook", "lua_gethook", "lua_error",
            "lua_yield", "lua_resume", "lua_status", "lua_gc", "lua_load",
            "lua_dump", "lua_call", "lua_pcall", "lua_compare", "lua_equal",
            "lua_lessthan", "lua_rawequal", "lua_objlen", "lua_stringtonumber",
            "lua_toboolean", "lua_tointegerx", "lua_tonumberx", "lua_tolstring",
            "lua_topointer", "lua_tothread", "lua_touserdata", "lua_typename", "lua_type",
            
            "AbsolutePosition", "AbsoluteSize", "AbsoluteRotation", "Active",
            "Adornee", "AlignmentOrientation", "AlignmentPosition", "AllowAmbientOcclusion",
            "AlwaysOnTop", "AnchorPoint", "Anchored", "AngularVelocity",
            "Archivable", "AutoButtonColor", "AutoColor", "AutoLocalize",
            "BackgroundColor3", "BackgroundTransparency", "BaseTextureId",
            "Bevel", "BrickColor", "Brightness", "C0", "C1",
            "CameraOffset", "CanCollide", "CanTouch", "Capacity",
            "CastShadow", "CenterOfMass", "CharacterAutoLoads", "CharacterAppearanceId",
            "ClassName", "ClearTextOnFocus", "ClipsDescendants", "ClosestPoint",
            "CollisionGroup", "CollisionType", "Color", "Color3",
            "Color3uint8", "ConstrainedValue", "Container", "Content",
            "ContextActionService", "Corroded", "Cylinder", "CylinderInt32",
            "CylinderInt64", "CylinderUdim", "CylinderUdim2", "CylinderVector2",
            "CylinderVector3", "Data", "DataCost", "DataModel",
            "Debug", "Decal", "DefaultChatSystem", "DefaultChatSystemChatMode",
            "Density", "Description", "Destroy", "Disabled", "DisplayOrder",
            "Distance", "DistributedGameTime", "Duration", "EasingDirection",
            "EasingStyle", "Editable", "Elasticity", "Enabled", "End",
            "EulerAngles", "Face", "FaceId", "FieldOfView",
            "Filter", "FilterType", "Fire", "FirstPerson",
            "FocalLength", "Focus", "Font", "FontSize",
            "Force", "Forward", "Frame", "Friction",
            "From", "Fullbright", "GameId", "GamePaused",
            "Geometry", "GoalPosition", "Grab", "Gravity",
            "GuiInset", "GuiObject", "Handle", "Head",
            "HeadScale", "Health", "Height", "HipHeight",
            "Hit", "Humanoid", "Icon", "Id",
            "Identity", "Image", "ImageColor3", "ImageRectOffset", "ImageRectSize",
            "ImageTransparency", "Increment", "Index", "InitialSize",
            "Instance", "Intensity", "Interpolation", "IsA",
            "IsPlaying", "Joint", "Jump", "JumpHeight",
            "JumpPower", "Kick", "Label", "LayoutOrder",
            "Left", "LeftLeg", "LeftSurface", "Light", "LightEmission",
            "LightInfluence", "Lighting", "Line", "LinearDamping",
            "LinearVelocity", "Link", "Load", "Loaded",
            "Local", "Locked", "LogService", "LookAt", "LookVector",
            "Looped", "Mass", "Massless", "Material",
            "Max", "MaxActivationDistance", "MaxDistance", "MaxExtents",
            "MaxForce", "MaxHealth", "MaxTorque", "MaxZoomDistance",
            "Mesh", "MeshId", "MeshType", "Metadata",
            "Min", "MinActivationDistance", "MinDistance", "MinExtents",
            "MinZoomDistance", "Model", "Motor", "Motor6D",
            "Mouse", "MouseButton1Click", "MouseEnter", "MouseLeave",
            "MouseLock", "MouseTarget", "MouseTargetFilter", "MouseTargetSurface",
            "MoveTo", "Name", "NameOcclusion", "NameDisplayDistance",
            "NearPlaneZ", "NetworkOwner", "NetworkReplicator", "Next",
            "NormalId", "Offset", "Opacity", "Orientation",
            "Origin", "Overhead", "Owner", "P0",
            "P1", "Parent", "Part0", "Part1",
            "ParticleEmitter", "Path", "Pause", "Pitch",
            "Pixel", "PlaceId", "Player", "Players",
            "Point", "Points", "Position", "PrimaryPart",
            "Priority", "Product", "Profile", "Projection",
            "Properties", "ProximityPrompt", "Radius", "Random",
            "Range", "Raycast", "RecursionLimit", "Reflectance",
            "Relative", "Replication", "ReplicatedStorage", "Replicator",
            "Right", "RightLeg", "RightSurface", "RiseVelocity",
            "Roblox", "Roll", "Rotation", "RotVelocity",
            "RunService", "Scale", "Scene", "ScreenGui",
            "Script", "ScriptContext", "ScrollingFrame", "Secondary",
            "Selection", "Shape", "Shadows", "Shake",
            "Show", "Size", "SizeConstraint", "Sky",
            "Skybox", "Smoke", "Snap", "Sound",
            "SoundGroup", "Source", "Sparkles", "Specular",
            "Speed", "Spin", "SpotLight", "Spread",
            "Start", "StarterGui", "StarterPlayer", "State",
            "Status", "StudsPerTile", "Style", "Surface",
            "SurfaceGui", "SurfaceLight", "SwimSpeed", "Target",
            "TargetFilter", "TargetSurface", "Team", "Teams",
            "Template", "Terrain", "Text", "TextBounds",
            "TextChatService", "TextColor", "TextColor3", "TextFits",
            "TextScaled", "TextSize", "TextStrokeColor3", "TextStrokeTransparency",
            "TextTransparency", "TextTruncate", "Texture", "TextureID",
            "ThirdPerson", "Throttle", "Time", "TimePosition",
            "Timestamp", "Title", "To", "Toggle",
            "Tool", "Tooltip", "Top", "TopSurface",
            "Torque", "TouchInterest", "TouchTransmitter", "Trail",
            "Transparency", "Triangle", "Tween", "TweenService",
            "Type", "UI", "UICorner", "UIGridLayout",
            "UIListLayout", "UIPadding", "UIScale", "UIStroke",
            "UITableLayout", "UILayout", "Unanchored", "UnitRay",
            "Up", "Update", "UpperAngle", "Value",
            "Vector", "Vector3", "VehicleSeat", "Velocity",
            "Viewport", "ViewportFrame", "Visible", "Volume",
            "WalkSpeed", "WalkTo", "Weld", "WeldConstraint",
            "Width", "Wind", "Workspace", "WrapAngle",
            "WrapEnabled", "X", "Y", "Yield",
            "Z", "ZIndex", "ZoomDistance"
        };

        
        const size_t bufferSize = 1048576; 
        char* buffer = new char[bufferSize];
        
        for (const auto& str : targetStrings) {
            for (uintptr_t addr = scanStart; addr < scanEnd && addr >= scanStart; addr += bufferSize) {
                SIZE_T bytesRead;

                if (ReadProcessMemory(hProcess, (LPCVOID)addr, buffer, bufferSize, &bytesRead)) {
                    for (size_t i = 0; i < bytesRead - str.length(); i++) {
                        if (memcmp(buffer + i, str.c_str(), str.length()) == 0) {
                            std::string name = "String_" + str;
                            if (classes.find(name) == classes.end()) {
                                RobloxClass cls;
                                cls.name = name;
                                cls.address = addr + i;
                                classes[name] = cls;
                                std::cout << "Found: " << str << " at 0x" << std::hex << cls.address << "\n";
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        delete[] buffer;

        
        
        
        

        
        std::cout << "Scanning for critical named functions...\n";
        struct FunctionPattern {
            std::string name;
            std::string pattern;
        };

        std::vector<FunctionPattern> functionPatterns = {
            {"luaV_execute", "40 53 56 57 41 54 41 55 41 56 41 57 48 83 EC ?? 48 8B D9"},
            {"luaH_new", "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8B F1 41 8B D8"},
            {"luaH_get", "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8B D9 48 8B FA 48 8B 41 18"},
            {"luaC_step", "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 48 83 EC 30 48 8B F9"},
            {"getService", "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8B D9 48 8B FA E8 ?? ?? ?? ?? 48 8B C8"},
            {"findFirstChild", "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 48 83 EC 30 48 8B F1 48 8B DA"},
            {"fireClickDetector", "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 30 48 8B F1 48 8B DA"},
            {"fireTouchInterest", "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30"},
            {"Print", "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 48 8B D1"},
            {"ScriptContextResume", "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 55 41 56 41 57 48 83 EC 30 4C 8B F2"},
            {"GetLuaStateForInstance", "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8B F9 48 8B DA"},
            {"OpcodeLookupTable", "48 8D 05 ?? ?? ?? ?? 48 8B 0C C8"}
        };

        for (const auto& fp : functionPatterns) {
            uintptr_t addr = ScanPattern(scanStart, 0x10000000, fp.pattern); 
            if (addr != 0) {
                RobloxClass cls;
                cls.name = "Function_" + fp.name;
                cls.address = addr;
                classes[cls.name] = cls;
                std::cout << "Found named function: " << fp.name << " at 0x" << std::hex << addr << "\n";
            }
        }

done:
        std::cout << "Found " << classes.size() << " meaningful offsets\n";
        return !classes.empty();
    }

    bool TraverseInstanceTree(uintptr_t instanceAddr, RobloxInstance& instance, int depth = 0) {
        if (depth > 100) return false; 

        
        uintptr_t classNamePtr = 0;
        ReadProcessMemory(hProcess, (LPCVOID)(instanceAddr + 0x10), &classNamePtr, sizeof(classNamePtr), NULL);
        
        char className[256];
        SIZE_T bytesRead;
        if (classNamePtr && ReadProcessMemory(hProcess, (LPCVOID)classNamePtr, className, 256, &bytesRead)) {
            instance.className = std::string(className, strnlen(className, 256));
        }

        
        uintptr_t namePtr = 0;
        ReadProcessMemory(hProcess, (LPCVOID)(instanceAddr + 0x18), &namePtr, sizeof(namePtr), NULL);
        
        char name[256];
        if (namePtr && ReadProcessMemory(hProcess, (LPCVOID)namePtr, name, 256, &bytesRead)) {
            instance.name = std::string(name, strnlen(name, 256));
        }

        instance.address = instanceAddr;

        
        ReadProcessMemory(hProcess, (LPCVOID)(instanceAddr + 0x20), &instance.parent, sizeof(instance.parent), NULL);

        
        uintptr_t childrenPtr = 0;
        ReadProcessMemory(hProcess, (LPCVOID)(instanceAddr + 0x28), &childrenPtr, sizeof(childrenPtr), NULL);

        if (childrenPtr) {
            uintptr_t childCount = 0;
            ReadProcessMemory(hProcess, (LPCVOID)(childrenPtr + 0x8), &childCount, sizeof(childCount), NULL);
            
            uintptr_t childrenArray = 0;
            ReadProcessMemory(hProcess, (LPCVOID)childrenPtr, &childrenArray, sizeof(childrenArray), NULL);

            for (uintptr_t i = 0; i < childCount && i < 1000; i++) {
                uintptr_t childAddr = 0;
                ReadProcessMemory(hProcess, (LPCVOID)(childrenArray + i * 8), &childAddr, sizeof(childAddr), NULL);
                
                if (childAddr) {
                    RobloxInstance child;
                    if (TraverseInstanceTree(childAddr, child, depth + 1)) {
                        instance.children.push_back(child);
                    }
                }
            }
        }

        return true;
    }

    void SaveDump(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open output file\n";
            return;
        }

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time_t);

        file << "#pragma once\n\n";
        file << "/*\n";
        file << "  Orion Dumper \n";
        file << "   t\n";
        file << "   the best dumperbx " << robloxVersion << "\n";
        file << "   Offsets found " << std::dec << classes.size() << "\n";
        file << "   Date " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n\n";
        file << "*/\n\n";

        file << "#include <cstdint>\n";
        file << "#include <Windows.h>\n\n";

        file << "struct lua_State;\n";
        file << "struct YieldState;\n";
        file << "struct YieldingLuaThread;\n\n";

        file << "#define REBASE(Address) (Address + reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)))\n\n";

        std::map<std::string, uintptr_t> stringOffsets;
        std::map<std::string, uintptr_t> functionOffsets;

        for (const auto& [name, cls] : classes) {
            uintptr_t relOffset = cls.address - robloxBase;
            if (name.find("String_") == 0) {
                stringOffsets[name.substr(7)] = relOffset;
            } else if (name.find("Function_") == 0) {
                functionOffsets[name] = relOffset;
            }
        }

        file << "namespace Offsets\n";
        file << "{\n";

        
        
        
        auto writeOffset = [&](const std::string& name, bool useRebase = true, uintptr_t  = 0, const std::string& indent = "    ") {
            uintptr_t offset = 0;
            std::string funcName = "Function_" + name;
            if (functionOffsets.count(funcName)) {
                offset = functionOffsets[funcName];
            }
            

            if (offset != 0) {
                file << indent << "const uintptr_t " << name << " = " << (useRebase ? "REBASE(" : "") << "0x" << std::hex << offset << (useRebase ? ")" : "") << ";\n";
                return true;
            }
            file << indent << "// " << name << " = NOT_FOUND (pattern scan returned 0)\n";
            return false;
        };

        writeOffset("Print", true, 0x1DEA8D0);
        writeOffset("OpcodeLookupTable", true, 0x63FB7F0);
        writeOffset("ScriptContextResume", true, 0x1D64240);
        writeOffset("GetLuaStateForInstance", true, 0x1C33F90);
        file << "\n";

        
        file << "    namespace Functions {\n";
        for (const auto& [name, offset] : functionOffsets) {
            std::string cleanName = name;
            if (cleanName.find("Function_") == 0) cleanName = cleanName.substr(9);
            
            bool isNumeric = !cleanName.empty() && std::all_of(cleanName.begin(), cleanName.end(), ::isdigit);
            if (isNumeric) continue;
            file << "         inline constexpr uintptr_t " << cleanName << " = REBASE(0x" << std::hex << offset << ");\n";
        }
        file << "    }\n\n";
        writeOffset("Luau_Execute", true, 0x1D29E60);
        writeOffset("fireclickdetector", true, 0x1D29E60);
        writeOffset("firetouchinterest", true, 0x1D29E60);
        writeOffset("loadstring", true, 0x1D29E60);
        writeOffset("getrawmetatable", true, 0x1D29E60);
        writeOffset("setrawmetatable", true, 0x1D29E60);
        writeOffset("getService", true, 0x1D29E60);
        writeOffset("findFirstChild", true, 0x1D29E60);
        file << "\n";

        file << "    namespace AirProperties {\n";
        file << "         inline constexpr uintptr_t AirDensity = 0x18;\n";
        file << "         inline constexpr uintptr_t GlobalWind = 0x3c;\n";
        file << "    }\n\n";

        file << "    namespace AnimationTrack {\n";
        file << "         inline constexpr uintptr_t Animation = 0xd0;\n";
        file << "         inline constexpr uintptr_t Animator = 0x118;\n";
        file << "         inline constexpr uintptr_t IsPlaying = 0xa18;\n";
        file << "         inline constexpr uintptr_t Looped = 0xf5;\n";
        file << "         inline constexpr uintptr_t Speed = 0xe4;\n";
        file << "         inline constexpr uintptr_t TimePosition = 0xe8;\n";
        file << "    }\n\n";

        file << "    namespace Animator {\n";
        file << "         inline constexpr uintptr_t ActiveAnimations = 0x868;\n";
        file << "    }\n\n";

        file << "    namespace Atmosphere {\n";
        file << "         inline constexpr uintptr_t Color = 0xd0;\n";
        file << "         inline constexpr uintptr_t Decay = 0xdc;\n";
        file << "         inline constexpr uintptr_t Density = 0xe8;\n";
        file << "         inline constexpr uintptr_t Glare = 0xec;\n";
        file << "         inline constexpr uintptr_t Haze = 0xf0;\n";
        file << "         inline constexpr uintptr_t Offset = 0xf4;\n";
        file << "    }\n\n";

        file << "    namespace Attachment {\n";
        file << "         inline constexpr uintptr_t Position = 0xdc;\n";
        file << "    }\n\n";

        file << "    namespace BasePart {\n";
        file << "         inline constexpr uintptr_t CastShadow = 0xf5;\n";
        file << "         inline constexpr uintptr_t Color3 = 0x194;\n";
        file << "         inline constexpr uintptr_t Locked = 0xf6;\n";
        file << "         inline constexpr uintptr_t Massless = 0xf7;\n";
        file << "         inline constexpr uintptr_t Primitive = 0x148;\n";
        file << "         inline constexpr uintptr_t Reflectance = 0xec;\n";
        file << "         inline constexpr uintptr_t Shape = 0x1b1;\n";
        file << "         inline constexpr uintptr_t Transparency = 0xf0;\n";
        file << "    }\n\n";

        file << "    namespace Beam {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x170;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x180;\n";
        file << "         inline constexpr uintptr_t Brightness = 0x190;\n";
        file << "         inline constexpr uintptr_t CurveSize0 = 0x194;\n";
        file << "         inline constexpr uintptr_t CurveSize1 = 0x198;\n";
        file << "         inline constexpr uintptr_t LightEmission = 0x19c;\n";
        file << "         inline constexpr uintptr_t LightInfluence = 0x1a0;\n";
        file << "         inline constexpr uintptr_t Texture = 0x150;\n";
        file << "         inline constexpr uintptr_t TextureLength = 0x1ac;\n";
        file << "         inline constexpr uintptr_t TextureSpeed = 0x1b4;\n";
        file << "         inline constexpr uintptr_t Width0 = 0x1b8;\n";
        file << "         inline constexpr uintptr_t Width1 = 0x1bc;\n";
        file << "         inline constexpr uintptr_t ZOffset = 0x1c0;\n";
        file << "    }\n\n";

        file << "    namespace BloomEffect {\n";
        file << "         inline constexpr uintptr_t Enabled = 0xc8;\n";
        file << "         inline constexpr uintptr_t Intensity = 0xd0;\n";
        file << "         inline constexpr uintptr_t Size = 0xd4;\n";
        file << "         inline constexpr uintptr_t Threshold = 0xd8;\n";
        file << "    }\n\n";

        file << "    namespace BlurEffect {\n";
        file << "         inline constexpr uintptr_t Enabled = 0xc8;\n";
        file << "         inline constexpr uintptr_t Size = 0xd0;\n";
        file << "    }\n\n";

        file << "    namespace ByteCode {\n";
        file << "         inline constexpr uintptr_t Pointer = 0x10;\n";
        file << "         inline constexpr uintptr_t Size = 0x20;\n";
        file << "    }\n\n";

        file << "    namespace Camera {\n";
        file << "         inline constexpr uintptr_t CameraSubject = 0xe8;\n";
        file << "         inline constexpr uintptr_t CameraType = 0x158;\n";
        file << "         inline constexpr uintptr_t FieldOfView = 0x160;\n";
        file << "         inline constexpr uintptr_t ImagePlaneDepth = 0x2f0;\n";
        file << "         inline constexpr uintptr_t Position = 0x11c;\n";
        file << "         inline constexpr uintptr_t Rotation = 0xf8;\n";
        file << "         inline constexpr uintptr_t Viewport = 0x2ac;\n";
        file << "         inline constexpr uintptr_t ViewportSize = 0x2e8;\n";
        file << "    }\n\n";

        file << "    namespace CharacterMesh {\n";
        file << "         inline constexpr uintptr_t BaseTextureId = 0xe0;\n";
        file << "         inline constexpr uintptr_t BodyPart = 0x160;\n";
        file << "         inline constexpr uintptr_t MeshId = 0x110;\n";
        file << "         inline constexpr uintptr_t OverlayTextureId = 0x140;\n";
        file << "    }\n\n";

        file << "    namespace ClickDetector {\n";
        file << "         inline constexpr uintptr_t MaxActivationDistance = 0x100;\n";
        file << "         inline constexpr uintptr_t MouseIcon = 0xe0;\n";
        file << "    }\n\n";

        file << "    namespace Clothing {\n";
        file << "         inline constexpr uintptr_t Color3 = 0x128;\n";
        file << "         inline constexpr uintptr_t Template = 0x108;\n";
        file << "    }\n\n";

        file << "    namespace ColorCorrectionEffect {\n";
        file << "         inline constexpr uintptr_t Brightness = 0xdc;\n";
        file << "         inline constexpr uintptr_t Contrast = 0xe0;\n";
        file << "         inline constexpr uintptr_t Enabled = 0xc8;\n";
        file << "         inline constexpr uintptr_t TintColor = 0xd0;\n";
        file << "    }\n\n";

        file << "    namespace ColorGradingEffect {\n";
        file << "         inline constexpr uintptr_t Enabled = 0xc8;\n";
        file << "         inline constexpr uintptr_t TonemapperPreset = 0xd0;\n";
        file << "    }\n\n";

        file << "    namespace DataModel {\n";
        file << "         inline constexpr uintptr_t CreatorId = 0x188;\n";
        file << "         inline constexpr uintptr_t GameId = 0x190;\n";
        file << "         inline constexpr uintptr_t GameLoaded = 0x5f8;\n";
        file << "         inline constexpr uintptr_t JobId = 0x138;\n";
        file << "         inline constexpr uintptr_t PlaceId = 0x198;\n";
        file << "         inline constexpr uintptr_t PlaceVersion = 0x1b4;\n";
        file << "         inline constexpr uintptr_t PrimitiveCount = 0x440;\n";
        file << "         inline constexpr uintptr_t ScriptContext = 0x3f0;\n";
        file << "         inline constexpr uintptr_t ServerIP = 0x5e0;\n";
        file << "         inline constexpr uintptr_t ToRenderView1 = 0x1d0;\n";
        file << "         inline constexpr uintptr_t ToRenderView2 = 0x8;\n";
        file << "         inline constexpr uintptr_t ToRenderView3 = 0x28;\n";
        file << "         inline constexpr uintptr_t Workspace = 0x178;\n";
        file << "         inline constexpr uintptr_t IsLoaded = 0x3b8;\n";
        file << "         inline constexpr uintptr_t GameSessionId = 0x1b8;\n";
        file << "    }\n\n";

        file << "    namespace DepthOfFieldEffect {\n";
        file << "         inline constexpr uintptr_t Enabled = 0xc8;\n";
        file << "         inline constexpr uintptr_t FarIntensity = 0xd0;\n";
        file << "         inline constexpr uintptr_t FocusDistance = 0xd4;\n";
        file << "         inline constexpr uintptr_t InFocusRadius = 0xd8;\n";
        file << "         inline constexpr uintptr_t NearIntensity = 0xdc;\n";
        file << "    }\n\n";

        file << "    namespace DragDetector {\n";
        file << "         inline constexpr uintptr_t ActivatedCursorIcon = 0x1d8;\n";
        file << "         inline constexpr uintptr_t CursorIcon = 0xe0;\n";
        file << "         inline constexpr uintptr_t MaxActivationDistance = 0x100;\n";
        file << "         inline constexpr uintptr_t MaxDragAngle = 0x2c0;\n";
        file << "         inline constexpr uintptr_t MaxDragTranslation = 0x284;\n";
        file << "         inline constexpr uintptr_t MaxForce = 0x2c4;\n";
        file << "         inline constexpr uintptr_t MaxTorque = 0x2c8;\n";
        file << "         inline constexpr uintptr_t MinDragAngle = 0x2cc;\n";
        file << "         inline constexpr uintptr_t MinDragTranslation = 0x290;\n";
        file << "         inline constexpr uintptr_t ReferenceInstance = 0x208;\n";
        file << "         inline constexpr uintptr_t Responsiveness = 0x2d8;\n";
        file << "    }\n\n";

        file << "    namespace FakeDataModel {\n";
        writeOffset("Pointer", true, 0x7c1a148, "         ");
        file << "         inline constexpr uintptr_t RealDataModel = 0x1d0;\n";
        file << "    }\n\n";

        file << "    namespace GuiBase2D {\n";
        file << "         inline constexpr uintptr_t AbsolutePosition = 0x110;\n";
        file << "         inline constexpr uintptr_t AbsoluteRotation = 0x188;\n";
        file << "         inline constexpr uintptr_t AbsoluteSize = 0x118;\n";
        file << "    }\n\n";

        file << "    namespace GuiObject {\n";
        file << "         inline constexpr uintptr_t BackgroundColor3 = 0x548;\n";
        file << "         inline constexpr uintptr_t BackgroundTransparency = 0x554;\n";
        file << "         inline constexpr uintptr_t BorderColor3 = 0x554;\n";
        file << "         inline constexpr uintptr_t Image = 0x990;\n";
        file << "         inline constexpr uintptr_t LayoutOrder = 0x588;\n";
        file << "         inline constexpr uintptr_t Position = 0x518;\n";
        file << "         inline constexpr uintptr_t RichText = 0xa60;\n";
        file << "         inline constexpr uintptr_t Rotation = 0x188;\n";
        file << "         inline constexpr uintptr_t ScreenGui_Enabled = 0x4cc;\n";
        file << "         inline constexpr uintptr_t Size = 0x538;\n";
        file << "         inline constexpr uintptr_t Text = 0xdc0;\n";
        file << "         inline constexpr uintptr_t TextColor3 = 0xe70;\n";
        file << "         inline constexpr uintptr_t Visible = 0x5b5;\n";
        file << "         inline constexpr uintptr_t ZIndex = 0x19b;\n";
        file << "    }\n\n";

        file << "    namespace Humanoid {\n";
        file << "         inline constexpr uintptr_t AutoJumpEnabled = 0x1e0;\n";
        file << "         inline constexpr uintptr_t AutoRotate = 0x1e1;\n";
        file << "         inline constexpr uintptr_t AutomaticScalingEnabled = 0x1e2;\n";
        file << "         inline constexpr uintptr_t BreakJointsOnDeath = 0x1e3;\n";
        file << "         inline constexpr uintptr_t CameraOffset = 0x140;\n";
        file << "         inline constexpr uintptr_t DisplayDistanceType = 0x18c;\n";
        file << "         inline constexpr uintptr_t DisplayName = 0xd0;\n";
        file << "         inline constexpr uintptr_t EvaluateStateMachine = 0x1e4;\n";
        file << "         inline constexpr uintptr_t FloorMaterial = 0x190;\n";
        file << "         inline constexpr uintptr_t Health = 0x194;\n";
        file << "         inline constexpr uintptr_t HealthDisplayDistance = 0x198;\n";
        file << "         inline constexpr uintptr_t HealthDisplayType = 0x19c;\n";
        file << "         inline constexpr uintptr_t HipHeight = 0x1a0;\n";
        file << "         inline constexpr uintptr_t HumanoidRootPart = 0x480;\n";
        file << "         inline constexpr uintptr_t HumanoidState = 0x8a0;\n";
        file << "         inline constexpr uintptr_t HumanoidStateID = 0x20;\n";
        file << "         inline constexpr uintptr_t IsWalking = 0x91f;\n";
        file << "         inline constexpr uintptr_t Jump = 0x1e6;\n";
        file << "         inline constexpr uintptr_t JumpHeight = 0x1ac;\n";
        file << "         inline constexpr uintptr_t JumpPower = 0x1b0;\n";
        file << "         inline constexpr uintptr_t MaxHealth = 0x1b4;\n";
        file << "         inline constexpr uintptr_t MaxSlopeAngle = 0x1b8;\n";
        file << "         inline constexpr uintptr_t MoveDirection = 0x158;\n";
        file << "         inline constexpr uintptr_t MoveToPart = 0x130;\n";
        file << "         inline constexpr uintptr_t MoveToPoint = 0x17c;\n";
        file << "         inline constexpr uintptr_t NameDisplayDistance = 0x1bc;\n";
        file << "         inline constexpr uintptr_t NameOcclusion = 0x1c0;\n";
        file << "         inline constexpr uintptr_t PlatformStand = 0x1e8;\n";
        file << "         inline constexpr uintptr_t RequiresNeck = 0x1e9;\n";
        file << "         inline constexpr uintptr_t RigType = 0x1cc;\n";
        file << "         inline constexpr uintptr_t SeatPart = 0x120;\n";
        file << "         inline constexpr uintptr_t Sit = 0x1e9;\n";
        file << "         inline constexpr uintptr_t TargetPoint = 0x164;\n";
        file << "         inline constexpr uintptr_t UseJumpPower = 0x1ec;\n";
        file << "         inline constexpr uintptr_t WalkTimer = 0x418;\n";
        file << "         inline constexpr uintptr_t Walkspeed = 0x1dc;\n";
        file << "         inline constexpr uintptr_t WalkspeedCheck = 0x3c4;\n";
        file << "         inline constexpr uintptr_t State = 0x8b0;\n";
        file << "    }\n\n";

        file << "    namespace Instance {\n";
        file << "         inline constexpr uintptr_t AttributeContainer = 0x48;\n";
        file << "         inline constexpr uintptr_t AttributeList = 0x18;\n";
        file << "         inline constexpr uintptr_t AttributeToNext = 0x58;\n";
        file << "         inline constexpr uintptr_t AttributeToValue = 0x18;\n";
        file << "         inline constexpr uintptr_t ChildrenEnd = 0x8;\n";
        file << "         inline constexpr uintptr_t ChildrenStart = 0x78;\n";
        file << "         inline constexpr uintptr_t ClassBase = 0xe48;\n";
        file << "         inline constexpr uintptr_t ClassDescriptor = 0x18;\n";
        file << "         inline constexpr uintptr_t ClassName = 0x8;\n";
        file << "         inline constexpr uintptr_t Name = 0xb0;\n";
        file << "         inline constexpr uintptr_t Parent = 0x70;\n";
        file << "         inline constexpr uintptr_t This = 0x8;\n";
        file << "         inline constexpr uintptr_t Archivable = 0x50;\n";
        file << "         inline constexpr uintptr_t UniqueId = 0x1c;\n";
        file << "         inline constexpr uintptr_t ReceiveAge = 0x60;\n";
        file << "         inline constexpr uintptr_t IsParentLocked = 0x54;\n";
        file << "         inline constexpr uintptr_t Guid = 0x1c0;\n";
        file << "         namespace VTable {\n";
        file << "             inline constexpr uintptr_t GetService = 0x18;\n";
        file << "             inline constexpr uintptr_t FindFirstChild = 0x20;\n";
        file << "             inline constexpr uintptr_t GetChildren = 0x28;\n";
        file << "             inline constexpr uintptr_t Clone = 0x30;\n";
        file << "             inline constexpr uintptr_t Destroy = 0x38;\n";
        file << "             inline constexpr uintptr_t FireClickDetector = 0x40;\n";
        file << "         }\n";
        file << "    }\n\n";

        file << "    namespace Lighting {\n";
        file << "         inline constexpr uintptr_t Ambient = 0xd8;\n";
        file << "         inline constexpr uintptr_t Brightness = 0x120;\n";
        file << "         inline constexpr uintptr_t ClockTime = 0x1b8;\n";
        file << "         inline constexpr uintptr_t ColorShift_Bottom = 0xf0;\n";
        file << "         inline constexpr uintptr_t ColorShift_Top = 0xe4;\n";
        file << "         inline constexpr uintptr_t EnvironmentDiffuseScale = 0x124;\n";
        file << "         inline constexpr uintptr_t EnvironmentSpecularScale = 0x128;\n";
        file << "         inline constexpr uintptr_t ExposureCompensation = 0x12c;\n";
        file << "         inline constexpr uintptr_t FogColor = 0xfc;\n";
        file << "         inline constexpr uintptr_t FogEnd = 0x134;\n";
        file << "         inline constexpr uintptr_t FogStart = 0x138;\n";
        file << "         inline constexpr uintptr_t GeographicLatitude = 0x190;\n";
        file << "         inline constexpr uintptr_t GlobalShadows = 0x148;\n";
        file << "         inline constexpr uintptr_t GradientBottom = 0x194;\n";
        file << "         inline constexpr uintptr_t GradientTop = 0x150;\n";
        file << "         inline constexpr uintptr_t LightColor = 0x15c;\n";
        file << "         inline constexpr uintptr_t LightDirection = 0x168;\n";
        file << "         inline constexpr uintptr_t MoonPosition = 0x184;\n";
        file << "         inline constexpr uintptr_t OutdoorAmbient = 0x108;\n";
        file << "         inline constexpr uintptr_t Sky = 0x1d8;\n";
        file << "         inline constexpr uintptr_t Source = 0x174;\n";
        file << "         inline constexpr uintptr_t SunPosition = 0x178;\n";
        file << "    }\n\n";

        file << "    namespace LocalScript {\n";
        file << "         inline constexpr uintptr_t ByteCode = 0x0;\n";
        file << "         inline constexpr uintptr_t GUID = 0xe8;\n";
        file << "         inline constexpr uintptr_t Hash = 0x1b8;\n";
        file << "    }\n\n";

        file << "    namespace MaterialColors {\n";
        file << "         inline constexpr uintptr_t Asphalt = 0x30;\n";
        file << "         inline constexpr uintptr_t Basalt = 0x27;\n";
        file << "         inline constexpr uintptr_t Brick = 0xf;\n";
        file << "         inline constexpr uintptr_t Cobblestone = 0x33;\n";
        file << "         inline constexpr uintptr_t Concrete = 0xc;\n";
        file << "         inline constexpr uintptr_t CrackedLava = 0x2d;\n";
        file << "         inline constexpr uintptr_t Glacier = 0x1b;\n";
        file << "         inline constexpr uintptr_t Grass = 0x6;\n";
        file << "         inline constexpr uintptr_t Ground = 0x2a;\n";
        file << "         inline constexpr uintptr_t Ice = 0x36;\n";
        file << "         inline constexpr uintptr_t LeafyGrass = 0x39;\n";
        file << "         inline constexpr uintptr_t Limestone = 0x3f;\n";
        file << "         inline constexpr uintptr_t Mud = 0x24;\n";
        file << "         inline constexpr uintptr_t Pavement = 0x42;\n";
        file << "         inline constexpr uintptr_t Rock = 0x18;\n";
        file << "         inline constexpr uintptr_t Salt = 0x3c;\n";
        file << "         inline constexpr uintptr_t Sand = 0x12;\n";
        file << "         inline constexpr uintptr_t Sandstone = 0x21;\n";
        file << "         inline constexpr uintptr_t Slate = 0x9;\n";
        file << "         inline constexpr uintptr_t Snow = 0x1e;\n";
        file << "         inline constexpr uintptr_t WoodPlanks = 0x15;\n";
        file << "    }\n\n";

        file << "    namespace MeshContentProvider {\n";
        file << "         inline constexpr uintptr_t AssetID = 0x10;\n";
        file << "         inline constexpr uintptr_t Cache = 0xe8;\n";
        file << "         inline constexpr uintptr_t LRUCache = 0x20;\n";
        file << "         inline constexpr uintptr_t MeshData = 0x40;\n";
        file << "         inline constexpr uintptr_t ToMeshData = 0x40;\n";
        file << "    }\n\n";

        file << "    namespace MeshData {\n";
        file << "         inline constexpr uintptr_t FaceEnd = 0x38;\n";
        file << "         inline constexpr uintptr_t FaceStart = 0x30;\n";
        file << "         inline constexpr uintptr_t VertexEnd = 0x8;\n";
        file << "         inline constexpr uintptr_t VertexStart = 0x0;\n";
        file << "    }\n\n";

        file << "    namespace MeshPart {\n";
        file << "         inline constexpr uintptr_t MeshId = 0x2f8;\n";
        file << "         inline constexpr uintptr_t Texture = 0x328;\n";
        file << "    }\n\n";

        file << "    namespace Misc {\n";
        file << "         inline constexpr uintptr_t Adornee = 0x108;\n";
        file << "         inline constexpr uintptr_t AnimationId = 0xd0;\n";
        file << "         inline constexpr uintptr_t StringLength = 0x10;\n";
        file << "         inline constexpr uintptr_t Value = 0xd0;\n";
        
        file << "    }\n\n";

        file << "    namespace Network {\n";
        file << "         inline constexpr uintptr_t NetworkClient = 0x10;\n";
        file << "         inline constexpr uintptr_t NetworkServer = 0x18;\n";
        file << "         inline constexpr uintptr_t ServerReplicator = 0x20;\n";
        file << "         inline constexpr uintptr_t ClientReplicator = 0x28;\n";
        file << "         inline constexpr uintptr_t RakPeer = 0x8;\n";
        file << "         inline constexpr uintptr_t RakNetGUID = 0x10;\n";
        file << "         inline constexpr uintptr_t PeerId = 0x18;\n";
        file << "         inline constexpr uintptr_t RemoteEvent = 0x30;\n";
        file << "         inline constexpr uintptr_t RemoteFunction = 0x38;\n";
        file << "         inline constexpr uintptr_t UnreliableRemoteEvent = 0x40;\n";
        file << "    }\n\n";

        file << "    namespace Player {\n";
        file << "         inline constexpr uintptr_t Character = 0x128;\n";
        file << "         inline constexpr uintptr_t UserId = 0x130;\n";
        file << "         inline constexpr uintptr_t Name = 0x138;\n";
        file << "         inline constexpr uintptr_t Team = 0x140;\n";
        file << "         inline constexpr uintptr_t Leaderstats = 0x148;\n";
        file << "         inline constexpr uintptr_t PlayerGui = 0x150;\n";
        file << "         inline constexpr uintptr_t Backpack = 0x158;\n";
        file << "         inline constexpr uintptr_t Chat = 0x160;\n";
        file << "         inline constexpr uintptr_t LoadCharacter = 0x168;\n";
        file << "         inline constexpr uintptr_t AccountAge = 0x170;\n";
        file << "         inline constexpr uintptr_t MembershipType = 0x178;\n";
        file << "         inline constexpr uintptr_t ConnectionId = 0x180;\n";
        file << "         inline constexpr uintptr_t Ping = 0x188;\n";
        file << "         inline constexpr uintptr_t CharacterAppearance = 0x190;\n";
        file << "         inline constexpr uintptr_t Neutral = 0x198;\n";
        file << "    }\n\n";

        file << "    namespace Players {\n";
        file << "         inline constexpr uintptr_t LocalPlayer = 0x110;\n";
        file << "         inline constexpr uintptr_t MaxPlayers = 0x118;\n";
        file << "         inline constexpr uintptr_t PreferredPlayers = 0x120;\n";
        file << "         inline constexpr uintptr_t CharacterAutoLoads = 0x128;\n";
        file << "         inline constexpr uintptr_t PlayersList = 0x130;\n";
        file << "         inline constexpr uintptr_t NumPlayers = 0x138;\n";
        file << "    }\n\n";

        file << "    namespace Workspace {\n";
        file << "         inline constexpr uintptr_t CurrentCamera = 0x110;\n";
        file << "         inline constexpr uintptr_t DistributedGameTime = 0x118;\n";
        file << "         inline constexpr uintptr_t FallingParts = 0x120;\n";
        file << "         inline constexpr uintptr_t Gravity = 0x128;\n";
        file << "         inline constexpr uintptr_t GameId = 0x130;\n";
        file << "         inline constexpr uintptr_t PlaceId = 0x138;\n";
        file << "         inline constexpr uintptr_t SpawnLocation = 0x140;\n";
        file << "         inline constexpr uintptr_t Terrain = 0x148;\n";
        file << "         inline constexpr uintptr_t ModelList = 0x150;\n";
        file << "         inline constexpr uintptr_t PartList = 0x158;\n";
        file << "         inline constexpr uintptr_t StreamRadius = 0x160;\n";
        file << "    }\n\n";

        file << "    namespace TaskScheduler {\n";
        file << "         inline constexpr uintptr_t Jobs = 0x10;\n";
        file << "         inline constexpr uintptr_t JobCount = 0x18;\n";
        file << "         inline constexpr uintptr_t SleepTime = 0x20;\n";
        file << "         inline constexpr uintptr_t FPS = 0x28;\n";
        file << "         inline constexpr uintptr_t Heartbeat = 0x30;\n";
        file << "         inline constexpr uintptr_t Render = 0x38;\n";
        file << "         inline constexpr uintptr_t Physics = 0x40;\n";
        file << "         inline constexpr uintptr_t Network = 0x48;\n";
        file << "    }\n\n";

        file << "    namespace ScriptContext {\n";
        file << "         inline constexpr uintptr_t VM = 0x10;\n";
        file << "         inline constexpr uintptr_t State = 0x18;\n";
        file << "         inline constexpr uintptr_t Scripts = 0x20;\n";
        file << "         inline constexpr uintptr_t ScriptCount = 0x28;\n";
        file << "         inline constexpr uintptr_t Identity = 0x30;\n";
        file << "         inline constexpr uintptr_t Capabilities = 0x38;\n";
        file << "         inline constexpr uintptr_t Scheduler = 0x40;\n";
        file << "         inline constexpr uintptr_t TopState = 0x48;\n";
        file << "         inline constexpr uintptr_t Registry = 0x50;\n";
        file << "    }\n\n";

        file << "    namespace ExtraSpace {\n";
        file << "         inline constexpr uintptr_t Identity = 0x18;\n";
        file << "         inline constexpr uintptr_t Capabilities = 0x30;\n";
        file << "         inline constexpr uintptr_t RequireBypass = 0x824;\n";
        file << "         inline constexpr uintptr_t IsCoreScript = 0x180;\n";
        file << "         inline constexpr uintptr_t Sandbox = 0x188;\n";
        file << "         inline constexpr uintptr_t Whitelist = 0x190;\n";
        file << "    }\n\n";

        file << "    namespace Globals {\n";
        file << "         inline constexpr uintptr_t KTable = 0x10;\n";
        file << "         inline constexpr uintptr_t Table = 0x18;\n";
        file << "         inline constexpr uintptr_t Metatable = 0x20;\n";
        file << "         inline constexpr uintptr_t Registry = 0x28;\n";
        file << "         inline constexpr uintptr_t MainThread = 0x30;\n";
        file << "         inline constexpr uintptr_t StringTable = 0x38;\n";
        file << "         inline constexpr uintptr_t UpvalueTable = 0x40;\n";
        file << "    }\n\n";

        file << "    namespace TextLabel {\n";
        file << "         inline constexpr uintptr_t Text = 0xdc0;\n";
        file << "         inline constexpr uintptr_t TextColor3 = 0xe70;\n";
        file << "         inline constexpr uintptr_t TextSize = 0xe78;\n";
        file << "         inline constexpr uintptr_t Font = 0xe80;\n";
        file << "         inline constexpr uintptr_t TextScaled = 0xe88;\n";
        file << "         inline constexpr uintptr_t TextWrapped = 0xe89;\n";
        file << "         inline constexpr uintptr_t TextStrokeColor3 = 0xe90;\n";
        file << "         inline constexpr uintptr_t TextStrokeTransparency = 0xe98;\n";
        file << "         inline constexpr uintptr_t TextTransparency = 0xea0;\n";
        file << "         inline constexpr uintptr_t TextTruncate = 0xea8;\n";
        file << "         inline constexpr uintptr_t TextXAlignment = 0xeb0;\n";
        file << "         inline constexpr uintptr_t TextYAlignment = 0xeb1;\n";
        file << "    }\n\n";

        file << "    namespace TextButton {\n";
        file << "         inline constexpr uintptr_t Text = 0xdc0;\n";
        file << "         inline constexpr uintptr_t TextColor3 = 0xe70;\n";
        file << "         inline constexpr uintptr_t TextSize = 0xe78;\n";
        file << "         inline constexpr uintptr_t Font = 0xe80;\n";
        file << "         inline constexpr uintptr_t AutoButtonColor = 0xe88;\n";
        file << "         inline constexpr uintptr_t Modal = 0xe89;\n";
        file << "         inline constexpr uintptr_t HoverColor3 = 0xe90;\n";
        file << "         inline constexpr uintptr_t PressColor3 = 0xe98;\n";
        file << "    }\n\n";

        file << "    namespace TextBox {\n";
        file << "         inline constexpr uintptr_t Text = 0xdc0;\n";
        file << "         inline constexpr uintptr_t TextColor3 = 0xe70;\n";
        file << "         inline constexpr uintptr_t TextSize = 0xe78;\n";
        file << "         inline constexpr uintptr_t Font = 0xe80;\n";
        file << "         inline constexpr uintptr_t PlaceholderText = 0xe88;\n";
        file << "         inline constexpr uintptr_t PlaceholderColor3 = 0xe90;\n";
        file << "         inline constexpr uintptr_t ClearTextOnFocus = 0xe98;\n";
        file << "         inline constexpr uintptr_t MaxVisibleGraphemes = 0xea0;\n";
        file << "         inline constexpr uintptr_t ShowNativeInput = 0xea8;\n";
        file << "    }\n\n";

        file << "    namespace ImageLabel {\n";
        file << "         inline constexpr uintptr_t Image = 0x990;\n";
        file << "         inline constexpr uintptr_t ImageColor3 = 0x998;\n";
        file << "         inline constexpr uintptr_t ImageRectOffset = 0x9a0;\n";
        file << "         inline constexpr uintptr_t ImageRectSize = 0x9a8;\n";
        file << "         inline constexpr uintptr_t ImageTransparency = 0x9b0;\n";
        file << "         inline constexpr uintptr_t ScaleType = 0x9b8;\n";
        file << "         inline constexpr uintptr_t TileSize = 0x9c0;\n";
        file << "    }\n\n";

        file << "    namespace ImageButton {\n";
        file << "         inline constexpr uintptr_t Image = 0x990;\n";
        file << "         inline constexpr uintptr_t ImageColor3 = 0x998;\n";
        file << "         inline constexpr uintptr_t HoverImage = 0x9a0;\n";
        file << "         inline constexpr uintptr_t PressImage = 0x9a8;\n";
        file << "         inline constexpr uintptr_t AutoButtonColor = 0x9b0;\n";
        file << "    }\n\n";

        file << "    namespace Frame {\n";
        file << "         inline constexpr uintptr_t Style = 0x590;\n";
        file << "         inline constexpr uintptr_t Active = 0x591;\n";
        file << "         inline constexpr uintptr_t BorderSizePixel = 0x592;\n";
        file << "         inline constexpr uintptr_t BackgroundColor3 = 0x548;\n";
        file << "         inline constexpr uintptr_t BackgroundTransparency = 0x554;\n";
        file << "    }\n\n";

        file << "    namespace ScrollingFrame {\n";
        file << "         inline constexpr uintptr_t ScrollBarThickness = 0x590;\n";
        file << "         inline constexpr uintptr_t ScrollBarImage = 0x598;\n";
        file << "         inline constexpr uintptr_t CanvasPosition = 0x5a0;\n";
        file << "         inline constexpr uintptr_t CanvasSize = 0x5a8;\n";
        file << "         inline constexpr uintptr_t ScrollingDirection = 0x5b0;\n";
        file << "         inline constexpr uintptr_t ElasticBehavior = 0x5b1;\n";
        file << "    }\n\n";

        file << "    namespace UIListLayout {\n";
        file << "         inline constexpr uintptr_t Padding = 0x590;\n";
        file << "         inline constexpr uintptr_t FillDirection = 0x598;\n";
        file << "         inline constexpr uintptr_t HorizontalAlignment = 0x599;\n";
        file << "         inline constexpr uintptr_t VerticalAlignment = 0x59a;\n";
        file << "         inline constexpr uintptr_t SortOrder = 0x59b;\n";
        file << "    }\n\n";

        file << "    namespace UIGridLayout {\n";
        file << "         inline constexpr uintptr_t CellSize = 0x590;\n";
        file << "         inline constexpr uintptr_t CellPadding = 0x598;\n";
        file << "         inline constexpr uintptr_t StartOffset = 0x5a0;\n";
        file << "         inline constexpr uintptr_t FillDirection = 0x5a8;\n";
        file << "         inline constexpr uintptr_t FillDirectionMaxCells = 0x5a9;\n";
        file << "    }\n\n";

        file << "    namespace UITableLayout {\n";
        file << "         inline constexpr uintptr_t MajorAxisCells = 0x590;\n";
        file << "         inline constexpr uintptr_t MajorAxisSizing = 0x598;\n";
        file << "         inline constexpr uintptr_t MinorAxisSizing = 0x599;\n";
        file << "    }\n\n";

        file << "    namespace UICorner {\n";
        file << "         inline constexpr uintptr_t CornerRadius = 0x590;\n";
        file << "    }\n\n";

        file << "    namespace UIStroke {\n";
        file << "         inline constexpr uintptr_t Color = 0x590;\n";
        file << "         inline constexpr uintptr_t Thickness = 0x598;\n";
        file << "         inline constexpr uintptr_t Transparency = 0x59a;\n";
        file << "         inline constexpr uintptr_t Join = 0x59b;\n";
        file << "    }\n\n";

        file << "    namespace UIGradient {\n";
        file << "         inline constexpr uintptr_t Color = 0x590;\n";
        file << "         inline constexpr uintptr_t Offset = 0x598;\n";
        file << "         inline constexpr uintptr_t Rotation = 0x5a0;\n";
        file << "         inline constexpr uintptr_t Enabled = 0x5a8;\n";
        file << "    }\n\n";

        file << "    namespace UIScale {\n";
        file << "         inline constexpr uintptr_t Scale = 0x590;\n";
        file << "    }\n\n";

        file << "    namespace UIPadding {\n";
        file << "         inline constexpr uintptr_t PaddingTop = 0x590;\n";
        file << "         inline constexpr uintptr_t PaddingBottom = 0x598;\n";
        file << "         inline constexpr uintptr_t PaddingLeft = 0x5a0;\n";
        file << "         inline constexpr uintptr_t PaddingRight = 0x5a8;\n";
        file << "    }\n\n";

        file << "    namespace RemoteEvent {\n";
        file << "         inline constexpr uintptr_t Name = 0xb0;\n";
        file << "         inline constexpr uintptr_t Parent = 0x70;\n";
        file << "         inline constexpr uintptr_t OnClientEvent = 0xc0;\n";
        file << "         inline constexpr uintptr_t OnServerEvent = 0xc8;\n";
        file << "    }\n\n";

        file << "    namespace RemoteFunction {\n";
        file << "         inline constexpr uintptr_t Name = 0xb0;\n";
        file << "         inline constexpr uintptr_t Parent = 0x70;\n";
        file << "         inline constexpr uintptr_t OnClientInvoke = 0xc0;\n";
        file << "         inline constexpr uintptr_t OnServerInvoke = 0xc8;\n";
        file << "    }\n\n";

        file << "    namespace Sound {\n";
        file << "         inline constexpr uintptr_t SoundId = 0xd0;\n";
        file << "         inline constexpr uintptr_t Volume = 0xd8;\n";
        file << "         inline constexpr uintptr_t Pitch = 0xdc;\n";
        file << "         inline constexpr uintptr_t Looped = 0xe0;\n";
        file << "         inline constexpr uintptr_t Playing = 0xe1;\n";
        file << "         inline constexpr uintptr_t TimePosition = 0xe8;\n";
        file << "         inline constexpr uintptr_t Duration = 0xf0;\n";
        file << "         inline constexpr uintptr_t IsPlaying = 0xf8;\n";
        file << "    }\n\n";

        file << "    namespace SoundGroup {\n";
        file << "         inline constexpr uintptr_t Volume = 0xd0;\n";
        file << "         inline constexpr uintptr_t Reverb = 0xd8;\n";
        file << "         inline constexpr uintptr_t DistanceFactor = 0xdc;\n";
        file << "         inline constexpr uintptr_t DopplerScale = 0xe0;\n";
        file << "    }\n\n";

        file << "    namespace ParticleEmitter {\n";
        file << "         inline constexpr uintptr_t Rate = 0xd0;\n";
        file << "         inline constexpr uintptr_t Lifetime = 0xd8;\n";
        file << "         inline constexpr uintptr_t Speed = 0xdc;\n";
        file << "         inline constexpr uintptr_t SpreadAngle = 0xe0;\n";
        file << "         inline constexpr uintptr_t Texture = 0xe8;\n";
        file << "         inline constexpr uintptr_t Size = 0xf0;\n";
        file << "         inline constexpr uintptr_t Color = 0xf8;\n";
        file << "         inline constexpr uintptr_t Enabled = 0x100;\n";
        file << "    }\n\n";

        file << "    namespace Trail {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x170;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x180;\n";
        file << "         inline constexpr uintptr_t Lifetime = 0x190;\n";
        file << "         inline constexpr uintptr_t MinLength = 0x198;\n";
        file << "         inline constexpr uintptr_t Texture = 0x1a0;\n";
        file << "         inline constexpr uintptr_t TextureLength = 0x1a8;\n";
        file << "         inline constexpr uintptr_t TextureMode = 0x1b0;\n";
        file << "         inline constexpr uintptr_t LightEmission = 0x1b8;\n";
        file << "         inline constexpr uintptr_t LightInfluence = 0x1c0;\n";
        file << "         inline constexpr uintptr_t FaceCamera = 0x1c8;\n";
        file << "    }\n\n";

        file << "    namespace Fire {\n";
        file << "         inline constexpr uintptr_t Color = 0xd0;\n";
        file << "         inline constexpr uintptr_t SecondaryColor = 0xd8;\n";
        file << "         inline constexpr uintptr_t Size = 0xe0;\n";
        file << "         inline constexpr uintptr_t Heat = 0xe8;\n";
        file << "         inline constexpr uintptr_t Enabled = 0xf0;\n";
        file << "    }\n\n";

        file << "    namespace Smoke {\n";
        file << "         inline constexpr uintptr_t Color = 0xd0;\n";
        file << "         inline constexpr uintptr_t Size = 0xd8;\n";
        file << "         inline constexpr uintptr_t Opacity = 0xe0;\n";
        file << "         inline constexpr uintptr_t RiseVelocity = 0xe8;\n";
        file << "         inline constexpr uintptr_t Enabled = 0xf0;\n";
        file << "    }\n\n";

        file << "    namespace SpotLight {\n";
        file << "         inline constexpr uintptr_t Angle = 0xd0;\n";
        file << "         inline constexpr uintptr_t Brightness = 0xd8;\n";
        file << "         inline constexpr uintptr_t Color = 0xe0;\n";
        file << "         inline constexpr uintptr_t Face = 0xe8;\n";
        file << "         inline constexpr uintptr_t Range = 0xf0;\n";
        file << "         inline constexpr uintptr_t Shadows = 0xf8;\n";
        file << "    }\n\n";

        file << "    namespace PointLight {\n";
        file << "         inline constexpr uintptr_t Brightness = 0xd0;\n";
        file << "         inline constexpr uintptr_t Color = 0xd8;\n";
        file << "         inline constexpr uintptr_t Range = 0xe0;\n";
        file << "         inline constexpr uintptr_t Shadows = 0xe8;\n";
        file << "    }\n\n";

        file << "    namespace SurfaceLight {\n";
        file << "         inline constexpr uintptr_t Brightness = 0xd0;\n";
        file << "         inline constexpr uintptr_t Color = 0xd8;\n";
        file << "         inline constexpr uintptr_t Range = 0xe0;\n";
        file << "         inline constexpr uintptr_t Face = 0xe8;\n";
        file << "    }\n\n";

        file << "    namespace Weld {\n";
        file << "         inline constexpr uintptr_t Part0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Part1 = 0x118;\n";
        file << "         inline constexpr uintptr_t C0 = 0x120;\n";
        file << "         inline constexpr uintptr_t C1 = 0x130;\n";
        file << "    }\n\n";

        file << "    namespace Motor6D {\n";
        file << "         inline constexpr uintptr_t Part0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Part1 = 0x118;\n";
        file << "         inline constexpr uintptr_t C0 = 0x120;\n";
        file << "         inline constexpr uintptr_t C1 = 0x130;\n";
        file << "         inline constexpr uintptr_t MaxVelocity = 0x140;\n";
        file << "         inline constexpr uintptr_t CurrentAngle = 0x148;\n";
        file << "         inline constexpr uintptr_t DesiredAngle = 0x150;\n";
        file << "    }\n\n";

        file << "    namespace Constraint {\n";
        file << "         inline constexpr uintptr_t Enabled = 0xd0;\n";
        file << "         inline constexpr uintptr_t LimitsEnabled = 0xd8;\n";
        file << "         inline constexpr uintptr_t Visible = 0xe0;\n";
        file << "    }\n\n";

        file << "    namespace BallSocketConstraint {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t LimitsEnabled = 0x120;\n";
        file << "         inline constexpr uintptr_t Radius = 0x128;\n";
        file << "    }\n\n";

        file << "    namespace HingeConstraint {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t LimitsEnabled = 0x120;\n";
        file << "         inline constexpr uintptr_t AngularSpeed = 0x128;\n";
        file << "         inline constexpr uintptr_t AngularVelocity = 0x130;\n";
        file << "         inline constexpr uintptr_t TargetAngle = 0x138;\n";
        file << "    }\n\n";

        file << "    namespace PrismaticConstraint {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t LimitsEnabled = 0x120;\n";
        file << "         inline constexpr uintptr_t Velocity = 0x128;\n";
        file << "         inline constexpr uintptr_t CurrentPosition = 0x130;\n";
        file << "         inline constexpr uintptr_t TargetPosition = 0x138;\n";
        file << "    }\n\n";

        file << "    namespace RopeConstraint {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t Length = 0x120;\n";
        file << "         inline constexpr uintptr_t Restitution = 0x128;\n";
        file << "         inline constexpr uintptr_t Thickness = 0x130;\n";
        file << "    }\n\n";

        file << "    namespace SpringConstraint {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t Stiffness = 0x120;\n";
        file << "         inline constexpr uintptr_t Damping = 0x128;\n";
        file << "         inline constexpr uintptr_t Length = 0x130;\n";
        file << "         inline constexpr uintptr_t FreeLength = 0x138;\n";
        file << "    }\n\n";

        file << "    namespace UniversalConstraint {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t LimitsEnabled = 0x120;\n";
        file << "    }\n\n";

        file << "    namespace CylindricalConstraint {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t LimitsEnabled = 0x120;\n";
        file << "    }\n\n";

        file << "    namespace AlignOrientation {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t Mode = 0x120;\n";
        file << "         inline constexpr uintptr_t CFrame = 0x128;\n";
        file << "         inline constexpr uintptr_t MaxAngularVelocity = 0x138;\n";
        file << "         inline constexpr uintptr_t Responsiveness = 0x140;\n";
        file << "         inline constexpr uintptr_t Rigid = 0x148;\n";
        file << "    }\n\n";

        file << "    namespace AlignPosition {\n";
        file << "         inline constexpr uintptr_t Attachment0 = 0x110;\n";
        file << "         inline constexpr uintptr_t Attachment1 = 0x118;\n";
        file << "         inline constexpr uintptr_t Mode = 0x120;\n";
        file << "         inline constexpr uintptr_t Position = 0x128;\n";
        file << "         inline constexpr uintptr_t MaxVelocity = 0x130;\n";
        file << "         inline constexpr uintptr_t Responsiveness = 0x138;\n";
        file << "         inline constexpr uintptr_t Rigid = 0x140;\n";
        file << "    }\n\n";

        file << "    namespace BodyVelocity {\n";
        file << "         inline constexpr uintptr_t Velocity = 0x110;\n";
        file << "         inline constexpr uintptr_t MaxForce = 0x120;\n";
        file << "         inline constexpr uintptr_t P = 0x130;\n";
        file << "    }\n\n";

        file << "    namespace BodyAngularVelocity {\n";
        file << "         inline constexpr uintptr_t AngularVelocity = 0x110;\n";
        file << "         inline constexpr uintptr_t MaxTorque = 0x120;\n";
        file << "         inline constexpr uintptr_t P = 0x130;\n";
        file << "    }\n\n";

        file << "    namespace BodyPosition {\n";
        file << "         inline constexpr uintptr_t Position = 0x110;\n";
        file << "         inline constexpr uintptr_t MaxForce = 0x120;\n";
        file << "         inline constexpr uintptr_t P = 0x130;\n";
        file << "         inline constexpr uintptr_t D = 0x138;\n";
        file << "    }\n\n";

        file << "    namespace BodyGyro {\n";
        file << "         inline constexpr uintptr_t CFrame = 0x110;\n";
        file << "         inline constexpr uintptr_t MaxTorque = 0x120;\n";
        file << "         inline constexpr uintptr_t P = 0x130;\n";
        file << "         inline constexpr uintptr_t D = 0x138;\n";
        file << "    }\n\n";

        file << "    namespace BodyForce {\n";
        file << "         inline constexpr uintptr_t Force = 0x110;\n";
        file << "    }\n\n";

        file << "    namespace BodyThrust {\n";
        file << "         inline constexpr uintptr_t Force = 0x110;\n";
        file << "         inline constexpr uintptr_t Location = 0x120;\n";
        file << "    }\n\n";

        file << "    namespace BodyAngularVelocity {\n";
        file << "         inline constexpr uintptr_t AngularVelocity = 0x110;\n";
        file << "         inline constexpr uintptr_t MaxTorque = 0x120;\n";
        file << "    }\n\n";

        file << "    namespace Torque {\n";
        file << "         inline constexpr uintptr_t Torque = 0x110;\n";
        file << "    }\n\n";

        file << "    namespace VectorForce {\n";
        file << "         inline constexpr uintptr_t Force = 0x110;\n";
        file << "         inline constexpr uintptr_t ApplyAtCenterOfMass = 0x120;\n";
        file << "    }\n\n";

        file << "    namespace Luau {\n";
        file << "         inline constexpr uintptr_t ExtraSpace = 0x48;\n";
        file << "         inline constexpr uintptr_t Top = 0x8;\n";
        file << "         inline constexpr uintptr_t Base = 0x10;\n";
        file << "         inline constexpr uintptr_t GlobalState = 0x18;\n";
        file << "         inline constexpr uintptr_t CallInfo = 0x20;\n";
        file << "         inline constexpr uintptr_t StackEnd = 0x28;\n";
        file << "         inline constexpr uintptr_t Identity = 0x18;\n";
        file << "         inline constexpr uintptr_t Capabilities = 0x30;\n";
        file << "         inline constexpr uintptr_t Next = 0x18;\n";
        file << "         inline constexpr uintptr_t Prev = 0x20;\n";
        file << "         inline constexpr uintptr_t LClosure_Proto = 0x20;\n";
        file << "         inline constexpr uintptr_t LClosure_Upvals = 0x28;\n";
        file << "         inline constexpr uintptr_t CClosure_Func = 0x18;\n";
        file << "         inline constexpr uintptr_t Proto_Code = 0x10;\n";
        file << "         inline constexpr uintptr_t Proto_K = 0x18;\n";
        file << "         inline constexpr uintptr_t Proto_P = 0x20;\n";
        file << "         inline constexpr uintptr_t Proto_SizeCode = 0x48;\n";
        file << "         inline constexpr uintptr_t Proto_NumParams = 0x4c;\n";
        file << "         inline constexpr uintptr_t Proto_MaxStackSize = 0x4d;\n";
        file << "         inline constexpr uintptr_t Proto_IsVarArg = 0x4e;\n";
        file << "         inline constexpr uintptr_t Table_Array = 0x10;\n";
        file << "         inline constexpr uintptr_t Table_Node = 0x18;\n";
        file << "         inline constexpr uintptr_t Table_LSizenode = 0x20;\n";
        file << "         inline constexpr uintptr_t Table_Metatable = 0x28;\n";
        file << "         inline constexpr uintptr_t TValue_Value = 0x0;\n";
        file << "         inline constexpr uintptr_t TValue_Tag = 0x8;\n";
        file << "         inline constexpr uintptr_t GlobalState_Registry = 0x8;\n";
        file << "         inline constexpr uintptr_t GlobalState_MainThread = 0x10;\n";
        file << "         inline constexpr uintptr_t GlobalState_TMName = 0x20;\n";
        file << "         inline constexpr uintptr_t GlobalState_Strt = 0x30;\n";
        file << "         namespace TValueTags {\n";
        file << "             inline constexpr int Nil = 0;\n";
        file << "             inline constexpr int Boolean = 1;\n";
        file << "             inline constexpr int LightUserdata = 2;\n";
        file << "             inline constexpr int Number = 3;\n";
        file << "             inline constexpr int Vector = 4;\n";
        file << "             inline constexpr int String = 5;\n";
        file << "             inline constexpr int Table = 6;\n";
        file << "             inline constexpr int LClosure = 7;\n";
        file << "             inline constexpr int CClosure = 8;\n";
        file << "             inline constexpr int Userdata = 9;\n";
        file << "             inline constexpr int Thread = 10;\n";
        file << "         }\n";
        file << "    }\n\n";

        file << "    namespace FLog {\n";
        writeOffset("FLog_Default", true, 0x0, "         ");
        writeOffset("DFLog_Default", true, 0x0, "         ");
        file << "         inline constexpr uintptr_t Value = 0x8;\n";
        file << "    }\n\n";

        file << "    namespace SystemAddress {\n";
        file << "         inline constexpr uintptr_t BinaryAddress = 0x0;\n";
        file << "         inline constexpr uintptr_t Port = 0x4;\n";
        file << "    }\n\n";

        file << "    namespace Model {\n";
        file << "         inline constexpr uintptr_t PrimaryPart = 0x278;\n";
        file << "         inline constexpr uintptr_t Scale = 0x164;\n";
        file << "    }\n\n";

        file << "    namespace ModuleScript {\n";
        file << "         inline constexpr uintptr_t ByteCode = 0x0;\n";
        file << "         inline constexpr uintptr_t GUID = 0xe8;\n";
        file << "         inline constexpr uintptr_t Hash = 0x160;\n";
        file << "         inline constexpr uintptr_t IsCoreScript = 0x0;\n";
        file << "    }\n\n";

        file << "    namespace MouseService {\n";
        file << "         inline constexpr uintptr_t InputObject = 0x100;\n";
        file << "         inline constexpr uintptr_t InputObject2 = 0x110;\n";
        file << "         inline constexpr uintptr_t MousePosition = 0xec;\n";
        writeOffset("SensitivityPointer", true, 0x7cb9ad0, "         ");
        file << "    }\n\n";

        file << "    namespace ParticleEmitter {\n";
        file << "         inline constexpr uintptr_t Acceleration = 0x1f0;\n";
        file << "         inline constexpr uintptr_t Brightness = 0x22c;\n";
        file << "         inline constexpr uintptr_t Drag = 0x230;\n";
        file << "         inline constexpr uintptr_t Lifetime = 0x204;\n";
        file << "         inline constexpr uintptr_t LightEmission = 0x248;\n";
        file << "         inline constexpr uintptr_t LightInfluence = 0x24c;\n";
        file << "         inline constexpr uintptr_t Rate = 0x258;\n";
        file << "         inline constexpr uintptr_t RotSpeed = 0x20c;\n";
        file << "         inline constexpr uintptr_t Rotation = 0x214;\n";
        file << "         inline constexpr uintptr_t Speed = 0x21c;\n";
        file << "         inline constexpr uintptr_t SpreadAngle = 0x224;\n";
        file << "         inline constexpr uintptr_t Texture = 0x1d0;\n";
        file << "         inline constexpr uintptr_t TimeScale = 0x26c;\n";
        file << "         inline constexpr uintptr_t VelocityInheritance = 0x270;\n";
        file << "         inline constexpr uintptr_t ZOffset = 0x274;\n";
        file << "    }\n\n";

        file << "    namespace Player {\n";
        file << "         inline constexpr uintptr_t AccountAge = 0x31c;\n";
        file << "         inline constexpr uintptr_t CameraMode = 0x328;\n";
        file << "         inline constexpr uintptr_t DisplayName = 0x130;\n";
        file << "         inline constexpr uintptr_t HealthDisplayDistance = 0x348;\n";
        file << "         inline constexpr uintptr_t LocalPlayer = 0x130;\n";
        file << "         inline constexpr uintptr_t LocaleId = 0x110;\n";
        file << "         inline constexpr uintptr_t MaxZoomDistance = 0x320;\n";
        file << "         inline constexpr uintptr_t MinZoomDistance = 0x324;\n";
        file << "         inline constexpr uintptr_t ModelInstance = 0x398;\n";
        file << "         inline constexpr uintptr_t Mouse = 0x1168;\n";
        file << "         inline constexpr uintptr_t NameDisplayDistance = 0x358;\n";
        file << "         inline constexpr uintptr_t Team = 0x2a0;\n";
        file << "         inline constexpr uintptr_t TeamColor = 0x364;\n";
        file << "         inline constexpr uintptr_t UserId = 0x2c8;\n";
        file << "         inline constexpr uintptr_t Character = 0x318;\n";
        file << "         inline constexpr uintptr_t Backpack = 0x320;\n";
        file << "         inline constexpr uintptr_t PlayerGui = 0x328;\n";
        file << "         inline constexpr uintptr_t PlayerScripts = 0x330;\n";
        file << "    }\n\n";

        file << "    namespace PlayerConfigurer {\n";
        writeOffset("Pointer", true, 0x7bee8e8, "         ");
        file << "    }\n\n";

        file << "    namespace PlayerMouse {\n";
        file << "         inline constexpr uintptr_t Icon = 0xe0;\n";
        file << "         inline constexpr uintptr_t Workspace = 0x168;\n";
        file << "    }\n\n";

        file << "    namespace Primitive {\n";
        file << "         inline constexpr uintptr_t AssemblyAngularVelocity = 0xfc;\n";
        file << "         inline constexpr uintptr_t AssemblyLinearVelocity = 0xf0;\n";
        file << "         inline constexpr uintptr_t Flags = 0x1ae;\n";
        file << "         inline constexpr uintptr_t Material = 0x0;\n";
        file << "         inline constexpr uintptr_t Owner = 0x1f8;\n";
        file << "         inline constexpr uintptr_t Position = 0xe4;\n";
        file << "         inline constexpr uintptr_t Rotation = 0xc0;\n";
        file << "         inline constexpr uintptr_t Size = 0x1b0;\n";
        file << "         inline constexpr uintptr_t Validate = 0x6;\n";
        file << "    }\n\n";

        file << "    namespace PrimitiveFlags {\n";
        file << "         inline constexpr uintptr_t Anchored = 0x2;\n";
        file << "         inline constexpr uintptr_t CanCollide = 0x8;\n";
        file << "         inline constexpr uintptr_t CanQuery = 0x20;\n";
        file << "         inline constexpr uintptr_t CanTouch = 0x10;\n";
        file << "    }\n\n";

        file << "    namespace ProximityPrompt {\n";
        file << "         inline constexpr uintptr_t ActionText = 0xc8;\n";
        file << "         inline constexpr uintptr_t Enabled = 0x14e;\n";
        file << "         inline constexpr uintptr_t GamepadKeyCode = 0x134;\n";
        file << "         inline constexpr uintptr_t HoldDuration = 0x138;\n";
        file << "         inline constexpr uintptr_t KeyCode = 0x13c;\n";
        file << "         inline constexpr uintptr_t MaxActivationDistance = 0x140;\n";
        file << "         inline constexpr uintptr_t ObjectText = 0xe8;\n";
        file << "         inline constexpr uintptr_t RequiresLineOfSight = 0x14f;\n";
        file << "    }\n\n";

        file << "    namespace RenderJob {\n";
        file << "         inline constexpr uintptr_t FakeDataModel = 0x38;\n";
        file << "         inline constexpr uintptr_t RealDataModel = 0x1c0;\n";
        file << "         inline constexpr uintptr_t RenderView = 0x1d0;\n";
        file << "    }\n\n";

        file << "    namespace RenderView {\n";
        file << "         inline constexpr uintptr_t DeviceD3D11 = 0x8;\n";
        file << "         inline constexpr uintptr_t LightingValid = 0x148;\n";
        file << "         inline constexpr uintptr_t SkyValid = 0x28d;\n";
        file << "         inline constexpr uintptr_t VisualEngine = 0x10;\n";
        file << "    }\n\n";

        file << "    namespace RunService {\n";
        file << "         inline constexpr uintptr_t HeartbeatFPS = 0xb4;\n";
        file << "         inline constexpr uintptr_t HeartbeatTask = 0x138;\n";
        file << "    }\n\n";

        file << "    namespace Script {\n";
        file << "         inline constexpr uintptr_t ByteCode = 0x0;\n";
        file << "         inline constexpr uintptr_t GUID = 0xe8;\n";
        file << "         inline constexpr uintptr_t Hash = 0x1b8;\n";
        file << "    }\n\n";

        file << "    namespace ScriptContext {\n";
        file << "         inline constexpr uintptr_t RequireBypass = 0x0;\n";
        file << "    }\n\n";

        file << "    namespace Seat {\n";
        file << "         inline constexpr uintptr_t Occupant = 0x220;\n";
        file << "    }\n\n";

        file << "    namespace Sky {\n";
        file << "         inline constexpr uintptr_t MoonAngularSize = 0x25c;\n";
        file << "         inline constexpr uintptr_t MoonTextureId = 0xe0;\n";
        file << "         inline constexpr uintptr_t SkyboxBk = 0x110;\n";
        file << "         inline constexpr uintptr_t SkyboxDn = 0x140;\n";
        file << "         inline constexpr uintptr_t SkyboxFt = 0x170;\n";
        file << "         inline constexpr uintptr_t SkyboxLf = 0x1a0;\n";
        file << "         inline constexpr uintptr_t SkyboxOrientation = 0x250;\n";
        file << "         inline constexpr uintptr_t SkyboxRt = 0x1d0;\n";
        file << "         inline constexpr uintptr_t SkyboxUp = 0x200;\n";
        file << "         inline constexpr uintptr_t StarCount = 0x260;\n";
        file << "         inline constexpr uintptr_t SunAngularSize = 0x254;\n";
        file << "         inline constexpr uintptr_t SunTextureId = 0x230;\n";
        file << "    }\n\n";

        file << "    namespace Sound {\n";
        file << "         inline constexpr uintptr_t Looped = 0x151;\n";
        file << "         inline constexpr uintptr_t PlaybackSpeed = 0x130;\n";
        file << "         inline constexpr uintptr_t Playing = 0x154;\n";
        file << "         inline constexpr uintptr_t RollOffMaxDistance = 0x134;\n";
        file << "         inline constexpr uintptr_t RollOffMinDistance = 0x138;\n";
        file << "         inline constexpr uintptr_t SoundGroup = 0x100;\n";
        file << "         inline constexpr uintptr_t SoundId = 0xe0;\n";
        file << "         inline constexpr uintptr_t Volume = 0x144;\n";
        file << "    }\n\n";

        file << "    namespace SpawnLocation {\n";
        file << "         inline constexpr uintptr_t AllowTeamChangeOnTouch = 0x45;\n";
        file << "         inline constexpr uintptr_t Enabled = 0x1f9;\n";
        file << "         inline constexpr uintptr_t ForcefieldDuration = 0x1f0;\n";
        file << "         inline constexpr uintptr_t Neutral = 0x1fa;\n";
        file << "         inline constexpr uintptr_t TeamColor = 0x1f4;\n";
        file << "    }\n\n";

        file << "    namespace SpecialMesh {\n";
        file << "         inline constexpr uintptr_t MeshId = 0x108;\n";
        file << "         inline constexpr uintptr_t Scale = 0xdc;\n";
        file << "    }\n\n";

        file << "    namespace StatsItem {\n";
        file << "         inline constexpr uintptr_t Value = 0xc8;\n";
        file << "    }\n\n";

        file << "    namespace SunRaysEffect {\n";
        file << "         inline constexpr uintptr_t Enabled = 0xc8;\n";
        file << "         inline constexpr uintptr_t Intensity = 0xd0;\n";
        file << "         inline constexpr uintptr_t Spread = 0xd4;\n";
        file << "    }\n\n";

        file << "    namespace SurfaceAppearance {\n";
        file << "         inline constexpr uintptr_t AlphaMode = 0x2a0;\n";
        file << "         inline constexpr uintptr_t Color = 0x288;\n";
        file << "         inline constexpr uintptr_t ColorMap = 0xe0;\n";
        file << "         inline constexpr uintptr_t EmissiveMaskContent = 0x110;\n";
        file << "         inline constexpr uintptr_t EmissiveStrength = 0x2a4;\n";
        file << "         inline constexpr uintptr_t EmissiveTint = 0x294;\n";
        file << "         inline constexpr uintptr_t MetalnessMap = 0x140;\n";
        file << "         inline constexpr uintptr_t NormalMap = 0x170;\n";
        file << "         inline constexpr uintptr_t RoughnessMap = 0x1a0;\n";
        file << "    }\n\n";

        file << "    namespace TaskScheduler {\n";
        file << "         inline constexpr uintptr_t JobEnd = 0xd0;\n";
        file << "         inline constexpr uintptr_t JobName = 0x18;\n";
        file << "         inline constexpr uintptr_t JobStart = 0xc8;\n";
        file << "         inline constexpr uintptr_t MaxFPS = 0xb0;\n";
        writeOffset("Pointer", true, 0x7cf5400, "         ");
        file << "         inline constexpr uintptr_t TaskSchedulerTargetFps = 0x118;\n";
        file << "         inline constexpr uintptr_t RenderJob = 0x10;\n";
        file << "         inline constexpr uintptr_t PhysicsJob = 0x18;\n";
        file << "         inline constexpr uintptr_t HeartbeatJob = 0x20;\n";
        file << "    }\n\n";

        file << "    namespace Team {\n";
        file << "         inline constexpr uintptr_t BrickColor = 0xd0;\n";
        file << "    }\n\n";

        file << "    namespace Terrain {\n";
        file << "         inline constexpr uintptr_t GrassLength = 0x1f8;\n";
        file << "         inline constexpr uintptr_t MaterialColors = 0x2a8;\n";
        file << "         inline constexpr uintptr_t WaterColor = 0x1e8;\n";
        file << "         inline constexpr uintptr_t WaterReflectance = 0x200;\n";
        file << "         inline constexpr uintptr_t WaterTransparency = 0x204;\n";
        file << "         inline constexpr uintptr_t WaterWaveSize = 0x208;\n";
        file << "         inline constexpr uintptr_t WaterWaveSpeed = 0x20c;\n";
        file << "    }\n\n";

        file << "    namespace Textures {\n";
        file << "         inline constexpr uintptr_t Decal_Texture = 0x198;\n";
        file << "         inline constexpr uintptr_t Texture_Texture = 0x198;\n";
        file << "    }\n\n";

        file << "    namespace Tool {\n";
        file << "         inline constexpr uintptr_t CanBeDropped = 0x4c8;\n";
        file << "         inline constexpr uintptr_t Enabled = 0x4c9;\n";
        file << "         inline constexpr uintptr_t Grip = 0x4bc;\n";
        file << "         inline constexpr uintptr_t ManualActivationOnly = 0x4ca;\n";
        file << "         inline constexpr uintptr_t RequiresHandle = 0x4cb;\n";
        file << "         inline constexpr uintptr_t TextureId = 0x370;\n";
        file << "         inline constexpr uintptr_t Tooltip = 0x478;\n";
        file << "    }\n\n";

        file << "    namespace UnionOperation {\n";
        file << "         inline constexpr uintptr_t AssetId = 0x2f0;\n";
        file << "    }\n\n";

        file << "    namespace UserInputService {\n";
        file << "         inline constexpr uintptr_t WindowInputState = 0x290;\n";
        file << "    }\n\n";

        file << "    namespace VehicleSeat {\n";
        file << "         inline constexpr uintptr_t MaxSpeed = 0x238;\n";
        file << "         inline constexpr uintptr_t SteerFloat = 0x240;\n";
        file << "         inline constexpr uintptr_t ThrottleFloat = 0x248;\n";
        file << "         inline constexpr uintptr_t Torque = 0x24c;\n";
        file << "         inline constexpr uintptr_t TurnSpeed = 0x250;\n";
        file << "    }\n\n";

        file << "    namespace VisualEngine {\n";
        file << "         inline constexpr uintptr_t Dimensions = 0xa90;\n";
        file << "         inline constexpr uintptr_t FakeDataModel = 0xa70;\n";
        writeOffset("Pointer", true, 0x77c6670, "         ");
        file << "         inline constexpr uintptr_t RenderView = 0xb70;\n";
        file << "         inline constexpr uintptr_t ViewMatrix = 0x130;\n";
        file << "         inline constexpr uintptr_t ProjectionMatrix = 0x170;\n";
        file << "         inline constexpr uintptr_t ViewportWidth = 0x2ac;\n";
        file << "         inline constexpr uintptr_t ViewportHeight = 0x2b0;\n";
        file << "         inline constexpr uintptr_t FarPlane = 0x2c0;\n";
        file << "         inline constexpr uintptr_t NearPlane = 0x2bc;\n";
        file << "    }\n\n";

        file << "    namespace Weld {\n";
        file << "         inline constexpr uintptr_t Part0 = 0x130;\n";
        file << "         inline constexpr uintptr_t Part1 = 0x140;\n";
        file << "    }\n\n";

        file << "    namespace WeldConstraint {\n";
        file << "         inline constexpr uintptr_t Part0 = 0xd0;\n";
        file << "         inline constexpr uintptr_t Part1 = 0xe0;\n";
        file << "    }\n\n";

        file << "    namespace WindowInputState {\n";
        file << "         inline constexpr uintptr_t CapsLock = 0x40;\n";
        file << "         inline constexpr uintptr_t CurrentTextBox = 0x48;\n";
        file << "    }\n\n";

        file << "    namespace Workspace {\n";
        file << "         inline constexpr uintptr_t CurrentCamera = 0x490;\n";
        file << "         inline constexpr uintptr_t DistributedGameTime = 0x4b0;\n";
        file << "         inline constexpr uintptr_t ReadOnlyGravity = 0x9b8;\n";
        file << "         inline constexpr uintptr_t World = 0x408;\n";
        file << "    }\n\n";

        file << "    namespace World {\n";
        file << "         inline constexpr uintptr_t AirProperties = 0x218;\n";
        file << "         inline constexpr uintptr_t FallenPartsDestroyHeight = 0x208;\n";
        file << "         inline constexpr uintptr_t Gravity = 0x210;\n";
        file << "         inline constexpr uintptr_t Primitives = 0x280;\n";
        file << "         inline constexpr uintptr_t worldStepsPerSec = 0x6b8;\n";
        file << "    }\n";

        file << "}\n\n";

        file << "namespace Roblox\n";
        file << "{\n";
        file << "    inline auto Print = (uintptr_t(*)(int, const char*, ...))Offsets::Print;\n";
        file << "    inline auto Luau_Execute = (void(__fastcall*)(lua_State*))Offsets::Luau::Luau_Execute;\n";
        file << "    inline auto GetLuaStateForInstance = (lua_State*(__fastcall*)(uint64_t, uint64_t*, uint64_t*))Offsets::GetLuaStateForInstance;\n";
        file << "    inline auto ScriptContextResume = (uint64_t(__fastcall*)(uint64_t, YieldState*, YieldingLuaThread**, uint32_t, uint8_t, uint64_t))Offsets::ScriptContextResume;\n";
        file << "}\n";

        file.close();
        std::cout << "Dump saved to " << filename << "\n";
    }

    void DumpMemory(const std::string& filename, uintptr_t start, size_t size) {
        std::vector<uint8_t> buffer(size);
        SIZE_T bytesRead;

        if (ReadProcessMemory(hProcess, (LPCVOID)start, buffer.data(), size, &bytesRead)) {
            std::ofstream file(filename, std::ios::binary);
            if (file.is_open()) {
                file.write((const char*)buffer.data(), bytesRead);
                file.close();
                std::cout << "Memory dump saved to " << filename << " (" << bytesRead << " bytes)\n";
            }
        } else {
            std::cerr << "Failed to dump memory\n";
        }
    }
};

int main(int argc, char* argv[]) {
    std::cout << "Orion Dumper\n";
    std::cout << "============================\n\n";

    try {
        DWORD pid = 0;
        std::string outputFile = "Offsets.hpp";

        if (argc >= 2) {
            std::string target = argv[1];
            
            
            try {
                pid = std::stoul(target);
            } catch (...) {
                
                RobloxDumper tempDumper;
                pid = tempDumper.FindProcessByName(target);
                if (pid == 0) {
                    std::cerr << "Process not found: " << target << "\n";
                    std::cout << "\nPress Enter to exit...";
                    std::cin.get();
                    return 1;
                }
                std::cout << "Found process: " << target << " (PID: " << pid << ")\n";
            }

            if (argc >= 3) {
                outputFile = argv[2];
            }
        } else {
            
            RobloxDumper tempDumper;
            pid = tempDumper.FindRobloxProcess();
            if (pid == 0) {
                std::cerr << "No Roblox process found. Please make sure Roblox is running.\n";
                std::cout << "\nPress Enter to exit...";
                std::cin.get();
                return 1;
            }
        }

        RobloxDumper dumper;
        
        
        std::cout << "Attempting to connect to kernel driver...\n";
        if (dumper.ConnectKernel()) {
            std::cout << "Using kernel driver for memory access\n";
        } else {
            std::cout << "Kernel driver not available, using user-mode API\n";
        }
        
        if (!dumper.AttachToProcess(pid)) {
            std::cout << "\nPress Enter to exit...";
            std::cin.get();
            return 1;
        }

        std::cout << "Attached to process " << pid << "\n";

        if (!dumper.FindRobloxBase()) {
            std::cerr << "Failed to find Roblox base address\n";
            std::cout << "\nPress Enter to exit...";
            std::cin.get();
            return 1;
        }

        std::cout << "Finding Roblox version...\n";
        dumper.FindVersion();

        std::cout << "Finding DataModel...\n";
        dumper.FindDataModel();
        
        std::cout << "Dumping classes...\n";
        dumper.DumpClasses();
        
        std::cout << "Saving offsets to " << outputFile << "...\n";
        dumper.SaveDump(outputFile);

        std::cout << "\nDump complete!\n";

        
        std::cout << "Creating GitHub release...\n";
        std::string version = dumper.GetVersion();
        if (version.empty() || version == "Unknown") {
            version = "latest";
        }

        std::filesystem::path releaseScript = "Create-Release.ps1";
        if (!std::filesystem::exists(releaseScript)) {
            releaseScript = "..\\Create-Release.ps1";
        }

        if (!std::filesystem::exists(releaseScript)) {
            std::cerr << "Create-Release.ps1 not found. Skipping GitHub release.\n";
        } else {
            std::string psCommand = "powershell -ExecutionPolicy Bypass -File \"";
            psCommand += releaseScript.string() + "\" ";
            psCommand += "-OffsetsFile \"" + outputFile + "\" ";
            psCommand += "-Version \"" + version + "\"";

            std::cout << "Running: " << psCommand << "\n";
            int releaseExitCode = system(psCommand.c_str());
            if (releaseExitCode != 0) {
                std::cerr << "GitHub release step failed with exit code " << releaseExitCode << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred\n";
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::cout << "\nPress Enter to exit...";
    std::cin.get();
    return 0;
}
