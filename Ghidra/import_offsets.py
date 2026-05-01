# Ghidra script to import offsets from Offsets.hpp and analyze Roblox
# @category RobloxDumper
# @author RobloxDumper

from ghidra.program.model.symbol import SourceType
from ghidra.program.model.listing import CodeUnit
from ghidra.program.model.data import DataType
from ghidra.program.model.mem import Memory
from ghidra.util.task import TaskMonitor
import os
import re

def parse_offsets_hpp():
    """Parse Offsets.hpp file and extract offsets"""
    # Try to find Offsets.hpp in the build directory
    script_dir = getSourceFile().getParentFile().getParentFile().getParentFile()
    offsets_path = os.path.join(str(script_dir), "build", "Offsets.hpp")
    
    if not os.path.exists(offsets_path):
        print("Offsets.hpp not found at: " + offsets_path)
        print("Please run the dumper first to generate Offsets.hpp")
        return None
    
    print("Reading offsets from: " + offsets_path)
    
    offsets = {}
    with open(offsets_path, 'r') as f:
        content = f.read()
        
        # Extract base addresses
        base_match = re.search(r'constexpr uintptr_t RobloxBase = (0x[0-9a-fA-F]+);', content)
        if base_match:
            offsets['RobloxBase'] = int(base_match.group(1), 16)
            print("RobloxBase: 0x%X" % offsets['RobloxBase'])
        
        # Extract DataModel
        datamodel_match = re.search(r'constexpr uintptr_t DataModel = (0x[0-9a-fA-F]+);', content)
        if datamodel_match:
            offsets['DataModel'] = int(datamodel_match.group(1), 16)
            print("DataModel: 0x%X" % offsets['DataModel'])
        
        # Extract string offsets
        string_matches = re.findall(r'constexpr uintptr_t Class_String_(\w+) = (0x[0-9a-fA-F]+);', content)
        for name, addr in string_matches:
            offsets['String_' + name] = int(addr, 16)
        
        # Extract vtable offsets
        vtable_matches = re.findall(r'constexpr uintptr_t Class_VTable_(\d+) = (0x[0-9a-fA-F]+);', content)
        for num, addr in vtable_matches:
            offsets['VTable_' + num] = int(addr, 16)
        
        # Extract instance offsets
        instance_matches = re.findall(r'constexpr size_t Instance_(\w+) = (0x[0-9a-fA-F]+);', content)
        for name, offset in instance_matches:
            offsets['Instance_' + name] = int(offset, 16)
    
    print("Found %d offsets" % len(offsets))
    return offsets

def create_label_at_address(address, name, comment=""):
    """Create a label at the given address"""
    try:
        addr = toAddr(address)
        if currentProgram.getMemory().contains(addr):
            createLabel(addr, name, True)
            if comment:
                setEOLComment(addr, comment)
            print("Created label: %s at 0x%X" % (name, address))
            return True
    except:
        print("Failed to create label at 0x%X" % address)
    return False

def define_instance_structure():
    """Define the Instance structure"""
    try:
        # Check if structure already exists
        dt = DataType.toDataType("Instance")
        if dt:
            print("Instance structure already exists")
            return dt
        
        # Create structure
        struct = StructureDataType("Instance", 0x40, currentProgram.getDataTypeManager())
        
        # Add common fields (adjust based on actual reversing)
        struct.add(DataType.toDataType("void*"), 8, "vtable", "Virtual function table")
        struct.add(DataType.toDataType("void*"), 8, 0x8, "reference_count")
        struct.add(DataType.toDataType("char*"), 8, 0x10, "class_name")
        struct.add(DataType.toDataType("char*"), 8, 0x18, "name")
        struct.add(DataType.toDataType("void*"), 8, 0x20, "parent")
        struct.add(DataType.toDataType("void*"), 8, 0x28, "children")
        struct.add(DataType.toDataType("bool"), 1, 0x30, "archivable")
        struct.add(DataType.toDataType("bool"), 1, 0x31, "roblox_locked")
        
        currentProgram.getDataTypeManager().addDataType(struct)
        print("Created Instance structure")
        return struct
    except Exception as e:
        print("Failed to create Instance structure: " + str(e))
        return None

def analyze_vtable(vtable_addr, name):
    """Analyze a vtable and create function labels"""
    try:
        addr = toAddr(vtable_addr)
        if not currentProgram.getMemory().contains(addr):
            return
        
        print("Analyzing vtable: %s at 0x%X" % (name, vtable_addr))
        
        # Create label for vtable
        createLabel(addr, name, "Virtual function table")
        
        # Analyze first 20 function pointers
        for i in range(20):
            try:
                func_addr = toAddr(getLong(addr.getOffset() + i * 8))
                if currentProgram.getMemory().contains(func_addr):
                    func_name = "%s_func_%d" % (name, i)
                    createLabel(func_addr, func_name, "Virtual function %d" % i)
                    
                    # Create function if it doesn't exist
                    func = getFunctionContaining(func_addr)
                    if not func:
                        createFunction(func_addr, func_name)
            except:
                break
    except Exception as e:
        print("Error analyzing vtable: " + str(e))

def analyze_string_reference(addr, name):
    """Analyze a string reference and create label"""
    try:
        string_addr = toAddr(addr)
        if not currentProgram.getMemory().contains(string_addr):
            return
        
        # Try to read the string
        try:
            string_data = readNullTerminatedString(string_addr)
            if string_data:
                createLabel(string_addr, "str_" + name, "String: " + string_data)
                print("Found string: %s at 0x%X" % (string_data, addr))
        except:
            createLabel(string_addr, "str_" + name, "String reference")
    except Exception as e:
        print("Error analyzing string: " + str(e))

def find_xrefs_to_address(address):
    """Find all cross-references to an address"""
    try:
        addr = toAddr(address)
        refs = getReferencesTo(addr)
        print("Found %d references to 0x%X" % (len(refs), address))
        for ref in refs:
            from_addr = ref.getFromAddress()
            print("  Reference from: 0x%X" % from_addr.getOffset())
    except Exception as e:
        print("Error finding xrefs: " + str(e))

def main():
    print("=== Roblox Offset Import and Analysis ===")
    print()
    
    # Parse offsets
    offsets = parse_offsets_hpp()
    if not offsets:
        print("No offsets to import")
        return
    
    print()
    
    # Get image base for address adjustment
    image_base = currentProgram.getImageBase().getOffset()
    print("Current image base: 0x%X" % image_base)
    
    if 'RobloxBase' in offsets:
        dumped_base = offsets['RobloxBase']
        base_diff = dumped_base - image_base
        print("Dumped base: 0x%X" % dumped_base)
        print("Base difference: 0x%X" % base_diff)
        print()
    
    # Define structures
    print("Defining structures...")
    define_instance_structure()
    print()
    
    # Import string offsets
    print("Importing string offsets...")
    string_count = 0
    for key, addr in offsets.items():
        if key.startswith('String_'):
            name = key.replace('String_', '')
            # Adjust address if needed
            adjusted_addr = addr
            if 'RobloxBase' in offsets:
                adjusted_addr = addr - offsets['RobloxBase'] + image_base
            
            if analyze_string_reference(adjusted_addr, name):
                string_count += 1
    
    print("Imported %d string references" % string_count)
    print()
    
    # Import vtable offsets
    print("Importing vtable offsets...")
    vtable_count = 0
    for key, addr in offsets.items():
        if key.startswith('VTable_'):
            name = key
            # Adjust address if needed
            adjusted_addr = addr
            if 'RobloxBase' in offsets:
                adjusted_addr = addr - offsets['RobloxBase'] + image_base
            
            analyze_vtable(adjusted_addr, name)
            vtable_count += 1
    
    print("Imported %d vtables" % vtable_count)
    print()
    
    # Find xrefs to important addresses
    print("Finding cross-references...")
    if 'DataModel' in offsets:
        adjusted_datamodel = offsets['DataModel']
        if 'RobloxBase' in offsets:
            adjusted_datamodel = offsets['DataModel'] - offsets['RobloxBase'] + image_base
        find_xrefs_to_address(adjusted_datamodel)
    
    print()
    print("=== Import Complete ===")
    print("Summary:")
    print("  Structures defined: Instance")
    print("  String references: %d" % string_count)
    print("  Vtables analyzed: %d" % vtable_count)

if __name__ == "__main__":
    main()
