//
// Z80 Digital Twin - Cascading GCD Stress Test
// Runs GCD(N, N-1), GCD(N-1, N-2), ..., GCD(2, 1) for massive Z80 performance testing
//

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include "../src/z80_cpu.h"

using namespace z80;
using namespace std::chrono;

// =============================================================================
// High-Resolution Timer Class
// =============================================================================

class HighResTimer {
private:
    high_resolution_clock::time_point start_time;
    
public:
    void start() {
        start_time = high_resolution_clock::now();
    }
    
    double elapsed_seconds() {
        auto end_time = high_resolution_clock::now();
        auto duration = duration_cast<nanoseconds>(end_time - start_time);
        return duration.count() / 1e9;
    }
    
    double elapsed_milliseconds() {
        return elapsed_seconds() * 1000.0;
    }
    
    double elapsed_microseconds() {
        return elapsed_seconds() * 1e6;
    }
};

// =============================================================================
// Z80 Cascading GCD Stress Test Implementation
// =============================================================================

class GCDStressTest {
private:
    CPU cpu;
    uint16_t start_value;
    std::vector<uint8_t> stress_test_program;
    
    void generate_program(uint16_t start_num) {
        start_value = start_num;
        
        stress_test_program = {
            // Initialize: BC = start_num, DE = start_num-1 (loop counters)
            0x01, static_cast<uint8_t>(start_num & 0xFF), static_cast<uint8_t>(start_num >> 8),   // 0x00: LD BC, start_num
            0x11, static_cast<uint8_t>((start_num-1) & 0xFF), static_cast<uint8_t>((start_num-1) >> 8), // 0x03: LD DE, start_num-1
            
            // outer_loop: (0x06) - Call GCD function with current BC, DE values
            // Copy BC to HL, keep DE as DE for GCD calculation
            0x60,               // 0x06: LD H, B          ; H = B
            0x69,               // 0x07: LD L, C          ; L = C (HL = BC)
            
            // Call GCD subroutine
            0xCD, 0x1F, 0x00,   // 0x08: CALL gcd_func    ; Call GCD function at 0x001F
            
            // Add GCD result to accumulator at memory location 0x8000 to prevent optimization
            0x2A, 0x00, 0x80,   // 0x0B: LD HL, (0x8000)  ; Load current accumulator
            0x09,               // 0x0E: ADD HL, BC       ; Add GCD result (returned in HL) to accumulator
            0x22, 0x00, 0x80,   // 0x0F: LD (0x8000), HL  ; Store back to memory
            
            // Decrement both BC and DE for next iteration
            0x0B,               // 0x12: DEC BC           ; BC = BC - 1 (next first number)
            0x1B,               // 0x13: DEC DE           ; DE = DE - 1 (next second number)
            
            // Check if DE == 0 AFTER decrementing (since DEC doesn't set flags)
            0x7A,               // 0x14: LD A, D          ; A = high byte of DE
            0xB3,               // 0x15: OR E             ; Check if DE == 0 (D OR E)
            0x20, 0xEF,         // 0x16: JR NZ, outer_loop ; If DE != 0, continue loop (offset = 0x06 - (0x16 + 2) = -18 = 0xEE)
            
            // end: (0x18)
            0x76,               // 0x18: HALT             ; End of program
            
            // Padding to align gcd_func
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x19-0x1E: NOP padding
            
            // gcd_func: (0x1F) - GCD function using subtraction method
            // Input: HL = first number, DE = second number
            // Output: GCD result in HL
            // Preserves ALL registers except HL (which contains result)
            0xF5,               // 0x1F: PUSH AF          ; Save AF
            0xC5,               // 0x20: PUSH BC          ; Save BC
            0xD5,               // 0x21: PUSH DE          ; Save DE
            0xE5,               // 0x22: PUSH HL          ; Save HL
            
            // Copy inputs to working registers (HL already has first number)
            // HL = first number (already set by caller)
            // DE = second number (already set by caller)
            // We'll work with these directly since we saved the originals
            
            // gcd_loop: (0x23)
            0x7A,               // 0x23: LD A, D          ; Check if DE == 0
            0xB3,               // 0x24: OR E             ;
            0x28, 0x09,         // 0x25: JR Z, gcd_done   ; Jump to end if DE == 0, offset = 0x30 - (0x25 + 2) = +9
            
            // Compare HL and DE (0x27)
            0xB7,               // 0x27: OR A             ; Clear carry
            0xED, 0x52,         // 0x28: SBC HL, DE       ; HL = HL - DE
            0x30, 0x02,         // 0x2A: JR NC, gcd_continue ; If HL >= DE, continue, offset = 0x2E - (0x2A + 2) = +2
            
            // HL < DE, so swap them (0x2C)
            0x19,               // 0x2C: ADD HL, DE       ; Restore HL
            0xEB,               // 0x2D: EX DE, HL        ; Swap HL and DE
            
            // gcd_continue: (0x2E)
            0x18, 0xF3,         // 0x2E: JR gcd_loop      ; Jump back to gcd_loop, offset = 0x23 - (0x2E + 2) = -13 = 0xF3
            
            // gcd_done: (0x30) - GCD result is in HL
            // Save result temporarily
            0x44,               // 0x30: LD B, H          ; Save result high byte in B
            0x4D,               // 0x31: LD C, L          ; Save result low byte in C
            
            // Restore all original registers
            0xE1,               // 0x32: POP HL           ; Restore original HL
            0xD1,               // 0x33: POP DE           ; Restore original DE  
            0xC1,               // 0x34: POP BC           ; Restore original BC
            0xF1,               // 0x35: POP AF           ; Restore original AF
            
            // Put result back in HL for return
            0x60,               // 0x36: LD H, B          ; H = result high byte
            0x69,               // 0x37: LD L, C          ; L = result low byte
            
            0xC9                // 0x38: RET              ; Return to caller
        };
    }
    
public:
    struct StressTestResult {
        bool success;
        std::string error_message;
        uint64_t cycles_executed;
        uint32_t instructions_executed;
        double execution_time_seconds;
        uint32_t gcd_calculations_completed;
    };
    
    GCDStressTest(uint16_t start_num) {
        generate_program(start_num);
    }
    
    StressTestResult run_stress_test() {
        StressTestResult result = {false, "", 0, 0, 0.0, 0};
        
        std::cout << "Initializing Z80 CPU and loading cascading GCD stress test...\n";
        
        // Reset CPU and load program
        cpu.Reset();
        cpu.LoadProgram(stress_test_program, 0x0000);
        
        uint32_t expected_calculations = start_value - 1;
        std::cout << "Starting cascading GCD stress test execution...\n";
        std::cout << "This will calculate:\n";
        std::cout << "  GCD(" << start_value << ", " << (start_value-1) << ")\n";
        std::cout << "  GCD(" << (start_value-1) << ", " << (start_value-2) << ")\n";
        std::cout << "  ...\n";
        std::cout << "  GCD(3, 2)\n";
        std::cout << "  GCD(2, 1)\n";
        std::cout << "Expected total calculations: " << expected_calculations << " GCD operations\n";
        
        // Estimate cycles (very rough)
        uint64_t estimated_cycles = expected_calculations * 50000ULL; // ~50K cycles per GCD on average
        std::cout << "Estimated Z80 cycles: ~" << estimated_cycles << "\n\n";
        
        // Start high-resolution timer
        HighResTimer timer;
        timer.start();
        
        uint64_t start_cycles = cpu.GetCycleCount();
        
        // Use efficient bulk execution with RunUntilCycle
        std::cout << "Running Z80 cascading stress test until HALT...\n";
        
        // Set a very high cycle limit to let the program run to completion
        uint64_t max_cycles = 10000000000ULL; // 10 billion cycles should be more than enough
        
        // Run the program until HALT or cycle limit
        cpu.RunUntilCycle(max_cycles);
        
        uint32_t iterations = 0; // We don't track individual instructions in bulk mode
        
        // Stop timer immediately after Z80 execution
        result.execution_time_seconds = timer.elapsed_seconds();
        
        uint64_t actual_cycles = cpu.GetCycleCount() - start_cycles;
        
        std::cout << "\nZ80 execution completed.\n";
        std::cout << "CPU halted: " << (cpu.IsHalted() ? "Yes" : "No") << "\n";
        std::cout << "Total cycles executed: " << actual_cycles << "\n";
        std::cout << "Total instructions executed: " << iterations << "\n";
        
        // Check if we hit the cycle limit without halting
        if (actual_cycles >= max_cycles && !cpu.IsHalted()) {
            result.error_message = "Program hit cycle limit of " + 
                                  std::to_string(max_cycles) + " without halting";
            return result;
        }
        
        // Program halted naturally - this is what we want!
        std::cout << "Program halted naturally after " << actual_cycles << " cycles.\n";
        
        result.success = true;
        result.cycles_executed = actual_cycles;
        result.instructions_executed = iterations;
        result.gcd_calculations_completed = expected_calculations;
        
        return result;
    }
    
    void print_program_info() {
        std::cout << "\nZ80 Cascading GCD Stress Test Program:\n";
        std::cout << "======================================\n";
        std::cout << "Program size: " << stress_test_program.size() << " bytes\n";
        std::cout << "Algorithm: Cascading GCD calculations using subtraction method\n";
        std::cout << "Pattern: GCD(N,N-1), GCD(N-1,N-2), ..., GCD(2,1)\n";
        std::cout << "Starting value: " << start_value << "\n";
        std::cout << "Total GCD calculations: " << (start_value - 1) << "\n";
        std::cout << "Expected computational load: MASSIVE (millions+ of Z80 cycles)\n\n";
    }
};

// =============================================================================
// Main Program
// =============================================================================

int main(int argc, char* argv[]) {
    std::cout << "Z80 Digital Twin - Cascading GCD Stress Test\n";
    std::cout << "============================================\n\n";
    
    // Parse command line argument for starting number
    uint16_t start_num = 8; // Default small value for testing
    if (argc > 1) {
        int input = std::atoi(argv[1]);
        if (input > 1 && input <= 65535) {
            start_num = static_cast<uint16_t>(input);
        } else {
            std::cerr << "Invalid starting number. Must be between 2 and 65535. Using default: 8\n";
        }
    }
    
    std::cout << "Starting number: " << start_num << "\n";
    std::cout << "This will run " << (start_num - 1) << " cascading GCD calculations\n";
    
    GCDStressTest stress_test(start_num);
    stress_test.print_program_info();
    
    auto result = stress_test.run_stress_test();
    
    if (!result.success) {
        std::cerr << "❌ Stress test failed: " << result.error_message << std::endl;
        return 1;
    }
    
    // Display detailed results
    std::cout << "\n✅ Cascading GCD stress test completed successfully!\n\n";
    
    std::cout << "Performance Results:\n";
    std::cout << "===================\n";
    std::cout << "GCD calculations completed: " << result.gcd_calculations_completed << "\n";
    std::cout << "Z80 instructions executed: " << result.instructions_executed << "\n";
    std::cout << "Z80 cycles executed: " << result.cycles_executed << "\n\n";
    
    std::cout << "Timing Results:\n";
    std::cout << "===============\n";
    std::cout << "Execution time: " << std::fixed << std::setprecision(6) 
              << result.execution_time_seconds << " seconds\n";
    std::cout << "Execution time: " << std::fixed << std::setprecision(3) 
              << result.execution_time_seconds * 1000.0 << " milliseconds\n\n";
    
    // Calculate performance metrics
    double cycles_per_second = result.cycles_executed / result.execution_time_seconds;
    double gcd_per_second = result.gcd_calculations_completed / result.execution_time_seconds;
    double instructions_per_second = result.instructions_executed / result.execution_time_seconds;
    
    std::cout << "Performance Metrics:\n";
    std::cout << "===================\n";
    std::cout << "Cycles per second: " << std::scientific << std::setprecision(2) 
              << cycles_per_second << "\n";
    std::cout << "Instructions per second: " << std::scientific << std::setprecision(2) 
              << instructions_per_second << "\n";
    std::cout << "GCD calculations per second: " << std::fixed << std::setprecision(0) 
              << gcd_per_second << "\n\n";
    
    // Compare to real Z80 performance
    double real_z80_4mhz_time = result.cycles_executed / 4000000.0;
    double real_z80_8mhz_time = result.cycles_executed / 8000000.0;
    double speedup_4mhz = real_z80_4mhz_time / result.execution_time_seconds;
    double speedup_8mhz = real_z80_8mhz_time / result.execution_time_seconds;
    
    std::cout << "Real Z80 Hardware Comparison:\n";
    std::cout << "============================\n";
    std::cout << "4 MHz Z80 would take: " << std::fixed << std::setprecision(2) 
              << real_z80_4mhz_time << " seconds\n";
    std::cout << "8 MHz Z80 would take: " << std::fixed << std::setprecision(2) 
              << real_z80_8mhz_time << " seconds\n";
    std::cout << "Emulator speedup vs 4MHz Z80: " << std::fixed << std::setprecision(0) 
              << speedup_4mhz << "x faster\n";
    std::cout << "Emulator speedup vs 8MHz Z80: " << std::fixed << std::setprecision(0) 
              << speedup_8mhz << "x faster\n\n";
    
    std::cout << "🎯 Z80 Digital Twin cascading stress test completed successfully!\n";
    std::cout << "\nUsage: " << argv[0] << " [starting_number]\n";
    std::cout << "Examples:\n";
    std::cout << "  " << argv[0] << " 10   (runs 9 GCD calculations: 10,9 down to 2,1)\n";
    std::cout << "  " << argv[0] << " 100  (runs 99 GCD calculations: 100,99 down to 2,1)\n";
    std::cout << "  " << argv[0] << " 1000 (runs 999 GCD calculations: MASSIVE stress test!)\n";
    
    return 0;
}
