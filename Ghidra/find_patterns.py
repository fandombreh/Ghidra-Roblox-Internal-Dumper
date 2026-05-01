# Ghidra script to find specific patterns in Roblox
# @category RobloxDumper
# @author RobloxDumper

from ghidra.program.model.mem import MemoryAccessException

def find_pattern(byte_pattern, mask=None):
    """
    Search for a byte pattern in the program
    
    Args:
        byte_pattern: String of hex bytes (e.g., "48 8B 0D ?? ?? ?? ??")
        mask: Optional mask for wildcards (e.g., "FF FF FF 00 00 00 00")
    """
    print(f"Searching for pattern: {byte_pattern}")
    
    # Convert pattern to bytes
    pattern_bytes = []
    wildcards = []
    
    parts = byte_pattern.split()
    for part in parts:
        if part == "??":
            pattern_bytes.append(0)
            wildcards.append(True)
        else:
            pattern_bytes.append(int(part, 16))
            wildcards.append(False)
    
    results = []
    
    # Search through all memory blocks
    for block in currentProgram.getMemory().getBlocks():
        if not block.isInitialized():
            continue
            
        start = block.getStart().getOffset()
        end = block.getEnd().getOffset()
        
        # Scan the block
        for offset in range(start, end - len(pattern_bytes) + 1):
            match = True
            for i, byte in enumerate(pattern_bytes):
                if not wildcards[i]:
                    try:
                        mem_byte = getByte(offset + i)
                        if mem_byte != byte:
                            match = False
                            break
                    except MemoryAccessException:
                        match = False
                        break
            
            if match:
                results.append(toAddr(offset))
                print(f"Found at 0x{offset:X}")
    
    return results

def find_data_model():
    """Find DataModel references"""
    print("Searching for DataModel...")
    
    # Common patterns for DataModel access
    patterns = [
        "48 8B 0D ?? ?? ?? ?? 48 85 C9 74 ?? 48 8B 01 FF 50 ??",
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20",
        "4C 8B DC 49 89 5B 08 49 89 6B 10 49 89 73 18 57 48 83 EC 30"
    ]
    
    for pattern in patterns:
        results = find_pattern(pattern)
        if results:
            print(f"DataModel pattern found at:")
            for addr in results:
                print(f"  0x{addr:X}")

def find_instance_creation():
    """Find Instance creation functions"""
    print("Searching for Instance creation...")
    
    patterns = [
        "48 83 EC 28 48 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ??",
        "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57"
    ]
    
    for pattern in patterns:
        results = find_pattern(pattern)
        if results:
            print(f"Instance creation pattern found at:")
            for addr in results:
                print(f"  0x{addr:X}")

def find_getchildren():
    """Find GetChildren method"""
    print("Searching for GetChildren...")
    
    patterns = [
        "48 8B 01 FF 50 ?? 48 85 C0 74 ?? 48 83 78 18 00",
        "40 53 48 83 EC 20 48 8B D9 48 8B 49 18"
    ]
    
    for pattern in patterns:
        results = find_pattern(pattern)
        if results:
            print(f"GetChildren pattern found at:")
            for addr in results:
                print(f"  0x{addr:X}")

def find_set_instance():
    """Find SetInstance method"""
    print("Searching for SetInstance...")
    
    patterns = [
        "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8B D9 48 8B FA 48 8B 49 18",
        "40 53 48 83 EC 20 48 8B D9 48 8B FA E8 ?? ?? ?? ?? 48 8B C8"
    ]
    
    for pattern in patterns:
        results = find_pattern(pattern)
        if results:
            print(f"SetInstance pattern found at:")
            for addr in results:
                print(f"  0x{addr:X}")

def find_taskscheduler():
    """Find TaskScheduler::get()"""
    print("Searching for TaskScheduler...")
    
    patterns = [
        "48 8B 05 ?? ?? ?? ?? 48 83 C1 ?? 48 8B 00",
        "E8 ?? ?? ?? ?? 48 8B D8 48 85 C0 74 ?? 48 8B CB E8 ?? ?? ?? ??"
    ]
    
    for pattern in patterns:
        results = find_pattern(pattern)
        if results:
            print(f"TaskScheduler pattern found at:")
            for addr in results:
                print(f"  0x{addr:X}")

def find_fireclickdetector():
    """Find fireclickdetector function"""
    print("Searching for fireclickdetector...")
    patterns = ["48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC 30 48 8B F1 48 8B DA"]
    for pattern in patterns:
        results = find_pattern(pattern)
        if results:
            for addr in results: print(f"fireclickdetector at 0x{addr:X}")

def find_firetouchinterest():
    """Find firetouchinterest function"""
    print("Searching for firetouchinterest...")
    patterns = ["48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 30"]
    for pattern in patterns:
        results = find_pattern(pattern)
        if results:
            for addr in results: print(f"firetouchinterest at 0x{addr:X}")

def find_luau_execute():
    """Find Luau execute function"""
    print("Searching for Luau execute...")
    patterns = ["40 53 56 57 41 54 41 55 41 56 41 57 48 83 EC ?? 48 8B D9"]
    for pattern in patterns:
        results = find_pattern(pattern)
        if results:
            for addr in results: print(f"Luau execute at 0x{addr:X}")

def find_job_names():
    """Find TaskScheduler job name references"""
    print("Searching for job names...")
    job_names = ["Render", "Physics", "Heartbeat", "WaitingHybrid", "DataModelJob"]
    for name in job_names:
        results = findBytes(name)
        if results:
            for addr in results:
                print(f"Job name '{name}' at 0x{addr.getOffset():X}")
                refs = getReferencesTo(addr)
                for ref in refs:
                    print(f"  Referenced at 0x{ref.getFromAddress().getOffset():X}")

def main():
    print("=== Pattern Search ===")
    print()
    
    find_data_model()
    print()
    
    find_instance_creation()
    print()
    
    find_getchildren()
    print()
    
    find_findfirstchild()
    print()
    
    find_set_instance()
    print()
    
    find_taskscheduler()
    print()

    find_fireclickdetector()
    print()

    find_firetouchinterest()
    print()

    find_luau_execute()
    print()

    find_job_names()
    print()
    
    print("=== Pattern Search Complete ===")

if __name__ == "__main__":
    main()
