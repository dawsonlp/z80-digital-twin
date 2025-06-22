//
// Z80 Digital Twin - Performance Benchmark Suite
// Comprehensive performance testing with multiple Z80 programs
// Demonstrates real-world CPU emulation performance characteristics
//

#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <string>
#include <algorithm>
#include <numeric>
#include <cmath>
#include "../src/z80_cpu.h"

using namespace z80;

// =============================================================================
// Performance Test Framework
// =============================================================================

class PerformanceBenchmark {
private:
    struct TestResult {
        std::string test_name;
        double execution_time_ms;
        uint64_t cycles_executed;
        uint32_t iterations_completed;
        double mhz_equivalent;
        bool success;
        std::string error_message;
    };
    
    std::vector<TestResult> results;
    
public:
    struct BenchmarkConfig {
        int test_iterations = 100;
        int max_cpu_cycles = 1000000;
        bool verbose_output = false;
        bool show_progress = true;
    };
    
    TestResult execute_benchmark(const std::string& test_name,
                               const std::vector<uint8_t>& program,
                               const BenchmarkConfig& config = {}) {
        TestResult result = {};
        result.test_name = test_name;
        result.success = false;
        
        if (config.show_progress) {
            std::cout << "Running: " << test_name << " (" << config.test_iterations << " iterations)";
            std::cout.flush();
        }
        
        CPU cpu;
        uint64_t total_cycles = 0;
        uint32_t successful_iterations = 0;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < config.test_iterations; ++i) {
            // Reset CPU and load program
            cpu.Reset();
            cpu.LoadProgram(program, 0x0000);
            
            uint64_t start_cycles = cpu.GetCycleCount();
            int cycles = 0;
            bool completed = false;
            
            // Execute program
            while (cycles < config.max_cpu_cycles) {
                uint16_t pc = cpu.PC();
                
                // Check bounds
                if (pc >= program.size()) {
                    result.error_message = "Program counter out of bounds";
                    break;
                }
                
                // Check for HALT
                uint8_t opcode = cpu.ReadMemory(pc);
                if (opcode == 0x76) { // HALT
                    completed = true;
                    break;
                }
                
                cpu.Step();
                cycles++;
            }
            
            if (completed) {
                total_cycles += (cpu.GetCycleCount() - start_cycles);
                successful_iterations++;
            } else if (result.error_message.empty()) {
                result.error_message = "Program timeout or execution error";
            }
            
            // Progress indicator
            if (config.show_progress && (i + 1) % (config.test_iterations / 10) == 0) {
                std::cout << ".";
                std::cout.flush();
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        
        if (successful_iterations > 0) {
            result.success = true;
            result.execution_time_ms = duration.count() / 1000000.0;
            result.cycles_executed = total_cycles;
            result.iterations_completed = successful_iterations;
            
            // Calculate equivalent MHz (cycles per second)
            double cycles_per_second = (total_cycles / (result.execution_time_ms / 1000.0));
            result.mhz_equivalent = cycles_per_second / 1000000.0;
        }
        
        if (config.show_progress) {
            std::cout << " ";
            if (result.success) {
                std::cout << "✅\n";
            } else {
                std::cout << "❌\n";
            }
        }
        
        results.push_back(result);
        return result;
    }
    
    void print_detailed_results() const {
        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "Z80 DIGITAL TWIN - PERFORMANCE BENCHMARK RESULTS\n";
        std::cout << std::string(80, '=') << "\n\n";
        
        // Header
        std::cout << std::left << std::setw(25) << "Test Name"
                  << std::right << std::setw(12) << "Time (ms)"
                  << std::setw(12) << "Cycles"
                  << std::setw(10) << "MHz Equiv"
                  << std::setw(12) << "Iterations"
                  << std::setw(9) << "Status" << "\n";
        std::cout << std::string(80, '-') << "\n";
        
        double total_time = 0;
        uint64_t total_cycles = 0;
        int successful_tests = 0;
        
        for (const auto& result : results) {
            std::cout << std::left << std::setw(25) << result.test_name;
            
            if (result.success) {
                std::cout << std::right << std::fixed << std::setprecision(2)
                          << std::setw(12) << result.execution_time_ms
                          << std::setw(12) << result.cycles_executed
                          << std::setw(10) << result.mhz_equivalent
                          << std::setw(12) << result.iterations_completed
                          << std::setw(9) << "PASS";
                
                total_time += result.execution_time_ms;
                total_cycles += result.cycles_executed;
                successful_tests++;
            } else {
                std::cout << std::right << std::setw(12) << "FAILED"
                          << std::setw(12) << "-"
                          << std::setw(10) << "-"
                          << std::setw(12) << "-"
                          << std::setw(9) << "FAIL";
            }
            std::cout << "\n";
        }
        
        // Summary
        std::cout << std::string(80, '-') << "\n";
        if (successful_tests > 0) {
            double avg_mhz = (total_cycles / (total_time / 1000.0)) / 1000000.0;
            std::cout << std::left << std::setw(25) << "SUMMARY"
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(12) << total_time
                      << std::setw(12) << total_cycles
                      << std::setw(10) << avg_mhz
                      << std::setw(12) << successful_tests
                      << std::setw(9) << "TOTAL" << "\n";
        }
        
        std::cout << "\n";
    }
    
    void print_performance_analysis() const {
        std::cout << "PERFORMANCE ANALYSIS\n";
        std::cout << std::string(40, '=') << "\n";
        
        if (results.empty()) {
            std::cout << "No benchmark results available.\n";
            return;
        }
        
        // Calculate statistics
        std::vector<double> mhz_values;
        for (const auto& result : results) {
            if (result.success) {
                mhz_values.push_back(result.mhz_equivalent);
            }
        }
        
        if (mhz_values.empty()) {
            std::cout << "No successful benchmark results for analysis.\n";
            return;
        }
        
        double avg_mhz = std::accumulate(mhz_values.begin(), mhz_values.end(), 0.0) / mhz_values.size();
        double min_mhz = *std::min_element(mhz_values.begin(), mhz_values.end());
        double max_mhz = *std::max_element(mhz_values.begin(), mhz_values.end());
        
        // Calculate standard deviation
        double variance = 0.0;
        for (double mhz : mhz_values) {
            variance += std::pow(mhz - avg_mhz, 2);
        }
        double std_dev = std::sqrt(variance / mhz_values.size());
        
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Average Performance: " << avg_mhz << " MHz equivalent\n";
        std::cout << "Performance Range:   " << min_mhz << " - " << max_mhz << " MHz\n";
        std::cout << "Standard Deviation:  " << std_dev << " MHz\n";
        std::cout << "Consistency:         " << (std_dev < 0.5 ? "Excellent" : 
                                                std_dev < 1.0 ? "Good" : 
                                                std_dev < 2.0 ? "Fair" : "Variable") << "\n\n";
        
        // Real Z80 comparison
        std::cout << "REAL Z80 COMPARISON\n";
        std::cout << std::string(25, '-') << "\n";
        std::cout << "Original Z80 (1976):     4.0 MHz\n";
        std::cout << "Z80A (1978):             6.0 MHz\n";
        std::cout << "Z80B (1982):             8.0 MHz\n";
        std::cout << "Digital Twin Average:    " << avg_mhz << " MHz\n\n";
        
        if (avg_mhz >= 4.0) {
            std::cout << "✅ Performance exceeds original Z80 specifications\n";
        } else {
            std::cout << "⚠️  Performance below original Z80 (optimization opportunities)\n";
        }
        
        std::cout << "\n";
    }
};

// =============================================================================
// Benchmark Test Programs
// =============================================================================

// Fibonacci sequence calculation (arithmetic intensive)
std::vector<uint8_t> create_fibonacci_benchmark() {
    return {
        // Calculate Fibonacci sequence iteratively
        // Input: B = number of iterations
        // Output: HL = final Fibonacci number
        
        0x21, 0x01, 0x00,     // 0x00: LD HL, 1        ; F(1) = 1
        0x11, 0x01, 0x00,     // 0x03: LD DE, 1        ; F(0) = 1  
        0x06, 0x20,           // 0x06: LD B, 32        ; Calculate 32 iterations
        
        // fibonacci_loop: (0x08)
        0x19,                 // 0x08: ADD HL, DE      ; HL = F(n) + F(n-1)
        0xEB,                 // 0x09: EX DE, HL       ; Swap registers
        0x10, 0xFC,           // 0x0A: DJNZ fibonacci_loop ; Loop until B=0
        
        0x76                  // 0x0C: HALT
    };
}

// Memory access pattern test (memory intensive)
std::vector<uint8_t> create_memory_benchmark() {
    return {
        // Memory fill and sum pattern
        // Tests memory access performance
        
        0x21, 0x00, 0x80,     // 0x00: LD HL, 0x8000   ; Start address
        0x01, 0x00, 0x04,     // 0x03: LD BC, 1024     ; 1KB of memory
        0x3E, 0xAA,           // 0x06: LD A, 0xAA      ; Fill pattern
        
        // fill_loop: (0x08)
        0x77,                 // 0x08: LD (HL), A      ; Store byte
        0x23,                 // 0x09: INC HL          ; Next address
        0x0B,                 // 0x0A: DEC BC          ; Decrement counter
        0x78,                 // 0x0B: LD A, B         ; Check if BC == 0
        0xB1,                 // 0x0C: OR C            ;
        0x20, 0xF9,           // 0x0D: JR NZ, fill_loop ; Continue if not zero
        
        // Now sum the memory
        0x21, 0x00, 0x80,     // 0x0F: LD HL, 0x8000   ; Reset address
        0x01, 0x00, 0x04,     // 0x12: LD BC, 1024     ; Reset counter
        0x16, 0x00,           // 0x15: LD D, 0         ; Sum accumulator
        
        // sum_loop: (0x17)
        0x7E,                 // 0x17: LD A, (HL)      ; Load byte
        0x82,                 // 0x18: ADD A, D        ; Add to sum
        0x57,                 // 0x19: LD D, A         ; Store sum
        0x23,                 // 0x1A: INC HL          ; Next address
        0x0B,                 // 0x1B: DEC BC          ; Decrement counter
        0x78,                 // 0x1C: LD A, B         ; Check if BC == 0
        0xB1,                 // 0x1D: OR C            ;
        0x20, 0xF7,           // 0x1E: JR NZ, sum_loop ; Continue if not zero
        
        0x76                  // 0x20: HALT
    };
}

// Sorting algorithm (control flow intensive)
std::vector<uint8_t> create_sorting_benchmark() {
    return {
        // Simple bubble sort on small array
        // Tests branching and control flow performance
        
        0x21, 0x00, 0x90,     // 0x00: LD HL, 0x9000   ; Array base address
        0x06, 0x08,           // 0x03: LD B, 8         ; Array size
        
        // Initialize array with descending values
        0x3E, 0x08,           // 0x05: LD A, 8         ; Start value
        // init_loop: (0x07)
        0x77,                 // 0x07: LD (HL), A      ; Store value
        0x23,                 // 0x08: INC HL          ; Next position
        0x3D,                 // 0x09: DEC A           ; Decrement value
        0x10, 0xFC,           // 0x0A: DJNZ init_loop  ; Continue until B=0
        
        // Bubble sort outer loop
        0x06, 0x07,           // 0x0C: LD B, 7         ; Outer loop counter
        // outer_loop: (0x0E)
        0x21, 0x00, 0x90,     // 0x0E: LD HL, 0x9000   ; Reset array pointer
        0x0E, 0x07,           // 0x11: LD C, 7         ; Inner loop counter
        
        // inner_loop: (0x13)
        0x7E,                 // 0x13: LD A, (HL)      ; Load current element
        0x23,                 // 0x14: INC HL          ; Point to next
        0xBE,                 // 0x15: CP (HL)         ; Compare with next element
        0x38, 0x08,           // 0x16: JR C, no_swap   ; Skip if in order
        
        // Swap elements
        0x56,                 // 0x18: LD D, (HL)      ; Load next element
        0x77,                 // 0x19: LD (HL), A      ; Store current in next pos
        0x2B,                 // 0x1A: DEC HL          ; Back to current pos
        0x72,                 // 0x1B: LD (HL), D      ; Store next in current pos
        0x23,                 // 0x1C: INC HL          ; Forward again
        
        // no_swap: (0x1D)
        0x0D,                 // 0x1D: DEC C           ; Decrement inner counter
        0x20, 0xF4,           // 0x1E: JR NZ, inner_loop ; Continue inner loop
        
        0x10, 0xED,           // 0x20: DJNZ outer_loop ; Continue outer loop
        
        0x76                  // 0x22: HALT
    };
}

// Prime number calculation (computational intensive)
std::vector<uint8_t> create_prime_benchmark() {
    return {
        // Find prime numbers using trial division
        // Tests computational performance
        
        0x3E, 0x02,           // 0x00: LD A, 2         ; Start with 2
        0x06, 0x10,           // 0x02: LD B, 16        ; Find first 16 primes
        0x21, 0x00, 0xA0,     // 0x04: LD HL, 0xA000   ; Prime storage
        
        // main_loop: (0x07)
        0x77,                 // 0x07: LD (HL), A      ; Store potential prime
        0x47,                 // 0x08: LD B, A         ; Copy to B for testing
        0x0E, 0x02,           // 0x09: LD C, 2         ; Start divisor at 2
        
        // test_prime: (0x0B)
        0x78,                 // 0x0B: LD A, B         ; Load number to test
        0x91,                 // 0x0C: SUB C           ; Subtract divisor
        0x28, 0x08,           // 0x0D: JR Z, not_prime ; If zero, not prime
        0x38, 0x06,           // 0x0F: JR C, is_prime  ; If negative, is prime
        0x47,                 // 0x11: LD B, A         ; Update remainder
        0x18, 0xF8,           // 0x12: JR test_prime   ; Continue testing
        
        // is_prime: (0x14)
        0x23,                 // 0x14: INC HL          ; Next storage position
        0x05,                 // 0x15: DEC B           ; Decrement prime counter
        0x20, 0x02,           // 0x16: JR NZ, next_num ; Continue if more needed
        0x76,                 // 0x18: HALT            ; Done
        
        // not_prime: (0x19)
        // next_num: (0x19)
        0x7E,                 // 0x19: LD A, (HL)      ; Reload current number
        0x3C,                 // 0x1A: INC A           ; Try next number
        0x18, 0xEB,           // 0x1B: JR main_loop    ; Continue main loop
    };
}

// =============================================================================
// Main Benchmark Runner
// =============================================================================

int main(int argc, char* argv[]) {
    std::cout << "Z80 Digital Twin - Performance Benchmark Suite\n";
    std::cout << "===============================================\n\n";
    
    // Parse command line options
    PerformanceBenchmark::BenchmarkConfig config;
    bool run_quick = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--quick" || arg == "-q") {
            run_quick = true;
            config.test_iterations = 50;
        } else if (arg == "--verbose" || arg == "-v") {
            config.verbose_output = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --quick, -q     Run quick benchmark (50 iterations)\n";
            std::cout << "  --verbose, -v   Verbose output\n";
            std::cout << "  --help, -h      Show this help\n";
            return 0;
        }
    }
    
    if (run_quick) {
        std::cout << "Running quick benchmark mode (50 iterations per test)\n\n";
    } else {
        std::cout << "Running full benchmark mode (100 iterations per test)\n\n";
    }
    
    PerformanceBenchmark benchmark;
    
    // Run benchmark tests
    std::cout << "Executing benchmark tests...\n";
    std::cout << std::string(40, '-') << "\n";
    
    benchmark.execute_benchmark("Fibonacci Calculation", create_fibonacci_benchmark(), config);
    benchmark.execute_benchmark("Memory Access Pattern", create_memory_benchmark(), config);
    benchmark.execute_benchmark("Sorting Algorithm", create_sorting_benchmark(), config);
    benchmark.execute_benchmark("Prime Number Search", create_prime_benchmark(), config);
    
    // Display results
    benchmark.print_detailed_results();
    benchmark.print_performance_analysis();
    
    // System information
    std::cout << "SYSTEM INFORMATION\n";
    std::cout << std::string(25, '=') << "\n";
    std::cout << "Compiler: Modern C++23\n";
    std::cout << "Build: Optimized (-O2)\n";
    std::cout << "Architecture: " << (sizeof(void*) == 8 ? "64-bit" : "32-bit") << "\n";
    std::cout << "Test Mode: " << (run_quick ? "Quick" : "Full") << " benchmark\n\n";
    
    std::cout << "🎯 Z80 Digital Twin performance benchmark completed!\n";
    std::cout << "   Use --quick for faster testing during development.\n";
    
    return 0;
}
