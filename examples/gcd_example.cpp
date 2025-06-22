//
// Z80 Digital Twin - GCD Algorithm Example
// Demonstrates the Euclidean GCD algorithm running on the Z80 emulator
// with command line interface and proper input validation
//

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <iomanip>
#include "../src/z80_cpu.h"

using namespace z80;

// =============================================================================
// Command Line Parsing and Validation
// =============================================================================

struct GCDInput {
    uint16_t a;
    uint16_t b;
    bool valid;
    std::string error_message;
};

GCDInput parse_arguments(int argc, char* argv[]) {
    GCDInput input = {0, 0, false, ""};
    
    if (argc != 3) {
        input.error_message = "Usage: " + std::string(argv[0]) + " <number1> <number2>\n"
                              "Calculate the Greatest Common Divisor (GCD) of two positive integers.\n"
                              "Both numbers must be between 1 and 65535 (16-bit unsigned integers).";
        return input;
    }
    
    // Parse first number
    char* endptr1;
    long num1 = std::strtol(argv[1], &endptr1, 10);
    if (*endptr1 != '\0' || num1 <= 0 || num1 > 65535) {
        input.error_message = "Error: First number must be a positive integer between 1 and 65535.\n"
                              "Got: " + std::string(argv[1]);
        return input;
    }
    
    // Parse second number
    char* endptr2;
    long num2 = std::strtol(argv[2], &endptr2, 10);
    if (*endptr2 != '\0' || num2 <= 0 || num2 > 65535) {
        input.error_message = "Error: Second number must be a positive integer between 1 and 65535.\n"
                              "Got: " + std::string(argv[2]);
        return input;
    }
    
    input.a = static_cast<uint16_t>(num1);
    input.b = static_cast<uint16_t>(num2);
    input.valid = true;
    
    return input;
}

// =============================================================================
// Z80 GCD Algorithm Implementation
// =============================================================================

class GCDCalculator {
private:
    CPU cpu;
    
    // Z80 Assembly program implementing Euclidean GCD algorithm using subtraction
    // Input: HL = first number, DE = second number
    // Output: HL = GCD result
    std::vector<uint8_t> gcd_program = {
        // main_loop: (0x00)
        0x7A,           // 0x00: LD A, D        ; Check if DE == 0
        0xB3,           // 0x01: OR E           ; 
        0x28, 0x0B,     // 0x02: JR Z, end      ; Jump to end if DE == 0 (offset +11 to 0x0F)
        
        // Compare HL and DE (0x04)
        0xB7,           // 0x04: OR A           ; Clear carry
        0xED, 0x52,     // 0x05: SBC HL, DE     ; HL = HL - DE
        0x30, 0x02,     // 0x07: JR NC, continue ; If HL >= DE, continue (offset +2 to 0x0B)
        
        // HL < DE, so swap them (0x09)
        0x19,           // 0x09: ADD HL, DE     ; Restore HL
        0xEB,           // 0x0A: EX DE, HL      ; Swap HL and DE
        0x18, 0xF3,     // 0x0B: JR main_loop   ; Jump back to start (offset -13 to 0x00)
        
        // continue: HL >= DE and HL = HL - DE (0x0D)
        0x18, 0xF1,     // 0x0D: JR main_loop   ; Jump back to start (offset -15 to 0x00)
        
        // end: (0x0F)
        0x76            // 0x0F: HALT           ; Result in HL
    };
    
public:
    struct GCDResult {
        uint16_t result;
        bool success;
        std::string error_message;
        uint64_t cycles_executed;
        uint32_t iterations;
    };
    
    GCDResult calculate(uint16_t a, uint16_t b) {
        GCDResult result = {0, false, "", 0, 0};
        
        // Reset CPU and load program
        cpu.Reset();
        cpu.LoadProgram(gcd_program, 0x0000);
        
        // Set input parameters in registers
        cpu.HL() = a;  // First number in HL
        cpu.DE() = b;  // Second number in DE
        
        // Execute the program - let it run to natural completion
        uint64_t start_cycles = cpu.GetCycleCount();
        uint32_t iterations = 0;
        
        while (true) {
            uint16_t pc = cpu.PC();
            
            // Check if PC is within program bounds
            if (pc >= gcd_program.size()) {
                result.error_message = "Program counter out of bounds: 0x" + 
                                     std::to_string(pc) + " (max: 0x" + 
                                     std::to_string(gcd_program.size() - 1) + ")";
                return result;
            }
            
            // Check for HALT instruction
            uint8_t opcode = cpu.ReadMemory(pc);
            if (opcode == 0x76) { // HALT
                result.success = true;
                result.result = cpu.HL();
                result.cycles_executed = cpu.GetCycleCount() - start_cycles;
                result.iterations = iterations;
                return result;
            }
            
            // Execute one instruction
            cpu.Step();
            iterations++;
        }
        
        // This should never be reached since HALT will terminate the loop
        result.error_message = "Unexpected end of execution loop";
        return result;
    }
    
    void print_program_disassembly() {
        std::cout << "\nZ80 Assembly Program (GCD Algorithm):\n";
        std::cout << "=====================================\n";
        std::cout << "Address  Opcode   Instruction       Comment\n";
        std::cout << "-------  -------  ----------------  ---------------------------\n";
        std::cout << "0x0000   7A       LD A, D           ; Check if DE == 0\n";
        std::cout << "0x0001   B3       OR E              ; \n";
        std::cout << "0x0002   28 0B    JR Z, +11         ; Jump to end if DE == 0\n";
        std::cout << "0x0004   B7       OR A              ; Clear carry flag\n";
        std::cout << "0x0005   ED 52    SBC HL, DE        ; HL = HL - DE\n";
        std::cout << "0x0007   30 02    JR NC, +2         ; If HL >= DE, continue\n";
        std::cout << "0x0009   19       ADD HL, DE        ; Restore HL (HL < DE case)\n";
        std::cout << "0x000A   EB       EX DE, HL         ; Swap HL and DE\n";
        std::cout << "0x000B   18 F3    JR -13            ; Jump back to main_loop\n";
        std::cout << "0x000D   18 F1    JR -15            ; Jump back to main_loop\n";
        std::cout << "0x000F   76       HALT              ; Result in HL register\n";
        std::cout << "\nAlgorithm: Euclidean GCD using subtraction method\n";
        std::cout << "Input:     HL = first number, DE = second number\n";
        std::cout << "Output:    HL = GCD result\n";
    }
};

// =============================================================================
// Main Program
// =============================================================================

int main(int argc, char* argv[]) {
    std::cout << "Z80 Digital Twin - GCD Calculator\n";
    std::cout << "=================================\n\n";
    
    // Parse command line arguments
    GCDInput input = parse_arguments(argc, argv);
    if (!input.valid) {
        std::cerr << input.error_message << std::endl;
        return 1;
    }
    
    // Display input validation and warnings
    std::cout << "Input Numbers:\n";
    std::cout << "  First number:  " << input.a << " (0x" << std::hex << input.a << std::dec << ")\n";
    std::cout << "  Second number: " << input.b << " (0x" << std::hex << input.b << std::dec << ")\n\n";
    
    // Warning about 16-bit limitations
    std::cout << "⚠️  16-bit Register Limitations:\n";
    std::cout << "   The Z80 CPU uses 16-bit registers, limiting input to 0-65535.\n";
    std::cout << "   For larger numbers, consider using a different algorithm or\n";
    std::cout << "   implementing multi-precision arithmetic.\n\n";
    
    // Create GCD calculator and show the assembly program
    GCDCalculator calculator;
    calculator.print_program_disassembly();
    
    // Execute the GCD calculation
    std::cout << "\nExecuting Z80 Program...\n";
    std::cout << "========================\n";
    
    auto result = calculator.calculate(input.a, input.b);
    
    if (!result.success) {
        std::cerr << "❌ Execution failed: " << result.error_message << std::endl;
        return 1;
    }
    
    // Display results
    std::cout << "✅ Execution completed successfully!\n\n";
    std::cout << "Results:\n";
    std::cout << "--------\n";
    std::cout << "GCD(" << input.a << ", " << input.b << ") = " << result.result << "\n";
    std::cout << "Hexadecimal: 0x" << std::hex << result.result << std::dec << "\n\n";
    
    std::cout << "Performance Statistics:\n";
    std::cout << "-----------------------\n";
    std::cout << "Z80 Cycles Executed: " << result.cycles_executed << "\n";
    std::cout << "Algorithm Iterations: " << result.iterations << "\n";
    
    // Calculate theoretical performance on real Z80
    double mhz_4 = result.cycles_executed / 4000000.0 * 1000000.0;  // 4 MHz Z80
    double mhz_8 = result.cycles_executed / 8000000.0 * 1000000.0;  // 8 MHz Z80
    
    std::cout << "Estimated execution time on real Z80:\n";
    std::cout << "  4 MHz Z80: " << std::fixed << std::setprecision(2) << mhz_4 << " microseconds\n";
    std::cout << "  8 MHz Z80: " << std::fixed << std::setprecision(2) << mhz_8 << " microseconds\n\n";
    
    // Verify result with standard library (for validation)
    uint16_t verification = input.a;
    uint16_t temp = input.b;
    while (temp != 0) {
        uint16_t remainder = verification % temp;
        verification = temp;
        temp = remainder;
    }
    
    if (result.result == verification) {
        std::cout << "✅ Result verified against standard GCD algorithm.\n";
    } else {
        std::cout << "❌ Result verification failed! Expected: " << verification 
                  << ", Got: " << result.result << "\n";
        return 1;
    }
    
    std::cout << "\n🎯 GCD calculation completed successfully using Z80 emulation!\n";
    
    return 0;
}
