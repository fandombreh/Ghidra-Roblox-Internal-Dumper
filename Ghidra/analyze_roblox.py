



from ghidra.program.model.symbol import SourceType
from ghidra.program.model.listing import CodeUnit
from ghidra.program.model.data import DataType
from ghidra.util.task import TaskMonitor
from ghidra.program.model.mem import MemoryAccessException
import struct

def find_all_vtables():
    """Find all vtables in the binary with validation"""
    print("Searching for vtables...")
    
    vtables = []
    
    for block in currentProgram.getMemory().getBlocks():
        if not block.isInitialized():
            continue
            
        start = block.getStart().getOffset()
        end = block.getEnd().getOffset()
        

        offset = start
        while offset < end - 32:
            try:

                ptr = toAddr(getLong(offset))
                if ptr and currentProgram.getMemory().contains(ptr):
                    func = getFunctionContaining(ptr)
                    if func:

                        valid_vtable = True
                        for i in range(1, 5):
                            if offset + i * 8 >= end:
                                valid_vtable = False
                                break
                            try:
                                next_ptr = toAddr(getLong(offset + i * 8))
                                if not next_ptr or not currentProgram.getMemory().contains(next_ptr):
                                    valid_vtable = False
                                    break
                                next_func = getFunctionContaining(next_ptr)
                                if not next_func:
                                    valid_vtable = False
                                    break
                            except:
                                valid_vtable = False
                                break
                        
                        if valid_vtable:
                            vtables.append(toAddr(offset))
                            print(f"Vtable at 0x{offset:X} (first function: {func.getName()})")
                            offset += 0x100
                            continue
            except:
                pass
            offset += 8
    
    print(f"Found {len(vtables)} vtables")
    return vtables

def analyze_rtti_comprehensive():
    """Comprehensive RTTI scanning"""
    print("Scanning for RTTI structures...")
    
    rtti_found = []
    

    patterns = [".?AV", ".?AU", ".?AV?"]
    
    for pattern in patterns:
        results = findBytes(pattern)
        if results:
            for addr in results:
                rtti_found.append(addr)

                try:
                    type_name = ""
                    offset = addr.getOffset()
                    for i in range(100):
                        b = getByte(offset + i)
                        if b == 0:
                            break
                        if 32 <= b <= 126:
                            type_name += chr(b)
                    print(f"RTTI at 0x{offset:X}: {type_name}")
                except:
                    print(f"RTTI at 0x{addr.getOffset():X}")
    
    print(f"Found {len(rtti_found)} RTTI entries")
    return rtti_found

def find_all_string_references():
    """Find references to all relevant strings"""
    print("Finding string references...")
    
    target_strings = [
        "TextLabel", "TextButton", "Frame", "ScreenGui",

        "lua_State", "luaL_", "lua_", "luau_", "Luau",
        "bytecode", "proto", "closure", "upvalue", "env",
        "getglobal", "setglobal", "getfield", "setfield",
        "call", "pcall", "xpcall", "loadstring", "load",
        "VM", "Execution", "Interpreter", "JIT",
        "GC", "garbage", "collect", "memory",
        "table", "array", "metatable", "__index", "__newindex",
        "thread", "coroutine", "resume", "yield",

        "NetworkClient", "NetworkServer", "NetworkReplicator", "PlayerReplicator",
        "RemoteEvent", "RemoteFunction", "UnreliableRemoteEvent",

        "RunService", "LogService", "Stats", "GuiService", "UserInputService",
        "ContextActionService", "HttpService", "AssetService", "TweenService",
        "ContentProvider", "PhysicsService", "Chat", "TextChatService",
        "VoiceChatService", "VRService", "ControllerService",

        "archivable", "name", "parent", "className", "children",
        "position", "size", "cframe", "velocity", "transparency",
        "reflectance", "canCollide", "anchored", "locked",
        "health", "maxHealth", "jump", "walkSpeed", "userId"
    ]
    
    all_refs = {}
    
    for string in target_strings:
        results = findBytes(string)
        if results:
            all_refs[string] = []
            for addr in results:
                refs = getReferencesTo(addr)
                for ref in refs:
                    all_refs[string].append(ref.getFromAddress())
            print(f"'{string}': {len(all_refs[string])} references")
    
    return all_refs

def analyze_vtable_functions(vtable_addr):
    """Analyze functions in a vtable"""
    print(f"\nAnalyzing vtable at 0x{vtable_addr.getOffset():X}")
    
    functions = []
    offset = vtable_addr.getOffset()
    
    for i in range(50):
        try:
            func_ptr = toAddr(getLong(offset + i * 8))
            if not func_ptr or not currentProgram.getMemory().contains(func_ptr):
                break
            
            func = getFunctionContaining(func_ptr)
            if func:
                functions.append((i, func_ptr, func))
                print(f"  [{i}] 0x{func_ptr.getOffset():X} - {func.getName()}")
            else:

                createFunction(func_ptr, f"vtable_func_{i}")
                functions.append((i, func_ptr, getFunctionAt(func_ptr)))
                print(f"  [{i}] 0x{func_ptr.getOffset():X} - (new function)")
        except:
            break
    
    return functions

def find_data_structures():
    """Find potential data structures by analyzing memory patterns"""
    print("Searching for data structures...")
    
    structures = []
    
    for block in currentProgram.getMemory().getBlocks():
        if not block.isInitialized() or not block.isRead():
            continue
            
        start = block.getStart().getOffset()
        end = block.getEnd().getOffset()
        


        offset = start
        while offset < end - 64:
            try:

                ptr_count = 0
                for i in range(8):
                    ptr = toAddr(getLong(offset + i * 8))
                    if ptr and currentProgram.getMemory().contains(ptr):
                        ptr_count += 1
                
                if ptr_count >= 4:
                    structures.append(toAddr(offset))
                    print(f"Potential structure at 0x{offset:X} ({ptr_count} pointers)")
                    offset += 0x100
            except:
                pass
            offset += 8
    
    print(f"Found {len(structures)} potential structures")
    return structures

def analyze_luau_vm():
    """Analyze Luau VM specific structures"""
    print("Analyzing Luau VM structures...")
    
    luau_patterns = {
        "lua_State": None,
        "luaL_Buffer": None,
        "TValue": None,
        "Proto": None,
        "Closure": None,
        "UpVal": None
    }
    

    for name in luau_patterns.keys():
        results = findBytes(name)
        if results:
            print(f"Found {name} reference at 0x{results[0].getOffset():X}")
            luau_patterns[name] = results[0]
    
    return luau_patterns

def export_offsets_to_file():
    """Export all found offsets to a file"""
    print("Exporting offsets to file...")
    
    offsets = {}
    

    vtables = find_all_vtables()
    offsets["vtables"] = [v.getOffset() for v in vtables]
    

    rtti = analyze_rtti_comprehensive()
    offsets["rtti"] = [r.getOffset() for r in rtti]
    

    string_refs = find_all_string_references()
    offsets["string_refs"] = string_refs
    
    print(f"Exported {len(offsets['vtables'])} vtables, {len(offsets['rtti'])} RTTI entries")
    return offsets

def main():
    print("=== Comprehensive Roblox Structure Analysis ===")
    print()
    

    vtables = find_all_vtables()
    print()
    
    rtti = analyze_rtti_comprehensive()
    print()
    
    string_refs = find_all_string_references()
    print()
    
    structures = find_data_structures()
    print()
    
    luau = analyze_luau_vm()
    print()
    

    if vtables:
        print("Analyzing first 3 vtables in detail:")
        for i, vtable in enumerate(vtables[:3]):
            analyze_vtable_functions(vtable)
    
    print()
    print("=== Analysis Complete ===")
    print(f"Summary:")
    print(f"  Vtables: {len(vtables)}")
    print(f"  RTTI entries: {len(rtti)}")
    print(f"  String references: {sum(len(refs) for refs in string_refs.values())}")
    print(f"  Potential structures: {len(structures)}")

if __name__ == "__main__":
    main()
