//
// Z80 Digital Twin - High-performance Z80 CPU emulator
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_CPU_H
#define Z80_CPU_H

#include <cstdint>
#include <vector>
#include <array>

namespace z80 {

// =============================================================================
// Type Definitions
// =============================================================================

/// @brief 16-bit register that can be accessed as two 8-bit registers
union RegisterPair {
    uint16_t r16;
    struct {
        uint8_t lo;  // Low byte comes first in little-endian
        uint8_t hi;  // High byte comes second in little-endian
    } r8;
    
    RegisterPair() : r16(0) {}
    explicit RegisterPair(uint16_t value) : r16(value) {}
};

// Forward declaration
class CPU;

/// @brief Function pointer type for Z80 instruction implementations
using InstructionHandler = void (CPU::*)();

/// @brief Z80 CPU execution states for prefix instruction handling
enum class CPUState : uint8_t {
    NORMAL = 0,        ///< Normal execution state - no prefix active
    CB_PREFIX = 1,     ///< CB prefix active - bit operations mode
    DD_PREFIX = 2,     ///< DD prefix active - IX register mode  
    ED_PREFIX = 3,     ///< ED prefix active - extended instruction mode
    FD_PREFIX = 4,     ///< FD prefix active - IY register mode
    DD_CB_PREFIX = 5,  ///< DD CB prefix sequence - IX bit operations with displacement
    FD_CB_PREFIX = 6   ///< FD CB prefix sequence - IY bit operations with displacement
};

// =============================================================================
// Constants
// =============================================================================

namespace Constants {
    constexpr uint32_t MEMORY_SIZE = 65536;
    constexpr uint16_t IO_PORTS = 256;
    constexpr uint16_t STACK_TOP = 0xFFFF;
    
    // Flag bit positions
    namespace Flags {
        constexpr uint8_t CARRY    = 0x01;
        constexpr uint8_t SUBTRACT = 0x02;
        constexpr uint8_t PARITY   = 0x04;
        constexpr uint8_t HALF     = 0x10;
        constexpr uint8_t ZERO     = 0x40;
        constexpr uint8_t SIGN     = 0x80;
    }
}

// =============================================================================
// Z80 CPU Class
// =============================================================================

class CPU {
public:
    // -------------------------------------------------------------------------
    // Construction/Destruction
    // -------------------------------------------------------------------------
    CPU();
    ~CPU();
    
    // -------------------------------------------------------------------------
    // Core Execution
    // -------------------------------------------------------------------------
    
    /// @brief Executes Z80 instructions until the specified cycle count
    /// @param target_cycle The cycle count to run until
    void RunUntilCycle(uint64_t target_cycle);
    
    /// @brief Executes a single instruction
    void Step();
    
    /// @brief Resets the CPU to initial state
    void Reset();
    
    // -------------------------------------------------------------------------
    // 16-bit Register Accessors
    // -------------------------------------------------------------------------
    uint16_t& BC() { return _BC.r16; }
    uint16_t& DE() { return _DE.r16; }
    uint16_t& HL() { return _HL.r16; }
    uint16_t& AF() { return _AF.r16; }
    uint16_t& SP() { return _SP; }
    uint16_t& PC() { return _PC; }
    uint16_t& IX() { return _IX.r16; }
    uint16_t& IY() { return _IY.r16; }
    uint16_t& IR() { return _IR.r16; }
    uint16_t& WZ() { return _WZ.r16; }
    
    // -------------------------------------------------------------------------
    // 8-bit Register Accessors
    // -------------------------------------------------------------------------
    uint8_t& A() { return _AF.r8.hi; }
    uint8_t& F() { return _AF.r8.lo; }
    uint8_t& B() { return _BC.r8.hi; }
    uint8_t& C() { return _BC.r8.lo; }
    uint8_t& D() { return _DE.r8.hi; }
    uint8_t& E() { return _DE.r8.lo; }
    uint8_t& H() { return _HL.r8.hi; }
    uint8_t& L() { return _HL.r8.lo; }
    uint8_t& I() { return _IR.r8.hi; }
    uint8_t& R() { return _IR.r8.lo; }
    
    // -------------------------------------------------------------------------
    // Flag and Interrupt Accessors
    // -------------------------------------------------------------------------
    bool& IFF1() { return _IFF1; }
    bool& IFF2() { return _IFF2; }
    
    // -------------------------------------------------------------------------
    // CPU State Accessors
    // -------------------------------------------------------------------------
    bool IsHalted() const { return _halted; }
    void SetHalted(bool halted) { _halted = halted; }
    
    // -------------------------------------------------------------------------
    // Memory and I/O Access
    // -------------------------------------------------------------------------
    
    /// @brief Writes a byte to memory
    /// @param address Memory address to write to
    /// @param value Byte value to write
    void WriteMemory(uint16_t address, uint8_t value) { memory[address] = value; }
    
    /// @brief Reads a byte from memory
    /// @param address Memory address to read from
    /// @return Byte value at the specified address
    uint8_t ReadMemory(uint16_t address) const { return memory[address]; }
    
    /// @brief Loads a program into memory
    /// @param program Vector of bytes to load
    /// @param start_address Starting address to load the program (default: 0)
    void LoadProgram(const std::vector<uint8_t>& program, uint16_t start_address = 0);
    
    /// @brief Writes a byte to an I/O port
    /// @param port Port number to write to
    /// @param value Byte value to write
    void WritePort(uint8_t port, uint8_t value) { io_ports[port] = value; }
    
    /// @brief Reads a byte from an I/O port
    /// @param port Port number to read from
    /// @return Byte value from the specified port
    uint8_t ReadPort(uint8_t port) const { return io_ports[port]; }
    
    // -------------------------------------------------------------------------
    // State Information
    // -------------------------------------------------------------------------
    
    /// @brief Gets the current cycle count
    /// @return Total T-states executed
    uint64_t GetCycleCount() const { return t_cycle; }
    
    /// @brief Sets the cycle count
    /// @param cycles New cycle count value
    void SetCycleCount(uint64_t cycles) { t_cycle = cycles; }

private:
    // -------------------------------------------------------------------------
    // CPU State
    // -------------------------------------------------------------------------
    uint64_t t_cycle;           ///< Total T-states executed
    uint16_t _PC;               ///< Program Counter
    uint16_t _SP;               ///< Stack Pointer
    
    // Main register set
    RegisterPair _AF;           ///< Accumulator and Flags
    RegisterPair _BC;           ///< BC register pair
    RegisterPair _DE;           ///< DE register pair
    RegisterPair _HL;           ///< HL register pair
    
    // Alternate register set
    RegisterPair _AF1;          ///< Alternate AF
    RegisterPair _BC1;          ///< Alternate BC
    RegisterPair _DE1;          ///< Alternate DE
    RegisterPair _HL1;          ///< Alternate HL
    
    // Index registers
    RegisterPair _IX;           ///< IX index register
    RegisterPair _IY;           ///< IY index register
    
    // Special registers
    RegisterPair _IR;           ///< I (interrupt vector) and R (refresh) registers
    RegisterPair _WZ;           ///< Internal temporary register for address calculations
    
    // Interrupt flags
    bool _IFF1;                 ///< Interrupt Enable Flag 1
    bool _IFF2;                 ///< Interrupt Enable Flag 2
    
    // Interrupt mode
    uint8_t _interrupt_mode;    ///< Interrupt mode (0, 1, or 2)
    
    // CPU execution state
    bool _halted;               ///< CPU halted state (HALT instruction)
    
    // Prefix state management
    CPUState current_state;     ///< Current CPU execution state for prefix handling
    int8_t current_displacement; ///< Displacement for DD CB/FD CB instructions
    
    // Memory and I/O
    std::array<uint8_t, Constants::MEMORY_SIZE> memory;    ///< 64KB memory space
    std::array<uint8_t, Constants::IO_PORTS> io_ports;     ///< 256 I/O ports
    
    // -------------------------------------------------------------------------
    // Instruction Dispatch Tables
    // -------------------------------------------------------------------------
    std::array<InstructionHandler, 256> basic_opcodes;     ///< Basic instruction set
    std::array<InstructionHandler, 256> ED_opcodes;        ///< ED-prefixed instructions
    
    // -------------------------------------------------------------------------
    // Instruction Implementation Helpers
    // -------------------------------------------------------------------------
    void InitializeInstructionTables();
    void SetCarryFlag(bool value);
    bool GetCarryFlag() const;
    void SetFlags_ADD(uint8_t result, uint8_t operand1, uint8_t operand2);
    void SetFlags_SUB(uint8_t result, uint8_t operand1, uint8_t operand2);
    void SetFlags_LOGIC(uint8_t result);
    uint8_t CalculateParity(uint8_t value);
    void PushWord(uint16_t value);
    uint16_t PopWord();
    bool CheckCondition(uint8_t condition);
    
    // CB instruction helpers
    void ExecuteCBInstruction(uint8_t opcode);
    uint8_t& GetCBRegister(uint8_t reg_code);
    uint8_t GetCBMemory(uint8_t reg_code);
    void SetCBMemory(uint8_t reg_code, uint8_t value);
    uint8_t RotateLeftCircular(uint8_t value);
    uint8_t RotateRightCircular(uint8_t value);
    uint8_t RotateLeft(uint8_t value);
    uint8_t RotateRight(uint8_t value);
    uint8_t ShiftLeftArithmetic(uint8_t value);
    uint8_t ShiftRightArithmetic(uint8_t value);
    uint8_t ShiftLeftLogical(uint8_t value);
    uint8_t ShiftRightLogical(uint8_t value);
    void TestBit(uint8_t value, uint8_t bit);
    uint8_t ResetBit(uint8_t value, uint8_t bit);
    uint8_t SetBit(uint8_t value, uint8_t bit);
    
    // State-aware IX/IY helper functions
    uint16_t GetEffectiveHL_Memory();     // For memory operations with displacement
    uint16_t& GetEffectiveHL_Register();  // For register operations without displacement
    uint8_t& GetEffectiveH();             // For H register access (H/IXH/IYH)
    uint8_t& GetEffectiveL();             // For L register access (L/IXL/IYL)
    uint8_t GetMemoryAccessCycles();      // Cycle count for memory operations
    uint8_t GetRegisterOpCycles();        // Cycle count for register operations
    uint8_t GetArithmeticMemCycles();     // Cycle count for arithmetic with memory
    
    // -------------------------------------------------------------------------
    // Basic Instructions (0x00-0x3F)
    // -------------------------------------------------------------------------
    void NOP();                 // 0x00 - No Operation
    void LD_BC_nn();           // 0x01 - Load 16-bit immediate to BC
    void LD_mBC_A();           // 0x02 - Load A to memory pointed by BC
    void INC_BC();             // 0x03 - Increment BC
    void INC_B();              // 0x04 - Increment B
    void DEC_B();              // 0x05 - Decrement B
    void LD_B_n();             // 0x06 - Load 8-bit immediate to B
    void RLCA();               // 0x07 - Rotate A left circular
    void EX_AF_AF();           // 0x08 - Exchange AF with AF'
    void ADD_HL_BC();          // 0x09 - Add BC to HL
    void LD_A_mBC();           // 0x0A - Load memory pointed by BC to A
    void DEC_BC();             // 0x0B - Decrement BC
    void INC_C();              // 0x0C - Increment C
    void DEC_C();              // 0x0D - Decrement C
    void LD_C_n();             // 0x0E - Load 8-bit immediate to C
    void RRCA();               // 0x0F - Rotate A right circular
    void DJNZ();               // 0x10 - Decrement B and jump if not zero
    void LD_DE_nn();           // 0x11 - Load 16-bit immediate to DE
    void LD_mDE_A();           // 0x12 - Load A to memory pointed by DE
    void INC_DE();             // 0x13 - Increment DE
    void INC_D();              // 0x14 - Increment D
    void DEC_D();              // 0x15 - Decrement D
    void LD_D_n();             // 0x16 - Load 8-bit immediate to D
    void RLA();                // 0x17 - Rotate A left through carry
    void JR();                 // 0x18 - Jump relative
    void ADD_HL_DE();          // 0x19 - Add DE to HL
    void LD_A_mDE();           // 0x1A - Load memory pointed by DE to A
    void DEC_DE();             // 0x1B - Decrement DE
    void INC_E();              // 0x1C - Increment E
    void DEC_E();              // 0x1D - Decrement E
    void LD_E_n();             // 0x1E - Load 8-bit immediate to E
    void RRA();                // 0x1F - Rotate A right through carry
    void JR_NZ();              // 0x20 - Jump relative if not zero
    void LD_HL_nn();           // 0x21 - Load 16-bit immediate to HL
    void LD_mnn_HL();          // 0x22 - Load HL to memory at 16-bit address
    void INC_HL();             // 0x23 - Increment HL
    void INC_H();              // 0x24 - Increment H
    void DEC_H();              // 0x25 - Decrement H
    void LD_H_n();             // 0x26 - Load 8-bit immediate to H
    void DAA();                // 0x27 - Decimal adjust accumulator
    void JR_Z();               // 0x28 - Jump relative if zero
    void ADD_HL_HL();          // 0x29 - Add HL to HL (double HL)
    void LD_HL_mnn();          // 0x2A - Load memory at 16-bit address to HL
    void DEC_HL();             // 0x2B - Decrement HL
    void INC_L();              // 0x2C - Increment L
    void DEC_L();              // 0x2D - Decrement L
    void LD_L_n();             // 0x2E - Load 8-bit immediate to L
    void CPL();                // 0x2F - Complement A
    void JR_NC();              // 0x30 - Jump relative if no carry
    void LD_SP_nn();           // 0x31 - Load 16-bit immediate to SP
    void LD_mnn_A();           // 0x32 - Load A to memory at 16-bit address
    void INC_SP();             // 0x33 - Increment SP
    void INC_mHL();            // 0x34 - Increment memory pointed by HL
    void DEC_mHL();            // 0x35 - Decrement memory pointed by HL
    void LD_mHL_n();           // 0x36 - Load 8-bit immediate to memory pointed by HL
    void SCF();                // 0x37 - Set carry flag
    void JR_C();               // 0x38 - Jump relative if carry
    void ADD_HL_SP();          // 0x39 - Add SP to HL
    void LD_A_mnn();           // 0x3A - Load memory at 16-bit address to A
    void DEC_SP();             // 0x3B - Decrement SP
    void INC_A();              // 0x3C - Increment A
    void DEC_A();              // 0x3D - Decrement A
    void LD_A_n();             // 0x3E - Load 8-bit immediate to A
    void CCF();                // 0x3F - Complement carry flag
    
    // -------------------------------------------------------------------------
    // Load Instructions (0x40-0x7F) - Basic register-to-register transfers
    // -------------------------------------------------------------------------
    void LD_B_B();             // 0x40 - Load B to B
    void LD_B_C();             // 0x41 - Load C to B
    void LD_B_D();             // 0x42 - Load D to B
    void LD_B_E();             // 0x43 - Load E to B
    void LD_B_H();             // 0x44 - Load H to B
    void LD_B_L();             // 0x45 - Load L to B
    void LD_B_mHL();           // 0x46 - Load memory pointed by HL to B
    void LD_B_A();             // 0x47 - Load A to B
    
    void LD_C_B();             // 0x48 - Load B to C
    void LD_C_C();             // 0x49 - Load C to C
    void LD_C_D();             // 0x4A - Load D to C
    void LD_C_E();             // 0x4B - Load E to C
    void LD_C_H();             // 0x4C - Load H to C
    void LD_C_L();             // 0x4D - Load L to C
    void LD_C_mHL();           // 0x4E - Load memory pointed by HL to C
    void LD_C_A();             // 0x4F - Load A to C
    
    void LD_D_B();             // 0x50 - Load B to D
    void LD_D_C();             // 0x51 - Load C to D
    void LD_D_D();             // 0x52 - Load D to D
    void LD_D_E();             // 0x53 - Load E to D
    void LD_D_H();             // 0x54 - Load H to D
    void LD_D_L();             // 0x55 - Load L to D
    void LD_D_mHL();           // 0x56 - Load memory pointed by HL to D
    void LD_D_A();             // 0x57 - Load A to D
    
    void LD_E_B();             // 0x58 - Load B to E
    void LD_E_C();             // 0x59 - Load C to E
    void LD_E_D();             // 0x5A - Load D to E
    void LD_E_E();             // 0x5B - Load E to E
    void LD_E_H();             // 0x5C - Load H to E
    void LD_E_L();             // 0x5D - Load L to E
    void LD_E_mHL();           // 0x5E - Load memory pointed by HL to E
    void LD_E_A();             // 0x5F - Load A to E
    
    void LD_H_B();             // 0x60 - Load B to H
    void LD_H_C();             // 0x61 - Load C to H
    void LD_H_D();             // 0x62 - Load D to H
    void LD_H_E();             // 0x63 - Load E to H
    void LD_H_H();             // 0x64 - Load H to H
    void LD_H_L();             // 0x65 - Load L to H
    void LD_H_mHL();           // 0x66 - Load memory pointed by HL to H
    void LD_H_A();             // 0x67 - Load A to H
    
    void LD_L_B();             // 0x68 - Load B to L
    void LD_L_C();             // 0x69 - Load C to L
    void LD_L_D();             // 0x6A - Load D to L
    void LD_L_E();             // 0x6B - Load E to L
    void LD_L_H();             // 0x6C - Load H to L
    void LD_L_L();             // 0x6D - Load L to L
    void LD_L_mHL();           // 0x6E - Load memory pointed by HL to L
    void LD_L_A();             // 0x6F - Load A to L
    
    void LD_mHL_B();           // 0x70 - Load B to memory pointed by HL
    void LD_mHL_C();           // 0x71 - Load C to memory pointed by HL
    void LD_mHL_D();           // 0x72 - Load D to memory pointed by HL
    void LD_mHL_E();           // 0x73 - Load E to memory pointed by HL
    void LD_mHL_H();           // 0x74 - Load H to memory pointed by HL
    void LD_mHL_L();           // 0x75 - Load L to memory pointed by HL
    void HALT();               // 0x76 - Halt processor
    void LD_mHL_A();           // 0x77 - Load A to memory pointed by HL
    
    void LD_A_B();             // 0x78 - Load B to A
    void LD_A_C();             // 0x79 - Load C to A
    void LD_A_D();             // 0x7A - Load D to A
    void LD_A_E();             // 0x7B - Load E to A
    void LD_A_H();             // 0x7C - Load H to A
    void LD_A_L();             // 0x7D - Load L to A
    void LD_A_mHL();           // 0x7E - Load memory pointed by HL to A
    void LD_A_A();             // 0x7F - Load A to A
    
    // -------------------------------------------------------------------------
    // Arithmetic and Logic Instructions (0x80-0xBF)
    // -------------------------------------------------------------------------
    void ADD_A_B();            // 0x80 - Add B to A
    void ADD_A_C();            // 0x81 - Add C to A
    void ADD_A_D();            // 0x82 - Add D to A
    void ADD_A_E();            // 0x83 - Add E to A
    void ADD_A_H();            // 0x84 - Add H to A
    void ADD_A_L();            // 0x85 - Add L to A
    void ADD_A_mHL();          // 0x86 - Add memory pointed by HL to A
    void ADD_A_A();            // 0x87 - Add A to A
    
    void ADC_A_B();            // 0x88 - Add B to A with carry
    void ADC_A_C();            // 0x89 - Add C to A with carry
    void ADC_A_D();            // 0x8A - Add D to A with carry
    void ADC_A_E();            // 0x8B - Add E to A with carry
    void ADC_A_H();            // 0x8C - Add H to A with carry
    void ADC_A_L();            // 0x8D - Add L to A with carry
    void ADC_A_mHL();          // 0x8E - Add memory pointed by HL to A with carry
    void ADC_A_A();            // 0x8F - Add A to A with carry
    
    void SUB_B();              // 0x90 - Subtract B from A
    void SUB_C();              // 0x91 - Subtract C from A
    void SUB_D();              // 0x92 - Subtract D from A
    void SUB_E();              // 0x93 - Subtract E from A
    void SUB_H();              // 0x94 - Subtract H from A
    void SUB_L();              // 0x95 - Subtract L from A
    void SUB_mHL();            // 0x96 - Subtract memory pointed by HL from A
    void SUB_A();              // 0x97 - Subtract A from A
    
    void SBC_A_B();            // 0x98 - Subtract B from A with carry
    void SBC_A_C();            // 0x99 - Subtract C from A with carry
    void SBC_A_D();            // 0x9A - Subtract D from A with carry
    void SBC_A_E();            // 0x9B - Subtract E from A with carry
    void SBC_A_H();            // 0x9C - Subtract H from A with carry
    void SBC_A_L();            // 0x9D - Subtract L from A with carry
    void SBC_A_mHL();          // 0x9E - Subtract memory pointed by HL from A with carry
    void SBC_A_A();            // 0x9F - Subtract A from A with carry
    
    void AND_B();              // 0xA0 - Logical AND B with A
    void AND_C();              // 0xA1 - Logical AND C with A
    void AND_D();              // 0xA2 - Logical AND D with A
    void AND_E();              // 0xA3 - Logical AND E with A
    void AND_H();              // 0xA4 - Logical AND H with A
    void AND_L();              // 0xA5 - Logical AND L with A
    void AND_mHL();            // 0xA6 - Logical AND memory pointed by HL with A
    void AND_A();              // 0xA7 - Logical AND A with A
    
    void XOR_B();              // 0xA8 - Logical XOR B with A
    void XOR_C();              // 0xA9 - Logical XOR C with A
    void XOR_D();              // 0xAA - Logical XOR D with A
    void XOR_E();              // 0xAB - Logical XOR E with A
    void XOR_H();              // 0xAC - Logical XOR H with A
    void XOR_L();              // 0xAD - Logical XOR L with A
    void XOR_mHL();            // 0xAE - Logical XOR memory pointed by HL with A
    void XOR_A();              // 0xAF - Logical XOR A with A
    
    void OR_B();               // 0xB0 - Logical OR B with A
    void OR_C();               // 0xB1 - Logical OR C with A
    void OR_D();               // 0xB2 - Logical OR D with A
    void OR_E();               // 0xB3 - Logical OR E with A
    void OR_H();               // 0xB4 - Logical OR H with A
    void OR_L();               // 0xB5 - Logical OR L with A
    void OR_mHL();             // 0xB6 - Logical OR memory pointed by HL with A
    void OR_A();               // 0xB7 - Logical OR A with A
    
    void CP_B();               // 0xB8 - Compare B with A
    void CP_C();               // 0xB9 - Compare C with A
    void CP_D();               // 0xBA - Compare D with A
    void CP_E();               // 0xBB - Compare E with A
    void CP_H();               // 0xBC - Compare H with A
    void CP_L();               // 0xBD - Compare L with A
    void CP_mHL();             // 0xBE - Compare memory pointed by HL with A
    void CP_A();               // 0xBF - Compare A with A
    
    // -------------------------------------------------------------------------
    // Control Flow, Stack, and I/O Instructions (0xC0-0xFF)
    // -------------------------------------------------------------------------
    void RET_NZ();             // 0xC0 - Return if not zero
    void POP_BC();             // 0xC1 - Pop BC from stack
    void JP_NZ_nn();           // 0xC2 - Jump if not zero
    void JP_nn();              // 0xC3 - Jump absolute
    void CALL_NZ_nn();         // 0xC4 - Call if not zero
    void PUSH_BC();            // 0xC5 - Push BC to stack
    void ADD_A_n();            // 0xC6 - Add immediate to A
    void RST_00();             // 0xC7 - Restart 0x00
    void RET_Z();              // 0xC8 - Return if zero
    void RET();                // 0xC9 - Return
    void JP_Z_nn();            // 0xCA - Jump if zero
    void PREFIX_CB();          // 0xCB - CB prefix (for later)
    void CALL_Z_nn();          // 0xCC - Call if zero
    void CALL_nn();            // 0xCD - Call absolute
    void ADC_A_n();            // 0xCE - Add immediate to A with carry
    void RST_08();             // 0xCF - Restart 0x08
    
    void RET_NC();             // 0xD0 - Return if no carry
    void POP_DE();             // 0xD1 - Pop DE from stack
    void JP_NC_nn();           // 0xD2 - Jump if no carry
    void OUT_n_A();            // 0xD3 - Output A to port
    void CALL_NC_nn();         // 0xD4 - Call if no carry
    void PUSH_DE();            // 0xD5 - Push DE to stack
    void SUB_n();              // 0xD6 - Subtract immediate from A
    void RST_10();             // 0xD7 - Restart 0x10
    void RET_C();              // 0xD8 - Return if carry
    void EXX();                // 0xD9 - Exchange register sets
    void JP_C_nn();            // 0xDA - Jump if carry
    void IN_A_n();             // 0xDB - Input from port to A
    void CALL_C_nn();          // 0xDC - Call if carry
    void PREFIX_DD();          // 0xDD - DD prefix (for later)
    void SBC_A_n();            // 0xDE - Subtract immediate from A with carry
    void RST_18();             // 0xDF - Restart 0x18
    
    void RET_PO();             // 0xE0 - Return if parity odd
    void POP_HL();             // 0xE1 - Pop HL from stack
    void JP_PO_nn();           // 0xE2 - Jump if parity odd
    void EX_mSP_HL();          // 0xE3 - Exchange (SP) with HL
    void CALL_PO_nn();         // 0xE4 - Call if parity odd
    void PUSH_HL();            // 0xE5 - Push HL to stack
    void AND_n();              // 0xE6 - AND immediate with A
    void RST_20();             // 0xE7 - Restart 0x20
    void RET_PE();             // 0xE8 - Return if parity even
    void JP_HL();              // 0xE9 - Jump to HL
    void JP_PE_nn();           // 0xEA - Jump if parity even
    void EX_DE_HL();           // 0xEB - Exchange DE with HL
    void CALL_PE_nn();         // 0xEC - Call if parity even
    void PREFIX_ED();          // 0xED - ED prefix (for later)
    void XOR_n();              // 0xEE - XOR immediate with A
    void RST_28();             // 0xEF - Restart 0x28
    
    void RET_P();              // 0xF0 - Return if positive
    void POP_AF();             // 0xF1 - Pop AF from stack
    void JP_P_nn();            // 0xF2 - Jump if positive
    void DI();                 // 0xF3 - Disable interrupts
    void CALL_P_nn();          // 0xF4 - Call if positive
    void PUSH_AF();            // 0xF5 - Push AF to stack
    void OR_n();               // 0xF6 - OR immediate with A
    void RST_30();             // 0xF7 - Restart 0x30
    void RET_M();              // 0xF8 - Return if minus
    void LD_SP_HL();           // 0xF9 - Load HL to SP
    void JP_M_nn();            // 0xFA - Jump if minus
    void EI();                 // 0xFB - Enable interrupts
    void CALL_M_nn();          // 0xFC - Call if minus
    void PREFIX_FD();          // 0xFD - FD prefix (for later)
    void CP_n();               // 0xFE - Compare immediate with A
    void RST_38();             // 0xFF - Restart 0x38
    
    // -------------------------------------------------------------------------
    // ED-Prefixed Instructions (Extended Instruction Set)
    // -------------------------------------------------------------------------
    
    // Default handler for undefined ED instructions
    void ED_NOP();             // Default NOP for undefined ED instructions
    
    // 16-bit arithmetic operations (ED 4x-7x)
    void SBC_HL_BC();          // ED 42 - Subtract BC from HL with carry
    void LD_mnn_BC();          // ED 43 - Load BC to memory at 16-bit address
    void NEG();                // ED 44 - Negate A (2's complement)
    void RETN();               // ED 45 - Return from non-maskable interrupt
    void IM_0();               // ED 46 - Set interrupt mode 0
    void LD_I_A();             // ED 47 - Load A to I register
    void ADC_HL_BC();          // ED 4A - Add BC to HL with carry
    void LD_BC_mnn();          // ED 4B - Load memory at 16-bit address to BC
    void RETI();               // ED 4D - Return from interrupt
    void LD_R_A();             // ED 4F - Load A to R register
    
    void SBC_HL_DE();          // ED 52 - Subtract DE from HL with carry
    void LD_mnn_DE();          // ED 53 - Load DE to memory at 16-bit address
    void IM_1();               // ED 56 - Set interrupt mode 1
    void LD_A_I();             // ED 57 - Load I register to A
    void ADC_HL_DE();          // ED 5A - Add DE to HL with carry
    void LD_DE_mnn();          // ED 5B - Load memory at 16-bit address to DE
    void IM_2();               // ED 5E - Set interrupt mode 2
    void LD_A_R();             // ED 5F - Load R register to A
    
    void SBC_HL_HL();          // ED 62 - Subtract HL from HL with carry
    void LD_mnn_HL_ED();       // ED 63 - Load HL to memory at 16-bit address (ED version)
    void RRD();                // ED 67 - Rotate right decimal
    void ADC_HL_HL();          // ED 6A - Add HL to HL with carry
    void LD_HL_mnn_ED();       // ED 6B - Load memory at 16-bit address to HL (ED version)
    void RLD();                // ED 6F - Rotate left decimal
    
    void SBC_HL_SP();          // ED 72 - Subtract SP from HL with carry
    void LD_mnn_SP();          // ED 73 - Load SP to memory at 16-bit address
    void ADC_HL_SP();          // ED 7A - Add SP to HL with carry
    void LD_SP_mnn();          // ED 7B - Load memory at 16-bit address to SP
    
    // Block operations (ED Ax-Bx)
    void LDI();                // ED A0 - Load and increment
    void CPI();                // ED A1 - Compare and increment
    void INI();                // ED A2 - Input and increment
    void OUTI();               // ED A3 - Output and increment
    void LDD();                // ED A8 - Load and decrement
    void CPD();                // ED A9 - Compare and decrement
    void IND();                // ED AA - Input and decrement
    void OUTD();               // ED AB - Output and decrement
    
    void LDIR();               // ED B0 - Load, increment and repeat
    void CPIR();               // ED B1 - Compare, increment and repeat
    void INIR();               // ED B2 - Input, increment and repeat
    void OTIR();               // ED B3 - Output, increment and repeat
    void LDDR();               // ED B8 - Load, decrement and repeat
    void CPDR();               // ED B9 - Compare, decrement and repeat
    void INDR();               // ED BA - Input, decrement and repeat
    void OTDR();               // ED BB - Output, decrement and repeat
    
    // Individual I/O operations using C register for port
    void IN_B_C();             // ED 40 - Input from port C to B
    void OUT_C_B();            // ED 41 - Output B to port C
    void IN_C_C();             // ED 48 - Input from port C to C
    void OUT_C_C();            // ED 49 - Output C to port C
    void IN_D_C();             // ED 50 - Input from port C to D
    void OUT_C_D();            // ED 51 - Output D to port C
    void IN_E_C();             // ED 58 - Input from port C to E
    void OUT_C_E();            // ED 59 - Output E to port C
    void IN_H_C();             // ED 60 - Input from port C to H
    void OUT_C_H();            // ED 61 - Output H to port C
    void IN_L_C();             // ED 68 - Input from port C to L
    void OUT_C_L();            // ED 69 - Output L to port C
    void IN_F_C();             // ED 70 - Input from port C (sets flags only)
    void OUT_C_0();            // ED 71 - Output 0 to port C
    void IN_A_C();             // ED 78 - Input from port C to A
    void OUT_C_A();            // ED 79 - Output A to port C
    
    // Undocumented ED instructions
    void SLL_mHL();            // ED 76 - Shift Left Logical (HL) (undocumented)
    
    // -------------------------------------------------------------------------
    // Legacy Helper Functions (for compatibility)
    // -------------------------------------------------------------------------
    void make_carry(bool value) { SetCarryFlag(value); }
    bool carry() const { return GetCarryFlag(); }
};

} // namespace z80

#endif // Z80_CPU_H
