#  Orion: Advanced Roblox Internal Dumper

<p align="center">
  <img src="https://img.shields.io/badge/Roblox-Dumper-blue?style=for-the-badge&logo=roblox" alt="Roblox Dumper">
  <img src="https://img.shields.io/badge/Platform-Windows-0078D4?style=for-the-badge&logo=windows" alt="Windows">
  <img src="https://img.shields.io/badge/Language-C++-00599C?style=for-the-badge&logo=c%2B%2B" alt="C++">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="MIT License">
</p>

---

##  Overview

**Orion** is a high-performance internal memory dumper and analysis suite for Roblox. It combines kernel-level memory access, advanced DLL injection techniques, and Ghidra-powered static analysis to provide the most comprehensive data extraction possible.

###  Key Features

- **Kernel-Level Access**: Read/Write memory through a custom driver to bypass common protections.
- **Stealth Injection**: Manual mapping and standard injection methods.
- **Deep Scanning**: Automatic discovery of Luau VM internals, class vtables, and function prologues.
- **Ghidra Integration**: Custom scripts to automatically analyze and define structures in Ghidra.
-  **Massive Offset Discovery**: Capable of finding 47,000+ unique memory references.

---

##  Project Structure

```text
.
├── Kernel/           # Kernel driver for low-level access (is not in use)
├── Injection/        # DLL injection utilities
├── Dumping/          # Roblox structure dumping & scanning
├── Ghidra/           # Ghidra Python analysis scripts
├── Dumps/           # Latest memory offsets and data
└── CMakeLists.txt    # Main build configuration
```

---

##  Latest Offsets (LIVE)

The repository includes a pre-generated `Offsets.hpp` containing the latest discovered addresses for the current Roblox version.

**Current Version:** `LIVE-WindowsPlayer-version-acc4b74f79e743b9`  
**Offsets Found:** `47,061`  
**Last Updated:** `2026-05-01`

> [!TIP]
> You can find the full list of offsets in [Dumps/Offsets.hpp](Dumps/Offsets.hpp). Use the `REBASE` macro to calculate absolute addresses at runtime.

---

##  Installation & Building

### Prerequisites

- Windows 10/11 (x64)
- Visual Studio 2019+
- [CMake 3.15+](https://cmake.org/download/)
- [Windows Driver Kit (WDK)](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk) (Optional, for Kernel Driver)

### Quick Build (User-mode)

```powershell
# Clone and build
git clone https://github.com/USER/orion-dumper.git
cd orion-dumper
cmake -B build
cmake --build build --config Release
```

---

## Usage Guide

### 1. Dumping Offsets
Run the dumper against a running Roblox process to generate a new `Offsets.hpp`:
```bash
./build/Dumping/Release/Dumper.exe <RobloxPID>
```

### 2. Ghidra Analysis
1. Load `RobloxPlayerBeta.exe` into Ghidra.
2. Run `Auto Analysis`.
3. Open the **Script Manager** and run `Ghidra/analyze_roblox.py`.
4. The script will automatically locate functions and define structures based on the dump data.

---

## Disclaimer

This project is for **educational and research purposes only**. Use responsibly and in accordance with Roblox's Terms of Service. The authors are not responsible for any misuse or damages resulting from this tool.

---

<p align="center">
  Developed with ❤️ for the Reverse Engineering Community
</p>

