//
// Z80 CPU Emulator - Implementation
// Created by Larry Dawson on 11/10/23.
//

#include "z80_cpu.h"
#include <algorithm>

namespace z80 {

// =============================================================================
// Construction/Destruction
// =============================================================================

CPU::CPU() {
    Reset();
    InitializeInstructionTables();
}

CPU::~CPU() = default;

void CPU::Reset() {
    // Initialize CPU state
    t_cycle = 0;
    _PC = 0;
    _SP = Constants::STACK_TOP;
    
    // Clear all registers
    _AF.r16 = 0;
    _BC.r16 = 0;
    _DE.r16 = 0;
    _HL.r16 = 0;
    _AF1.r16 = 0;
    _BC1.r16 = 0;
    _DE1.r16 = 0;
    _HL1.r16 = 0;
    _IX.r16 = 0;
    _IY.r16 = 0;
    _IR.r16 = 0;
    _WZ.r16 = 0;
    
    // Clear interrupt flags
    _IFF1 = false;
    _IFF2 = false;
    
    // Initialize interrupt mode
    _interrupt_mode = 0;
    
    // Clear halted state
    _halted = false;
    
    // Initialize prefix state
    current_state = CPUState::NORMAL;
    
    // Clear memory and I/O - Skip for now to avoid potential stack issues
    // memory.fill(0);
    // io_ports.fill(0);
}

// =============================================================================
// Core Execution
// =============================================================================

void CPU::RunUntilCycle(uint64_t target_cycle) {
    while (t_cycle < target_cycle && !_halted) {
        Step();
    }
}

void CPU::Step() {
    // Fetch instruction opcode
    uint8_t opcode = memory[PC()++];
    
    // Execute based on current CPU state
    switch (current_state) {
        case CPUState::NORMAL:
            // Handle prefix instructions that change state
            if (opcode == 0xCB) {
                current_state = CPUState::CB_PREFIX;
                t_cycle += 4;
            } else if (opcode == 0xDD) {
                current_state = CPUState::DD_PREFIX;
                t_cycle += 4;
            } else if (opcode == 0xED) {
                current_state = CPUState::ED_PREFIX;
                t_cycle += 4;
            } else if (opcode == 0xFD) {
                current_state = CPUState::FD_PREFIX;
                t_cycle += 4;
            } else {
                // Execute normal instruction
                (this->*basic_opcodes[opcode])();
            }
            break;
            
        case CPUState::CB_PREFIX:
            // Execute CB-prefixed instruction using compact decoder
            ExecuteCBInstruction(opcode);
            current_state = CPUState::NORMAL;
            break;
            
        case CPUState::DD_PREFIX:
            // Handle DD prefix state transitions
            if (opcode == 0xCB) {
                // DD CB requires displacement byte before CB instruction
                // Read displacement but don't advance PC yet - CB instruction will handle it
                current_state = CPUState::DD_CB_PREFIX;
                t_cycle += 4;
            } else if (opcode == 0xDD) {
                // Multiple DD prefixes - stay in DD state
                t_cycle += 4;
            } else if (opcode == 0xED) {
                current_state = CPUState::ED_PREFIX;
                t_cycle += 4;
            } else if (opcode == 0xFD) {
                current_state = CPUState::FD_PREFIX;
                t_cycle += 4;
            } else {
                // Execute DD-prefixed instruction (IX operations) using state-aware basic instructions
                (this->*basic_opcodes[opcode])();
                current_state = CPUState::NORMAL;
            }
            break;
            
        case CPUState::ED_PREFIX:
            // Execute ED-prefixed instruction
            (this->*ED_opcodes[opcode])();
            current_state = CPUState::NORMAL;
            break;
            
        case CPUState::FD_PREFIX:
            // Handle FD prefix state transitions
            if (opcode == 0xCB) {
                current_state = CPUState::FD_CB_PREFIX;
                t_cycle += 4;
            } else if (opcode == 0xDD) {
                current_state = CPUState::DD_PREFIX;
                t_cycle += 4;
            } else if (opcode == 0xED) {
                current_state = CPUState::ED_PREFIX;
                t_cycle += 4;
            } else if (opcode == 0xFD) {
                // Multiple FD prefixes - stay in FD state
                t_cycle += 4;
            } else {
                // Execute FD-prefixed instruction (IY operations) using state-aware basic instructions
                (this->*basic_opcodes[opcode])();
                current_state = CPUState::NORMAL;
            }
            break;
            
        case CPUState::DD_CB_PREFIX:
            // DD CB instructions have format: DD CB displacement opcode
            // The opcode we just read is the displacement, we need to read the actual CB opcode
            {
                current_displacement = static_cast<int8_t>(opcode);  // Store displacement
                uint8_t cb_opcode = memory[PC()++];  // Read the actual CB instruction
                
                // Execute the CB instruction with stored displacement
                ExecuteCBInstruction(cb_opcode);
                
                current_state = CPUState::NORMAL;
            }
            break;
            
        case CPUState::FD_CB_PREFIX:
            // FD CB instructions have format: FD CB displacement opcode
            // The opcode we just read is the displacement, we need to read the actual CB opcode
            {
                current_displacement = static_cast<int8_t>(opcode);  // Store displacement
                uint8_t cb_opcode = memory[PC()++];  // Read the actual CB instruction
                
                // Execute the CB instruction with stored displacement
                ExecuteCBInstruction(cb_opcode);
                
                current_state = CPUState::NORMAL;
            }
            break;
    }
}

// =============================================================================
// Memory and I/O Access
// =============================================================================

void CPU::LoadProgram(const std::vector<uint8_t>& program, uint16_t start_address) {
    for (size_t i = 0; i < program.size() && (start_address + i) < Constants::MEMORY_SIZE; ++i) {
        memory[start_address + i] = program[i];
    }
}

// =============================================================================
// Instruction Table Initialization
// =============================================================================

void CPU::InitializeInstructionTables() {
    // Initialize all tables to NOP
    basic_opcodes.fill(&CPU::NOP);
    ED_opcodes.fill(&CPU::ED_NOP);
    
    // Set up the implemented opcodes
    basic_opcodes[0x00] = &CPU::NOP;
    basic_opcodes[0x01] = &CPU::LD_BC_nn;
    basic_opcodes[0x02] = &CPU::LD_mBC_A;
    basic_opcodes[0x03] = &CPU::INC_BC;
    basic_opcodes[0x04] = &CPU::INC_B;
    basic_opcodes[0x05] = &CPU::DEC_B;
    basic_opcodes[0x06] = &CPU::LD_B_n;
    basic_opcodes[0x07] = &CPU::RLCA;
    basic_opcodes[0x08] = &CPU::EX_AF_AF;
    basic_opcodes[0x09] = &CPU::ADD_HL_BC;
    basic_opcodes[0x0A] = &CPU::LD_A_mBC;
    basic_opcodes[0x0B] = &CPU::DEC_BC;
    basic_opcodes[0x0C] = &CPU::INC_C;
    basic_opcodes[0x0D] = &CPU::DEC_C;
    basic_opcodes[0x0E] = &CPU::LD_C_n;
    basic_opcodes[0x0F] = &CPU::RRCA;
    basic_opcodes[0x10] = &CPU::DJNZ;
    basic_opcodes[0x11] = &CPU::LD_DE_nn;
    basic_opcodes[0x12] = &CPU::LD_mDE_A;
    basic_opcodes[0x13] = &CPU::INC_DE;
    basic_opcodes[0x14] = &CPU::INC_D;
    basic_opcodes[0x15] = &CPU::DEC_D;
    basic_opcodes[0x16] = &CPU::LD_D_n;
    basic_opcodes[0x17] = &CPU::RLA;
    basic_opcodes[0x18] = &CPU::JR;
    basic_opcodes[0x19] = &CPU::ADD_HL_DE;
    basic_opcodes[0x1A] = &CPU::LD_A_mDE;
    basic_opcodes[0x1B] = &CPU::DEC_DE;
    basic_opcodes[0x1C] = &CPU::INC_E;
    basic_opcodes[0x1D] = &CPU::DEC_E;
    basic_opcodes[0x1E] = &CPU::LD_E_n;
    basic_opcodes[0x1F] = &CPU::RRA;
    basic_opcodes[0x20] = &CPU::JR_NZ;
    basic_opcodes[0x21] = &CPU::LD_HL_nn;
    basic_opcodes[0x22] = &CPU::LD_mnn_HL;
    basic_opcodes[0x23] = &CPU::INC_HL;
    basic_opcodes[0x24] = &CPU::INC_H;
    basic_opcodes[0x25] = &CPU::DEC_H;
    basic_opcodes[0x26] = &CPU::LD_H_n;
    basic_opcodes[0x27] = &CPU::DAA;
    basic_opcodes[0x28] = &CPU::JR_Z;
    basic_opcodes[0x29] = &CPU::ADD_HL_HL;
    basic_opcodes[0x2A] = &CPU::LD_HL_mnn;
    basic_opcodes[0x2B] = &CPU::DEC_HL;
    basic_opcodes[0x2C] = &CPU::INC_L;
    basic_opcodes[0x2D] = &CPU::DEC_L;
    basic_opcodes[0x2E] = &CPU::LD_L_n;
    basic_opcodes[0x2F] = &CPU::CPL;
    basic_opcodes[0x30] = &CPU::JR_NC;
    basic_opcodes[0x31] = &CPU::LD_SP_nn;
    basic_opcodes[0x32] = &CPU::LD_mnn_A;
    basic_opcodes[0x33] = &CPU::INC_SP;
    basic_opcodes[0x34] = &CPU::INC_mHL;
    basic_opcodes[0x35] = &CPU::DEC_mHL;
    basic_opcodes[0x36] = &CPU::LD_mHL_n;
    basic_opcodes[0x37] = &CPU::SCF;
    basic_opcodes[0x38] = &CPU::JR_C;
    basic_opcodes[0x39] = &CPU::ADD_HL_SP;
    basic_opcodes[0x3A] = &CPU::LD_A_mnn;
    basic_opcodes[0x3B] = &CPU::DEC_SP;
    basic_opcodes[0x3C] = &CPU::INC_A;
    basic_opcodes[0x3D] = &CPU::DEC_A;
    basic_opcodes[0x3E] = &CPU::LD_A_n;
    basic_opcodes[0x3F] = &CPU::CCF;
    basic_opcodes[0x40] = &CPU::LD_B_B;
    basic_opcodes[0x41] = &CPU::LD_B_C;
    basic_opcodes[0x42] = &CPU::LD_B_D;
    basic_opcodes[0x43] = &CPU::LD_B_E;
    basic_opcodes[0x44] = &CPU::LD_B_H;
    basic_opcodes[0x45] = &CPU::LD_B_L;
    basic_opcodes[0x46] = &CPU::LD_B_mHL;
    basic_opcodes[0x47] = &CPU::LD_B_A;
    basic_opcodes[0x48] = &CPU::LD_C_B;
    basic_opcodes[0x49] = &CPU::LD_C_C;
    basic_opcodes[0x4A] = &CPU::LD_C_D;
    basic_opcodes[0x4B] = &CPU::LD_C_E;
    basic_opcodes[0x4C] = &CPU::LD_C_H;
    basic_opcodes[0x4D] = &CPU::LD_C_L;
    basic_opcodes[0x4E] = &CPU::LD_C_mHL;
    basic_opcodes[0x4F] = &CPU::LD_C_A;
    basic_opcodes[0x50] = &CPU::LD_D_B;
    basic_opcodes[0x51] = &CPU::LD_D_C;
    basic_opcodes[0x52] = &CPU::LD_D_D;
    basic_opcodes[0x53] = &CPU::LD_D_E;
    basic_opcodes[0x54] = &CPU::LD_D_H;
    basic_opcodes[0x55] = &CPU::LD_D_L;
    basic_opcodes[0x56] = &CPU::LD_D_mHL;
    basic_opcodes[0x57] = &CPU::LD_D_A;
    basic_opcodes[0x58] = &CPU::LD_E_B;
    basic_opcodes[0x59] = &CPU::LD_E_C;
    basic_opcodes[0x5A] = &CPU::LD_E_D;
    basic_opcodes[0x5B] = &CPU::LD_E_E;
    basic_opcodes[0x5C] = &CPU::LD_E_H;
    basic_opcodes[0x5D] = &CPU::LD_E_L;
    basic_opcodes[0x5E] = &CPU::LD_E_mHL;
    basic_opcodes[0x5F] = &CPU::LD_E_A;
    basic_opcodes[0x60] = &CPU::LD_H_B;
    basic_opcodes[0x61] = &CPU::LD_H_C;
    basic_opcodes[0x62] = &CPU::LD_H_D;
    basic_opcodes[0x63] = &CPU::LD_H_E;
    basic_opcodes[0x64] = &CPU::LD_H_H;
    basic_opcodes[0x65] = &CPU::LD_H_L;
    basic_opcodes[0x66] = &CPU::LD_H_mHL;
    basic_opcodes[0x67] = &CPU::LD_H_A;
    basic_opcodes[0x68] = &CPU::LD_L_B;
    basic_opcodes[0x69] = &CPU::LD_L_C;
    basic_opcodes[0x6A] = &CPU::LD_L_D;
    basic_opcodes[0x6B] = &CPU::LD_L_E;
    basic_opcodes[0x6C] = &CPU::LD_L_H;
    basic_opcodes[0x6D] = &CPU::LD_L_L;
    basic_opcodes[0x6E] = &CPU::LD_L_mHL;
    basic_opcodes[0x6F] = &CPU::LD_L_A;
    basic_opcodes[0x70] = &CPU::LD_mHL_B;
    basic_opcodes[0x71] = &CPU::LD_mHL_C;
    basic_opcodes[0x72] = &CPU::LD_mHL_D;
    basic_opcodes[0x73] = &CPU::LD_mHL_E;
    basic_opcodes[0x74] = &CPU::LD_mHL_H;
    basic_opcodes[0x75] = &CPU::LD_mHL_L;
    basic_opcodes[0x76] = &CPU::HALT;
    basic_opcodes[0x77] = &CPU::LD_mHL_A;
    basic_opcodes[0x78] = &CPU::LD_A_B;
    basic_opcodes[0x79] = &CPU::LD_A_C;
    basic_opcodes[0x7A] = &CPU::LD_A_D;
    basic_opcodes[0x7B] = &CPU::LD_A_E;
    basic_opcodes[0x7C] = &CPU::LD_A_H;
    basic_opcodes[0x7D] = &CPU::LD_A_L;
    basic_opcodes[0x7E] = &CPU::LD_A_mHL;
    basic_opcodes[0x7F] = &CPU::LD_A_A;
    basic_opcodes[0x80] = &CPU::ADD_A_B;
    basic_opcodes[0x81] = &CPU::ADD_A_C;
    basic_opcodes[0x82] = &CPU::ADD_A_D;
    basic_opcodes[0x83] = &CPU::ADD_A_E;
    basic_opcodes[0x84] = &CPU::ADD_A_H;
    basic_opcodes[0x85] = &CPU::ADD_A_L;
    basic_opcodes[0x86] = &CPU::ADD_A_mHL;
    basic_opcodes[0x87] = &CPU::ADD_A_A;
    basic_opcodes[0x88] = &CPU::ADC_A_B;
    basic_opcodes[0x89] = &CPU::ADC_A_C;
    basic_opcodes[0x8A] = &CPU::ADC_A_D;
    basic_opcodes[0x8B] = &CPU::ADC_A_E;
    basic_opcodes[0x8C] = &CPU::ADC_A_H;
    basic_opcodes[0x8D] = &CPU::ADC_A_L;
    basic_opcodes[0x8E] = &CPU::ADC_A_mHL;
    basic_opcodes[0x8F] = &CPU::ADC_A_A;
    basic_opcodes[0x90] = &CPU::SUB_B;
    basic_opcodes[0x91] = &CPU::SUB_C;
    basic_opcodes[0x92] = &CPU::SUB_D;
    basic_opcodes[0x93] = &CPU::SUB_E;
    basic_opcodes[0x94] = &CPU::SUB_H;
    basic_opcodes[0x95] = &CPU::SUB_L;
    basic_opcodes[0x96] = &CPU::SUB_mHL;
    basic_opcodes[0x97] = &CPU::SUB_A;
    basic_opcodes[0x98] = &CPU::SBC_A_B;
    basic_opcodes[0x99] = &CPU::SBC_A_C;
    basic_opcodes[0x9A] = &CPU::SBC_A_D;
    basic_opcodes[0x9B] = &CPU::SBC_A_E;
    basic_opcodes[0x9C] = &CPU::SBC_A_H;
    basic_opcodes[0x9D] = &CPU::SBC_A_L;
    basic_opcodes[0x9E] = &CPU::SBC_A_mHL;
    basic_opcodes[0x9F] = &CPU::SBC_A_A;
    basic_opcodes[0xA0] = &CPU::AND_B;
    basic_opcodes[0xA1] = &CPU::AND_C;
    basic_opcodes[0xA2] = &CPU::AND_D;
    basic_opcodes[0xA3] = &CPU::AND_E;
    basic_opcodes[0xA4] = &CPU::AND_H;
    basic_opcodes[0xA5] = &CPU::AND_L;
    basic_opcodes[0xA6] = &CPU::AND_mHL;
    basic_opcodes[0xA7] = &CPU::AND_A;
    basic_opcodes[0xA8] = &CPU::XOR_B;
    basic_opcodes[0xA9] = &CPU::XOR_C;
    basic_opcodes[0xAA] = &CPU::XOR_D;
    basic_opcodes[0xAB] = &CPU::XOR_E;
    basic_opcodes[0xAC] = &CPU::XOR_H;
    basic_opcodes[0xAD] = &CPU::XOR_L;
    basic_opcodes[0xAE] = &CPU::XOR_mHL;
    basic_opcodes[0xAF] = &CPU::XOR_A;
    basic_opcodes[0xB0] = &CPU::OR_B;
    basic_opcodes[0xB1] = &CPU::OR_C;
    basic_opcodes[0xB2] = &CPU::OR_D;
    basic_opcodes[0xB3] = &CPU::OR_E;
    basic_opcodes[0xB4] = &CPU::OR_H;
    basic_opcodes[0xB5] = &CPU::OR_L;
    basic_opcodes[0xB6] = &CPU::OR_mHL;
    basic_opcodes[0xB7] = &CPU::OR_A;
    basic_opcodes[0xB8] = &CPU::CP_B;
    basic_opcodes[0xB9] = &CPU::CP_C;
    basic_opcodes[0xBA] = &CPU::CP_D;
    basic_opcodes[0xBB] = &CPU::CP_E;
    basic_opcodes[0xBC] = &CPU::CP_H;
    basic_opcodes[0xBD] = &CPU::CP_L;
    basic_opcodes[0xBE] = &CPU::CP_mHL;
    basic_opcodes[0xBF] = &CPU::CP_A;
    basic_opcodes[0xC0] = &CPU::RET_NZ;
    basic_opcodes[0xC1] = &CPU::POP_BC;
    basic_opcodes[0xC2] = &CPU::JP_NZ_nn;
    basic_opcodes[0xC3] = &CPU::JP_nn;
    basic_opcodes[0xC4] = &CPU::CALL_NZ_nn;
    basic_opcodes[0xC5] = &CPU::PUSH_BC;
    basic_opcodes[0xC6] = &CPU::ADD_A_n;
    basic_opcodes[0xC7] = &CPU::RST_00;
    basic_opcodes[0xC8] = &CPU::RET_Z;
    basic_opcodes[0xC9] = &CPU::RET;
    basic_opcodes[0xCA] = &CPU::JP_Z_nn;
    basic_opcodes[0xCB] = &CPU::PREFIX_CB;
    basic_opcodes[0xCC] = &CPU::CALL_Z_nn;
    basic_opcodes[0xCD] = &CPU::CALL_nn;
    basic_opcodes[0xCE] = &CPU::ADC_A_n;
    basic_opcodes[0xCF] = &CPU::RST_08;
    basic_opcodes[0xD0] = &CPU::RET_NC;
    basic_opcodes[0xD1] = &CPU::POP_DE;
    basic_opcodes[0xD2] = &CPU::JP_NC_nn;
    basic_opcodes[0xD3] = &CPU::OUT_n_A;
    basic_opcodes[0xD4] = &CPU::CALL_NC_nn;
    basic_opcodes[0xD5] = &CPU::PUSH_DE;
    basic_opcodes[0xD6] = &CPU::SUB_n;
    basic_opcodes[0xD7] = &CPU::RST_10;
    basic_opcodes[0xD8] = &CPU::RET_C;
    basic_opcodes[0xD9] = &CPU::EXX;
    basic_opcodes[0xDA] = &CPU::JP_C_nn;
    basic_opcodes[0xDB] = &CPU::IN_A_n;
    basic_opcodes[0xDC] = &CPU::CALL_C_nn;
    basic_opcodes[0xDD] = &CPU::PREFIX_DD;
    basic_opcodes[0xDE] = &CPU::SBC_A_n;
    basic_opcodes[0xDF] = &CPU::RST_18;
    basic_opcodes[0xE0] = &CPU::RET_PO;
    basic_opcodes[0xE1] = &CPU::POP_HL;
    basic_opcodes[0xE2] = &CPU::JP_PO_nn;
    basic_opcodes[0xE3] = &CPU::EX_mSP_HL;
    basic_opcodes[0xE4] = &CPU::CALL_PO_nn;
    basic_opcodes[0xE5] = &CPU::PUSH_HL;
    basic_opcodes[0xE6] = &CPU::AND_n;
    basic_opcodes[0xE7] = &CPU::RST_20;
    basic_opcodes[0xE8] = &CPU::RET_PE;
    basic_opcodes[0xE9] = &CPU::JP_HL;
    basic_opcodes[0xEA] = &CPU::JP_PE_nn;
    basic_opcodes[0xEB] = &CPU::EX_DE_HL;
    basic_opcodes[0xEC] = &CPU::CALL_PE_nn;
    basic_opcodes[0xED] = &CPU::PREFIX_ED;
    basic_opcodes[0xEE] = &CPU::XOR_n;
    basic_opcodes[0xEF] = &CPU::RST_28;
    basic_opcodes[0xF0] = &CPU::RET_P;
    basic_opcodes[0xF1] = &CPU::POP_AF;
    basic_opcodes[0xF2] = &CPU::JP_P_nn;
    basic_opcodes[0xF3] = &CPU::DI;
    basic_opcodes[0xF4] = &CPU::CALL_P_nn;
    basic_opcodes[0xF5] = &CPU::PUSH_AF;
    basic_opcodes[0xF6] = &CPU::OR_n;
    basic_opcodes[0xF7] = &CPU::RST_30;
    basic_opcodes[0xF8] = &CPU::RET_M;
    basic_opcodes[0xF9] = &CPU::LD_SP_HL;
    basic_opcodes[0xFA] = &CPU::JP_M_nn;
    basic_opcodes[0xFB] = &CPU::EI;
    basic_opcodes[0xFC] = &CPU::CALL_M_nn;
    basic_opcodes[0xFD] = &CPU::PREFIX_FD;
    basic_opcodes[0xFE] = &CPU::CP_n;
    basic_opcodes[0xFF] = &CPU::RST_38;
    
    // Initialize ED instruction table - most entries default to ED_NOP
    ED_opcodes.fill(&CPU::ED_NOP);
    
    // Map implemented ED instructions
    
    // 16-bit arithmetic operations
    ED_opcodes[0x42] = &CPU::SBC_HL_BC;  // ED 42 - SBC HL, BC
    ED_opcodes[0x4A] = &CPU::ADC_HL_BC;  // ED 4A - ADC HL, BC
    ED_opcodes[0x52] = &CPU::SBC_HL_DE;  // ED 52 - SBC HL, DE
    ED_opcodes[0x5A] = &CPU::ADC_HL_DE;  // ED 5A - ADC HL, DE
    ED_opcodes[0x62] = &CPU::SBC_HL_HL;  // ED 62 - SBC HL, HL
    ED_opcodes[0x6A] = &CPU::ADC_HL_HL;  // ED 6A - ADC HL, HL
    ED_opcodes[0x72] = &CPU::SBC_HL_SP;  // ED 72 - SBC HL, SP
    ED_opcodes[0x7A] = &CPU::ADC_HL_SP;  // ED 7A - ADC HL, SP
    
    // 16-bit load/store operations
    ED_opcodes[0x43] = &CPU::LD_mnn_BC;  // ED 43 - LD (nn), BC
    ED_opcodes[0x4B] = &CPU::LD_BC_mnn;  // ED 4B - LD BC, (nn)
    ED_opcodes[0x53] = &CPU::LD_mnn_DE;  // ED 53 - LD (nn), DE
    ED_opcodes[0x5B] = &CPU::LD_DE_mnn;  // ED 5B - LD DE, (nn)
    ED_opcodes[0x63] = &CPU::LD_mnn_HL_ED;  // ED 63 - LD (nn), HL (ED version)
    ED_opcodes[0x6B] = &CPU::LD_HL_mnn_ED;  // ED 6B - LD HL, (nn) (ED version)
    ED_opcodes[0x73] = &CPU::LD_mnn_SP;  // ED 73 - LD (nn), SP
    ED_opcodes[0x7B] = &CPU::LD_SP_mnn;  // ED 7B - LD SP, (nn)
    
    // Special operations and register transfers
    ED_opcodes[0x44] = &CPU::NEG;        // ED 44 - NEG (negate A)
    ED_opcodes[0x4C] = &CPU::NEG;        // ED 4C - NEG (alternate, undocumented)
    ED_opcodes[0x54] = &CPU::NEG;        // ED 54 - NEG (alternate, undocumented)
    ED_opcodes[0x5C] = &CPU::NEG;        // ED 5C - NEG (alternate, undocumented)
    ED_opcodes[0x64] = &CPU::NEG;        // ED 64 - NEG (alternate, undocumented)
    ED_opcodes[0x6C] = &CPU::NEG;        // ED 6C - NEG (alternate, undocumented)
    ED_opcodes[0x74] = &CPU::NEG;        // ED 74 - NEG (alternate, undocumented)
    ED_opcodes[0x7C] = &CPU::NEG;        // ED 7C - NEG (alternate, undocumented)
    
    ED_opcodes[0x45] = &CPU::RETN;       // ED 45 - RETN (return from NMI)
    ED_opcodes[0x55] = &CPU::RETN;       // ED 55 - RETN (alternate, undocumented)
    ED_opcodes[0x5D] = &CPU::RETN;       // ED 5D - RETN (alternate, undocumented)
    ED_opcodes[0x65] = &CPU::RETN;       // ED 65 - RETN (alternate, undocumented)
    ED_opcodes[0x6D] = &CPU::RETN;       // ED 6D - RETN (alternate, undocumented)
    ED_opcodes[0x75] = &CPU::RETN;       // ED 75 - RETN (alternate, undocumented)
    ED_opcodes[0x7D] = &CPU::RETN;       // ED 7D - RETN (alternate, undocumented)
    
    ED_opcodes[0x76] = &CPU::SLL_mHL;    // ED 76 - SLL (HL) (shift left logical, undocumented)
    
    ED_opcodes[0x46] = &CPU::IM_0;       // ED 46 - IM 0 (interrupt mode 0)
    ED_opcodes[0x4E] = &CPU::IM_0;       // ED 4E - IM 0 (alternate, undocumented)
    ED_opcodes[0x66] = &CPU::IM_0;       // ED 66 - IM 0 (alternate, undocumented)
    ED_opcodes[0x6E] = &CPU::IM_0;       // ED 6E - IM 0 (alternate, undocumented)
    
    ED_opcodes[0x47] = &CPU::LD_I_A;     // ED 47 - LD I, A
    ED_opcodes[0x4D] = &CPU::RETI;       // ED 4D - RETI (return from interrupt)
    ED_opcodes[0x4F] = &CPU::LD_R_A;     // ED 4F - LD R, A
    ED_opcodes[0x56] = &CPU::IM_1;       // ED 56 - IM 1 (interrupt mode 1)
    ED_opcodes[0x57] = &CPU::LD_A_I;     // ED 57 - LD A, I
    ED_opcodes[0x5E] = &CPU::IM_2;       // ED 5E - IM 2 (interrupt mode 2)
    ED_opcodes[0x5F] = &CPU::LD_A_R;     // ED 5F - LD A, R
    ED_opcodes[0x67] = &CPU::RRD;        // ED 67 - RRD (rotate right decimal)
    ED_opcodes[0x6F] = &CPU::RLD;        // ED 6F - RLD (rotate left decimal)
    
    // Individual I/O operations using C register for port
    ED_opcodes[0x40] = &CPU::IN_B_C;     // ED 40 - IN B, (C)
    ED_opcodes[0x41] = &CPU::OUT_C_B;    // ED 41 - OUT (C), B
    ED_opcodes[0x48] = &CPU::IN_C_C;     // ED 48 - IN C, (C)
    ED_opcodes[0x49] = &CPU::OUT_C_C;    // ED 49 - OUT (C), C
    ED_opcodes[0x50] = &CPU::IN_D_C;     // ED 50 - IN D, (C)
    ED_opcodes[0x51] = &CPU::OUT_C_D;    // ED 51 - OUT (C), D
    ED_opcodes[0x58] = &CPU::IN_E_C;     // ED 58 - IN E, (C)
    ED_opcodes[0x59] = &CPU::OUT_C_E;    // ED 59 - OUT (C), E
    ED_opcodes[0x60] = &CPU::IN_H_C;     // ED 60 - IN H, (C)
    ED_opcodes[0x61] = &CPU::OUT_C_H;    // ED 61 - OUT (C), H
    ED_opcodes[0x68] = &CPU::IN_L_C;     // ED 68 - IN L, (C)
    ED_opcodes[0x69] = &CPU::OUT_C_L;    // ED 69 - OUT (C), L
    ED_opcodes[0x70] = &CPU::IN_F_C;     // ED 70 - IN F, (C) (undocumented - sets flags only)
    ED_opcodes[0x71] = &CPU::OUT_C_0;    // ED 71 - OUT (C), 0 (undocumented)
    ED_opcodes[0x78] = &CPU::IN_A_C;     // ED 78 - IN A, (C)
    ED_opcodes[0x79] = &CPU::OUT_C_A;    // ED 79 - OUT (C), A
    
    // Block operations
    ED_opcodes[0xA0] = &CPU::LDI;        // ED A0 - LDI (load and increment)
    ED_opcodes[0xA1] = &CPU::CPI;        // ED A1 - CPI (compare and increment)
    ED_opcodes[0xA2] = &CPU::INI;        // ED A2 - INI (input and increment)
    ED_opcodes[0xA3] = &CPU::OUTI;       // ED A3 - OUTI (output and increment)
    ED_opcodes[0xA8] = &CPU::LDD;        // ED A8 - LDD (load and decrement)
    ED_opcodes[0xA9] = &CPU::CPD;        // ED A9 - CPD (compare and decrement)
    ED_opcodes[0xAA] = &CPU::IND;        // ED AA - IND (input and decrement)
    ED_opcodes[0xAB] = &CPU::OUTD;       // ED AB - OUTD (output and decrement)
    ED_opcodes[0xB0] = &CPU::LDIR;       // ED B0 - LDIR (load, increment, repeat)
    ED_opcodes[0xB1] = &CPU::CPIR;       // ED B1 - CPIR (compare, increment, repeat)
    ED_opcodes[0xB2] = &CPU::INIR;       // ED B2 - INIR (input, increment, repeat)
    ED_opcodes[0xB3] = &CPU::OTIR;       // ED B3 - OTIR (output, increment, repeat)
    ED_opcodes[0xB8] = &CPU::LDDR;       // ED B8 - LDDR (load, decrement, repeat)
    ED_opcodes[0xB9] = &CPU::CPDR;       // ED B9 - CPDR (compare, decrement, repeat)
    ED_opcodes[0xBA] = &CPU::INDR;       // ED BA - INDR (input, decrement, repeat)
    ED_opcodes[0xBB] = &CPU::OTDR;       // ED BB - OTDR (output, decrement, repeat)
}

// =============================================================================
// Helper Functions
// =============================================================================

void CPU::SetCarryFlag(bool value) {
    if (value) {
        F() |= Constants::Flags::CARRY;
    } else {
        F() &= ~Constants::Flags::CARRY;
    }
}

bool CPU::GetCarryFlag() const {
    return (_AF.r8.lo & Constants::Flags::CARRY) != 0;
}

// =============================================================================
// Basic Instructions (0x00-0x3F)
// =============================================================================

void CPU::NOP() {
    t_cycle += 4;
}

void CPU::LD_BC_nn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    BC() = WZ();
    PC() += 2;
    t_cycle += 10;
}

void CPU::LD_mBC_A() {
    WZ() = BC();
    memory[WZ()] = A();
    t_cycle += 7;
}

void CPU::INC_BC() {
    BC()++;
    t_cycle += 6;
}

void CPU::INC_B() {
    uint8_t old_b = B();
    B()++;
    F() &= 0x01; // Preserve carry
    if (B() == 0) F() |= 0x40; // Zero
    if (B() & 0x80) F() |= 0x80; // Sign
    if ((old_b & 0x0F) == 0x0F) F() |= 0x10; // Half-carry
    if (old_b == 0x7F) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::DEC_B() {
    uint8_t old_b = B();
    B()--;
    F() &= 0x01; // Preserve carry
    F() |= 0x02; // Set N flag
    if (B() == 0) F() |= 0x40; // Zero
    if (B() & 0x80) F() |= 0x80; // Sign
    if ((old_b & 0x0F) == 0) F() |= 0x10; // Half-carry
    if (old_b == 0x80) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::LD_B_n() {
    B() = memory[PC()++];
    t_cycle += 7;
}

void CPU::RLCA() {
    uint8_t old_bit7 = (A() & 0x80) ? 1 : 0;
    A() = (A() << 1) | old_bit7;
    F() = (F() & 0xEC) | old_bit7;
    t_cycle += 4;
}

void CPU::EX_AF_AF() {
    uint16_t temp = AF();
    AF() = _AF1.r16;
    _AF1.r16 = temp;
    t_cycle += 4;
}

void CPU::ADD_HL_BC() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    uint32_t result = hl_reg + BC();
    F() &= 0xC4; // Preserve S, Z, P/V
    if (result & 0x10000) F() |= 0x01; // Carry
    if (((hl_reg & 0x0FFF) + (BC() & 0x0FFF)) & 0x1000) F() |= 0x10; // Half-carry
    hl_reg = result & 0xFFFF;
    
    // Timing: HL=11 cycles, IX/IY=15 cycles (prefix adds 4 cycles)
    t_cycle += (current_state == CPUState::NORMAL) ? 11 : 15;
}

void CPU::LD_A_mBC() {
    WZ() = BC();
    A() = memory[WZ()];
    t_cycle += 7;
}

void CPU::DEC_BC() {
    BC()--;
    t_cycle += 6;
}

void CPU::INC_C() {
    uint8_t old_c = C();
    C()++;
    F() &= 0x01; // Preserve carry
    if (C() == 0) F() |= 0x40; // Zero
    if (C() & 0x80) F() |= 0x80; // Sign
    if ((old_c & 0x0F) == 0x0F) F() |= 0x10; // Half-carry
    if (old_c == 0x7F) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::DEC_C() {
    uint8_t old_c = C();
    C()--;
    F() &= 0x01; // Preserve carry
    F() |= 0x02; // Set N flag
    if (C() == 0) F() |= 0x40; // Zero
    if (C() & 0x80) F() |= 0x80; // Sign
    if ((old_c & 0x0F) == 0) F() |= 0x10; // Half-carry
    if (old_c == 0x80) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::LD_C_n() {
    C() = memory[PC()++];
    t_cycle += 7;
}

void CPU::RRCA() {
    uint8_t old_bit0 = A() & 0x01;
    A() = (A() >> 1) | (old_bit0 << 7);
    F() = (F() & 0xEC) | old_bit0;
    t_cycle += 4;
}

void CPU::DJNZ() {
    int8_t displacement = memory[PC()++];
    B()--;
    if (B() != 0) {
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 13;
    } else {
        t_cycle += 8;
    }
}

void CPU::LD_DE_nn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    DE() = WZ();
    PC() += 2;
    t_cycle += 10;
}

void CPU::LD_mDE_A() {
    WZ() = DE();
    memory[WZ()] = A();
    t_cycle += 7;
}

void CPU::INC_DE() {
    DE()++;
    t_cycle += 6;
}

void CPU::INC_D() {
    uint8_t old_d = D();
    D()++;
    F() &= 0x01; // Preserve carry
    if (D() == 0) F() |= 0x40; // Zero
    if (D() & 0x80) F() |= 0x80; // Sign
    if ((old_d & 0x0F) == 0x0F) F() |= 0x10; // Half-carry
    if (old_d == 0x7F) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::DEC_D() {
    uint8_t old_d = D();
    D()--;
    F() &= 0x01; // Preserve carry
    F() |= 0x02; // Set N flag
    if (D() == 0) F() |= 0x40; // Zero
    if (D() & 0x80) F() |= 0x80; // Sign
    if ((old_d & 0x0F) == 0) F() |= 0x10; // Half-carry
    if (old_d == 0x80) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::LD_D_n() {
    D() = memory[PC()++];
    t_cycle += 7;
}

void CPU::RLA() {
    uint8_t old_carry = F() & 0x01;
    uint8_t new_carry = (A() & 0x80) ? 1 : 0;
    A() = (A() << 1) | old_carry;
    F() = (F() & 0xEC) | new_carry;
    t_cycle += 4;
}

void CPU::JR() {
    int8_t displacement = memory[PC()++];
    WZ() = PC() + displacement;
    PC() = WZ();
    t_cycle += 12;
}

void CPU::ADD_HL_DE() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    uint32_t result = hl_reg + DE();
    F() &= 0xC4; // Preserve S, Z, P/V
    if (result & 0x10000) F() |= 0x01; // Carry
    if (((hl_reg & 0x0FFF) + (DE() & 0x0FFF)) & 0x1000) F() |= 0x10; // Half-carry
    hl_reg = result & 0xFFFF;
    
    // Timing: HL=11 cycles, IX/IY=15 cycles (prefix adds 4 cycles)
    t_cycle += (current_state == CPUState::NORMAL) ? 11 : 15;
}

void CPU::LD_A_mDE() {
    WZ() = DE();
    A() = memory[WZ()];
    t_cycle += 7;
}

void CPU::DEC_DE() {
    DE()--;
    t_cycle += 6;
}

void CPU::INC_E() {
    uint8_t old_e = E();
    E()++;
    F() &= 0x01; // Preserve carry
    if (E() == 0) F() |= 0x40; // Zero
    if (E() & 0x80) F() |= 0x80; // Sign
    if ((old_e & 0x0F) == 0x0F) F() |= 0x10; // Half-carry
    if (old_e == 0x7F) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::DEC_E() {
    uint8_t old_e = E();
    E()--;
    F() &= 0x01; // Preserve carry
    F() |= 0x02; // Set N flag
    if (E() == 0) F() |= 0x40; // Zero
    if (E() & 0x80) F() |= 0x80; // Sign
    if ((old_e & 0x0F) == 0) F() |= 0x10; // Half-carry
    if (old_e == 0x80) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::LD_E_n() {
    E() = memory[PC()++];
    t_cycle += 7;
}

void CPU::RRA() {
    uint8_t old_carry = F() & 0x01;
    uint8_t new_carry = A() & 0x01;
    A() = (A() >> 1) | (old_carry << 7);
    F() = (F() & 0xEC) | new_carry;
    t_cycle += 4;
}

void CPU::JR_NZ() {
    int8_t displacement = memory[PC()++];
    if (!(F() & 0x40)) { // Zero flag not set
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 12;
    } else {
        t_cycle += 7;
    }
}

void CPU::LD_HL_nn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    GetEffectiveHL_Register() = WZ();
    PC() += 2;
    t_cycle += 10; // Base instruction timing - prefix adds its own 4 cycles
}

void CPU::LD_mnn_HL() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    PC() += 2;
    uint16_t& hl_reg = GetEffectiveHL_Register();
    memory[WZ()] = hl_reg & 0xFF;        // Low byte
    memory[WZ() + 1] = (hl_reg >> 8);    // High byte
    t_cycle += 16;
}

void CPU::INC_HL() {
    GetEffectiveHL_Register()++;
    t_cycle += GetRegisterOpCycles();
}

void CPU::INC_H() {
    uint8_t& h_reg = GetEffectiveH();
    uint8_t old_h = h_reg;
    h_reg++;
    F() &= 0x01; // Preserve carry
    if (h_reg == 0) F() |= 0x40; // Zero
    if (h_reg & 0x80) F() |= 0x80; // Sign
    if ((old_h & 0x0F) == 0x0F) F() |= 0x10; // Half-carry
    if (old_h == 0x7F) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::DEC_H() {
    uint8_t& h_reg = GetEffectiveH();
    uint8_t old_h = h_reg;
    h_reg--;
    F() &= 0x01; // Preserve carry
    F() |= 0x02; // Set N flag
    if (h_reg == 0) F() |= 0x40; // Zero
    if (h_reg & 0x80) F() |= 0x80; // Sign
    if ((old_h & 0x0F) == 0) F() |= 0x10; // Half-carry
    if (old_h == 0x80) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::LD_H_n() {
    GetEffectiveH() = memory[PC()++];
    t_cycle += 7;
}

void CPU::DAA() {
    uint8_t correction = 0;
    bool carry = F() & 0x01;
    
    if ((A() & 0x0F) > 9 || (F() & 0x10)) {
        correction += 0x06;
    }
    
    if (A() > 0x99 || carry) {
        correction += 0x60;
        F() |= 0x01; // Set carry
    } else {
        F() &= 0xFE; // Clear carry
    }
    
    if (F() & 0x02) { // N flag set (subtraction)
        A() -= correction;
    } else {
        A() += correction;
    }
    
    F() &= 0x13; // Preserve C, N
    if (A() == 0) F() |= 0x40; // Zero
    if (A() & 0x80) F() |= 0x80; // Sign
    // Parity calculation would go here
    t_cycle += 4;
}

void CPU::JR_Z() {
    int8_t displacement = memory[PC()++];
    if (F() & 0x40) { // Zero flag set
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 12;
    } else {
        t_cycle += 7;
    }
}

void CPU::ADD_HL_HL() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    uint32_t result = hl_reg + hl_reg;
    F() &= 0xC4; // Preserve S, Z, P/V
    if (result & 0x10000) F() |= 0x01; // Carry
    if (((hl_reg & 0x0FFF) + (hl_reg & 0x0FFF)) & 0x1000) F() |= 0x10; // Half-carry
    hl_reg = result & 0xFFFF;
    
    // Timing: HL=11 cycles, IX/IY=15 cycles (prefix adds 4 cycles)
    t_cycle += (current_state == CPUState::NORMAL) ? 11 : 15;
}

void CPU::LD_HL_mnn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    PC() += 2;
    uint16_t& hl_reg = GetEffectiveHL_Register();
    hl_reg = memory[WZ()] | (memory[WZ() + 1] << 8);
    t_cycle += 16;
}

void CPU::DEC_HL() {
    GetEffectiveHL_Register()--;
    t_cycle += GetRegisterOpCycles();
}

void CPU::INC_L() {
    uint8_t& l_reg = GetEffectiveL();
    uint8_t old_l = l_reg;
    l_reg++;
    F() &= 0x01; // Preserve carry
    if (l_reg == 0) F() |= 0x40; // Zero
    if (l_reg & 0x80) F() |= 0x80; // Sign
    if ((old_l & 0x0F) == 0x0F) F() |= 0x10; // Half-carry
    if (old_l == 0x7F) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::DEC_L() {
    uint8_t& l_reg = GetEffectiveL();
    uint8_t old_l = l_reg;
    l_reg--;
    F() &= 0x01; // Preserve carry
    F() |= 0x02; // Set N flag
    if (l_reg == 0) F() |= 0x40; // Zero
    if (l_reg & 0x80) F() |= 0x80; // Sign
    if ((old_l & 0x0F) == 0) F() |= 0x10; // Half-carry
    if (old_l == 0x80) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::LD_L_n() {
    GetEffectiveL() = memory[PC()++];
    t_cycle += 7;
}

void CPU::CPL() {
    A() = ~A();
    F() |= 0x12; // Set N and H flags
    t_cycle += 4;
}

void CPU::JR_NC() {
    int8_t displacement = memory[PC()++];
    if (!(F() & 0x01)) { // Carry flag not set
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 12;
    } else {
        t_cycle += 7;
    }
}

void CPU::LD_SP_nn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    SP() = WZ();
    PC() += 2;
    t_cycle += 10;
}

void CPU::LD_mnn_A() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    PC() += 2;
    memory[WZ()] = A();
    t_cycle += 13;
}

void CPU::INC_SP() {
    SP()++;
    t_cycle += 6;
}

void CPU::INC_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t old_value = value;
    value++;
    memory[address] = value;
    F() &= 0x01; // Preserve carry
    if (value == 0) F() |= 0x40; // Zero
    if (value & 0x80) F() |= 0x80; // Sign
    if ((old_value & 0x0F) == 0x0F) F() |= 0x10; // Half-carry
    if (old_value == 0x7F) F() |= 0x04; // Overflow
    t_cycle += 11;
}

void CPU::DEC_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t old_value = value;
    value--;
    memory[address] = value;
    F() &= 0x01; // Preserve carry
    F() |= 0x02; // Set N flag
    if (value == 0) F() |= 0x40; // Zero
    if (value & 0x80) F() |= 0x80; // Sign
    if ((old_value & 0x0F) == 0) F() |= 0x10; // Half-carry
    if (old_value == 0x80) F() |= 0x04; // Overflow
    t_cycle += 11;
}

void CPU::LD_mHL_n() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = memory[PC()++];
    t_cycle += GetMemoryAccessCycles() + 3; // Base 7 cycles + 3 for immediate byte
}

void CPU::SCF() {
    F() |= 0x01; // Set carry
    F() &= 0xED; // Clear N and H
    t_cycle += 4;
}

void CPU::JR_C() {
    int8_t displacement = memory[PC()++];
    if (F() & 0x01) { // Carry flag set
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 12;
    } else {
        t_cycle += 7;
    }
}

void CPU::ADD_HL_SP() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    uint32_t result = hl_reg + SP();
    F() &= 0xC4; // Preserve S, Z, P/V
    if (result & 0x10000) F() |= 0x01; // Carry
    if (((hl_reg & 0x0FFF) + (SP() & 0x0FFF)) & 0x1000) F() |= 0x10; // Half-carry
    hl_reg = result & 0xFFFF;
    
    // Timing: HL=11 cycles, IX/IY=15 cycles (prefix adds 4 cycles)
    t_cycle += (current_state == CPUState::NORMAL) ? 11 : 15;
}

void CPU::LD_A_mnn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    PC() += 2;
    A() = memory[WZ()];
    t_cycle += 13;
}

void CPU::DEC_SP() {
    SP()--;
    t_cycle += 6;
}

void CPU::INC_A() {
    uint8_t old_a = A();
    A()++;
    F() &= 0x01; // Preserve carry
    if (A() == 0) F() |= 0x40; // Zero
    if (A() & 0x80) F() |= 0x80; // Sign
    if ((old_a & 0x0F) == 0x0F) F() |= 0x10; // Half-carry
    if (old_a == 0x7F) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::DEC_A() {
    uint8_t old_a = A();
    A()--;
    F() &= 0x01; // Preserve carry
    F() |= 0x02; // Set N flag
    if (A() == 0) F() |= 0x40; // Zero
    if (A() & 0x80) F() |= 0x80; // Sign
    if ((old_a & 0x0F) == 0) F() |= 0x10; // Half-carry
    if (old_a == 0x80) F() |= 0x04; // Overflow
    t_cycle += 4;
}

void CPU::LD_A_n() {
    A() = memory[PC()++];
    t_cycle += 7;
}

void CPU::CCF() {
    F() ^= 0x01; // Flip carry
    F() &= 0xED; // Clear N and H
    t_cycle += 4;
}

void CPU::LD_B_B() {
    // B = B (NOP equivalent)
    t_cycle += 4;
}

void CPU::LD_B_C() {
    B() = C();
    t_cycle += 4;
}

void CPU::LD_B_D() {
    B() = D();
    t_cycle += 4;
}

void CPU::LD_B_E() {
    B() = E();
    t_cycle += 4;
}

void CPU::LD_B_H() {
    B() = GetEffectiveH();
    t_cycle += 4;
}

void CPU::LD_B_L() {
    B() = GetEffectiveL();
    t_cycle += 4;
}

void CPU::LD_B_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    B() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_B_A() {
    B() = A();
    t_cycle += 4;
}

// =============================================================================
// Load Instructions (0x48-0x7F) - Remaining register-to-register transfers
// =============================================================================

void CPU::LD_C_B() {
    C() = B();
    t_cycle += 4;
}

void CPU::LD_C_C() {
    // C = C (NOP equivalent)
    t_cycle += 4;
}

void CPU::LD_C_D() {
    C() = D();
    t_cycle += 4;
}

void CPU::LD_C_E() {
    C() = E();
    t_cycle += 4;
}

void CPU::LD_C_H() {
    C() = GetEffectiveH();
    t_cycle += 4;
}

void CPU::LD_C_L() {
    C() = GetEffectiveL();
    t_cycle += 4;
}

void CPU::LD_C_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    C() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_C_A() {
    C() = A();
    t_cycle += 4;
}

void CPU::LD_D_B() {
    D() = B();
    t_cycle += 4;
}

void CPU::LD_D_C() {
    D() = C();
    t_cycle += 4;
}

void CPU::LD_D_D() {
    // D = D (NOP equivalent)
    t_cycle += 4;
}

void CPU::LD_D_E() {
    D() = E();
    t_cycle += 4;
}

void CPU::LD_D_H() {
    D() = GetEffectiveH();
    t_cycle += 4;
}

void CPU::LD_D_L() {
    D() = GetEffectiveL();
    t_cycle += 4;
}

void CPU::LD_D_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    D() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_D_A() {
    D() = A();
    t_cycle += 4;
}

void CPU::LD_E_B() {
    E() = B();
    t_cycle += 4;
}

void CPU::LD_E_C() {
    E() = C();
    t_cycle += 4;
}

void CPU::LD_E_D() {
    E() = D();
    t_cycle += 4;
}

void CPU::LD_E_E() {
    // E = E (NOP equivalent)
    t_cycle += 4;
}

void CPU::LD_E_H() {
    E() = GetEffectiveH();
    t_cycle += 4;
}

void CPU::LD_E_L() {
    E() = GetEffectiveL();
    t_cycle += 4;
}

void CPU::LD_E_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    E() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_E_A() {
    E() = A();
    t_cycle += 4;
}

void CPU::LD_H_B() {
    GetEffectiveH() = B();
    t_cycle += 4;
}

void CPU::LD_H_C() {
    GetEffectiveH() = C();
    t_cycle += 4;
}

void CPU::LD_H_D() {
    GetEffectiveH() = D();
    t_cycle += 4;
}

void CPU::LD_H_E() {
    GetEffectiveH() = E();
    t_cycle += 4;
}

void CPU::LD_H_H() {
    // H = H (NOP equivalent)
    t_cycle += 4;
}

void CPU::LD_H_L() {
    GetEffectiveH() = GetEffectiveL();
    t_cycle += 4;
}

void CPU::LD_H_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    H() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_H_A() {
    GetEffectiveH() = A();
    t_cycle += 4;
}

void CPU::LD_L_B() {
    GetEffectiveL() = B();
    t_cycle += 4;
}

void CPU::LD_L_C() {
    GetEffectiveL() = C();
    t_cycle += 4;
}

void CPU::LD_L_D() {
    GetEffectiveL() = D();
    t_cycle += 4;
}

void CPU::LD_L_E() {
    GetEffectiveL() = E();
    t_cycle += 4;
}

void CPU::LD_L_H() {
    GetEffectiveL() = GetEffectiveH();
    t_cycle += 4;
}

void CPU::LD_L_L() {
    // L = L (NOP equivalent)
    t_cycle += 4;
}

void CPU::LD_L_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    L() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_L_A() {
    GetEffectiveL() = A();
    t_cycle += 4;
}

void CPU::LD_mHL_B() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = B();
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_mHL_C() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = C();
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_mHL_D() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = D();
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_mHL_E() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = E();
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_mHL_H() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = H();
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_mHL_L() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = L();
    t_cycle += GetMemoryAccessCycles();
}

void CPU::HALT() {
    // HALT instruction - processor stops until interrupt
    _halted = true;
    t_cycle += 4;  // HALT instruction takes 4 cycles
}

void CPU::LD_mHL_A() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = A();
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_A_B() {
    A() = B();
    t_cycle += 4;
}

void CPU::LD_A_C() {
    A() = C();
    t_cycle += 4;
}

void CPU::LD_A_D() {
    A() = D();
    t_cycle += 4;
}

void CPU::LD_A_E() {
    A() = E();
    t_cycle += 4;
}

void CPU::LD_A_H() {
    A() = GetEffectiveH();
    t_cycle += 4;
}

void CPU::LD_A_L() {
    A() = GetEffectiveL();
    t_cycle += 4;
}

void CPU::LD_A_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    A() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

void CPU::LD_A_A() {
    // A = A (NOP equivalent)
    t_cycle += 4;
}

// =============================================================================
// Flag Helper Functions
// =============================================================================

void CPU::SetFlags_ADD(uint8_t result, uint8_t operand1, uint8_t operand2) {
    F() = 0; // Clear all flags
    
    // Sign flag (bit 7)
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    
    // Zero flag
    if (result == 0) F() |= Constants::Flags::ZERO;
    
    // Half carry flag (carry from bit 3 to bit 4)
    if (((operand1 & 0x0F) + (operand2 & 0x0F)) & 0x10) F() |= Constants::Flags::HALF;
    
    // Parity/Overflow flag (overflow for signed arithmetic)
    if (((operand1 ^ result) & (operand2 ^ result) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    
    // Carry flag (carry from bit 7)
    if ((static_cast<uint16_t>(operand1) + static_cast<uint16_t>(operand2)) & 0x100) F() |= Constants::Flags::CARRY;
    
    // N flag is cleared for addition
}

void CPU::SetFlags_SUB(uint8_t result, uint8_t operand1, uint8_t operand2) {
    F() = Constants::Flags::SUBTRACT; // Set N flag for subtraction
    
    // Sign flag (bit 7)
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    
    // Zero flag
    if (result == 0) F() |= Constants::Flags::ZERO;
    
    // Half carry flag (borrow from bit 4 to bit 3)
    if ((operand1 & 0x0F) < (operand2 & 0x0F)) F() |= Constants::Flags::HALF;
    
    // Parity/Overflow flag (overflow for signed arithmetic)
    if (((operand1 ^ operand2) & (operand1 ^ result) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    
    // Carry flag (borrow)
    if (operand1 < operand2) F() |= Constants::Flags::CARRY;
}

void CPU::SetFlags_LOGIC(uint8_t result) {
    F() = 0; // Clear all flags
    
    // Sign flag (bit 7)
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    
    // Zero flag
    if (result == 0) F() |= Constants::Flags::ZERO;
    
    // Half carry flag is set for logical operations
    F() |= Constants::Flags::HALF;
    
    // Parity flag
    F() |= CalculateParity(result);
    
    // Carry flag is cleared for logical operations
    // N flag is cleared for logical operations
}

uint8_t CPU::CalculateParity(uint8_t value) {
    uint8_t parity = 0;
    for (int i = 0; i < 8; ++i) {
        if (value & (1 << i)) parity++;
    }
    return (parity & 1) ? 0 : Constants::Flags::PARITY; // Even parity
}

// =============================================================================
// Arithmetic and Logic Instructions (0x80-0xBF)
// =============================================================================

void CPU::ADD_A_B() {
    uint8_t old_a = A();
    A() += B();
    SetFlags_ADD(A(), old_a, B());
    t_cycle += 4;
}

void CPU::ADD_A_C() {
    uint8_t old_a = A();
    A() += C();
    SetFlags_ADD(A(), old_a, C());
    t_cycle += 4;
}

void CPU::ADD_A_D() {
    uint8_t old_a = A();
    A() += D();
    SetFlags_ADD(A(), old_a, D());
    t_cycle += 4;
}

void CPU::ADD_A_E() {
    uint8_t old_a = A();
    A() += E();
    SetFlags_ADD(A(), old_a, E());
    t_cycle += 4;
}

void CPU::ADD_A_H() {
    uint8_t old_a = A();
    uint8_t h_val = GetEffectiveH();
    A() += h_val;
    SetFlags_ADD(A(), old_a, h_val);
    t_cycle += 4;
}

void CPU::ADD_A_L() {
    uint8_t old_a = A();
    uint8_t l_val = GetEffectiveL();
    A() += l_val;
    SetFlags_ADD(A(), old_a, l_val);
    t_cycle += 4;
}

void CPU::ADD_A_mHL() {
    uint8_t old_a = A();
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    A() += value;
    SetFlags_ADD(A(), old_a, value);
    t_cycle += GetMemoryAccessCycles();
}

void CPU::ADD_A_A() {
    uint8_t old_a = A();
    A() += A();
    SetFlags_ADD(A(), old_a, old_a);
    t_cycle += 4;
}

void CPU::ADC_A_B() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(B()) + carry;
    A() = result & 0xFF;
    
    F() = 0; // Clear all flags
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (B() & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (B() ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::ADC_A_C() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(C()) + carry;
    A() = result & 0xFF;
    
    F() = 0;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (C() & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (C() ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::ADC_A_D() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(D()) + carry;
    A() = result & 0xFF;
    
    F() = 0;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (D() & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (D() ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::ADC_A_E() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(E()) + carry;
    A() = result & 0xFF;
    
    F() = 0;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (E() & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (E() ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::ADC_A_H() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t h_val = GetEffectiveH();
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(h_val) + carry;
    A() = result & 0xFF;
    
    F() = 0;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (h_val & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (h_val ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::ADC_A_L() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t l_val = GetEffectiveL();
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(l_val) + carry;
    A() = result & 0xFF;
    
    F() = 0;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (l_val & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (l_val ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::ADC_A_mHL() {
    uint8_t old_a = A();
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(value) + carry;
    A() = result & 0xFF;
    
    F() = 0;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (value & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (value ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += GetMemoryAccessCycles();
}

void CPU::ADC_A_A() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(A()) + carry;
    A() = result & 0xFF;
    
    F() = 0;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (old_a & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::SUB_B() {
    uint8_t old_a = A();
    A() -= B();
    SetFlags_SUB(A(), old_a, B());
    t_cycle += 4;
}

void CPU::SUB_C() {
    uint8_t old_a = A();
    A() -= C();
    SetFlags_SUB(A(), old_a, C());
    t_cycle += 4;
}

void CPU::SUB_D() {
    uint8_t old_a = A();
    A() -= D();
    SetFlags_SUB(A(), old_a, D());
    t_cycle += 4;
}

void CPU::SUB_E() {
    uint8_t old_a = A();
    A() -= E();
    SetFlags_SUB(A(), old_a, E());
    t_cycle += 4;
}

void CPU::SUB_H() {
    uint8_t old_a = A();
    uint8_t h_val = GetEffectiveH();
    A() -= h_val;
    SetFlags_SUB(A(), old_a, h_val);
    t_cycle += 4;
}

void CPU::SUB_L() {
    uint8_t old_a = A();
    uint8_t l_val = GetEffectiveL();
    A() -= l_val;
    SetFlags_SUB(A(), old_a, l_val);
    t_cycle += 4;
}

void CPU::SUB_mHL() {
    uint8_t old_a = A();
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    A() -= value;
    SetFlags_SUB(A(), old_a, value);
    t_cycle += GetMemoryAccessCycles();
}

void CPU::SUB_A() {
    uint8_t old_a = A();
    A() -= A(); // Result is always 0
    SetFlags_SUB(A(), old_a, old_a);
    t_cycle += 4;
}

void CPU::SBC_A_B() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(B()) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((B() & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ B()) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::SBC_A_C() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(C()) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((C() & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ C()) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::SBC_A_D() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(D()) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((D() & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ D()) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::SBC_A_E() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(E()) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((E() & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ E()) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::SBC_A_H() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t h_val = GetEffectiveH();
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(h_val) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((h_val & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ h_val) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::SBC_A_L() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t l_val = GetEffectiveL();
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(l_val) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((l_val & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ l_val) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::SBC_A_mHL() {
    uint8_t old_a = A();
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(value) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((value & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ value) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += GetMemoryAccessCycles();
}

void CPU::SBC_A_A() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(A()) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((old_a & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ old_a) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 4;
}

void CPU::AND_B() {
    A() &= B();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::AND_C() {
    A() &= C();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::AND_D() {
    A() &= D();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::AND_E() {
    A() &= E();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::AND_H() {
    A() &= GetEffectiveH();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::AND_L() {
    A() &= GetEffectiveL();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::AND_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    A() &= memory[address];
    SetFlags_LOGIC(A());
    t_cycle += GetMemoryAccessCycles();
}

void CPU::AND_A() {
    A() &= A();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::XOR_B() {
    A() ^= B();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::XOR_C() {
    A() ^= C();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::XOR_D() {
    A() ^= D();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::XOR_E() {
    A() ^= E();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::XOR_H() {
    A() ^= GetEffectiveH();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::XOR_L() {
    A() ^= GetEffectiveL();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::XOR_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    A() ^= memory[address];
    SetFlags_LOGIC(A());
    t_cycle += GetMemoryAccessCycles();
}

void CPU::XOR_A() {
    A() ^= A(); // Result is always 0
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::OR_B() {
    A() |= B();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::OR_C() {
    A() |= C();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::OR_D() {
    A() |= D();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::OR_E() {
    A() |= E();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::OR_H() {
    A() |= GetEffectiveH();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::OR_L() {
    A() |= GetEffectiveL();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::OR_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    A() |= memory[address];
    SetFlags_LOGIC(A());
    t_cycle += GetMemoryAccessCycles();
}

void CPU::OR_A() {
    A() |= A();
    SetFlags_LOGIC(A());
    t_cycle += 4;
}

void CPU::CP_B() {
    uint8_t result = A() - B();
    SetFlags_SUB(result, A(), B());
    t_cycle += 4;
}

void CPU::CP_C() {
    uint8_t result = A() - C();
    SetFlags_SUB(result, A(), C());
    t_cycle += 4;
}

void CPU::CP_D() {
    uint8_t result = A() - D();
    SetFlags_SUB(result, A(), D());
    t_cycle += 4;
}

void CPU::CP_E() {
    uint8_t result = A() - E();
    SetFlags_SUB(result, A(), E());
    t_cycle += 4;
}

void CPU::CP_H() {
    uint8_t h_val = GetEffectiveH();
    uint8_t result = A() - h_val;
    SetFlags_SUB(result, A(), h_val);
    t_cycle += 4;
}

void CPU::CP_L() {
    uint8_t l_val = GetEffectiveL();
    uint8_t result = A() - l_val;
    SetFlags_SUB(result, A(), l_val);
    t_cycle += 4;
}

void CPU::CP_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t result = A() - value;
    SetFlags_SUB(result, A(), value);
    t_cycle += GetMemoryAccessCycles();
}

void CPU::CP_A() {
    uint8_t result = A() - A();
    SetFlags_SUB(result, A(), A());
    t_cycle += 4;
}

// =============================================================================
// Stack and Condition Helper Functions
// =============================================================================

void CPU::PushWord(uint16_t value) {
    SP() -= 2;
    memory[SP()] = value & 0xFF;        // Low byte
    memory[SP() + 1] = (value >> 8);    // High byte
}

uint16_t CPU::PopWord() {
    uint16_t value = memory[SP()] | (memory[SP() + 1] << 8);
    SP() += 2;
    return value;
}

bool CPU::CheckCondition(uint8_t condition) {
    switch (condition) {
        case 0: return !(F() & Constants::Flags::ZERO);     // NZ - not zero
        case 1: return (F() & Constants::Flags::ZERO);      // Z - zero
        case 2: return !(F() & Constants::Flags::CARRY);    // NC - no carry
        case 3: return (F() & Constants::Flags::CARRY);     // C - carry
        case 4: return !(F() & Constants::Flags::PARITY);   // PO - parity odd
        case 5: return (F() & Constants::Flags::PARITY);    // PE - parity even
        case 6: return !(F() & Constants::Flags::SIGN);     // P - positive
        case 7: return (F() & Constants::Flags::SIGN);      // M - minus
        default: return false;
    }
}

// =============================================================================
// State-Aware IX/IY Helper Functions
// =============================================================================

uint16_t CPU::GetEffectiveHL_Memory() {
    switch (current_state) {
        case CPUState::NORMAL:
            return HL();
        case CPUState::DD_PREFIX:
            // Displacement consumed atomically as part of instruction execution
            return IX() + static_cast<int8_t>(memory[PC()++]);
        case CPUState::FD_PREFIX:
            // Displacement consumed atomically as part of instruction execution
            return IY() + static_cast<int8_t>(memory[PC()++]);
        case CPUState::DD_CB_PREFIX:
            // For DD CB instructions, displacement was already stored
            return IX() + current_displacement;
        case CPUState::FD_CB_PREFIX:
            // For FD CB instructions, displacement was already stored
            return IY() + current_displacement;
        default:
            return HL();
    }
}

uint16_t& CPU::GetEffectiveHL_Register() {
    switch (current_state) {
        case CPUState::DD_PREFIX:
            return IX();
        case CPUState::FD_PREFIX:
            return IY();
        default:
            return HL();
    }
}

uint8_t& CPU::GetEffectiveH() {
    switch (current_state) {
        case CPUState::DD_PREFIX:
            return _IX.r8.hi; // IXH
        case CPUState::FD_PREFIX:
            return _IY.r8.hi; // IYH
        default:
            return H();
    }
}

uint8_t& CPU::GetEffectiveL() {
    switch (current_state) {
        case CPUState::DD_PREFIX:
            return _IX.r8.lo; // IXL
        case CPUState::FD_PREFIX:
            return _IY.r8.lo; // IYL
        default:
            return L();
    }
}

uint8_t CPU::GetMemoryAccessCycles() {
    // Memory operations: HL=7 cycles, IX/IY=19 cycles (+12 for displacement calculation)
    return (current_state == CPUState::NORMAL) ? 7 : 19;
}

uint8_t CPU::GetRegisterOpCycles() {
    // Register operations: HL=6 cycles, IX/IY=6 cycles (prefix adds its own 4 cycles)
    return 6;
}

uint8_t CPU::GetArithmeticMemCycles() {
    // Arithmetic with memory: HL=7 cycles, IX/IY=19 cycles (+12 for displacement)
    return (current_state == CPUState::NORMAL) ? 7 : 19;
}

// =============================================================================
// Control Flow, Stack, and I/O Instructions (0xC0-0xFF)
// =============================================================================

void CPU::RET_NZ() {
    if (CheckCondition(0)) { // NZ
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

void CPU::POP_BC() {
    BC() = PopWord();
    t_cycle += 10;
}

void CPU::JP_NZ_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(0)) { // NZ
        PC() = address;
    }
    t_cycle += 10;
}

void CPU::JP_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() = address;
    t_cycle += 10;
}

void CPU::CALL_NZ_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(0)) { // NZ
        PushWord(PC());
        PC() = address;
        t_cycle += 17;
    } else {
        t_cycle += 10;
    }
}

void CPU::PUSH_BC() {
    PushWord(BC());
    t_cycle += 11;
}

void CPU::ADD_A_n() {
    uint8_t value = memory[PC()++];
    uint8_t old_a = A();
    A() += value;
    SetFlags_ADD(A(), old_a, value);
    t_cycle += 7;
}

void CPU::RST_00() {
    PushWord(PC());
    PC() = 0x00;
    t_cycle += 11;
}

void CPU::RET_Z() {
    if (CheckCondition(1)) { // Z
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

void CPU::RET() {
    PC() = PopWord();
    t_cycle += 10;
}

void CPU::JP_Z_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(1)) { // Z
        PC() = address;
    }
    t_cycle += 10;
}

void CPU::PREFIX_CB() {
    // This should never be called - CB prefix is handled in Step()
    // If we reach here, it means the state machine has a bug
    t_cycle += 4;
}

void CPU::CALL_Z_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(1)) { // Z
        PushWord(PC());
        PC() = address;
        t_cycle += 17;
    } else {
        t_cycle += 10;
    }
}

void CPU::CALL_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    PushWord(PC());
    PC() = address;
    t_cycle += 17;
}

void CPU::ADC_A_n() {
    uint8_t value = memory[PC()++];
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(value) + carry;
    A() = result & 0xFF;
    
    F() = 0;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (((old_a & 0x0F) + (value & 0x0F) + carry) & 0x10) F() |= Constants::Flags::HALF;
    if (((old_a ^ A()) & (value ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x100) F() |= Constants::Flags::CARRY;
    
    t_cycle += 7;
}

void CPU::RST_08() {
    PushWord(PC());
    PC() = 0x08;
    t_cycle += 11;
}

void CPU::RET_NC() {
    if (CheckCondition(2)) { // NC
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

void CPU::POP_DE() {
    DE() = PopWord();
    t_cycle += 10;
}

void CPU::JP_NC_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(2)) { // NC
        PC() = address;
    }
    t_cycle += 10;
}

void CPU::OUT_n_A() {
    uint8_t port = memory[PC()++];
    WritePort(port, A());
    t_cycle += 11;
}

void CPU::CALL_NC_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(2)) { // NC
        PushWord(PC());
        PC() = address;
        t_cycle += 17;
    } else {
        t_cycle += 10;
    }
}

void CPU::PUSH_DE() {
    PushWord(DE());
    t_cycle += 11;
}

void CPU::SUB_n() {
    uint8_t value = memory[PC()++];
    uint8_t old_a = A();
    A() -= value;
    SetFlags_SUB(A(), old_a, value);
    t_cycle += 7;
}

void CPU::RST_10() {
    PushWord(PC());
    PC() = 0x10;
    t_cycle += 11;
}

void CPU::RET_C() {
    if (CheckCondition(3)) { // C
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

void CPU::EXX() {
    // Exchange BC, DE, HL with BC', DE', HL'
    uint16_t temp;
    temp = BC(); BC() = _BC1.r16; _BC1.r16 = temp;
    temp = DE(); DE() = _DE1.r16; _DE1.r16 = temp;
    temp = HL(); HL() = _HL1.r16; _HL1.r16 = temp;
    t_cycle += 4;
}

void CPU::JP_C_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(3)) { // C
        PC() = address;
    }
    t_cycle += 10;
}

void CPU::IN_A_n() {
    uint8_t port = memory[PC()++];
    A() = ReadPort(port);
    t_cycle += 11;
}

void CPU::CALL_C_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(3)) { // C
        PushWord(PC());
        PC() = address;
        t_cycle += 17;
    } else {
        t_cycle += 10;
    }
}

void CPU::PREFIX_DD() {
    // DD prefix handling is implemented in the main Step() state machine
    t_cycle += 4;
}

void CPU::SBC_A_n() {
    uint8_t value = memory[PC()++];
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(value) - carry;
    A() = result & 0xFF;
    
    F() = Constants::Flags::SUBTRACT;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if ((old_a & 0x0F) < ((value & 0x0F) + carry)) F() |= Constants::Flags::HALF;
    if (((old_a ^ value) & (old_a ^ A()) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 7;
}

void CPU::RST_18() {
    PushWord(PC());
    PC() = 0x18;
    t_cycle += 11;
}

void CPU::RET_PO() {
    if (CheckCondition(4)) { // PO
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

void CPU::POP_HL() {
    GetEffectiveHL_Register() = PopWord();
    t_cycle += 10;
}

void CPU::JP_PO_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(4)) { // PO
        PC() = address;
    }
    t_cycle += 10;
}

void CPU::EX_mSP_HL() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    uint16_t temp = memory[SP()] | (memory[SP() + 1] << 8);
    memory[SP()] = hl_reg & 0xFF;        // Low byte
    memory[SP() + 1] = (hl_reg >> 8);    // High byte
    hl_reg = temp;
    t_cycle += 19;
}

void CPU::CALL_PO_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(4)) { // PO
        PushWord(PC());
        PC() = address;
        t_cycle += 17;
    } else {
        t_cycle += 10;
    }
}

void CPU::PUSH_HL() {
    PushWord(GetEffectiveHL_Register());
    t_cycle += 11;
}

void CPU::AND_n() {
    uint8_t value = memory[PC()++];
    A() &= value;
    SetFlags_LOGIC(A());
    t_cycle += 7;
}

void CPU::RST_20() {
    PushWord(PC());
    PC() = 0x20;
    t_cycle += 11;
}

void CPU::RET_PE() {
    if (CheckCondition(5)) { // PE
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

void CPU::JP_HL() {
    PC() = GetEffectiveHL_Register();
    t_cycle += 4;
}

void CPU::JP_PE_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(5)) { // PE
        PC() = address;
    }
    t_cycle += 10;
}

void CPU::EX_DE_HL() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    uint16_t temp = DE();
    DE() = hl_reg;
    hl_reg = temp;
    t_cycle += 4;
}

void CPU::CALL_PE_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(5)) { // PE
        PushWord(PC());
        PC() = address;
        t_cycle += 17;
    } else {
        t_cycle += 10;
    }
}

void CPU::PREFIX_ED() {
    // ED prefix handling is implemented in the main Step() state machine
    t_cycle += 4;
}

void CPU::XOR_n() {
    uint8_t value = memory[PC()++];
    A() ^= value;
    SetFlags_LOGIC(A());
    t_cycle += 7;
}

void CPU::RST_28() {
    PushWord(PC());
    PC() = 0x28;
    t_cycle += 11;
}

void CPU::RET_P() {
    if (CheckCondition(6)) { // P
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

void CPU::POP_AF() {
    AF() = PopWord();
    t_cycle += 10;
}

void CPU::JP_P_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(6)) { // P
        PC() = address;
    }
    t_cycle += 10;
}

void CPU::DI() {
    IFF1() = false;
    IFF2() = false;
    t_cycle += 4;
}

void CPU::CALL_P_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(6)) { // P
        PushWord(PC());
        PC() = address;
        t_cycle += 17;
    } else {
        t_cycle += 10;
    }
}

void CPU::PUSH_AF() {
    PushWord(AF());
    t_cycle += 11;
}

void CPU::OR_n() {
    uint8_t value = memory[PC()++];
    A() |= value;
    SetFlags_LOGIC(A());
    t_cycle += 7;
}

void CPU::RST_30() {
    PushWord(PC());
    PC() = 0x30;
    t_cycle += 11;
}

void CPU::RET_M() {
    if (CheckCondition(7)) { // M
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

void CPU::LD_SP_HL() {
    SP() = GetEffectiveHL_Register();
    t_cycle += 6;
}

void CPU::JP_M_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(7)) { // M
        PC() = address;
    }
    t_cycle += 10;
}

void CPU::EI() {
    IFF1() = true;
    IFF2() = true;
    t_cycle += 4;
}

void CPU::CALL_M_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(7)) { // M
        PushWord(PC());
        PC() = address;
        t_cycle += 17;
    } else {
        t_cycle += 10;
    }
}

void CPU::PREFIX_FD() {
    // FD prefix handling is implemented in the main Step() state machine
    t_cycle += 4;
}

void CPU::CP_n() {
    uint8_t value = memory[PC()++];
    uint8_t result = A() - value;
    SetFlags_SUB(result, A(), value);
    t_cycle += 7;
}

void CPU::RST_38() {
    PushWord(PC());
    PC() = 0x38;
    t_cycle += 11;
}

// =============================================================================
// CB Instruction Implementation - Compact Decoder
// =============================================================================

void CPU::ExecuteCBInstruction(uint8_t opcode) {
    // Decode CB instruction structure: OOORRRRR
    // OOO = Operation (bits 7-6-5 or 7-6 for bit operations)
    // RRR = Register/Memory target (bits 2-1-0)
    
    uint8_t reg_code = opcode & 0x07;        // Bits 2-1-0: register/memory target
    uint8_t operation = (opcode >> 6) & 0x03; // Bits 7-6: operation type
    
    if (operation == 0) {
        // Rotate/Shift operations (bits 7-6 = 00)
        uint8_t shift_op = (opcode >> 3) & 0x07; // Bits 5-4-3: specific operation
        
        if (reg_code == 6) {
            // Memory operation (HL) or (IX+d)/(IY+d)
            uint8_t value = GetCBMemory(reg_code);
            uint8_t result;
            
            switch (shift_op) {
                case 0: result = RotateLeftCircular(value); break;
                case 1: result = RotateRightCircular(value); break;
                case 2: result = RotateLeft(value); break;
                case 3: result = RotateRight(value); break;
                case 4: result = ShiftLeftArithmetic(value); break;
                case 5: result = ShiftRightArithmetic(value); break;
                case 6: result = ShiftLeftLogical(value); break;
                case 7: result = ShiftRightLogical(value); break;
                default: result = value; break;
            }
            
            SetCBMemory(reg_code, result);
            // Timing: CB (HL)=15 cycles, DD CB/FD CB=23 cycles (total including prefixes)
            if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                t_cycle += 23; // DD CB/FD CB operations take 23 cycles total
            } else {
                t_cycle += 15; // CB (HL) operations take 15 cycles
            }
        } else {
            // Register operation or DD CB/FD CB with register destination
            if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                // DD CB/FD CB instructions: operate on memory but store result in register too
                uint8_t value = GetCBMemory(6); // Always use memory for DD CB/FD CB
                uint8_t result;
                
                switch (shift_op) {
                    case 0: result = RotateLeftCircular(value); break;
                    case 1: result = RotateRightCircular(value); break;
                    case 2: result = RotateLeft(value); break;
                    case 3: result = RotateRight(value); break;
                    case 4: result = ShiftLeftArithmetic(value); break;
                    case 5: result = ShiftRightArithmetic(value); break;
                    case 6: result = ShiftLeftLogical(value); break;
                    case 7: result = ShiftRightLogical(value); break;
                    default: result = value; break;
                }
                
                // Store result in both memory and register
                SetCBMemory(6, result);
                GetCBRegister(reg_code) = result;
                t_cycle += 23; // DD CB/FD CB operations take 23 cycles total
            } else {
                // Normal CB register operation
                uint8_t& reg = GetCBRegister(reg_code);
                
                switch (shift_op) {
                    case 0: reg = RotateLeftCircular(reg); break;
                    case 1: reg = RotateRightCircular(reg); break;
                    case 2: reg = RotateLeft(reg); break;
                    case 3: reg = RotateRight(reg); break;
                    case 4: reg = ShiftLeftArithmetic(reg); break;
                    case 5: reg = ShiftRightArithmetic(reg); break;
                    case 6: reg = ShiftLeftLogical(reg); break;
                    case 7: reg = ShiftRightLogical(reg); break;
                }
                
                t_cycle += 8; // CB register operations take 8 cycles
            }
        }
    } else {
        // Bit operations (bits 7-6 = 01, 10, or 11)
        uint8_t bit_num = (opcode >> 3) & 0x07; // Bits 5-4-3: bit number
        
        if (reg_code == 6) {
            // Memory operation
            uint8_t value = GetCBMemory(reg_code);
            
            switch (operation) {
                case 1: // BIT - test bit
                    TestBit(value, bit_num);
                    // Timing: BIT (HL)=12 cycles, DD CB/FD CB BIT=20 cycles (total including prefixes)
                    if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                        t_cycle += 20; // DD CB/FD CB BIT operations take 20 cycles total
                    } else {
                        t_cycle += 12; // BIT (HL) takes 12 cycles
                    }
                    break;
                case 2: // RES - reset bit
                    SetCBMemory(reg_code, ResetBit(value, bit_num));
                    // Timing: RES (HL)=15 cycles, DD CB/FD CB RES=23 cycles (total including prefixes)
                    if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                        t_cycle += 23; // DD CB/FD CB RES operations take 23 cycles total
                    } else {
                        t_cycle += 15; // RES (HL) takes 15 cycles
                    }
                    break;
                case 3: // SET - set bit
                    SetCBMemory(reg_code, SetBit(value, bit_num));
                    // Timing: SET (HL)=15 cycles, DD CB/FD CB SET=23 cycles (total including prefixes)
                    if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                        t_cycle += 23; // DD CB/FD CB SET operations take 23 cycles total
                    } else {
                        t_cycle += 15; // SET (HL) takes 15 cycles
                    }
                    break;
            }
        } else {
            // Register operation
            uint8_t& reg = GetCBRegister(reg_code);
            
            switch (operation) {
                case 1: // BIT - test bit
                    TestBit(reg, bit_num);
                    t_cycle += 8; // BIT register takes 8 cycles
                    break;
                case 2: // RES - reset bit
                    reg = ResetBit(reg, bit_num);
                    t_cycle += 8; // RES register takes 8 cycles
                    break;
                case 3: // SET - set bit
                    reg = SetBit(reg, bit_num);
                    t_cycle += 8; // SET register takes 8 cycles
                    break;
            }
        }
    }
}

uint8_t& CPU::GetCBRegister(uint8_t reg_code) {
    switch (reg_code) {
        case 0: return B();
        case 1: return C();
        case 2: return D();
        case 3: return E();
        case 4: 
            // For DD CB and FD CB instructions, H/L always refer to actual H/L registers
            if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                return H(); // Always use actual H register for DD CB/FD CB
            } else {
                return GetEffectiveH(); // Use state-aware H for normal CB
            }
        case 5:
            // For DD CB and FD CB instructions, H/L always refer to actual H/L registers
            if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                return L(); // Always use actual L register for DD CB/FD CB
            } else {
                return GetEffectiveL(); // Use state-aware L for normal CB
            }
        case 7: return A();
        default: return A(); // Should never happen for reg_code 6
    }
}

uint8_t CPU::GetCBMemory(uint8_t reg_code) {
    if (reg_code == 6) {
        uint16_t address = GetEffectiveHL_Memory();
        return memory[address];
    }
    return 0; // Should never happen
}

void CPU::SetCBMemory(uint8_t reg_code, uint8_t value) {
    if (reg_code == 6) {
        uint16_t address = GetEffectiveHL_Memory();
        memory[address] = value;
    }
}

// =============================================================================
// CB Instruction Helper Functions
// =============================================================================

uint8_t CPU::RotateLeftCircular(uint8_t value) {
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = (value << 1) | bit7;
    
    F() &= 0xEC; // Preserve S, Z, P/V
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    
    return result;
}

uint8_t CPU::RotateRightCircular(uint8_t value) {
    uint8_t bit0 = value & 0x01;
    uint8_t result = (value >> 1) | (bit0 << 7);
    
    F() &= 0xEC; // Preserve S, Z, P/V
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit0) F() |= Constants::Flags::CARRY;
    
    return result;
}

uint8_t CPU::RotateLeft(uint8_t value) {
    uint8_t old_carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = (value << 1) | old_carry;
    
    F() &= 0xEC; // Preserve S, Z, P/V
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    
    return result;
}

uint8_t CPU::RotateRight(uint8_t value) {
    uint8_t old_carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t bit0 = value & 0x01;
    uint8_t result = (value >> 1) | (old_carry << 7);
    
    F() &= 0xEC; // Preserve S, Z, P/V
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit0) F() |= Constants::Flags::CARRY;
    
    return result;
}

uint8_t CPU::ShiftLeftArithmetic(uint8_t value) {
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = value << 1;
    
    F() &= 0xEC; // Preserve S, Z, P/V
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    
    return result;
}

uint8_t CPU::ShiftRightArithmetic(uint8_t value) {
    uint8_t bit0 = value & 0x01;
    uint8_t bit7 = value & 0x80; // Preserve sign bit
    uint8_t result = (value >> 1) | bit7;
    
    F() &= 0xEC; // Preserve S, Z, P/V
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit0) F() |= Constants::Flags::CARRY;
    
    return result;
}

uint8_t CPU::ShiftLeftLogical(uint8_t value) {
    // Undocumented instruction - same as SLA but sets bit 0
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = (value << 1) | 0x01;
    
    F() &= 0xEC; // Preserve S, Z, P/V
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    
    return result;
}

uint8_t CPU::ShiftRightLogical(uint8_t value) {
    uint8_t bit0 = value & 0x01;
    uint8_t result = value >> 1; // Logical shift - bit 7 becomes 0
    
    F() &= 0xEC; // Preserve S, Z, P/V
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit0) F() |= Constants::Flags::CARRY;
    
    return result;
}

void CPU::TestBit(uint8_t value, uint8_t bit) {
    uint8_t bit_mask = 1 << bit;
    bool bit_set = (value & bit_mask) != 0;
    
    F() &= Constants::Flags::CARRY; // Preserve carry only
    F() |= Constants::Flags::HALF;  // H flag always set for BIT
    
    if (!bit_set) F() |= Constants::Flags::ZERO;
    if (bit == 7 && bit_set) F() |= Constants::Flags::SIGN;
    if (!bit_set) F() |= Constants::Flags::PARITY; // P/V flag = Z flag for BIT
}

uint8_t CPU::ResetBit(uint8_t value, uint8_t bit) {
    uint8_t bit_mask = ~(1 << bit);
    return value & bit_mask;
}

uint8_t CPU::SetBit(uint8_t value, uint8_t bit) {
    uint8_t bit_mask = 1 << bit;
    return value | bit_mask;
}

// =============================================================================
// ED Instruction Implementations
// =============================================================================

void CPU::ED_NOP() {
    // Default handler for undefined ED instructions
    t_cycle += 8; // Most ED instructions take 8 cycles minimum
}

void CPU::SBC_HL_DE() {
    // ED 52 - Subtract DE from HL with carry
    uint16_t old_hl = HL();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int32_t result = static_cast<int32_t>(HL()) - static_cast<int32_t>(DE()) - carry;
    HL() = result & 0xFFFF;
    
    // Set flags for 16-bit subtraction
    F() = Constants::Flags::SUBTRACT; // N flag always set for subtraction
    if (HL() == 0) F() |= Constants::Flags::ZERO;
    if (HL() & 0x8000) F() |= Constants::Flags::SIGN;
    if ((old_hl & 0x0FFF) < ((DE() & 0x0FFF) + carry)) F() |= Constants::Flags::HALF;
    if (((old_hl ^ DE()) & (old_hl ^ HL()) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 15;
}

void CPU::ADC_HL_DE() {
    // ED 5A - Add DE to HL with carry
    uint16_t old_hl = HL();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint32_t result = static_cast<uint32_t>(HL()) + static_cast<uint32_t>(DE()) + carry;
    HL() = result & 0xFFFF;
    
    // Set flags for 16-bit addition
    F() = 0; // Clear N flag for addition
    if (HL() == 0) F() |= Constants::Flags::ZERO;
    if (HL() & 0x8000) F() |= Constants::Flags::SIGN;
    if (((old_hl & 0x0FFF) + (DE() & 0x0FFF) + carry) & 0x1000) F() |= Constants::Flags::HALF;
    if (((old_hl ^ HL()) & (DE() ^ HL()) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x10000) F() |= Constants::Flags::CARRY;
    
    t_cycle += 15;
}

// =============================================================================
// Additional 16-bit Arithmetic ED Instructions
// =============================================================================

void CPU::SBC_HL_BC() {
    // ED 42 - Subtract BC from HL with carry
    uint16_t old_hl = HL();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int32_t result = static_cast<int32_t>(HL()) - static_cast<int32_t>(BC()) - carry;
    HL() = result & 0xFFFF;
    
    // Set flags for 16-bit subtraction
    F() = Constants::Flags::SUBTRACT; // N flag always set for subtraction
    if (HL() == 0) F() |= Constants::Flags::ZERO;
    if (HL() & 0x8000) F() |= Constants::Flags::SIGN;
    if ((old_hl & 0x0FFF) < ((BC() & 0x0FFF) + carry)) F() |= Constants::Flags::HALF;
    if (((old_hl ^ BC()) & (old_hl ^ HL()) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 15;
}

void CPU::ADC_HL_BC() {
    // ED 4A - Add BC to HL with carry
    uint16_t old_hl = HL();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint32_t result = static_cast<uint32_t>(HL()) + static_cast<uint32_t>(BC()) + carry;
    HL() = result & 0xFFFF;
    
    // Set flags for 16-bit addition
    F() = 0; // Clear N flag for addition
    if (HL() == 0) F() |= Constants::Flags::ZERO;
    if (HL() & 0x8000) F() |= Constants::Flags::SIGN;
    if (((old_hl & 0x0FFF) + (BC() & 0x0FFF) + carry) & 0x1000) F() |= Constants::Flags::HALF;
    if (((old_hl ^ HL()) & (BC() ^ HL()) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x10000) F() |= Constants::Flags::CARRY;
    
    t_cycle += 15;
}

void CPU::SBC_HL_HL() {
    // ED 62 - Subtract HL from HL with carry
    uint16_t old_hl = HL();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int32_t result = static_cast<int32_t>(HL()) - static_cast<int32_t>(HL()) - carry;
    HL() = result & 0xFFFF;
    
    // Set flags for 16-bit subtraction
    F() = Constants::Flags::SUBTRACT; // N flag always set for subtraction
    if (HL() == 0) F() |= Constants::Flags::ZERO;
    if (HL() & 0x8000) F() |= Constants::Flags::SIGN;
    if ((old_hl & 0x0FFF) < ((old_hl & 0x0FFF) + carry)) F() |= Constants::Flags::HALF;
    if (((old_hl ^ old_hl) & (old_hl ^ HL()) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 15;
}

void CPU::ADC_HL_HL() {
    // ED 6A - Add HL to HL with carry
    uint16_t old_hl = HL();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint32_t result = static_cast<uint32_t>(HL()) + static_cast<uint32_t>(HL()) + carry;
    HL() = result & 0xFFFF;
    
    // Set flags for 16-bit addition
    F() = 0; // Clear N flag for addition
    if (HL() == 0) F() |= Constants::Flags::ZERO;
    if (HL() & 0x8000) F() |= Constants::Flags::SIGN;
    if (((old_hl & 0x0FFF) + (old_hl & 0x0FFF) + carry) & 0x1000) F() |= Constants::Flags::HALF;
    if (((old_hl ^ HL()) & (old_hl ^ HL()) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x10000) F() |= Constants::Flags::CARRY;
    
    t_cycle += 15;
}

void CPU::SBC_HL_SP() {
    // ED 72 - Subtract SP from HL with carry
    uint16_t old_hl = HL();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int32_t result = static_cast<int32_t>(HL()) - static_cast<int32_t>(SP()) - carry;
    HL() = result & 0xFFFF;
    
    // Set flags for 16-bit subtraction
    F() = Constants::Flags::SUBTRACT; // N flag always set for subtraction
    if (HL() == 0) F() |= Constants::Flags::ZERO;
    if (HL() & 0x8000) F() |= Constants::Flags::SIGN;
    if ((old_hl & 0x0FFF) < ((SP() & 0x0FFF) + carry)) F() |= Constants::Flags::HALF;
    if (((old_hl ^ SP()) & (old_hl ^ HL()) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (result < 0) F() |= Constants::Flags::CARRY;
    
    t_cycle += 15;
}

void CPU::ADC_HL_SP() {
    // ED 7A - Add SP to HL with carry
    uint16_t old_hl = HL();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint32_t result = static_cast<uint32_t>(HL()) + static_cast<uint32_t>(SP()) + carry;
    HL() = result & 0xFFFF;
    
    // Set flags for 16-bit addition
    F() = 0; // Clear N flag for addition
    if (HL() == 0) F() |= Constants::Flags::ZERO;
    if (HL() & 0x8000) F() |= Constants::Flags::SIGN;
    if (((old_hl & 0x0FFF) + (SP() & 0x0FFF) + carry) & 0x1000) F() |= Constants::Flags::HALF;
    if (((old_hl ^ HL()) & (SP() ^ HL()) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (result & 0x10000) F() |= Constants::Flags::CARRY;
    
    t_cycle += 15;
}

// =============================================================================
// 16-bit Load/Store ED Instructions
// =============================================================================

void CPU::LD_mnn_BC() {
    // ED 43 - Load BC to memory at 16-bit address
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    memory[address] = BC() & 0xFF;        // Low byte
    memory[address + 1] = (BC() >> 8);    // High byte
    t_cycle += 20;
}

void CPU::LD_BC_mnn() {
    // ED 4B - Load memory at 16-bit address to BC
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    BC() = memory[address] | (memory[address + 1] << 8);
    t_cycle += 20;
}

void CPU::LD_mnn_DE() {
    // ED 53 - Load DE to memory at 16-bit address
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    memory[address] = DE() & 0xFF;        // Low byte
    memory[address + 1] = (DE() >> 8);    // High byte
    t_cycle += 20;
}

void CPU::LD_DE_mnn() {
    // ED 5B - Load memory at 16-bit address to DE
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    DE() = memory[address] | (memory[address + 1] << 8);
    t_cycle += 20;
}

void CPU::LD_mnn_HL_ED() {
    // ED 63 - Load HL to memory at 16-bit address (ED version)
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    memory[address] = HL() & 0xFF;        // Low byte
    memory[address + 1] = (HL() >> 8);    // High byte
    t_cycle += 20;
}

void CPU::LD_HL_mnn_ED() {
    // ED 6B - Load memory at 16-bit address to HL (ED version)
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    HL() = memory[address] | (memory[address + 1] << 8);
    t_cycle += 20;
}

void CPU::LD_mnn_SP() {
    // ED 73 - Load SP to memory at 16-bit address
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    memory[address] = SP() & 0xFF;        // Low byte
    memory[address + 1] = (SP() >> 8);    // High byte
    t_cycle += 20;
}

void CPU::LD_SP_mnn() {
    // ED 7B - Load memory at 16-bit address to SP
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    SP() = memory[address] | (memory[address + 1] << 8);
    t_cycle += 20;
}

// =============================================================================
// Special Operations and Register Transfer ED Instructions
// =============================================================================

void CPU::NEG() {
    // ED 44 - Negate A (2's complement)
    uint8_t old_a = A();
    A() = (~A()) + 1;  // 2's complement negation
    
    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag always set for NEG
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if ((old_a & 0x0F) != 0) F() |= Constants::Flags::HALF; // Half-carry from bit 3
    if (old_a == 0x80) F() |= Constants::Flags::PARITY; // Overflow if A was 0x80
    if (old_a != 0) F() |= Constants::Flags::CARRY; // Carry if A was not 0
    
    t_cycle += 8;
}

void CPU::RETN() {
    // ED 45 - Return from non-maskable interrupt
    PC() = PopWord();
    IFF1() = IFF2(); // Restore interrupt state
    t_cycle += 14;
}

void CPU::IM_0() {
    // ED 46 - Set interrupt mode 0
    _interrupt_mode = 0;
    t_cycle += 8;
}

void CPU::LD_I_A() {
    // ED 47 - Load A to I register
    I() = A();
    t_cycle += 9;
}

void CPU::RETI() {
    // ED 4D - Return from interrupt
    PC() = PopWord();
    IFF1() = IFF2(); // Restore interrupt state
    t_cycle += 14;
}

void CPU::LD_R_A() {
    // ED 4F - Load A to R register
    R() = A();
    t_cycle += 9;
}

void CPU::IM_1() {
    // ED 56 - Set interrupt mode 1
    _interrupt_mode = 1;
    t_cycle += 8;
}

void CPU::LD_A_I() {
    // ED 57 - Load I register to A
    A() = I();
    
    // Set flags based on I register value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (IFF2()) F() |= Constants::Flags::PARITY; // P/V = IFF2
    
    t_cycle += 9;
}

void CPU::IM_2() {
    // ED 5E - Set interrupt mode 2
    _interrupt_mode = 2;
    t_cycle += 8;
}

void CPU::LD_A_R() {
    // ED 5F - Load R register to A
    A() = R();
    
    // Set flags based on R register value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (IFF2()) F() |= Constants::Flags::PARITY; // P/V = IFF2
    
    t_cycle += 9;
}

void CPU::RRD() {
    // ED 67 - Rotate right decimal (4-bit)
    uint8_t mem_val = memory[HL()];
    uint8_t a_low = A() & 0x0F;
    uint8_t mem_low = mem_val & 0x0F;
    uint8_t mem_high = (mem_val >> 4) & 0x0F;
    
    // Rotate: A[3:0] -> mem[7:4] -> mem[3:0] -> A[3:0]
    A() = (A() & 0xF0) | mem_low;
    memory[HL()] = (a_low << 4) | mem_high;
    
    // Set flags
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(A());
    
    t_cycle += 18;
}

void CPU::RLD() {
    // ED 6F - Rotate left decimal (4-bit)
    uint8_t mem_val = memory[HL()];
    uint8_t a_low = A() & 0x0F;
    uint8_t mem_low = mem_val & 0x0F;
    uint8_t mem_high = (mem_val >> 4) & 0x0F;
    
    // Rotate: A[3:0] -> mem[3:0] -> mem[7:4] -> A[3:0]
    A() = (A() & 0xF0) | mem_high;
    memory[HL()] = (mem_low << 4) | a_low;
    
    // Set flags
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(A());
    
    t_cycle += 18;
}

// =============================================================================
// Block Operation ED Instructions
// =============================================================================

void CPU::LDI() {
    // ED A0 - Load and increment
    memory[DE()] = memory[HL()];
    HL()++;
    DE()++;
    BC()--;
    
    // Set flags
    F() &= (Constants::Flags::CARRY | Constants::Flags::ZERO | Constants::Flags::SIGN); // Preserve C, Z, S
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
    
    t_cycle += 16;
}

void CPU::CPI() {
    // ED A1 - Compare and increment
    uint8_t result = A() - memory[HL()];
    HL()++;
    BC()--;
    
    // Set flags
    F() &= Constants::Flags::CARRY; // Preserve carry only
    F() |= Constants::Flags::SUBTRACT; // N flag set for compare
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    if ((A() & 0x0F) < (memory[HL()-1] & 0x0F)) F() |= Constants::Flags::HALF;
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
    
    t_cycle += 16;
}

void CPU::INI() {
    // ED A2 - Input and increment
    memory[HL()] = ReadPort(C());
    HL()++;
    B()--;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag set
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;
    
    t_cycle += 16;
}

void CPU::OUTI() {
    // ED A3 - Output and increment
    WritePort(C(), memory[HL()]);
    HL()++;
    B()--;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag set
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;
    
    t_cycle += 16;
}

void CPU::LDD() {
    // ED A8 - Load and decrement
    memory[DE()] = memory[HL()];
    HL()--;
    DE()--;
    BC()--;
    
    // Set flags
    F() &= (Constants::Flags::CARRY | Constants::Flags::ZERO | Constants::Flags::SIGN); // Preserve C, Z, S
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
    
    t_cycle += 16;
}

void CPU::CPD() {
    // ED A9 - Compare and decrement
    uint8_t result = A() - memory[HL()];
    HL()--;
    BC()--;
    
    // Set flags
    F() &= Constants::Flags::CARRY; // Preserve carry only
    F() |= Constants::Flags::SUBTRACT; // N flag set for compare
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    if ((A() & 0x0F) < (memory[HL()+1] & 0x0F)) F() |= Constants::Flags::HALF;
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
    
    t_cycle += 16;
}

void CPU::IND() {
    // ED AA - Input and decrement
    memory[HL()] = ReadPort(C());
    HL()--;
    B()--;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag set
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;
    
    t_cycle += 16;
}

void CPU::OUTD() {
    // ED AB - Output and decrement
    WritePort(C(), memory[HL()]);
    HL()--;
    B()--;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag set
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;
    
    t_cycle += 16;
}

void CPU::LDIR() {
    // ED B0 - Load, increment and repeat
    do {
        memory[DE()] = memory[HL()];
        HL()++;
        DE()++;
        BC()--;
        t_cycle += 21; // 21 cycles per iteration
    } while (BC() != 0);
    
    // Adjust for last iteration (16 cycles instead of 21)
    t_cycle -= 5;
    
    // Set flags
    F() &= (Constants::Flags::CARRY | Constants::Flags::ZERO | Constants::Flags::SIGN); // Preserve C, Z, S
    // P/V is reset (BC = 0)
}

void CPU::CPIR() {
    // ED B1 - Compare, increment and repeat
    uint8_t result;
    do {
        result = A() - memory[HL()];
        HL()++;
        BC()--;
        t_cycle += 21; // 21 cycles per iteration
    } while (BC() != 0 && result != 0);
    
    // Adjust for last iteration (16 cycles instead of 21)
    t_cycle -= 5;
    
    // Set flags
    F() &= Constants::Flags::CARRY; // Preserve carry only
    F() |= Constants::Flags::SUBTRACT; // N flag set for compare
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
}

void CPU::INIR() {
    // ED B2 - Input, increment and repeat
    do {
        memory[HL()] = ReadPort(C());
        HL()++;
        B()--;
        t_cycle += 21; // 21 cycles per iteration
    } while (B() != 0);
    
    // Adjust for last iteration (16 cycles instead of 21)
    t_cycle -= 5;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT | Constants::Flags::ZERO; // N=1, Z=1 (B=0)
}

void CPU::OTIR() {
    // ED B3 - Output, increment and repeat
    do {
        WritePort(C(), memory[HL()]);
        HL()++;
        B()--;
        t_cycle += 21; // 21 cycles per iteration
    } while (B() != 0);
    
    // Adjust for last iteration (16 cycles instead of 21)
    t_cycle -= 5;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT | Constants::Flags::ZERO; // N=1, Z=1 (B=0)
}

void CPU::LDDR() {
    // ED B8 - Load, decrement and repeat
    do {
        memory[DE()] = memory[HL()];
        HL()--;
        DE()--;
        BC()--;
        t_cycle += 21; // 21 cycles per iteration
    } while (BC() != 0);
    
    // Adjust for last iteration (16 cycles instead of 21)
    t_cycle -= 5;
    
    // Set flags
    F() &= (Constants::Flags::CARRY | Constants::Flags::ZERO | Constants::Flags::SIGN); // Preserve C, Z, S
    // P/V is reset (BC = 0)
}

void CPU::CPDR() {
    // ED B9 - Compare, decrement and repeat
    uint8_t result;
    do {
        result = A() - memory[HL()];
        HL()--;
        BC()--;
        t_cycle += 21; // 21 cycles per iteration
    } while (BC() != 0 && result != 0);
    
    // Adjust for last iteration (16 cycles instead of 21)
    t_cycle -= 5;
    
    // Set flags
    F() &= Constants::Flags::CARRY; // Preserve carry only
    F() |= Constants::Flags::SUBTRACT; // N flag set for compare
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
}

void CPU::INDR() {
    // ED BA - Input, decrement and repeat
    do {
        memory[HL()] = ReadPort(C());
        HL()--;
        B()--;
        t_cycle += 21; // 21 cycles per iteration
    } while (B() != 0);
    
    // Adjust for last iteration (16 cycles instead of 21)
    t_cycle -= 5;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT | Constants::Flags::ZERO; // N=1, Z=1 (B=0)
}

void CPU::OTDR() {
    // ED BB - Output, decrement and repeat
    do {
        WritePort(C(), memory[HL()]);
        HL()--;
        B()--;
        t_cycle += 21; // 21 cycles per iteration
    } while (B() != 0);
    
    // Adjust for last iteration (16 cycles instead of 21)
    t_cycle -= 5;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT | Constants::Flags::ZERO; // N=1, Z=1 (B=0)
}

// =============================================================================
// Individual I/O ED Instructions
// =============================================================================

void CPU::IN_B_C() {
    // ED 40 - Input from port C to B
    B() = ReadPort(C());
    
    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(B());
    
    t_cycle += 12;
}

void CPU::OUT_C_B() {
    // ED 41 - Output B to port C
    WritePort(C(), B());
    t_cycle += 12;
}

void CPU::IN_C_C() {
    // ED 48 - Input from port C to C
    C() = ReadPort(C());
    
    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (C() == 0) F() |= Constants::Flags::ZERO;
    if (C() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(C());
    
    t_cycle += 12;
}

void CPU::OUT_C_C() {
    // ED 49 - Output C to port C
    WritePort(C(), C());
    t_cycle += 12;
}

void CPU::IN_D_C() {
    // ED 50 - Input from port C to D
    D() = ReadPort(C());
    
    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (D() == 0) F() |= Constants::Flags::ZERO;
    if (D() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(D());
    
    t_cycle += 12;
}

void CPU::OUT_C_D() {
    // ED 51 - Output D to port C
    WritePort(C(), D());
    t_cycle += 12;
}

void CPU::IN_E_C() {
    // ED 58 - Input from port C to E
    E() = ReadPort(C());
    
    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (E() == 0) F() |= Constants::Flags::ZERO;
    if (E() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(E());
    
    t_cycle += 12;
}

void CPU::OUT_C_E() {
    // ED 59 - Output E to port C
    WritePort(C(), E());
    t_cycle += 12;
}

void CPU::IN_H_C() {
    // ED 60 - Input from port C to H
    H() = ReadPort(C());
    
    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (H() == 0) F() |= Constants::Flags::ZERO;
    if (H() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(H());
    
    t_cycle += 12;
}

void CPU::OUT_C_H() {
    // ED 61 - Output H to port C
    WritePort(C(), H());
    t_cycle += 12;
}

void CPU::IN_L_C() {
    // ED 68 - Input from port C to L
    L() = ReadPort(C());
    
    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (L() == 0) F() |= Constants::Flags::ZERO;
    if (L() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(L());
    
    t_cycle += 12;
}

void CPU::OUT_C_L() {
    // ED 69 - Output L to port C
    WritePort(C(), L());
    t_cycle += 12;
}

void CPU::IN_F_C() {
    // ED 70 - Input from port C (undocumented - sets flags only, doesn't store value)
    uint8_t value = ReadPort(C());
    
    // Set flags based on input value but don't store it anywhere
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (value == 0) F() |= Constants::Flags::ZERO;
    if (value & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(value);
    
    t_cycle += 12;
}

void CPU::OUT_C_0() {
    // ED 71 - Output 0 to port C (undocumented)
    WritePort(C(), 0);
    t_cycle += 12;
}

void CPU::IN_A_C() {
    // ED 78 - Input from port C to A
    A() = ReadPort(C());
    
    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(A());
    
    t_cycle += 12;
}

void CPU::OUT_C_A() {
    // ED 79 - Output A to port C
    WritePort(C(), A());
    t_cycle += 12;
}

// =============================================================================
// Undocumented ED Instructions
// =============================================================================

void CPU::SLL_mHL() {
    // ED 76 - Shift Left Logical (HL) - undocumented instruction
    // This is like SLA but always sets bit 0 to 1
    uint8_t value = memory[HL()];
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = (value << 1) | 0x01; // Shift left and set bit 0
    
    memory[HL()] = result;
    
    // Set flags
    F() = 0; // Clear all flags
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    // H and N flags are reset (already 0)
    
    t_cycle += 15;
}

} // namespace z80
