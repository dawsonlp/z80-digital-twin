# Z80 Digital Twin - TODO List

## High Priority Development Tasks

### 1. Z80 Assembler Integration
**Goal**: Replace hand-coded byte arrays with proper Z80 assembly source files

## Z80 Assembler Options Review:

### **Top Recommended Assemblers:**

**1. SjASMPlus** ⭐⭐⭐⭐⭐ (RECOMMENDED)
- **GitHub**: z00m128/sjasmplus (402 stars)
- **Features**: Modern, actively maintained, excellent macro support
- **Platforms**: Cross-platform (Windows, Linux, macOS)
- **Syntax**: Enhanced Z80 with modern features
- **Output**: Multiple formats (.bin, .hex, etc.)
- **Why Choose**: Most popular modern Z80 assembler

**2. Pasmo** ⭐⭐⭐⭐
- **Features**: Simple, reliable, portable
- **Platforms**: Cross-platform
- **Syntax**: Standard Z80 assembly
- **Why Choose**: Lightweight, easy integration

**3. z80asm (various implementations)**
- **Features**: Traditional Z80 assembler
- **Platforms**: Unix/Linux focused
- **Syntax**: Classic Z80 assembly

### **VSCode Extensions Available:**

**Essential Extensions:**
1. **"Z80 Macro-Assembler"** by mborik (9.6K downloads, 5⭐)
   - Full language support for Z80 assembly
   - Syntax highlighting, IntelliSense
   - Supports multiple assembler syntaxes

2. **"Z80 Assembly meter"** by theNestruo (4.7K downloads, 5⭐)
   - Real-time cycle counting and bytecode size
   - Perfect for performance optimization

3. **"ASM Code Lens"** by maziac (242K downloads, 5⭐)
   - Code lens, references, hover information
   - Symbol renaming and outline view
   - Works with Z80 assembly

**Debugging Extensions:**
4. **"DeZog"** by maziac (8.7K downloads, 5⭐)
   - Full Z80 debugger for VSCode
   - ZX Spectrum/ZX81 support
   - Breakpoints, step debugging, memory view

5. **"Z80 Instruction Set"** by maziac (5.7K downloads)
   - Hover documentation for Z80 instructions
   - Instant reference while coding

**Development Extensions:**
6. **"Retro Assembler"** by Engine Designs (24K downloads, 5⭐)
   - Multi-CPU assembler (includes Z80)
   - Integrated development environment

### **Recommended Setup:**

**Phase 1 Implementation**:
1. **Assembler**: SjASMPlus (most feature-complete)
2. **VSCode Extensions**:
   - Z80 Macro-Assembler (syntax highlighting)
   - Z80 Assembly meter (performance analysis)
   - ASM Code Lens (navigation)
   - DeZog (debugging)

**Implementation Steps**:
1. Add SjASMPlus as build dependency
2. Install recommended VSCode extensions
3. Create assembly source files (.asm) for examples
4. Modify CMakeLists.txt to invoke SjASMPlus during build
5. Update examples to load assembled binaries instead of byte arrays

### 2. Binary Loading System
**Goal**: Load assembled Z80 binaries into emulator memory

**Requirements**:
- Load binary files at specified memory addresses
- Support multiple file formats (.bin, .hex, Intel HEX, etc.)
- Validate memory bounds and overlaps
- Support loading multiple files (ROM, RAM, etc.)
- Provide debugging information (symbols, labels)

**Implementation Steps**:
1. Create BinaryLoader class
2. Support common Z80 binary formats
3. Add memory mapping configuration
4. Integrate with CPU class LoadProgram method
5. Add command-line options for binary loading

### 3. Development Workflow Enhancement
**Goal**: Streamline Z80 development process

**Features**:
- Assembly source → Binary → Load → Run workflow
- Automatic rebuilding when source changes
- Symbol table integration for debugging
- Disassembly support with original labels
- Memory dump utilities

### 4. Example Assembly Programs
**Goal**: Convert existing examples to proper assembly source

**Tasks**:
- Convert gcd_example.cpp to gcd_example.asm
- Convert gcd_stress_test.cpp to stress_test.asm
- Create additional example programs
- Add comprehensive comments and documentation
- Include build instructions

## Benefits of This Approach

### For Developers:
- **Readable source code**: Standard Z80 assembly syntax
- **Proper toolchain**: Industry-standard assemblers
- **Debugging support**: Symbols and labels preserved
- **Maintainability**: Easier to modify and extend programs

### For the Project:
- **Professional workflow**: Matches real Z80 development
- **Educational value**: Teaches proper Z80 assembly
- **Extensibility**: Easy to add new programs
- **Authenticity**: Uses real Z80 development tools

## Implementation Priority:
1. **Phase 1**: Basic assembler integration and binary loading
2. **Phase 2**: Enhanced development workflow
3. **Phase 3**: Convert all examples to assembly source
4. **Phase 4**: Advanced debugging and analysis tools

## Example Workflow (Target):
```bash
# Assemble Z80 source
z80asm gcd_example.asm -o gcd_example.bin

# Run in Z80 Digital Twin
./z80_emulator --load gcd_example.bin:0x0000 --run
```

This enhancement would transform the Z80 Digital Twin from a proof-of-concept into a practical Z80 development platform!
