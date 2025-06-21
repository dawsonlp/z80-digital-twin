# Z80 Digital Twin - Example Programs

This directory contains example programs that demonstrate the capabilities of the Z80 Digital Twin emulator.

## GCD Calculator Example

The `gcd_example` program demonstrates the implementation of the Euclidean GCD (Greatest Common Divisor) algorithm running on the Z80 emulator.

### Building

The example is built automatically when you build the main project:

```bash
make
```

This creates the `gcd_example` executable in the project root directory.

### Usage

```bash
./gcd_example <number1> <number2>
```

**Parameters:**
- `number1`: First positive integer (1-65535)
- `number2`: Second positive integer (1-65535)

**Example:**
```bash
./gcd_example 48 18
```

### Features

#### Algorithm Implementation
- **Pure Z80 Assembly**: The GCD algorithm is implemented in Z80 machine code
- **Euclidean Method**: Uses the subtraction-based Euclidean algorithm
- **Register Usage**: 
  - Input: HL register (first number), DE register (second number)
  - Output: HL register (GCD result)

#### Program Features
- **Command Line Interface**: Easy-to-use command line with proper argument validation
- **Input Validation**: Checks for valid 16-bit unsigned integers (1-65535)
- **16-bit Limitations Warning**: Alerts users about Z80 register size constraints
- **Assembly Disassembly**: Shows the actual Z80 assembly code being executed
- **Performance Metrics**: Reports cycle count, iterations, and estimated execution time
- **Result Verification**: Validates results against standard library GCD implementation

#### Error Handling
- Invalid argument count
- Non-numeric inputs
- Numbers outside valid range (1-65535)
- Program execution safety limits

### Sample Output

```
Z80 Digital Twin - GCD Calculator
=================================

Input Numbers:
  First number:  48 (0x30)
  Second number: 18 (0x12)

⚠️  16-bit Register Limitations:
   The Z80 CPU uses 16-bit registers, limiting input to 0-65535.
   For larger numbers, consider using a different algorithm or
   implementing multi-precision arithmetic.

Z80 Assembly Program (GCD Algorithm):
=====================================
Address  Opcode   Instruction       Comment
-------  -------  ----------------  ---------------------------
0x0000   7A       LD A, D           ; Check if DE == 0
0x0001   B3       OR E              ; 
0x0002   28 0B    JR Z, +11         ; Jump to end if DE == 0
0x0004   B7       OR A              ; Clear carry flag
0x0005   ED 52    SBC HL, DE        ; HL = HL - DE
0x0007   30 02    JR NC, +2         ; If HL >= DE, continue
0x0009   19       ADD HL, DE        ; Restore HL (HL < DE case)
0x000A   EB       EX DE, HL         ; Swap HL and DE
0x000B   18 F3    JR -13            ; Jump back to main_loop
0x000D   18 F1    JR -15            ; Jump back to main_loop
0x000F   76       HALT              ; Result in HL register

Algorithm: Euclidean GCD using subtraction method
Input:     HL = first number, DE = second number
Output:    HL = GCD result

Executing Z80 Program...
========================
✅ Execution completed successfully!

Results:
--------
GCD(48, 18) = 6
Hexadecimal: 0x6

Performance Statistics:
-----------------------
Z80 Cycles Executed: 546
Algorithm Iterations: 73
Estimated execution time on real Z80:
  4 MHz Z80: 136.50 microseconds
  8 MHz Z80: 68.25 microseconds

✅ Result verified against standard GCD algorithm.

🎯 GCD calculation completed successfully using Z80 emulation!
```

### Algorithm Details

The GCD algorithm uses the subtraction-based Euclidean method:

1. **Input Check**: If the second number (DE) is zero, the first number (HL) is the GCD
2. **Subtraction**: Subtract the smaller number from the larger number
3. **Swap**: If HL < DE after subtraction, restore HL and swap the registers
4. **Repeat**: Continue until one number becomes zero
5. **Result**: The remaining non-zero number is the GCD

This method is well-suited for the Z80's instruction set, using:
- `SBC HL, DE` for 16-bit subtraction with carry
- `EX DE, HL` for efficient register swapping
- Conditional jumps for control flow

### Test Cases

The program has been tested with various inputs:

- **Basic cases**: GCD(48, 18) = 6, GCD(12, 8) = 4
- **Prime numbers**: GCD(17, 19) = 1, GCD(23, 29) = 1
- **Large numbers**: GCD(1071, 462) = 21, GCD(252, 198) = 18
- **Edge cases**: GCD(100, 1) = 1, GCD(144, 144) = 144

### Educational Value

This example demonstrates:
- **Z80 Programming**: Real Z80 assembly language programming
- **Algorithm Implementation**: Converting mathematical algorithms to assembly
- **Register Management**: Efficient use of Z80 registers
- **Performance Analysis**: Understanding cycle counts and execution time
- **Emulation Accuracy**: Verification against known-good implementations

### Limitations

- **16-bit Range**: Limited to numbers 1-65535 due to Z80 register size
- **Subtraction Method**: Uses subtraction instead of modulo (division) for simplicity
- **Performance**: Subtraction method can be slower for large numbers with small GCDs

For larger numbers or better performance, consider implementing:
- Multi-precision arithmetic
- Division-based Euclidean algorithm
- Binary GCD algorithm (Stein's algorithm)
