# Ghidra script to define Roblox structures
# @category RobloxDumper
# @author RobloxDumper

from ghidra.program.model.data import StructureDataType, PointerDataType, QWordDataType, DWordDataType, ByteDataType, CharDataType
from ghidra.program.model.symbol import SourceType

def create_instance_struct():
    """Create the Instance structure definition"""
    print("Creating Instance structure...")
    
    # These offsets are examples - adjust based on actual reversing
    struct = StructureDataType("Instance", 0)
    
    struct.add(PointerDataType(), 0, "vtable", None)
    struct.add(PointerDataType(), 8, "className", None)
    struct.add(PointerDataType(), 16, "name", None)
    struct.add(PointerDataType(), 24, "parent", None)
    struct.add(PointerDataType(), 32, "children", None)
    struct.add(ByteDataType(), 40, "archivable", None)
    struct.add(ByteDataType(), 41, "robloxLocked", None)
    struct.add(ByteDataType(), 42, "reserved1", None)
    struct.add(ByteDataType(), 43, "reserved2", None)
    struct.add(QWordDataType(), 48, "refCount", None)
    
    # Add to program
    dtm = currentProgram.getDataTypeManager()
    existing = dtm.getDataType(struct.getCategoryPath(), struct.getName())
    if existing:
        dtm.replaceDataType(existing, struct, True)
    else:
        dtm.addDataType(struct, None)
    
    print(f"Created structure: {struct.getName()} ({struct.getLength()} bytes)")
    return struct

def create_part_struct():
    """Create the Part structure definition"""
    print("Creating Part structure...")
    
    # Part extends Instance
    struct = StructureDataType("Part", 0)
    
    # Instance fields
    struct.add(PointerDataType(), 0, "vtable", None)
    struct.add(PointerDataType(), 8, "className", None)
    struct.add(PointerDataType(), 16, "name", None)
    struct.add(PointerDataType(), 24, "parent", None)
    struct.add(PointerDataType(), 32, "children", None)
    struct.add(ByteDataType(), 40, "archivable", None)
    struct.add(ByteDataType(), 41, "robloxLocked", None)
    struct.add(ByteDataType(), 42, "reserved1", None)
    struct.add(ByteDataType(), 43, "reserved2", None)
    struct.add(QWordDataType(), 48, "refCount", None)
    
    # Part-specific fields
    struct.add(QWordDataType(), 56, "position", None)  # Vector3
    struct.add(QWordDataType(), 64, "rotation", None)  # Vector3
    struct.add(QWordDataType(), 72, "size", None)      # Vector3
    struct.add(QWordDataType(), 80, "cframe", None)    # CFrame
    struct.add(DWordDataType(), 88, "shape", None)
    struct.add(DWordDataType(), 92, "material", None)
    struct.add(DWordDataType(), 96, "brickColor", None)
    
    dtm = currentProgram.getDataTypeManager()
    existing = dtm.getDataType(struct.getCategoryPath(), struct.getName())
    if existing:
        dtm.replaceDataType(existing, struct, True)
    else:
        dtm.addDataType(struct, None)
    
    print(f"Created structure: {struct.getName()} ({struct.getLength()} bytes)")
    return struct

def create_model_struct():
    """Create the Model structure definition"""
    print("Creating Model structure...")
    
    struct = StructureDataType("Model", 0)
    
    # Instance fields
    struct.add(PointerDataType(), 0, "vtable", None)
    struct.add(PointerDataType(), 8, "className", None)
    struct.add(PointerDataType(), 16, "name", None)
    struct.add(PointerDataType(), 24, "parent", None)
    struct.add(PointerDataType(), 32, "children", None)
    struct.add(ByteDataType(), 40, "archivable", None)
    struct.add(ByteDataType(), 41, "robloxLocked", None)
    struct.add(ByteDataType(), 42, "reserved1", None)
    struct.add(ByteDataType(), 43, "reserved2", None)
    struct.add(QWordDataType(), 48, "refCount", None)
    
    # Model-specific fields
    struct.add(QWordDataType(), 56, "primaryPart", None)
    struct.add(DWordDataType(), 64, "modelScale", None)
    
    dtm = currentProgram.getDataTypeManager()
    existing = dtm.getDataType(struct.getCategoryPath(), struct.getName())
    if existing:
        dtm.replaceDataType(existing, struct, True)
    else:
        dtm.addDataType(struct, None)
    
    print(f"Created structure: {struct.getName()} ({struct.getLength()} bytes)")
    return struct

def apply_struct_to_data():
    """Apply structures to known data locations"""
    print("Applying structures to data...")
    
    # This would require knowing actual addresses
    # Example:
    # addr = toAddr(0x12345678)
    # createData(addr, Instance)
    
    print("Manual application required - use the structure definitions")

def main():
    print("=== Structure Definitions ===")
    print()
    
    create_instance_struct()
    print()
    
    create_part_struct()
    print()
    
    create_model_struct()
    print()
    
    apply_struct_to_data()
    print()
    
    print("=== Structure Definitions Complete ===")

if __name__ == "__main__":
    main()
