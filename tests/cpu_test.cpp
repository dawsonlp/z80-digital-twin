//
// Comprehensive Z80 CPU Test Harness
// Consolidates all test programs with proper error handling and reporting
//

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <iomanip>
#include <cassert>
#include <chrono>
#include "z80_cpu.h"

using namespace z80;

// =============================================================================
// Test Framework
// =============================================================================

class TestFramework {
private:
    struct TestResult {
        std::string name;
        bool passed;
        std::string error_message;
        double execution_time_ms;
    };
    
    std::vector<TestResult> results;
    
public:
    // Test assertion methods
    bool assert_equal_16(uint16_t actual, uint16_t expected, const std::string& description) {
        if (actual == expected) {
            std::cout << "  ✓ " << description << " (0x" << std::hex << actual << ")" << std::endl;
            return true;
        } else {
            std::cout << "  ✗ " << description << " - Expected 0x" << std::hex << expected 
                      << " but got 0x" << actual << std::endl;
            return false;
        }
    }
    
    bool assert_equal_8(uint8_t actual, uint8_t expected, const std::string& description) {
        if (actual == expected) {
            std::cout << "  ✓ " << description << " (0x" << std::hex << (int)actual << ")" << std::endl;
            return true;
        } else {
            std::cout << "  ✗ " << description << " - Expected 0x" << std::hex << (int)expected 
                      << " but got 0x" << (int)actual << std::endl;
            return false;
        }
    }
    
    bool assert_true(bool condition, const std::string& description) {
        if (condition) {
            std::cout << "  ✓ " << description << std::endl;
            return true;
        } else {
            std::cout << "  ✗ " << description << " - Condition failed" << std::endl;
            return false;
        }
    }
    
    // Execute a program until HALT with safety checks
    bool execute_until_halt(CPU& cpu, const std::vector<uint8_t>& program, 
                           uint16_t start_address = 0x0000, int max_cycles = 10000) {
        cpu.Reset();
        cpu.LoadProgram(program, start_address);
        
        int cycles = 0;
        while (cycles < max_cycles) {
            uint16_t pc = cpu.PC();
            
            // Check bounds - PC should be within the loaded program area
            if (pc < start_address || pc >= start_address + program.size()) {
                std::cout << "  ✗ PC out of program bounds: 0x" << std::hex << pc 
                          << " (program: 0x" << start_address << " - 0x" 
                          << (start_address + program.size() - 1) << ")" << std::endl;
                return false;
            }
            
            uint8_t opcode = cpu.ReadMemory(pc);
            if (opcode == 0x76) { // HALT
                return true;
            }
            
            cpu.Step();
            cycles++;
        }
        
        std::cout << "  ✗ Program didn't halt within " << max_cycles << " cycles" << std::endl;
        return false;
    }
    
    // Run a test function and record results
    void run_test(const std::string& test_name, std::function<bool()> test_func) {
        std::cout << "\n=== " << test_name << " ===" << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        bool passed = false;
        std::string error_msg;
        
        try {
            passed = test_func();
        } catch (const std::exception& e) {
            error_msg = e.what();
            std::cout << "  ✗ Exception: " << error_msg << std::endl;
        } catch (...) {
            error_msg = "Unknown exception";
            std::cout << "  ✗ Unknown exception occurred" << std::endl;
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        double time_ms = duration.count() / 1000.0;
        
        results.push_back({test_name, passed, error_msg, time_ms});
        
        if (passed) {
            std::cout << "✅ " << test_name << " PASSED (" << std::fixed << std::setprecision(2) 
                      << time_ms << "ms)" << std::endl;
        } else {
            std::cout << "❌ " << test_name << " FAILED (" << std::fixed << std::setprecision(2) 
                      << time_ms << "ms)" << std::endl;
        }
    }
    
    // Print final summary
    void print_summary() {
        int passed = 0;
        int total = results.size();
        double total_time = 0;
        
        for (const auto& result : results) {
            if (result.passed) passed++;
            total_time += result.execution_time_ms;
        }
        
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "TEST SUMMARY" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        std::cout << "Total Tests: " << total << std::endl;
        std::cout << "Passed:      " << passed << std::endl;
        std::cout << "Failed:      " << (total - passed) << std::endl;
        std::cout << "Success Rate: " << std::fixed << std::setprecision(1) 
                  << (100.0 * passed / total) << "%" << std::endl;
        std::cout << "Total Time:  " << std::fixed << std::setprecision(2) 
                  << total_time << "ms" << std::endl;
        
        if (passed == total) {
            std::cout << "\n🎯 ALL TESTS PASSED! Z80 CPU emulator is working correctly." << std::endl;
        } else {
            std::cout << "\n❌ SOME TESTS FAILED. Details:" << std::endl;
            for (const auto& result : results) {
                if (!result.passed) {
                    std::cout << "  - " << result.name;
                    if (!result.error_message.empty()) {
                        std::cout << ": " << result.error_message;
                    }
                    std::cout << std::endl;
                }
            }
        }
    }
    
    bool all_tests_passed() const {
        for (const auto& result : results) {
            if (!result.passed) return false;
        }
        return true;
    }
};

// =============================================================================
// Individual Test Functions
// =============================================================================

bool test_basic_arithmetic(TestFramework& framework) {
    CPU cpu;
    
    std::vector<uint8_t> program = {
        0x3E, 0x05,     // LD A, 5
        0x06, 0x03,     // LD B, 3  
        0x80,           // ADD A, B
        0x90,           // SUB B
        0x76            // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program)) return false;
    
    bool success = true;
    success &= framework.assert_equal_8(cpu.A(), 0x05, "A = 5 + 3 - 3 = 5");
    success &= framework.assert_equal_8(cpu.B(), 0x03, "B register unchanged");
    
    return success;
}

bool test_hl_operations(TestFramework& framework) {
    CPU cpu;
    
    std::vector<uint8_t> program = {
        0x26, 0x12,     // LD H, 0x12
        0x2E, 0x34,     // LD L, 0x34
        0x7C,           // LD A, H
        0x85,           // ADD A, L
        0x76            // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program)) return false;
    
    bool success = true;
    success &= framework.assert_equal_8(cpu.H(), 0x12, "H register = 0x12");
    success &= framework.assert_equal_8(cpu.L(), 0x34, "L register = 0x34");
    success &= framework.assert_equal_16(cpu.HL(), 0x1234, "HL register pair = 0x1234");
    success &= framework.assert_equal_8(cpu.A(), 0x46, "A = H + L = 0x12 + 0x34 = 0x46");
    
    return success;
}

bool test_memory_operations(TestFramework& framework) {
    CPU cpu;
    
    std::vector<uint8_t> program = {
        0x21, 0x00, 0x80, // LD HL, 0x8000
        0x3E, 0xAB,       // LD A, 0xAB
        0x77,             // LD (HL), A
        0x3E, 0x00,       // LD A, 0
        0x7E,             // LD A, (HL)
        0x76              // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program)) return false;
    
    bool success = true;
    success &= framework.assert_equal_16(cpu.HL(), 0x8000, "HL = 0x8000");
    success &= framework.assert_equal_8(cpu.A(), 0xAB, "A loaded from memory = 0xAB");
    success &= framework.assert_equal_8(cpu.ReadMemory(0x8000), 0xAB, "Memory[0x8000] = 0xAB");
    
    return success;
}

bool test_ix_register_operations(TestFramework& framework) {
    CPU cpu;
    
    std::vector<uint8_t> program = {
        0xDD, 0x21, 0x34, 0x12, // LD IX, 0x1234
        0xDD, 0x7C,             // LD A, IXH
        0x47,                   // LD B, A
        0xDD, 0x7D,             // LD A, IXL  
        0x4F,                   // LD C, A
        0x76                    // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program)) return false;
    
    bool success = true;
    success &= framework.assert_equal_16(cpu.IX(), 0x1234, "IX = 0x1234");
    success &= framework.assert_equal_8(cpu.B(), 0x12, "B = IXH = 0x12");
    success &= framework.assert_equal_8(cpu.C(), 0x34, "C = IXL = 0x34");
    
    return success;
}

bool test_iy_register_operations(TestFramework& framework) {
    CPU cpu;
    
    std::vector<uint8_t> program = {
        0xFD, 0x21, 0x78, 0x56, // LD IY, 0x5678
        0xFD, 0x7C,             // LD A, IYH
        0x47,                   // LD B, A
        0xFD, 0x7D,             // LD A, IYL
        0x4F,                   // LD C, A
        0x76                    // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program)) return false;
    
    bool success = true;
    success &= framework.assert_equal_16(cpu.IY(), 0x5678, "IY = 0x5678");
    success &= framework.assert_equal_8(cpu.B(), 0x56, "B = IYH = 0x56");
    success &= framework.assert_equal_8(cpu.C(), 0x78, "C = IYL = 0x78");
    
    return success;
}

bool test_ddcb_register_behavior(TestFramework& framework) {
    CPU cpu;
    cpu.Reset();
    
    // Set up test scenario
    cpu.IX() = 0x2000;
    cpu.HL() = 0x1234;  // Different from IX to verify behavior
    cpu.WriteMemory(0x2005, 0x81);  // 10000001 binary
    
    std::vector<uint8_t> program = {
        0xDD, 0xCB, 0x05, 0x05  // DD CB 05 05 = RLC (IX+5) -> L
    };
    
    cpu.LoadProgram(program, 0x0000);
    
    // Execute the DD CB instruction sequence
    cpu.Step(); // DD prefix
    cpu.Step(); // CB prefix  
    cpu.Step(); // displacement + CB opcode
    
    bool success = true;
    success &= framework.assert_equal_8(cpu.L(), 0x03, "L register = 0x03 (rotated 0x81)");
    success &= framework.assert_equal_8(cpu.ReadMemory(0x2005), 0x03, "Memory[0x2005] = 0x03");
    success &= framework.assert_equal_8(cpu.H(), 0x12, "H register unchanged");
    success &= framework.assert_equal_16(cpu.IX(), 0x2000, "IX register unchanged");
    
    return success;
}

bool test_prefix_state_isolation(TestFramework& framework) {
    CPU cpu;
    
    std::vector<uint8_t> program = {
        // Set IX and IY to different values
        0xDD, 0x21, 0xAA, 0xBB, // LD IX, 0xBBAA
        0xFD, 0x21, 0xCC, 0xDD, // LD IY, 0xDDCC
        
        // Normal HL operations should use HL, not IX/IY
        0x26, 0x11,             // LD H, 0x11
        0x2E, 0x22,             // LD L, 0x22
        0x7C,                   // LD A, H
        0x85,                   // ADD A, L
        
        0x76                    // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program)) return false;
    
    bool success = true;
    success &= framework.assert_equal_16(cpu.IX(), 0xBBAA, "IX unchanged = 0xBBAA");
    success &= framework.assert_equal_16(cpu.IY(), 0xDDCC, "IY unchanged = 0xDDCC");
    success &= framework.assert_equal_16(cpu.HL(), 0x1122, "HL = 0x1122 (normal operation)");
    success &= framework.assert_equal_8(cpu.A(), 0x33, "A = H + L = 0x11 + 0x22 = 0x33");
    
    return success;
}

bool test_simple_gcd_algorithm(TestFramework& framework) {
    CPU cpu;
    
    // Simple GCD using subtraction method
    std::vector<uint8_t> program = {
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
    
    // Helper function to run GCD test
    auto run_gcd_test = [&](uint16_t a, uint16_t b, uint16_t expected, const std::string& description) -> bool {
        cpu.Reset();
        cpu.LoadProgram(program, 0x0000);
        cpu.HL() = a;
        cpu.DE() = b;
        
        // Execute with higher cycle limit for larger numbers
        int cycles = 0;
        int max_cycles = (a > 1000 || b > 1000) ? 50000 : 5000;
        
        while (cycles < max_cycles) {
            uint16_t pc = cpu.PC();
            
            if (pc >= program.size()) {
                std::cout << "  ✗ PC out of program bounds: 0x" << std::hex << pc << std::endl;
                return false;
            }
            
            uint8_t opcode = cpu.ReadMemory(pc);
            if (opcode == 0x76) { // HALT
                break;
            }
            
            cpu.Step();
            cycles++;
        }
        
        if (cycles >= max_cycles) {
            std::cout << "  ✗ Program didn't halt within " << max_cycles << " cycles for " << description << std::endl;
            return false;
        }
        
        return framework.assert_equal_16(cpu.HL(), expected, description);
    };
    
    bool success = true;
    
    // Basic test cases
    success &= run_gcd_test(6, 4, 2, "GCD(6, 4) = 2");
    success &= run_gcd_test(12, 8, 4, "GCD(12, 8) = 4");
    success &= run_gcd_test(15, 25, 5, "GCD(15, 25) = 5");
    
    // Prime number tests (GCD should be 1)
    success &= run_gcd_test(17, 19, 1, "GCD(17, 19) = 1 (both prime)");
    success &= run_gcd_test(23, 29, 1, "GCD(23, 29) = 1 (both prime)");
    success &= run_gcd_test(13, 21, 1, "GCD(13, 21) = 1 (13 prime, 21 composite)");
    success &= run_gcd_test(31, 77, 1, "GCD(31, 77) = 1 (31 prime, 77 = 7×11)");
    
    // Larger composite numbers
    success &= run_gcd_test(48, 18, 6, "GCD(48, 18) = 6 (48 = 2⁴×3, 18 = 2×3²)");
    success &= run_gcd_test(60, 48, 12, "GCD(60, 48) = 12 (60 = 2²×3×5, 48 = 2⁴×3)");
    success &= run_gcd_test(84, 36, 12, "GCD(84, 36) = 12 (84 = 2²×3×7, 36 = 2²×3²)");
    success &= run_gcd_test(105, 91, 7, "GCD(105, 91) = 7 (105 = 3×5×7, 91 = 7×13)");
    
    // Larger numbers (stress test)
    success &= run_gcd_test(252, 198, 18, "GCD(252, 198) = 18 (252 = 2²×3²×7, 198 = 2×3²×11)");
    success &= run_gcd_test(1071, 462, 21, "GCD(1071, 462) = 21 (1071 = 3²×7×17, 462 = 2×3×7×11)");
    
    // Edge cases
    success &= run_gcd_test(100, 1, 1, "GCD(100, 1) = 1");
    success &= run_gcd_test(144, 144, 144, "GCD(144, 144) = 144 (identical numbers)");
    success &= run_gcd_test(1024, 512, 512, "GCD(1024, 512) = 512 (powers of 2)");
    
    return success;
}

bool test_flag_operations(TestFramework& framework) {
    CPU cpu;
    
    std::vector<uint8_t> program = {
        0x3E, 0xFF,     // LD A, 0xFF
        0x3C,           // INC A          ; Should set zero flag
        0x3E, 0x7F,     // LD A, 0x7F
        0x3C,           // INC A          ; Should set sign flag and overflow
        0x76            // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program)) return false;
    
    bool success = true;
    success &= framework.assert_equal_8(cpu.A(), 0x80, "A = 0x80 after INC 0x7F");
    success &= framework.assert_true((cpu.F() & 0x80) != 0, "Sign flag set");
    success &= framework.assert_true((cpu.F() & 0x04) != 0, "Overflow flag set");
    
    return success;
}

bool test_ed_instructions(TestFramework& framework) {
    CPU cpu;
    
    // Test 1: SBC HL, DE (no carry)
    std::vector<uint8_t> program1 = {
        0x21, 0x00, 0x10, // LD HL, 0x1000
        0x11, 0x00, 0x05, // LD DE, 0x0500
        0xB7,             // OR A (clear carry)
        0xED, 0x52,       // SBC HL, DE
        0x76              // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program1)) return false;
    
    bool success = true;
    success &= framework.assert_equal_16(cpu.HL(), 0x0B00, "SBC HL, DE (no carry): 0x1000 - 0x0500 = 0x0B00");
    
    // Test 2: SBC HL, DE (with carry)
    std::vector<uint8_t> program2 = {
        0x21, 0x00, 0x10, // LD HL, 0x1000
        0x11, 0x00, 0x05, // LD DE, 0x0500
        0x37,             // SCF (set carry)
        0xED, 0x52,       // SBC HL, DE
        0x76              // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program2)) return false;
    
    success &= framework.assert_equal_16(cpu.HL(), 0x0AFF, "SBC HL, DE (with carry): 0x1000 - 0x0500 - 1 = 0x0AFF");
    
    // Test 3: Flag setting for zero result
    std::vector<uint8_t> program3 = {
        0x21, 0x00, 0x00, // LD HL, 0x0000
        0x11, 0x00, 0x00, // LD DE, 0x0000
        0xB7,             // OR A (clear carry)
        0xED, 0x52,       // SBC HL, DE
        0x76              // HALT
    };
    
    if (!framework.execute_until_halt(cpu, program3)) return false;
    
    success &= framework.assert_equal_16(cpu.HL(), 0x0000, "SBC HL, DE zero result");
    success &= framework.assert_true((cpu.F() & 0x40) != 0, "Zero flag set for zero result");
    success &= framework.assert_true((cpu.F() & 0x02) != 0, "N flag set for subtraction");
    
    return success;
}

// =============================================================================
// Main Test Runner
// =============================================================================

int main() {
    std::cout << "Z80 CPU Comprehensive Test Harness" << std::endl;
    std::cout << "===================================" << std::endl;
    
    TestFramework framework;
    
    // Run all tests
    framework.run_test("Basic Arithmetic Operations", 
                      [&]() { return test_basic_arithmetic(framework); });
    
    framework.run_test("H/L Register Operations", 
                      [&]() { return test_hl_operations(framework); });
    
    framework.run_test("Memory Operations", 
                      [&]() { return test_memory_operations(framework); });
    
    framework.run_test("IX Register Operations", 
                      [&]() { return test_ix_register_operations(framework); });
    
    framework.run_test("IY Register Operations", 
                      [&]() { return test_iy_register_operations(framework); });
    
    framework.run_test("DD CB Register Behavior", 
                      [&]() { return test_ddcb_register_behavior(framework); });
    
    framework.run_test("Prefix State Isolation", 
                      [&]() { return test_prefix_state_isolation(framework); });
    
    framework.run_test("Simple GCD Algorithm", 
                      [&]() { return test_simple_gcd_algorithm(framework); });
    
    framework.run_test("Flag Operations", 
                      [&]() { return test_flag_operations(framework); });
    
    framework.run_test("ED Instructions", 
                      [&]() { return test_ed_instructions(framework); });
    
    // Print final summary
    framework.print_summary();
    
    return framework.all_tests_passed() ? 0 : 1;
}
