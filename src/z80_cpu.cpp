//
// Z80 CPU Emulator - Implementation
// Created by Larry Dawson on 11/10/23.
//

#include "z80_cpu.h"
#include "memory/observable_memory.h"
#include "io/latched_io.h"
#include "io/observable_io.h"
#include "io/callback_io.h"
#include <algorithm>

namespace z80 {

// =============================================================================
// Construction/Destruction
// =============================================================================

template <class Memory, class Io>
CPUImpl<Memory, Io>::CPUImpl() {
    Reset();
    InitializeInstructionTables();
}

template <class Memory, class Io>
CPUImpl<Memory, Io>::~CPUImpl() = default;

template <class Memory, class Io>
void CPUImpl<Memory, Io>::Reset() {
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
    ei_defer_ = false;
    
    // Initialize interrupt mode
    _interrupt_mode = 0;
    
    // Clear halted state
    _halted = false;
    
    // Initialize prefix state
    current_state = CPUState::NORMAL;
    
    // Memory and I/O devices manage their own initial state (the policies).
}

template <class Memory, class Io>
bool CPUImpl<Memory, Io>::Interrupt(uint8_t bus) {
    // Maskable interrupt: accepted only when enabled and not in the one-
    // instruction shadow of an EI (so an `EI : RET` handler tail can't be
    // re-entered between the two).
    if (!_IFF1 || ei_defer_) return false;

    // Acceptance wakes a halted CPU. PC already points past the HALT (the fetch
    // advanced it), so it is the correct return address.
    _halted = false;

    // Acknowledge: mask further interrupts and save the return address.
    _IFF1 = false;
    _IFF2 = false;
    PushWord(_PC);

    // The interrupt-acknowledge cycle is an M1, so it bumps R too (low 7 bits;
    // bit 7 preserved) — keeps R consistent for refresh-keyed code across an ISR.
    R() = static_cast<uint8_t>((R() & 0x80u) | ((R() + 1u) & 0x7Fu));

    switch (_interrupt_mode) {
        case 2: {
            // Vector table: address = (I << 8) | bus; PC = word at that address.
            const uint16_t vector = static_cast<uint16_t>((I() << 8) | bus);
            const uint16_t lo = memory[vector];
            const uint16_t hi = memory[static_cast<uint16_t>(vector + 1)];
            _PC = static_cast<uint16_t>(lo | (hi << 8));
            t_cycle += 19;
            break;
        }
        case 0:
            // The device places an instruction on the bus; in practice an RST
            // (Spectrum bus = 0xFF = RST 38). Jump to that RST vector.
            _PC = static_cast<uint16_t>(bus & 0x38);
            t_cycle += 13;
            break;
        case 1:
        default:
            _PC = 0x0038;
            t_cycle += 13;
            break;
    }
    return true;
}

// =============================================================================
// Core Execution
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RunUntilCycle(uint64_t target_cycle) {
    while (t_cycle < target_cycle && !_halted) {
        Step();
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::Step() {
    // EI defers interrupt acceptance until *after* the following instruction.
    // Capture the flag here; clear it once that following instruction completes.
    const bool ei_was_pending = ei_defer_;

    // Fetch instruction opcode
    uint8_t opcode = memory[PC()++];

    // R (memory-refresh) register: its low 7 bits increment on every M1 opcode
    // fetch; bit 7 is preserved (only LD R,A changes it). Each Step() fetches one
    // opcode/prefix byte = one M1, so increment once per Step — EXCEPT the
    // DDCB/FDCB states, whose Step reads the displacement + sub-opcode as operands
    // (those bytes are not M1 fetches; DDCB/FDCB have just two refreshes, for the
    // DD/FD and CB bytes). R-keyed self-decrypting loaders (e.g. Speedlock, used by
    // Arkanoid) depend on this exact sequence via LD A,R — without it the key is
    // constant and the decrypt produces garbage. See FLOATING_BUS_DESIGN.md notes.
    if (current_state != CPUState::DD_CB_PREFIX && current_state != CPUState::FD_CB_PREFIX)
        R() = static_cast<uint8_t>((R() & 0x80u) | ((R() + 1u) & 0x7Fu));

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
            // Execute CB-prefixed instruction using compact decoder. The CB
            // prefix fetch above already charged its 4 T M1; the body adds only
            // the remaining cycles.
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
            // Execute ED-prefixed instruction. The ED prefix fetch above charged
            // its 4 T M1; the body adds only the remaining cycles. Block-repeat
            // ops run one iteration per step.
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

                // Execute the CB instruction with stored displacement. The DD and
                // CB prefix fetches above each charged 4 T (two M1s); the body
                // adds only the remaining cycles.
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
                
                // Execute the CB instruction with stored displacement. The FD and
                // CB prefix fetches above each charged 4 T (two M1s); the body
                // adds only the remaining cycles.
                ExecuteCBInstruction(cb_opcode);

                current_state = CPUState::NORMAL;
            }
            break;
    }

    // If an EI was pending before this instruction (and this instruction was not
    // itself the EI), the one-instruction deferral window has now closed.
    if (ei_was_pending) ei_defer_ = false;
}

// =============================================================================
// Memory and I/O Access
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LoadProgram(const std::vector<uint8_t>& program, uint16_t start_address) {
    for (size_t i = 0; i < program.size() && (start_address + i) < Constants::MEMORY_SIZE; ++i) {
        memory[start_address + i] = program[i];
    }
}

// =============================================================================
// Instruction Table Initialization
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::InitializeInstructionTables() {
    // Initialize all tables to NOP
    basic_opcodes.fill(&CPUImpl::NOP);
    ED_opcodes.fill(&CPUImpl::ED_NOP);
    
    // Set up the implemented opcodes
    basic_opcodes[0x00] = &CPUImpl::NOP;
    basic_opcodes[0x01] = &CPUImpl::LD_BC_nn;
    basic_opcodes[0x02] = &CPUImpl::LD_mBC_A;
    basic_opcodes[0x03] = &CPUImpl::INC_BC;
    basic_opcodes[0x04] = &CPUImpl::INC_B;
    basic_opcodes[0x05] = &CPUImpl::DEC_B;
    basic_opcodes[0x06] = &CPUImpl::LD_B_n;
    basic_opcodes[0x07] = &CPUImpl::RLCA;
    basic_opcodes[0x08] = &CPUImpl::EX_AF_AF;
    basic_opcodes[0x09] = &CPUImpl::ADD_HL_BC;
    basic_opcodes[0x0A] = &CPUImpl::LD_A_mBC;
    basic_opcodes[0x0B] = &CPUImpl::DEC_BC;
    basic_opcodes[0x0C] = &CPUImpl::INC_C;
    basic_opcodes[0x0D] = &CPUImpl::DEC_C;
    basic_opcodes[0x0E] = &CPUImpl::LD_C_n;
    basic_opcodes[0x0F] = &CPUImpl::RRCA;
    basic_opcodes[0x10] = &CPUImpl::DJNZ;
    basic_opcodes[0x11] = &CPUImpl::LD_DE_nn;
    basic_opcodes[0x12] = &CPUImpl::LD_mDE_A;
    basic_opcodes[0x13] = &CPUImpl::INC_DE;
    basic_opcodes[0x14] = &CPUImpl::INC_D;
    basic_opcodes[0x15] = &CPUImpl::DEC_D;
    basic_opcodes[0x16] = &CPUImpl::LD_D_n;
    basic_opcodes[0x17] = &CPUImpl::RLA;
    basic_opcodes[0x18] = &CPUImpl::JR;
    basic_opcodes[0x19] = &CPUImpl::ADD_HL_DE;
    basic_opcodes[0x1A] = &CPUImpl::LD_A_mDE;
    basic_opcodes[0x1B] = &CPUImpl::DEC_DE;
    basic_opcodes[0x1C] = &CPUImpl::INC_E;
    basic_opcodes[0x1D] = &CPUImpl::DEC_E;
    basic_opcodes[0x1E] = &CPUImpl::LD_E_n;
    basic_opcodes[0x1F] = &CPUImpl::RRA;
    basic_opcodes[0x20] = &CPUImpl::JR_NZ;
    basic_opcodes[0x21] = &CPUImpl::LD_HL_nn;
    basic_opcodes[0x22] = &CPUImpl::LD_mnn_HL;
    basic_opcodes[0x23] = &CPUImpl::INC_HL;
    basic_opcodes[0x24] = &CPUImpl::INC_H;
    basic_opcodes[0x25] = &CPUImpl::DEC_H;
    basic_opcodes[0x26] = &CPUImpl::LD_H_n;
    basic_opcodes[0x27] = &CPUImpl::DAA;
    basic_opcodes[0x28] = &CPUImpl::JR_Z;
    basic_opcodes[0x29] = &CPUImpl::ADD_HL_HL;
    basic_opcodes[0x2A] = &CPUImpl::LD_HL_mnn;
    basic_opcodes[0x2B] = &CPUImpl::DEC_HL;
    basic_opcodes[0x2C] = &CPUImpl::INC_L;
    basic_opcodes[0x2D] = &CPUImpl::DEC_L;
    basic_opcodes[0x2E] = &CPUImpl::LD_L_n;
    basic_opcodes[0x2F] = &CPUImpl::CPL;
    basic_opcodes[0x30] = &CPUImpl::JR_NC;
    basic_opcodes[0x31] = &CPUImpl::LD_SP_nn;
    basic_opcodes[0x32] = &CPUImpl::LD_mnn_A;
    basic_opcodes[0x33] = &CPUImpl::INC_SP;
    basic_opcodes[0x34] = &CPUImpl::INC_mHL;
    basic_opcodes[0x35] = &CPUImpl::DEC_mHL;
    basic_opcodes[0x36] = &CPUImpl::LD_mHL_n;
    basic_opcodes[0x37] = &CPUImpl::SCF;
    basic_opcodes[0x38] = &CPUImpl::JR_C;
    basic_opcodes[0x39] = &CPUImpl::ADD_HL_SP;
    basic_opcodes[0x3A] = &CPUImpl::LD_A_mnn;
    basic_opcodes[0x3B] = &CPUImpl::DEC_SP;
    basic_opcodes[0x3C] = &CPUImpl::INC_A;
    basic_opcodes[0x3D] = &CPUImpl::DEC_A;
    basic_opcodes[0x3E] = &CPUImpl::LD_A_n;
    basic_opcodes[0x3F] = &CPUImpl::CCF;
    basic_opcodes[0x40] = &CPUImpl::LD_B_B;
    basic_opcodes[0x41] = &CPUImpl::LD_B_C;
    basic_opcodes[0x42] = &CPUImpl::LD_B_D;
    basic_opcodes[0x43] = &CPUImpl::LD_B_E;
    basic_opcodes[0x44] = &CPUImpl::LD_B_H;
    basic_opcodes[0x45] = &CPUImpl::LD_B_L;
    basic_opcodes[0x46] = &CPUImpl::LD_B_mHL;
    basic_opcodes[0x47] = &CPUImpl::LD_B_A;
    basic_opcodes[0x48] = &CPUImpl::LD_C_B;
    basic_opcodes[0x49] = &CPUImpl::LD_C_C;
    basic_opcodes[0x4A] = &CPUImpl::LD_C_D;
    basic_opcodes[0x4B] = &CPUImpl::LD_C_E;
    basic_opcodes[0x4C] = &CPUImpl::LD_C_H;
    basic_opcodes[0x4D] = &CPUImpl::LD_C_L;
    basic_opcodes[0x4E] = &CPUImpl::LD_C_mHL;
    basic_opcodes[0x4F] = &CPUImpl::LD_C_A;
    basic_opcodes[0x50] = &CPUImpl::LD_D_B;
    basic_opcodes[0x51] = &CPUImpl::LD_D_C;
    basic_opcodes[0x52] = &CPUImpl::LD_D_D;
    basic_opcodes[0x53] = &CPUImpl::LD_D_E;
    basic_opcodes[0x54] = &CPUImpl::LD_D_H;
    basic_opcodes[0x55] = &CPUImpl::LD_D_L;
    basic_opcodes[0x56] = &CPUImpl::LD_D_mHL;
    basic_opcodes[0x57] = &CPUImpl::LD_D_A;
    basic_opcodes[0x58] = &CPUImpl::LD_E_B;
    basic_opcodes[0x59] = &CPUImpl::LD_E_C;
    basic_opcodes[0x5A] = &CPUImpl::LD_E_D;
    basic_opcodes[0x5B] = &CPUImpl::LD_E_E;
    basic_opcodes[0x5C] = &CPUImpl::LD_E_H;
    basic_opcodes[0x5D] = &CPUImpl::LD_E_L;
    basic_opcodes[0x5E] = &CPUImpl::LD_E_mHL;
    basic_opcodes[0x5F] = &CPUImpl::LD_E_A;
    basic_opcodes[0x60] = &CPUImpl::LD_H_B;
    basic_opcodes[0x61] = &CPUImpl::LD_H_C;
    basic_opcodes[0x62] = &CPUImpl::LD_H_D;
    basic_opcodes[0x63] = &CPUImpl::LD_H_E;
    basic_opcodes[0x64] = &CPUImpl::LD_H_H;
    basic_opcodes[0x65] = &CPUImpl::LD_H_L;
    basic_opcodes[0x66] = &CPUImpl::LD_H_mHL;
    basic_opcodes[0x67] = &CPUImpl::LD_H_A;
    basic_opcodes[0x68] = &CPUImpl::LD_L_B;
    basic_opcodes[0x69] = &CPUImpl::LD_L_C;
    basic_opcodes[0x6A] = &CPUImpl::LD_L_D;
    basic_opcodes[0x6B] = &CPUImpl::LD_L_E;
    basic_opcodes[0x6C] = &CPUImpl::LD_L_H;
    basic_opcodes[0x6D] = &CPUImpl::LD_L_L;
    basic_opcodes[0x6E] = &CPUImpl::LD_L_mHL;
    basic_opcodes[0x6F] = &CPUImpl::LD_L_A;
    basic_opcodes[0x70] = &CPUImpl::LD_mHL_B;
    basic_opcodes[0x71] = &CPUImpl::LD_mHL_C;
    basic_opcodes[0x72] = &CPUImpl::LD_mHL_D;
    basic_opcodes[0x73] = &CPUImpl::LD_mHL_E;
    basic_opcodes[0x74] = &CPUImpl::LD_mHL_H;
    basic_opcodes[0x75] = &CPUImpl::LD_mHL_L;
    basic_opcodes[0x76] = &CPUImpl::HALT;
    basic_opcodes[0x77] = &CPUImpl::LD_mHL_A;
    basic_opcodes[0x78] = &CPUImpl::LD_A_B;
    basic_opcodes[0x79] = &CPUImpl::LD_A_C;
    basic_opcodes[0x7A] = &CPUImpl::LD_A_D;
    basic_opcodes[0x7B] = &CPUImpl::LD_A_E;
    basic_opcodes[0x7C] = &CPUImpl::LD_A_H;
    basic_opcodes[0x7D] = &CPUImpl::LD_A_L;
    basic_opcodes[0x7E] = &CPUImpl::LD_A_mHL;
    basic_opcodes[0x7F] = &CPUImpl::LD_A_A;
    basic_opcodes[0x80] = &CPUImpl::ADD_A_B;
    basic_opcodes[0x81] = &CPUImpl::ADD_A_C;
    basic_opcodes[0x82] = &CPUImpl::ADD_A_D;
    basic_opcodes[0x83] = &CPUImpl::ADD_A_E;
    basic_opcodes[0x84] = &CPUImpl::ADD_A_H;
    basic_opcodes[0x85] = &CPUImpl::ADD_A_L;
    basic_opcodes[0x86] = &CPUImpl::ADD_A_mHL;
    basic_opcodes[0x87] = &CPUImpl::ADD_A_A;
    basic_opcodes[0x88] = &CPUImpl::ADC_A_B;
    basic_opcodes[0x89] = &CPUImpl::ADC_A_C;
    basic_opcodes[0x8A] = &CPUImpl::ADC_A_D;
    basic_opcodes[0x8B] = &CPUImpl::ADC_A_E;
    basic_opcodes[0x8C] = &CPUImpl::ADC_A_H;
    basic_opcodes[0x8D] = &CPUImpl::ADC_A_L;
    basic_opcodes[0x8E] = &CPUImpl::ADC_A_mHL;
    basic_opcodes[0x8F] = &CPUImpl::ADC_A_A;
    basic_opcodes[0x90] = &CPUImpl::SUB_B;
    basic_opcodes[0x91] = &CPUImpl::SUB_C;
    basic_opcodes[0x92] = &CPUImpl::SUB_D;
    basic_opcodes[0x93] = &CPUImpl::SUB_E;
    basic_opcodes[0x94] = &CPUImpl::SUB_H;
    basic_opcodes[0x95] = &CPUImpl::SUB_L;
    basic_opcodes[0x96] = &CPUImpl::SUB_mHL;
    basic_opcodes[0x97] = &CPUImpl::SUB_A;
    basic_opcodes[0x98] = &CPUImpl::SBC_A_B;
    basic_opcodes[0x99] = &CPUImpl::SBC_A_C;
    basic_opcodes[0x9A] = &CPUImpl::SBC_A_D;
    basic_opcodes[0x9B] = &CPUImpl::SBC_A_E;
    basic_opcodes[0x9C] = &CPUImpl::SBC_A_H;
    basic_opcodes[0x9D] = &CPUImpl::SBC_A_L;
    basic_opcodes[0x9E] = &CPUImpl::SBC_A_mHL;
    basic_opcodes[0x9F] = &CPUImpl::SBC_A_A;
    basic_opcodes[0xA0] = &CPUImpl::AND_B;
    basic_opcodes[0xA1] = &CPUImpl::AND_C;
    basic_opcodes[0xA2] = &CPUImpl::AND_D;
    basic_opcodes[0xA3] = &CPUImpl::AND_E;
    basic_opcodes[0xA4] = &CPUImpl::AND_H;
    basic_opcodes[0xA5] = &CPUImpl::AND_L;
    basic_opcodes[0xA6] = &CPUImpl::AND_mHL;
    basic_opcodes[0xA7] = &CPUImpl::AND_A;
    basic_opcodes[0xA8] = &CPUImpl::XOR_B;
    basic_opcodes[0xA9] = &CPUImpl::XOR_C;
    basic_opcodes[0xAA] = &CPUImpl::XOR_D;
    basic_opcodes[0xAB] = &CPUImpl::XOR_E;
    basic_opcodes[0xAC] = &CPUImpl::XOR_H;
    basic_opcodes[0xAD] = &CPUImpl::XOR_L;
    basic_opcodes[0xAE] = &CPUImpl::XOR_mHL;
    basic_opcodes[0xAF] = &CPUImpl::XOR_A;
    basic_opcodes[0xB0] = &CPUImpl::OR_B;
    basic_opcodes[0xB1] = &CPUImpl::OR_C;
    basic_opcodes[0xB2] = &CPUImpl::OR_D;
    basic_opcodes[0xB3] = &CPUImpl::OR_E;
    basic_opcodes[0xB4] = &CPUImpl::OR_H;
    basic_opcodes[0xB5] = &CPUImpl::OR_L;
    basic_opcodes[0xB6] = &CPUImpl::OR_mHL;
    basic_opcodes[0xB7] = &CPUImpl::OR_A;
    basic_opcodes[0xB8] = &CPUImpl::CP_B;
    basic_opcodes[0xB9] = &CPUImpl::CP_C;
    basic_opcodes[0xBA] = &CPUImpl::CP_D;
    basic_opcodes[0xBB] = &CPUImpl::CP_E;
    basic_opcodes[0xBC] = &CPUImpl::CP_H;
    basic_opcodes[0xBD] = &CPUImpl::CP_L;
    basic_opcodes[0xBE] = &CPUImpl::CP_mHL;
    basic_opcodes[0xBF] = &CPUImpl::CP_A;
    basic_opcodes[0xC0] = &CPUImpl::RET_NZ;
    basic_opcodes[0xC1] = &CPUImpl::POP_BC;
    basic_opcodes[0xC2] = &CPUImpl::JP_NZ_nn;
    basic_opcodes[0xC3] = &CPUImpl::JP_nn;
    basic_opcodes[0xC4] = &CPUImpl::CALL_NZ_nn;
    basic_opcodes[0xC5] = &CPUImpl::PUSH_BC;
    basic_opcodes[0xC6] = &CPUImpl::ADD_A_n;
    basic_opcodes[0xC7] = &CPUImpl::RST_00;
    basic_opcodes[0xC8] = &CPUImpl::RET_Z;
    basic_opcodes[0xC9] = &CPUImpl::RET;
    basic_opcodes[0xCA] = &CPUImpl::JP_Z_nn;
    basic_opcodes[0xCB] = &CPUImpl::PREFIX_CB;
    basic_opcodes[0xCC] = &CPUImpl::CALL_Z_nn;
    basic_opcodes[0xCD] = &CPUImpl::CALL_nn;
    basic_opcodes[0xCE] = &CPUImpl::ADC_A_n;
    basic_opcodes[0xCF] = &CPUImpl::RST_08;
    basic_opcodes[0xD0] = &CPUImpl::RET_NC;
    basic_opcodes[0xD1] = &CPUImpl::POP_DE;
    basic_opcodes[0xD2] = &CPUImpl::JP_NC_nn;
    basic_opcodes[0xD3] = &CPUImpl::OUT_n_A;
    basic_opcodes[0xD4] = &CPUImpl::CALL_NC_nn;
    basic_opcodes[0xD5] = &CPUImpl::PUSH_DE;
    basic_opcodes[0xD6] = &CPUImpl::SUB_n;
    basic_opcodes[0xD7] = &CPUImpl::RST_10;
    basic_opcodes[0xD8] = &CPUImpl::RET_C;
    basic_opcodes[0xD9] = &CPUImpl::EXX;
    basic_opcodes[0xDA] = &CPUImpl::JP_C_nn;
    basic_opcodes[0xDB] = &CPUImpl::IN_A_n;
    basic_opcodes[0xDC] = &CPUImpl::CALL_C_nn;
    basic_opcodes[0xDD] = &CPUImpl::PREFIX_DD;
    basic_opcodes[0xDE] = &CPUImpl::SBC_A_n;
    basic_opcodes[0xDF] = &CPUImpl::RST_18;
    basic_opcodes[0xE0] = &CPUImpl::RET_PO;
    basic_opcodes[0xE1] = &CPUImpl::POP_HL;
    basic_opcodes[0xE2] = &CPUImpl::JP_PO_nn;
    basic_opcodes[0xE3] = &CPUImpl::EX_mSP_HL;
    basic_opcodes[0xE4] = &CPUImpl::CALL_PO_nn;
    basic_opcodes[0xE5] = &CPUImpl::PUSH_HL;
    basic_opcodes[0xE6] = &CPUImpl::AND_n;
    basic_opcodes[0xE7] = &CPUImpl::RST_20;
    basic_opcodes[0xE8] = &CPUImpl::RET_PE;
    basic_opcodes[0xE9] = &CPUImpl::JP_HL;
    basic_opcodes[0xEA] = &CPUImpl::JP_PE_nn;
    basic_opcodes[0xEB] = &CPUImpl::EX_DE_HL;
    basic_opcodes[0xEC] = &CPUImpl::CALL_PE_nn;
    basic_opcodes[0xED] = &CPUImpl::PREFIX_ED;
    basic_opcodes[0xEE] = &CPUImpl::XOR_n;
    basic_opcodes[0xEF] = &CPUImpl::RST_28;
    basic_opcodes[0xF0] = &CPUImpl::RET_P;
    basic_opcodes[0xF1] = &CPUImpl::POP_AF;
    basic_opcodes[0xF2] = &CPUImpl::JP_P_nn;
    basic_opcodes[0xF3] = &CPUImpl::DI;
    basic_opcodes[0xF4] = &CPUImpl::CALL_P_nn;
    basic_opcodes[0xF5] = &CPUImpl::PUSH_AF;
    basic_opcodes[0xF6] = &CPUImpl::OR_n;
    basic_opcodes[0xF7] = &CPUImpl::RST_30;
    basic_opcodes[0xF8] = &CPUImpl::RET_M;
    basic_opcodes[0xF9] = &CPUImpl::LD_SP_HL;
    basic_opcodes[0xFA] = &CPUImpl::JP_M_nn;
    basic_opcodes[0xFB] = &CPUImpl::EI;
    basic_opcodes[0xFC] = &CPUImpl::CALL_M_nn;
    basic_opcodes[0xFD] = &CPUImpl::PREFIX_FD;
    basic_opcodes[0xFE] = &CPUImpl::CP_n;
    basic_opcodes[0xFF] = &CPUImpl::RST_38;
    
    // Initialize ED instruction table - most entries default to ED_NOP
    ED_opcodes.fill(&CPUImpl::ED_NOP);
    
    // Map implemented ED instructions
    
    // 16-bit arithmetic operations
    ED_opcodes[0x42] = &CPUImpl::SBC_HL_BC;  // ED 42 - SBC HL, BC
    ED_opcodes[0x4A] = &CPUImpl::ADC_HL_BC;  // ED 4A - ADC HL, BC
    ED_opcodes[0x52] = &CPUImpl::SBC_HL_DE;  // ED 52 - SBC HL, DE
    ED_opcodes[0x5A] = &CPUImpl::ADC_HL_DE;  // ED 5A - ADC HL, DE
    ED_opcodes[0x62] = &CPUImpl::SBC_HL_HL;  // ED 62 - SBC HL, HL
    ED_opcodes[0x6A] = &CPUImpl::ADC_HL_HL;  // ED 6A - ADC HL, HL
    ED_opcodes[0x72] = &CPUImpl::SBC_HL_SP;  // ED 72 - SBC HL, SP
    ED_opcodes[0x7A] = &CPUImpl::ADC_HL_SP;  // ED 7A - ADC HL, SP
    
    // 16-bit load/store operations
    ED_opcodes[0x43] = &CPUImpl::LD_mnn_BC;  // ED 43 - LD (nn), BC
    ED_opcodes[0x4B] = &CPUImpl::LD_BC_mnn;  // ED 4B - LD BC, (nn)
    ED_opcodes[0x53] = &CPUImpl::LD_mnn_DE;  // ED 53 - LD (nn), DE
    ED_opcodes[0x5B] = &CPUImpl::LD_DE_mnn;  // ED 5B - LD DE, (nn)
    ED_opcodes[0x63] = &CPUImpl::LD_mnn_HL_ED;  // ED 63 - LD (nn), HL (ED version)
    ED_opcodes[0x6B] = &CPUImpl::LD_HL_mnn_ED;  // ED 6B - LD HL, (nn) (ED version)
    ED_opcodes[0x73] = &CPUImpl::LD_mnn_SP;  // ED 73 - LD (nn), SP
    ED_opcodes[0x7B] = &CPUImpl::LD_SP_mnn;  // ED 7B - LD SP, (nn)
    
    // Special operations and register transfers
    ED_opcodes[0x44] = &CPUImpl::NEG;        // ED 44 - NEG (negate A)
    ED_opcodes[0x4C] = &CPUImpl::NEG;        // ED 4C - NEG (alternate, undocumented)
    ED_opcodes[0x54] = &CPUImpl::NEG;        // ED 54 - NEG (alternate, undocumented)
    ED_opcodes[0x5C] = &CPUImpl::NEG;        // ED 5C - NEG (alternate, undocumented)
    ED_opcodes[0x64] = &CPUImpl::NEG;        // ED 64 - NEG (alternate, undocumented)
    ED_opcodes[0x6C] = &CPUImpl::NEG;        // ED 6C - NEG (alternate, undocumented)
    ED_opcodes[0x74] = &CPUImpl::NEG;        // ED 74 - NEG (alternate, undocumented)
    ED_opcodes[0x7C] = &CPUImpl::NEG;        // ED 7C - NEG (alternate, undocumented)
    
    ED_opcodes[0x45] = &CPUImpl::RETN;       // ED 45 - RETN (return from NMI)
    ED_opcodes[0x55] = &CPUImpl::RETN;       // ED 55 - RETN (alternate, undocumented)
    ED_opcodes[0x5D] = &CPUImpl::RETN;       // ED 5D - RETN (alternate, undocumented)
    ED_opcodes[0x65] = &CPUImpl::RETN;       // ED 65 - RETN (alternate, undocumented)
    ED_opcodes[0x6D] = &CPUImpl::RETN;       // ED 6D - RETN (alternate, undocumented)
    ED_opcodes[0x75] = &CPUImpl::RETN;       // ED 75 - RETN (alternate, undocumented)
    ED_opcodes[0x7D] = &CPUImpl::RETN;       // ED 7D - RETN (alternate, undocumented)
    
    ED_opcodes[0x76] = &CPUImpl::SLL_mHL;    // ED 76 - SLL (HL) (shift left logical, undocumented)
    
    ED_opcodes[0x46] = &CPUImpl::IM_0;       // ED 46 - IM 0 (interrupt mode 0)
    ED_opcodes[0x4E] = &CPUImpl::IM_0;       // ED 4E - IM 0 (alternate, undocumented)
    ED_opcodes[0x66] = &CPUImpl::IM_0;       // ED 66 - IM 0 (alternate, undocumented)
    ED_opcodes[0x6E] = &CPUImpl::IM_0;       // ED 6E - IM 0 (alternate, undocumented)
    
    ED_opcodes[0x47] = &CPUImpl::LD_I_A;     // ED 47 - LD I, A
    ED_opcodes[0x4D] = &CPUImpl::RETI;       // ED 4D - RETI (return from interrupt)
    ED_opcodes[0x4F] = &CPUImpl::LD_R_A;     // ED 4F - LD R, A
    ED_opcodes[0x56] = &CPUImpl::IM_1;       // ED 56 - IM 1 (interrupt mode 1)
    ED_opcodes[0x57] = &CPUImpl::LD_A_I;     // ED 57 - LD A, I
    ED_opcodes[0x5E] = &CPUImpl::IM_2;       // ED 5E - IM 2 (interrupt mode 2)
    ED_opcodes[0x5F] = &CPUImpl::LD_A_R;     // ED 5F - LD A, R
    ED_opcodes[0x67] = &CPUImpl::RRD;        // ED 67 - RRD (rotate right decimal)
    ED_opcodes[0x6F] = &CPUImpl::RLD;        // ED 6F - RLD (rotate left decimal)
    
    // Individual I/O operations using C register for port
    ED_opcodes[0x40] = &CPUImpl::IN_B_C;     // ED 40 - IN B, (C)
    ED_opcodes[0x41] = &CPUImpl::OUT_C_B;    // ED 41 - OUT (C), B
    ED_opcodes[0x48] = &CPUImpl::IN_C_C;     // ED 48 - IN C, (C)
    ED_opcodes[0x49] = &CPUImpl::OUT_C_C;    // ED 49 - OUT (C), C
    ED_opcodes[0x50] = &CPUImpl::IN_D_C;     // ED 50 - IN D, (C)
    ED_opcodes[0x51] = &CPUImpl::OUT_C_D;    // ED 51 - OUT (C), D
    ED_opcodes[0x58] = &CPUImpl::IN_E_C;     // ED 58 - IN E, (C)
    ED_opcodes[0x59] = &CPUImpl::OUT_C_E;    // ED 59 - OUT (C), E
    ED_opcodes[0x60] = &CPUImpl::IN_H_C;     // ED 60 - IN H, (C)
    ED_opcodes[0x61] = &CPUImpl::OUT_C_H;    // ED 61 - OUT (C), H
    ED_opcodes[0x68] = &CPUImpl::IN_L_C;     // ED 68 - IN L, (C)
    ED_opcodes[0x69] = &CPUImpl::OUT_C_L;    // ED 69 - OUT (C), L
    ED_opcodes[0x70] = &CPUImpl::IN_F_C;     // ED 70 - IN F, (C) (undocumented - sets flags only)
    ED_opcodes[0x71] = &CPUImpl::OUT_C_0;    // ED 71 - OUT (C), 0 (undocumented)
    ED_opcodes[0x78] = &CPUImpl::IN_A_C;     // ED 78 - IN A, (C)
    ED_opcodes[0x79] = &CPUImpl::OUT_C_A;    // ED 79 - OUT (C), A
    
    // Block operations
    ED_opcodes[0xA0] = &CPUImpl::LDI;        // ED A0 - LDI (load and increment)
    ED_opcodes[0xA1] = &CPUImpl::CPI;        // ED A1 - CPI (compare and increment)
    ED_opcodes[0xA2] = &CPUImpl::INI;        // ED A2 - INI (input and increment)
    ED_opcodes[0xA3] = &CPUImpl::OUTI;       // ED A3 - OUTI (output and increment)
    ED_opcodes[0xA8] = &CPUImpl::LDD;        // ED A8 - LDD (load and decrement)
    ED_opcodes[0xA9] = &CPUImpl::CPD;        // ED A9 - CPD (compare and decrement)
    ED_opcodes[0xAA] = &CPUImpl::IND;        // ED AA - IND (input and decrement)
    ED_opcodes[0xAB] = &CPUImpl::OUTD;       // ED AB - OUTD (output and decrement)
    ED_opcodes[0xB0] = &CPUImpl::LDIR;       // ED B0 - LDIR (load, increment, repeat)
    ED_opcodes[0xB1] = &CPUImpl::CPIR;       // ED B1 - CPIR (compare, increment, repeat)
    ED_opcodes[0xB2] = &CPUImpl::INIR;       // ED B2 - INIR (input, increment, repeat)
    ED_opcodes[0xB3] = &CPUImpl::OTIR;       // ED B3 - OTIR (output, increment, repeat)
    ED_opcodes[0xB8] = &CPUImpl::LDDR;       // ED B8 - LDDR (load, decrement, repeat)
    ED_opcodes[0xB9] = &CPUImpl::CPDR;       // ED B9 - CPDR (compare, decrement, repeat)
    ED_opcodes[0xBA] = &CPUImpl::INDR;       // ED BA - INDR (input, decrement, repeat)
    ED_opcodes[0xBB] = &CPUImpl::OTDR;       // ED BB - OTDR (output, decrement, repeat)
}

// =============================================================================
// Helper Functions
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetCarryFlag(bool value) {
    if (value) {
        F() |= Constants::Flags::CARRY;
    } else {
        F() &= ~Constants::Flags::CARRY;
    }
}

template <class Memory, class Io>
bool CPUImpl<Memory, Io>::GetCarryFlag() const {
    return (_AF.r8.lo & Constants::Flags::CARRY) != 0;
}

// =============================================================================
// Basic Instructions (0x00-0x3F)
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::NOP() {
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_BC_nn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    BC() = WZ();
    PC() += 2;
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mBC_A() {
    WZ() = BC();
    memory[WZ()] = A();
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_BC() {
    BC()++;
    t_cycle += 6;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_B() {
    uint8_t old_b = B();
    B()++;
    SetFlags_INC(B(), old_b);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_B() {
    uint8_t old_b = B();
    B()--;
    SetFlags_DEC(B(), old_b);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_n() {
    B() = memory[PC()++];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RLCA() {
    uint8_t old_bit7 = (A() & 0x80) ? 1 : 0;
    A() = (A() << 1) | old_bit7;
    F() = (F() & (Constants::Flags::SIGN | Constants::Flags::ZERO | Constants::Flags::PARITY)) |
          (A() & (Constants::Flags::X | Constants::Flags::Y)) | old_bit7;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::EX_AF_AF() {
    uint16_t temp = AF();
    AF() = _AF1.r16;
    _AF1.r16 = temp;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_HL_BC() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    const uint16_t old_hl = hl_reg;
    const uint16_t operand = BC();
    hl_reg = static_cast<uint16_t>(old_hl + operand);
    SetFlags_ADD16(hl_reg, old_hl, operand);
    
    // ADD HL,rr is 11 T; the IX/IY form is 15 T, but the extra 4 is the DD/FD
    // prefix M1 already charged at the prefix fetch — so the body is 11 either way.
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_mBC() {
    WZ() = BC();
    A() = memory[WZ()];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_BC() {
    BC()--;
    t_cycle += 6;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_C() {
    uint8_t old_c = C();
    C()++;
    SetFlags_INC(C(), old_c);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_C() {
    uint8_t old_c = C();
    C()--;
    SetFlags_DEC(C(), old_c);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_n() {
    C() = memory[PC()++];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RRCA() {
    uint8_t old_bit0 = A() & 0x01;
    A() = (A() >> 1) | (old_bit0 << 7);
    F() = (F() & (Constants::Flags::SIGN | Constants::Flags::ZERO | Constants::Flags::PARITY)) |
          (A() & (Constants::Flags::X | Constants::Flags::Y)) | old_bit0;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DJNZ() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_DE_nn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    DE() = WZ();
    PC() += 2;
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mDE_A() {
    WZ() = DE();
    memory[WZ()] = A();
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_DE() {
    DE()++;
    t_cycle += 6;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_D() {
    uint8_t old_d = D();
    D()++;
    SetFlags_INC(D(), old_d);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_D() {
    uint8_t old_d = D();
    D()--;
    SetFlags_DEC(D(), old_d);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_n() {
    D() = memory[PC()++];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RLA() {
    uint8_t old_carry = F() & 0x01;
    uint8_t new_carry = (A() & 0x80) ? 1 : 0;
    A() = (A() << 1) | old_carry;
    F() = (F() & (Constants::Flags::SIGN | Constants::Flags::ZERO | Constants::Flags::PARITY)) |
          (A() & (Constants::Flags::X | Constants::Flags::Y)) | new_carry;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JR() {
    int8_t displacement = memory[PC()++];
    WZ() = PC() + displacement;
    PC() = WZ();
    t_cycle += 12;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_HL_DE() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    const uint16_t old_hl = hl_reg;
    const uint16_t operand = DE();
    hl_reg = static_cast<uint16_t>(old_hl + operand);
    SetFlags_ADD16(hl_reg, old_hl, operand);
    
    // ADD HL,rr is 11 T; the IX/IY form is 15 T, but the extra 4 is the DD/FD
    // prefix M1 already charged at the prefix fetch — so the body is 11 either way.
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_mDE() {
    WZ() = DE();
    A() = memory[WZ()];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_DE() {
    DE()--;
    t_cycle += 6;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_E() {
    uint8_t old_e = E();
    E()++;
    SetFlags_INC(E(), old_e);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_E() {
    uint8_t old_e = E();
    E()--;
    SetFlags_DEC(E(), old_e);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_n() {
    E() = memory[PC()++];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RRA() {
    uint8_t old_carry = F() & 0x01;
    uint8_t new_carry = A() & 0x01;
    A() = (A() >> 1) | (old_carry << 7);
    F() = (F() & (Constants::Flags::SIGN | Constants::Flags::ZERO | Constants::Flags::PARITY)) |
          (A() & (Constants::Flags::X | Constants::Flags::Y)) | new_carry;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JR_NZ() {
    int8_t displacement = memory[PC()++];
    if (!(F() & 0x40)) { // Zero flag not set
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 12;
    } else {
        t_cycle += 7;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_HL_nn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    GetEffectiveHL_Register() = WZ();
    PC() += 2;
    t_cycle += 10; // Base instruction timing - prefix adds its own 4 cycles
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mnn_HL() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    PC() += 2;
    uint16_t& hl_reg = GetEffectiveHL_Register();
    memory[WZ()] = hl_reg & 0xFF;        // Low byte
    memory[WZ() + 1] = (hl_reg >> 8);    // High byte
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_HL() {
    GetEffectiveHL_Register()++;
    t_cycle += GetRegisterOpCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_H() {
    uint8_t& h_reg = GetEffectiveH();
    uint8_t old_h = h_reg;
    h_reg++;
    SetFlags_INC(h_reg, old_h);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_H() {
    uint8_t& h_reg = GetEffectiveH();
    uint8_t old_h = h_reg;
    h_reg--;
    SetFlags_DEC(h_reg, old_h);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_n() {
    GetEffectiveH() = memory[PC()++];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DAA() {
    // Decimal-adjust A after a BCD add/sub. The correction gates are evaluated
    // from the incoming A/H/C even for otherwise-invalid flag combinations; the
    // N flag only chooses whether that correction is added or subtracted.
    const uint8_t a = A();
    const bool n = F() & Constants::Flags::SUBTRACT;
    const bool h = F() & Constants::Flags::HALF;
    const bool c = F() & Constants::Flags::CARRY;

    uint8_t correction = 0;
    bool out_carry = c;
    if (h || (a & 0x0F) > 9) correction |= 0x06;
    if (c || a > 0x99) {
        correction |= 0x60;
        out_carry = true;
    }

    if (!n) {
        A() = static_cast<uint8_t>(a + correction);
    } else {
        A() = static_cast<uint8_t>(a - correction);
    }
    const bool out_half = (a ^ A()) & Constants::Flags::HALF;

    F() = 0;
    if (n)         F() |= Constants::Flags::SUBTRACT;   // N is unchanged
    if (out_carry) F() |= Constants::Flags::CARRY;
    if (out_half)  F() |= Constants::Flags::HALF;
    F() |= Flags_SZXY(A());
    F() |= CalculateParity(A());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JR_Z() {
    int8_t displacement = memory[PC()++];
    if (F() & 0x40) { // Zero flag set
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 12;
    } else {
        t_cycle += 7;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_HL_HL() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    const uint16_t old_hl = hl_reg;
    hl_reg = static_cast<uint16_t>(old_hl + old_hl);
    SetFlags_ADD16(hl_reg, old_hl, old_hl);
    
    // ADD HL,rr is 11 T; the IX/IY form is 15 T, but the extra 4 is the DD/FD
    // prefix M1 already charged at the prefix fetch — so the body is 11 either way.
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_HL_mnn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    PC() += 2;
    uint16_t& hl_reg = GetEffectiveHL_Register();
    hl_reg = memory[WZ()] | (memory[WZ() + 1] << 8);
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_HL() {
    GetEffectiveHL_Register()--;
    t_cycle += GetRegisterOpCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_L() {
    uint8_t& l_reg = GetEffectiveL();
    uint8_t old_l = l_reg;
    l_reg++;
    SetFlags_INC(l_reg, old_l);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_L() {
    uint8_t& l_reg = GetEffectiveL();
    uint8_t old_l = l_reg;
    l_reg--;
    SetFlags_DEC(l_reg, old_l);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_n() {
    GetEffectiveL() = memory[PC()++];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CPL() {
    A() = ~A();
    F() = (F() & (Constants::Flags::SIGN | Constants::Flags::ZERO |
                  Constants::Flags::PARITY | Constants::Flags::CARRY)) |
          (A() & (Constants::Flags::X | Constants::Flags::Y)) |
          Constants::Flags::HALF | Constants::Flags::SUBTRACT;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JR_NC() {
    int8_t displacement = memory[PC()++];
    if (!(F() & 0x01)) { // Carry flag not set
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 12;
    } else {
        t_cycle += 7;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_SP_nn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    SP() = WZ();
    PC() += 2;
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mnn_A() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    PC() += 2;
    memory[WZ()] = A();
    t_cycle += 13;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_SP() {
    SP()++;
    t_cycle += 6;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t old_value = value;
    value++;
    memory[address] = value;
    SetFlags_INC(value, old_value);
    // INC (HL)=11 T; INC (IX/IY+d)=23 T (body 19 + DD/FD prefix M1 charged above).
    t_cycle += (current_state == CPUState::NORMAL) ? 11 : 19;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t old_value = value;
    value--;
    memory[address] = value;
    SetFlags_DEC(value, old_value);
    // DEC (HL)=11 T; DEC (IX/IY+d)=23 T (body 19 + DD/FD prefix M1 charged above).
    t_cycle += (current_state == CPUState::NORMAL) ? 11 : 19;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mHL_n() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = memory[PC()++];
    // LD (HL),n=10 T; LD (IX/IY+d),n=19 T (body 15 + DD/FD prefix M1 charged above).
    t_cycle += (current_state == CPUState::NORMAL) ? 10 : 15;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SCF() {
    F() = (F() & (Constants::Flags::SIGN | Constants::Flags::ZERO |
                  Constants::Flags::PARITY)) |
          (A() & (Constants::Flags::X | Constants::Flags::Y)) |
          Constants::Flags::CARRY;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JR_C() {
    int8_t displacement = memory[PC()++];
    if (F() & 0x01) { // Carry flag set
        WZ() = PC() + displacement;
        PC() = WZ();
        t_cycle += 12;
    } else {
        t_cycle += 7;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_HL_SP() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    const uint16_t old_hl = hl_reg;
    const uint16_t operand = SP();
    hl_reg = static_cast<uint16_t>(old_hl + operand);
    SetFlags_ADD16(hl_reg, old_hl, operand);
    
    // ADD HL,rr is 11 T; the IX/IY form is 15 T, but the extra 4 is the DD/FD
    // prefix M1 already charged at the prefix fetch — so the body is 11 either way.
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_mnn() {
    WZ() = memory[PC()] | (memory[PC()+1] << 8);
    PC() += 2;
    A() = memory[WZ()];
    t_cycle += 13;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_SP() {
    SP()--;
    t_cycle += 6;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INC_A() {
    uint8_t old_a = A();
    A()++;
    SetFlags_INC(A(), old_a);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DEC_A() {
    uint8_t old_a = A();
    A()--;
    SetFlags_DEC(A(), old_a);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_n() {
    A() = memory[PC()++];
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CCF() {
    const bool old_carry = F() & Constants::Flags::CARRY;
    F() = (F() & (Constants::Flags::SIGN | Constants::Flags::ZERO |
                  Constants::Flags::PARITY)) |
          (A() & (Constants::Flags::X | Constants::Flags::Y));
    if (old_carry) {
        F() |= Constants::Flags::HALF;
    } else {
        F() |= Constants::Flags::CARRY;
    }
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_B() {
    // B = B (NOP equivalent)
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_C() {
    B() = C();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_D() {
    B() = D();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_E() {
    B() = E();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_H() {
    B() = GetEffectiveH();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_L() {
    B() = GetEffectiveL();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    B() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_B_A() {
    B() = A();
    t_cycle += 4;
}

// =============================================================================
// Load Instructions (0x48-0x7F) - Remaining register-to-register transfers
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_B() {
    C() = B();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_C() {
    // C = C (NOP equivalent)
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_D() {
    C() = D();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_E() {
    C() = E();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_H() {
    C() = GetEffectiveH();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_L() {
    C() = GetEffectiveL();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    C() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_C_A() {
    C() = A();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_B() {
    D() = B();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_C() {
    D() = C();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_D() {
    // D = D (NOP equivalent)
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_E() {
    D() = E();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_H() {
    D() = GetEffectiveH();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_L() {
    D() = GetEffectiveL();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    D() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_D_A() {
    D() = A();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_B() {
    E() = B();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_C() {
    E() = C();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_D() {
    E() = D();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_E() {
    // E = E (NOP equivalent)
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_H() {
    E() = GetEffectiveH();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_L() {
    E() = GetEffectiveL();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    E() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_E_A() {
    E() = A();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_B() {
    GetEffectiveH() = B();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_C() {
    GetEffectiveH() = C();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_D() {
    GetEffectiveH() = D();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_E() {
    GetEffectiveH() = E();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_H() {
    // H = H (NOP equivalent)
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_L() {
    GetEffectiveH() = GetEffectiveL();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    H() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_H_A() {
    GetEffectiveH() = A();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_B() {
    GetEffectiveL() = B();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_C() {
    GetEffectiveL() = C();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_D() {
    GetEffectiveL() = D();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_E() {
    GetEffectiveL() = E();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_H() {
    GetEffectiveL() = GetEffectiveH();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_L() {
    // L = L (NOP equivalent)
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    L() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_L_A() {
    GetEffectiveL() = A();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mHL_B() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = B();
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mHL_C() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = C();
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mHL_D() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = D();
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mHL_E() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = E();
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mHL_H() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = H();
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mHL_L() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = L();
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::HALT() {
    // HALT instruction - processor stops until interrupt
    _halted = true;
    t_cycle += 4;  // HALT instruction takes 4 cycles
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mHL_A() {
    uint16_t address = GetEffectiveHL_Memory();
    memory[address] = A();
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_B() {
    A() = B();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_C() {
    A() = C();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_D() {
    A() = D();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_E() {
    A() = E();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_H() {
    A() = GetEffectiveH();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_L() {
    A() = GetEffectiveL();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    A() = memory[address];
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_A() {
    // A = A (NOP equivalent)
    t_cycle += 4;
}

// =============================================================================
// Flag Helper Functions
// =============================================================================

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::Flags_SZXY(uint8_t value) const {
    uint8_t flags = value & (Constants::Flags::SIGN | Constants::Flags::X | Constants::Flags::Y);
    if (value == 0) flags |= Constants::Flags::ZERO;
    return flags;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::Flags_SZXY16(uint16_t value) const {
    uint8_t flags = static_cast<uint8_t>((value >> 8) &
                                         (Constants::Flags::SIGN | Constants::Flags::X | Constants::Flags::Y));
    if (value == 0) flags |= Constants::Flags::ZERO;
    return flags;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_ADD(uint8_t result, uint8_t operand1, uint8_t operand2) {
    SetFlags_ADC(result, operand1, operand2, 0);
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_ADC(uint8_t result, uint8_t operand1, uint8_t operand2, uint8_t carry) {
    const uint16_t full = static_cast<uint16_t>(operand1) + operand2 + carry;
    F() = Flags_SZXY(result);
    if (((operand1 ^ operand2 ^ result) & 0x10) != 0) F() |= Constants::Flags::HALF;
    if ((~(operand1 ^ operand2) & (operand1 ^ result) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (full & 0x100) F() |= Constants::Flags::CARRY;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_SUB(uint8_t result, uint8_t operand1, uint8_t operand2) {
    SetFlags_SBC(result, operand1, operand2, 0);
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_SBC(uint8_t result, uint8_t operand1, uint8_t operand2, uint8_t carry) {
    const uint16_t subtrahend = static_cast<uint16_t>(operand2) + carry;
    F() = Flags_SZXY(result) | Constants::Flags::SUBTRACT;
    if (((operand1 ^ operand2 ^ result) & 0x10) != 0) F() |= Constants::Flags::HALF;
    if (((operand1 ^ operand2) & (operand1 ^ result) & 0x80) != 0) F() |= Constants::Flags::PARITY;
    if (static_cast<uint16_t>(operand1) < subtrahend) F() |= Constants::Flags::CARRY;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_CP(uint8_t result, uint8_t operand1, uint8_t operand2) {
    SetFlags_SUB(result, operand1, operand2);
    F() = (F() & ~(Constants::Flags::X | Constants::Flags::Y)) |
          (operand2 & (Constants::Flags::X | Constants::Flags::Y));
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_LOGIC(uint8_t result, bool half_carry) {
    F() = Flags_SZXY(result);
    if (half_carry) F() |= Constants::Flags::HALF;
    F() |= CalculateParity(result);
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_INC(uint8_t result, uint8_t old_value) {
    F() = (F() & Constants::Flags::CARRY) | Flags_SZXY(result);
    if ((old_value & 0x0F) == 0x0F) F() |= Constants::Flags::HALF;
    if (old_value == 0x7F) F() |= Constants::Flags::PARITY;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_DEC(uint8_t result, uint8_t old_value) {
    F() = (F() & Constants::Flags::CARRY) | Flags_SZXY(result) | Constants::Flags::SUBTRACT;
    if ((old_value & 0x0F) == 0) F() |= Constants::Flags::HALF;
    if (old_value == 0x80) F() |= Constants::Flags::PARITY;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_ADD16(uint16_t result, uint16_t operand1, uint16_t operand2) {
    const uint32_t full = static_cast<uint32_t>(operand1) + operand2;
    F() = (F() & (Constants::Flags::SIGN | Constants::Flags::ZERO | Constants::Flags::PARITY)) |
          (static_cast<uint8_t>(result >> 8) & (Constants::Flags::X | Constants::Flags::Y));
    if (((operand1 ^ operand2 ^ result) & 0x1000) != 0) F() |= Constants::Flags::HALF;
    if (full & 0x10000) F() |= Constants::Flags::CARRY;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_ADC16(uint16_t result, uint16_t operand1, uint16_t operand2, uint8_t carry) {
    const uint32_t full = static_cast<uint32_t>(operand1) + operand2 + carry;
    F() = Flags_SZXY16(result);
    if (((operand1 ^ operand2 ^ result) & 0x1000) != 0) F() |= Constants::Flags::HALF;
    if ((~(operand1 ^ operand2) & (operand1 ^ result) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (full & 0x10000) F() |= Constants::Flags::CARRY;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetFlags_SBC16(uint16_t result, uint16_t operand1, uint16_t operand2, uint8_t carry) {
    const uint32_t subtrahend = static_cast<uint32_t>(operand2) + carry;
    F() = Flags_SZXY16(result) | Constants::Flags::SUBTRACT;
    if (((operand1 ^ operand2 ^ result) & 0x1000) != 0) F() |= Constants::Flags::HALF;
    if (((operand1 ^ operand2) & (operand1 ^ result) & 0x8000) != 0) F() |= Constants::Flags::PARITY;
    if (static_cast<uint32_t>(operand1) < subtrahend) F() |= Constants::Flags::CARRY;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::CalculateParity(uint8_t value) {
    uint8_t parity = 0;
    for (int i = 0; i < 8; ++i) {
        if (value & (1 << i)) parity++;
    }
    return (parity & 1) ? 0 : Constants::Flags::PARITY; // Even parity
}

// =============================================================================
// Arithmetic and Logic Instructions (0x80-0xBF)
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_B() {
    uint8_t old_a = A();
    A() += B();
    SetFlags_ADD(A(), old_a, B());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_C() {
    uint8_t old_a = A();
    A() += C();
    SetFlags_ADD(A(), old_a, C());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_D() {
    uint8_t old_a = A();
    A() += D();
    SetFlags_ADD(A(), old_a, D());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_E() {
    uint8_t old_a = A();
    A() += E();
    SetFlags_ADD(A(), old_a, E());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_H() {
    uint8_t old_a = A();
    uint8_t h_val = GetEffectiveH();
    A() += h_val;
    SetFlags_ADD(A(), old_a, h_val);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_L() {
    uint8_t old_a = A();
    uint8_t l_val = GetEffectiveL();
    A() += l_val;
    SetFlags_ADD(A(), old_a, l_val);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_mHL() {
    uint8_t old_a = A();
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    A() += value;
    SetFlags_ADD(A(), old_a, value);
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_A() {
    uint8_t old_a = A();
    A() += A();
    SetFlags_ADD(A(), old_a, old_a);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_B() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(B()) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, B(), carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_C() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(C()) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, C(), carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_D() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(D()) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, D(), carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_E() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(E()) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, E(), carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_H() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t h_val = GetEffectiveH();
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(h_val) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, h_val, carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_L() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t l_val = GetEffectiveL();
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(l_val) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, l_val, carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_mHL() {
    uint8_t old_a = A();
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(value) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, value, carry);
    
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_A() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(A()) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, old_a, carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_B() {
    uint8_t old_a = A();
    A() -= B();
    SetFlags_SUB(A(), old_a, B());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_C() {
    uint8_t old_a = A();
    A() -= C();
    SetFlags_SUB(A(), old_a, C());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_D() {
    uint8_t old_a = A();
    A() -= D();
    SetFlags_SUB(A(), old_a, D());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_E() {
    uint8_t old_a = A();
    A() -= E();
    SetFlags_SUB(A(), old_a, E());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_H() {
    uint8_t old_a = A();
    uint8_t h_val = GetEffectiveH();
    A() -= h_val;
    SetFlags_SUB(A(), old_a, h_val);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_L() {
    uint8_t old_a = A();
    uint8_t l_val = GetEffectiveL();
    A() -= l_val;
    SetFlags_SUB(A(), old_a, l_val);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_mHL() {
    uint8_t old_a = A();
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    A() -= value;
    SetFlags_SUB(A(), old_a, value);
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_A() {
    uint8_t old_a = A();
    A() -= A(); // Result is always 0
    SetFlags_SUB(A(), old_a, old_a);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_B() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(B()) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, B(), carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_C() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(C()) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, C(), carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_D() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(D()) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, D(), carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_E() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(E()) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, E(), carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_H() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t h_val = GetEffectiveH();
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(h_val) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, h_val, carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_L() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t l_val = GetEffectiveL();
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(l_val) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, l_val, carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_mHL() {
    uint8_t old_a = A();
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(value) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, value, carry);
    
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_A() {
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(A()) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, old_a, carry);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_B() {
    A() &= B();
    SetFlags_LOGIC(A(), true);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_C() {
    A() &= C();
    SetFlags_LOGIC(A(), true);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_D() {
    A() &= D();
    SetFlags_LOGIC(A(), true);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_E() {
    A() &= E();
    SetFlags_LOGIC(A(), true);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_H() {
    A() &= GetEffectiveH();
    SetFlags_LOGIC(A(), true);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_L() {
    A() &= GetEffectiveL();
    SetFlags_LOGIC(A(), true);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    A() &= memory[address];
    SetFlags_LOGIC(A(), true);
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_A() {
    A() &= A();
    SetFlags_LOGIC(A(), true);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_B() {
    A() ^= B();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_C() {
    A() ^= C();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_D() {
    A() ^= D();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_E() {
    A() ^= E();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_H() {
    A() ^= GetEffectiveH();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_L() {
    A() ^= GetEffectiveL();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    A() ^= memory[address];
    SetFlags_LOGIC(A(), false);
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_A() {
    A() ^= A(); // Result is always 0
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_B() {
    A() |= B();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_C() {
    A() |= C();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_D() {
    A() |= D();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_E() {
    A() |= E();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_H() {
    A() |= GetEffectiveH();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_L() {
    A() |= GetEffectiveL();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    A() |= memory[address];
    SetFlags_LOGIC(A(), false);
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_A() {
    A() |= A();
    SetFlags_LOGIC(A(), false);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_B() {
    uint8_t result = A() - B();
    SetFlags_CP(result, A(), B());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_C() {
    uint8_t result = A() - C();
    SetFlags_CP(result, A(), C());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_D() {
    uint8_t result = A() - D();
    SetFlags_CP(result, A(), D());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_E() {
    uint8_t result = A() - E();
    SetFlags_CP(result, A(), E());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_H() {
    uint8_t h_val = GetEffectiveH();
    uint8_t result = A() - h_val;
    SetFlags_CP(result, A(), h_val);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_L() {
    uint8_t l_val = GetEffectiveL();
    uint8_t result = A() - l_val;
    SetFlags_CP(result, A(), l_val);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_mHL() {
    uint16_t address = GetEffectiveHL_Memory();
    uint8_t value = memory[address];
    uint8_t result = A() - value;
    SetFlags_CP(result, A(), value);
    t_cycle += GetMemoryAccessCycles();
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_A() {
    uint8_t result = A() - A();
    SetFlags_CP(result, A(), A());
    t_cycle += 4;
}

// =============================================================================
// Stack and Condition Helper Functions
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PushWord(uint16_t value) {
    SP() -= 2;
    memory[SP()] = value & 0xFF;        // Low byte
    memory[SP() + 1] = (value >> 8);    // High byte
}

template <class Memory, class Io>
uint16_t CPUImpl<Memory, Io>::PopWord() {
    uint16_t value = memory[SP()] | (memory[SP() + 1] << 8);
    SP() += 2;
    return value;
}

template <class Memory, class Io>
bool CPUImpl<Memory, Io>::CheckCondition(uint8_t condition) {
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

template <class Memory, class Io>
uint16_t CPUImpl<Memory, Io>::GetEffectiveHL_Memory() {
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

template <class Memory, class Io>
uint16_t& CPUImpl<Memory, Io>::GetEffectiveHL_Register() {
    switch (current_state) {
        case CPUState::DD_PREFIX:
            return IX();
        case CPUState::FD_PREFIX:
            return IY();
        default:
            return HL();
    }
}

template <class Memory, class Io>
uint8_t& CPUImpl<Memory, Io>::GetEffectiveH() {
    switch (current_state) {
        case CPUState::DD_PREFIX:
            return _IX.r8.hi; // IXH
        case CPUState::FD_PREFIX:
            return _IY.r8.hi; // IYH
        default:
            return H();
    }
}

template <class Memory, class Io>
uint8_t& CPUImpl<Memory, Io>::GetEffectiveL() {
    switch (current_state) {
        case CPUState::DD_PREFIX:
            return _IX.r8.lo; // IXL
        case CPUState::FD_PREFIX:
            return _IY.r8.lo; // IYL
        default:
            return L();
    }
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::GetMemoryAccessCycles() {
    // (HL)=7 T; (IX/IY+d)=19 T. The body returns 15 for the indexed form (the
    // extra 8 over (HL) is the displacement read + internal add); the remaining 4
    // is the DD/FD prefix M1, charged at the prefix fetch.
    return (current_state == CPUState::NORMAL) ? 7 : 15;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::GetRegisterOpCycles() {
    // Register operations: HL=6 cycles, IX/IY=6 cycles (prefix adds its own 4 cycles)
    return 6;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::GetArithmeticMemCycles() {
    // A,(HL)=7 T; A,(IX/IY+d)=19 T. Body returns 15 for the indexed form; the
    // remaining 4 is the DD/FD prefix M1, charged at the prefix fetch.
    return (current_state == CPUState::NORMAL) ? 7 : 15;
}

// =============================================================================
// Control Flow, Stack, and I/O Instructions (0xC0-0xFF)
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET_NZ() {
    if (CheckCondition(0)) { // NZ
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::POP_BC() {
    BC() = PopWord();
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_NZ_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(0)) { // NZ
        PC() = address;
    }
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() = address;
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_NZ_nn() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PUSH_BC() {
    PushWord(BC());
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADD_A_n() {
    uint8_t value = memory[PC()++];
    uint8_t old_a = A();
    A() += value;
    SetFlags_ADD(A(), old_a, value);
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RST_00() {
    PushWord(PC());
    PC() = 0x00;
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET_Z() {
    if (CheckCondition(1)) { // Z
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET() {
    PC() = PopWord();
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_Z_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(1)) { // Z
        PC() = address;
    }
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PREFIX_CB() {
    // This should never be called - CB prefix is handled in Step()
    // If we reach here, it means the state machine has a bug
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_Z_nn() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    PushWord(PC());
    PC() = address;
    t_cycle += 17;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_A_n() {
    uint8_t value = memory[PC()++];
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint16_t result = static_cast<uint16_t>(A()) + static_cast<uint16_t>(value) + carry;
    A() = result & 0xFF;
    SetFlags_ADC(A(), old_a, value, carry);
    
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RST_08() {
    PushWord(PC());
    PC() = 0x08;
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET_NC() {
    if (CheckCondition(2)) { // NC
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::POP_DE() {
    DE() = PopWord();
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_NC_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(2)) { // NC
        PC() = address;
    }
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_n_A() {
    uint8_t port = memory[PC()++];
    // OUT (n),A drives A onto the high address byte (full 16-bit port).
    t_cycle += 7;
    io.Out((static_cast<uint16_t>(A()) << 8) | port, A());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_NC_nn() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PUSH_DE() {
    PushWord(DE());
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SUB_n() {
    uint8_t value = memory[PC()++];
    uint8_t old_a = A();
    A() -= value;
    SetFlags_SUB(A(), old_a, value);
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RST_10() {
    PushWord(PC());
    PC() = 0x10;
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET_C() {
    if (CheckCondition(3)) { // C
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::EXX() {
    // Exchange BC, DE, HL with BC', DE', HL'
    uint16_t temp;
    temp = BC(); BC() = _BC1.r16; _BC1.r16 = temp;
    temp = DE(); DE() = _DE1.r16; _DE1.r16 = temp;
    temp = HL(); HL() = _HL1.r16; _HL1.r16 = temp;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_C_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(3)) { // C
        PC() = address;
    }
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_A_n() {
    uint8_t port = memory[PC()++];
    // IN A,(n) drives A onto the high address byte; A's old value forms the port.
    // I/O timing split (see FLOATING_BUS_DESIGN.md §5): charge the fetch M-cycles
    // (M1=4 + operand=3) BEFORE the port read, so a device sampling the clock
    // (the ULA's floating bus) sees the I/O M-cycle T-state; charge M3=4 after.
    // Total is unchanged (11), so instruction_timing_test stays green.
    t_cycle += 7;
    A() = io.In((static_cast<uint16_t>(A()) << 8) | port);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_C_nn() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PREFIX_DD() {
    // DD prefix handling is implemented in the main Step() state machine
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_A_n() {
    uint8_t value = memory[PC()++];
    uint8_t old_a = A();
    uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    int16_t result = static_cast<int16_t>(A()) - static_cast<int16_t>(value) - carry;
    A() = result & 0xFF;
    SetFlags_SBC(A(), old_a, value, carry);
    
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RST_18() {
    PushWord(PC());
    PC() = 0x18;
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET_PO() {
    if (CheckCondition(4)) { // PO
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::POP_HL() {
    GetEffectiveHL_Register() = PopWord();
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_PO_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(4)) { // PO
        PC() = address;
    }
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::EX_mSP_HL() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    uint16_t temp = memory[SP()] | (memory[SP() + 1] << 8);
    memory[SP()] = hl_reg & 0xFF;        // Low byte
    memory[SP() + 1] = (hl_reg >> 8);    // High byte
    hl_reg = temp;
    t_cycle += 19;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_PO_nn() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PUSH_HL() {
    PushWord(GetEffectiveHL_Register());
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::AND_n() {
    uint8_t value = memory[PC()++];
    A() &= value;
    SetFlags_LOGIC(A(), true);
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RST_20() {
    PushWord(PC());
    PC() = 0x20;
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET_PE() {
    if (CheckCondition(5)) { // PE
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_HL() {
    PC() = GetEffectiveHL_Register();
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_PE_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(5)) { // PE
        PC() = address;
    }
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::EX_DE_HL() {
    uint16_t& hl_reg = GetEffectiveHL_Register();
    uint16_t temp = DE();
    DE() = hl_reg;
    hl_reg = temp;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_PE_nn() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PREFIX_ED() {
    // ED prefix handling is implemented in the main Step() state machine
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::XOR_n() {
    uint8_t value = memory[PC()++];
    A() ^= value;
    SetFlags_LOGIC(A(), false);
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RST_28() {
    PushWord(PC());
    PC() = 0x28;
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET_P() {
    if (CheckCondition(6)) { // P
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::POP_AF() {
    AF() = PopWord();
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_P_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(6)) { // P
        PC() = address;
    }
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::DI() {
    IFF1() = false;
    IFF2() = false;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_P_nn() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PUSH_AF() {
    PushWord(AF());
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OR_n() {
    uint8_t value = memory[PC()++];
    A() |= value;
    SetFlags_LOGIC(A(), false);
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RST_30() {
    PushWord(PC());
    PC() = 0x30;
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RET_M() {
    if (CheckCondition(7)) { // M
        PC() = PopWord();
        t_cycle += 11;
    } else {
        t_cycle += 5;
    }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_SP_HL() {
    SP() = GetEffectiveHL_Register();
    t_cycle += 6;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::JP_M_nn() {
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    if (CheckCondition(7)) { // M
        PC() = address;
    }
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::EI() {
    IFF1() = true;
    IFF2() = true;
    ei_defer_ = true;   // interrupts not accepted until after the next instruction
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CALL_M_nn() {
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

template <class Memory, class Io>
void CPUImpl<Memory, Io>::PREFIX_FD() {
    // FD prefix handling is implemented in the main Step() state machine
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CP_n() {
    uint8_t value = memory[PC()++];
    uint8_t result = A() - value;
    SetFlags_CP(result, A(), value);
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RST_38() {
    PushWord(PC());
    PC() = 0x38;
    t_cycle += 11;
}

// =============================================================================
// CB Instruction Implementation - Compact Decoder
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ExecuteCBInstruction(uint8_t opcode) {
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
            // Timing remainders after prefix M1s: CB (HL)=11, DD CB/FD CB=15.
            if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                t_cycle += 15;
            } else {
                t_cycle += 11;
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
                t_cycle += 15;
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
                
                t_cycle += 4;
            }
        }
    } else {
        // Bit operations (bits 7-6 = 01, 10, or 11)
        uint8_t bit_num = (opcode >> 3) & 0x07; // Bits 5-4-3: bit number
        
        if (reg_code == 6) {
            // Memory operation
            const uint16_t address = GetEffectiveHL_Memory();
            uint8_t value = memory[address];
            
            switch (operation) {
                case 1: // BIT - test bit
                    TestBit(value, bit_num, static_cast<uint8_t>(address >> 8));
                    // Timing remainders after prefix M1s: BIT (HL)=8, DDCB/FDCB BIT=12.
                    if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                        t_cycle += 12;
                    } else {
                        t_cycle += 8;
                    }
                    break;
                case 2: // RES - reset bit
                    memory[address] = ResetBit(value, bit_num);
                    // Timing remainders after prefix M1s: RES (HL)=11, DDCB/FDCB RES=15.
                    if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                        t_cycle += 15;
                    } else {
                        t_cycle += 11;
                    }
                    break;
                case 3: // SET - set bit
                    memory[address] = SetBit(value, bit_num);
                    // Timing remainders after prefix M1s: SET (HL)=11, DDCB/FDCB SET=15.
                    if (current_state == CPUState::DD_CB_PREFIX || current_state == CPUState::FD_CB_PREFIX) {
                        t_cycle += 15;
                    } else {
                        t_cycle += 11;
                    }
                    break;
            }
        } else {
            // Register operation
            uint8_t& reg = GetCBRegister(reg_code);
            
            switch (operation) {
                case 1: // BIT - test bit
                    TestBit(reg, bit_num, reg);
                    t_cycle += 4;
                    break;
                case 2: // RES - reset bit
                    reg = ResetBit(reg, bit_num);
                    t_cycle += 4;
                    break;
                case 3: // SET - set bit
                    reg = SetBit(reg, bit_num);
                    t_cycle += 4;
                    break;
            }
        }
    }
}

template <class Memory, class Io>
uint8_t& CPUImpl<Memory, Io>::GetCBRegister(uint8_t reg_code) {
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

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::GetCBMemory(uint8_t reg_code) {
    if (reg_code == 6) {
        uint16_t address = GetEffectiveHL_Memory();
        return memory[address];
    }
    return 0; // Should never happen
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SetCBMemory(uint8_t reg_code, uint8_t value) {
    if (reg_code == 6) {
        uint16_t address = GetEffectiveHL_Memory();
        memory[address] = value;
    }
}

// =============================================================================
// CB Instruction Helper Functions
// =============================================================================

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::RotateLeftCircular(uint8_t value) {
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = (value << 1) | bit7;
    
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    
    return result;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::RotateRightCircular(uint8_t value) {
    uint8_t bit0 = value & 0x01;
    uint8_t result = (value >> 1) | (bit0 << 7);
    
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit0) F() |= Constants::Flags::CARRY;
    
    return result;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::RotateLeft(uint8_t value) {
    uint8_t old_carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = (value << 1) | old_carry;
    
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    
    return result;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::RotateRight(uint8_t value) {
    uint8_t old_carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    uint8_t bit0 = value & 0x01;
    uint8_t result = (value >> 1) | (old_carry << 7);
    
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit0) F() |= Constants::Flags::CARRY;
    
    return result;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::ShiftLeftArithmetic(uint8_t value) {
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = value << 1;
    
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    
    return result;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::ShiftRightArithmetic(uint8_t value) {
    uint8_t bit0 = value & 0x01;
    uint8_t bit7 = value & 0x80; // Preserve sign bit
    uint8_t result = (value >> 1) | bit7;
    
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit0) F() |= Constants::Flags::CARRY;
    
    return result;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::ShiftLeftLogical(uint8_t value) {
    // Undocumented instruction - same as SLA but sets bit 0
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = (value << 1) | 0x01;
    
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    
    return result;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::ShiftRightLogical(uint8_t value) {
    uint8_t bit0 = value & 0x01;
    uint8_t result = value >> 1; // Logical shift - bit 7 becomes 0
    
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit0) F() |= Constants::Flags::CARRY;
    
    return result;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::TestBit(uint8_t value, uint8_t bit, uint8_t xy_source) {
    uint8_t bit_mask = 1 << bit;
    bool bit_set = (value & bit_mask) != 0;
    
    F() &= Constants::Flags::CARRY; // Preserve carry only
    F() |= Constants::Flags::HALF;  // H flag always set for BIT
    F() |= xy_source & (Constants::Flags::X | Constants::Flags::Y);
    
    if (!bit_set) F() |= Constants::Flags::ZERO;
    if (bit == 7 && bit_set) F() |= Constants::Flags::SIGN;
    if (!bit_set) F() |= Constants::Flags::PARITY; // P/V flag = Z flag for BIT
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::ResetBit(uint8_t value, uint8_t bit) {
    uint8_t bit_mask = ~(1 << bit);
    return value & bit_mask;
}

template <class Memory, class Io>
uint8_t CPUImpl<Memory, Io>::SetBit(uint8_t value, uint8_t bit) {
    uint8_t bit_mask = 1 << bit;
    return value | bit_mask;
}

// =============================================================================
// ED Instruction Implementations
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ED_NOP() {
    // Default handler for undefined ED instructions
    t_cycle += 4; // ED prefix M1 was already charged
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_HL_DE() {
    // ED 52 - Subtract DE from HL with carry
    const uint16_t old_hl = HL();
    const uint16_t operand = DE();
    const uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    HL() = static_cast<uint16_t>(old_hl - operand - carry);
    SetFlags_SBC16(HL(), old_hl, operand, carry);
    
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_HL_DE() {
    // ED 5A - Add DE to HL with carry
    const uint16_t old_hl = HL();
    const uint16_t operand = DE();
    const uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    HL() = static_cast<uint16_t>(old_hl + operand + carry);
    SetFlags_ADC16(HL(), old_hl, operand, carry);
    
    t_cycle += 11;
}

// =============================================================================
// Additional 16-bit Arithmetic ED Instructions
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_HL_BC() {
    // ED 42 - Subtract BC from HL with carry
    const uint16_t old_hl = HL();
    const uint16_t operand = BC();
    const uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    HL() = static_cast<uint16_t>(old_hl - operand - carry);
    SetFlags_SBC16(HL(), old_hl, operand, carry);
    
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_HL_BC() {
    // ED 4A - Add BC to HL with carry
    const uint16_t old_hl = HL();
    const uint16_t operand = BC();
    const uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    HL() = static_cast<uint16_t>(old_hl + operand + carry);
    SetFlags_ADC16(HL(), old_hl, operand, carry);
    
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_HL_HL() {
    // ED 62 - Subtract HL from HL with carry
    const uint16_t old_hl = HL();
    const uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    HL() = static_cast<uint16_t>(old_hl - old_hl - carry);
    SetFlags_SBC16(HL(), old_hl, old_hl, carry);
    
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_HL_HL() {
    // ED 6A - Add HL to HL with carry
    const uint16_t old_hl = HL();
    const uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    HL() = static_cast<uint16_t>(old_hl + old_hl + carry);
    SetFlags_ADC16(HL(), old_hl, old_hl, carry);
    
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SBC_HL_SP() {
    // ED 72 - Subtract SP from HL with carry
    const uint16_t old_hl = HL();
    const uint16_t operand = SP();
    const uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    HL() = static_cast<uint16_t>(old_hl - operand - carry);
    SetFlags_SBC16(HL(), old_hl, operand, carry);
    
    t_cycle += 11;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::ADC_HL_SP() {
    // ED 7A - Add SP to HL with carry
    const uint16_t old_hl = HL();
    const uint16_t operand = SP();
    const uint8_t carry = (F() & Constants::Flags::CARRY) ? 1 : 0;
    HL() = static_cast<uint16_t>(old_hl + operand + carry);
    SetFlags_ADC16(HL(), old_hl, operand, carry);
    
    t_cycle += 11;
}

// =============================================================================
// 16-bit Load/Store ED Instructions
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mnn_BC() {
    // ED 43 - Load BC to memory at 16-bit address
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    memory[address] = BC() & 0xFF;        // Low byte
    memory[address + 1] = (BC() >> 8);    // High byte
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_BC_mnn() {
    // ED 4B - Load memory at 16-bit address to BC
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    BC() = memory[address] | (memory[address + 1] << 8);
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mnn_DE() {
    // ED 53 - Load DE to memory at 16-bit address
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    memory[address] = DE() & 0xFF;        // Low byte
    memory[address + 1] = (DE() >> 8);    // High byte
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_DE_mnn() {
    // ED 5B - Load memory at 16-bit address to DE
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    DE() = memory[address] | (memory[address + 1] << 8);
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mnn_HL_ED() {
    // ED 63 - Load HL to memory at 16-bit address (ED version)
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    memory[address] = HL() & 0xFF;        // Low byte
    memory[address + 1] = (HL() >> 8);    // High byte
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_HL_mnn_ED() {
    // ED 6B - Load memory at 16-bit address to HL (ED version)
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    HL() = memory[address] | (memory[address + 1] << 8);
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_mnn_SP() {
    // ED 73 - Load SP to memory at 16-bit address
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    memory[address] = SP() & 0xFF;        // Low byte
    memory[address + 1] = (SP() >> 8);    // High byte
    t_cycle += 16;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_SP_mnn() {
    // ED 7B - Load memory at 16-bit address to SP
    uint16_t address = memory[PC()] | (memory[PC() + 1] << 8);
    PC() += 2;
    SP() = memory[address] | (memory[address + 1] << 8);
    t_cycle += 16;
}

// =============================================================================
// Special Operations and Register Transfer ED Instructions
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::NEG() {
    // ED 44 - Negate A (2's complement)
    uint8_t old_a = A();
    A() = (~A()) + 1;  // 2's complement negation
    SetFlags_SUB(A(), 0, old_a);
    
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RETN() {
    // ED 45 - Return from non-maskable interrupt
    PC() = PopWord();
    IFF1() = IFF2(); // Restore interrupt state
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IM_0() {
    // ED 46 - Set interrupt mode 0
    _interrupt_mode = 0;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_I_A() {
    // ED 47 - Load A to I register
    I() = A();
    t_cycle += 5;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RETI() {
    // ED 4D - Return from interrupt
    PC() = PopWord();
    IFF1() = IFF2(); // Restore interrupt state
    t_cycle += 10;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_R_A() {
    // ED 4F - Load A to R register
    R() = A();
    t_cycle += 5;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IM_1() {
    // ED 56 - Set interrupt mode 1
    _interrupt_mode = 1;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_I() {
    // ED 57 - Load I register to A
    A() = I();
    
    // Set flags based on I register value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (IFF2()) F() |= Constants::Flags::PARITY; // P/V = IFF2
    
    t_cycle += 5;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IM_2() {
    // ED 5E - Set interrupt mode 2
    _interrupt_mode = 2;
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LD_A_R() {
    // ED 5F - Load R register to A
    A() = R();
    
    // Set flags based on R register value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    if (IFF2()) F() |= Constants::Flags::PARITY; // P/V = IFF2
    
    t_cycle += 5;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RRD() {
    // ED 67 - Rotate right decimal (4-bit)
    uint8_t mem_val = memory[HL()];
    uint8_t a_low = A() & 0x0F;
    uint8_t mem_low = mem_val & 0x0F;
    uint8_t mem_high = (mem_val >> 4) & 0x0F;
    
    // Rotate: A[3:0] -> mem[7:4] -> mem[3:0] -> A[3:0]
    A() = (A() & 0xF0) | mem_low;
    memory[HL()] = (a_low << 4) | mem_high;
    
    F() = (F() & Constants::Flags::CARRY) | Flags_SZXY(A());
    F() |= CalculateParity(A());
    
    t_cycle += 14;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::RLD() {
    // ED 6F - Rotate left decimal (4-bit)
    uint8_t mem_val = memory[HL()];
    uint8_t a_low = A() & 0x0F;
    uint8_t mem_low = mem_val & 0x0F;
    uint8_t mem_high = (mem_val >> 4) & 0x0F;
    
    // Rotate: A[3:0] -> mem[3:0] -> mem[7:4] -> A[3:0]
    A() = (A() & 0xF0) | mem_high;
    memory[HL()] = (mem_low << 4) | a_low;
    
    F() = (F() & Constants::Flags::CARRY) | Flags_SZXY(A());
    F() |= CalculateParity(A());
    
    t_cycle += 14;
}

// =============================================================================
// Block Operation ED Instructions
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LDI() {
    // ED A0 - Load and increment
    const uint8_t value = memory[HL()];
    memory[DE()] = value;
    HL()++;
    DE()++;
    BC()--;
    
    const uint8_t sum = static_cast<uint8_t>(A() + value);
    F() &= (Constants::Flags::CARRY | Constants::Flags::ZERO | Constants::Flags::SIGN);
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
    if (sum & 0x08) F() |= Constants::Flags::X;
    if (sum & 0x02) F() |= Constants::Flags::Y;
    
    t_cycle += 12;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CPI() {
    // ED A1 - Compare and increment
    const uint8_t value = memory[HL()];
    const uint8_t result = static_cast<uint8_t>(A() - value);
    const bool half = (A() & 0x0F) < (value & 0x0F);
    const uint8_t xy = static_cast<uint8_t>(result - (half ? 1 : 0));
    HL()++;
    BC()--;
    
    F() &= Constants::Flags::CARRY; // Preserve carry only
    F() |= Constants::Flags::SUBTRACT; // N flag set for compare
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    if (half) F() |= Constants::Flags::HALF;
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
    if (xy & 0x08) F() |= Constants::Flags::X;
    if (xy & 0x02) F() |= Constants::Flags::Y;
    
    t_cycle += 12;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INI() {
    // ED A2 - Input and increment
    // I/O timing split (see FLOATING_BUS_DESIGN.md §5): charge the opcode M1 (5 T
    // for block I/O; the prefix M1 was charged by Step's dispatch) before the port
    // read, the remaining 7 T after. Prefix + handler total stays 16.
    t_cycle += 5;
    memory[HL()] = io.In(BC());
    HL()++;
    B()--;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag set
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;

    t_cycle += 7;   // (5 charged before the I/O read; body total 12)
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUTI() {
    // ED A3 - Output and increment
    t_cycle += 5;                   // opcode M1 before the I/O write (see INI split)
    io.Out(BC(), memory[HL()]);
    HL()++;
    B()--;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag set
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;
    
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LDD() {
    // ED A8 - Load and decrement
    const uint8_t value = memory[HL()];
    memory[DE()] = value;
    HL()--;
    DE()--;
    BC()--;
    
    const uint8_t sum = static_cast<uint8_t>(A() + value);
    F() &= (Constants::Flags::CARRY | Constants::Flags::ZERO | Constants::Flags::SIGN);
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
    if (sum & 0x08) F() |= Constants::Flags::X;
    if (sum & 0x02) F() |= Constants::Flags::Y;
    
    t_cycle += 12;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CPD() {
    // ED A9 - Compare and decrement
    const uint8_t value = memory[HL()];
    const uint8_t result = static_cast<uint8_t>(A() - value);
    const bool half = (A() & 0x0F) < (value & 0x0F);
    const uint8_t xy = static_cast<uint8_t>(result - (half ? 1 : 0));
    HL()--;
    BC()--;
    
    F() &= Constants::Flags::CARRY; // Preserve carry only
    F() |= Constants::Flags::SUBTRACT; // N flag set for compare
    if (result == 0) F() |= Constants::Flags::ZERO;
    if (result & 0x80) F() |= Constants::Flags::SIGN;
    if (half) F() |= Constants::Flags::HALF;
    if (BC() != 0) F() |= Constants::Flags::PARITY; // P/V = (BC != 0)
    if (xy & 0x08) F() |= Constants::Flags::X;
    if (xy & 0x02) F() |= Constants::Flags::Y;
    
    t_cycle += 12;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IND() {
    // ED AA - Input and decrement
    t_cycle += 5;                   // opcode M1 before the I/O read (see INI split)
    memory[HL()] = io.In(BC());
    HL()--;
    B()--;

    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag set
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;

    t_cycle += 7;   // (5 charged before the I/O read; body total 12)
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUTD() {
    // ED AB - Output and decrement
    t_cycle += 5;                   // opcode M1 before the I/O write (see INI split)
    io.Out(BC(), memory[HL()]);
    HL()--;
    B()--;
    
    // Set flags
    F() = Constants::Flags::SUBTRACT; // N flag set
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;
    
    t_cycle += 7;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LDIR() {
    // ED B0 - Load, increment and repeat.
    //
    // Executed ONE iteration per instruction step, the way a real Z80 does it:
    // perform a single LDI, then if BC != 0 rewind PC by 2 so the next fetch
    // re-executes this same instruction. Two consequences, both authentic:
    //   * It is *interruptible* between iterations — when an interrupt is taken
    //     mid-copy the pushed return address points back at the LDIR, so RETI
    //     resumes it. (The atomic version ran the whole copy in one step, so a
    //     long LDIR could not be broken by the 50 Hz frame interrupt.)
    //   * A single step never advances more than one iteration's T-states, so
    //     the frame clock can't overrun a whole frame on one instruction.
    // Timing per iteration: prefix + LDI body is 16 T; the repeat adds 5 (21 T),
    // the final iteration is 16 T — matching the hardware.
    LDI();
    if (BC() != 0) { PC() -= 2; t_cycle += 5; }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CPIR() {
    // ED B1 - Compare, increment and repeat (one iteration per step; see LDIR).
    // Repeats while BC != 0 and no match was found (Z clear). Interruptible
    // between iterations.
    CPI();
    if (BC() != 0 && !(F() & Constants::Flags::ZERO)) { PC() -= 2; t_cycle += 5; }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INIR() {
    // ED B2 - Input, increment and repeat (one iteration per step; see LDIR).
    // Repeats while B != 0. Interruptible between iterations.
    INI();
    if (B() != 0) { PC() -= 2; t_cycle += 5; }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OTIR() {
    // ED B3 - Output, increment and repeat (one iteration per step; see LDIR).
    // Repeats while B != 0. Interruptible between iterations.
    OUTI();
    if (B() != 0) { PC() -= 2; t_cycle += 5; }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::LDDR() {
    // ED B8 - Load, decrement and repeat (one iteration per step; see LDIR).
    // Repeats while BC != 0. Interruptible between iterations.
    LDD();
    if (BC() != 0) { PC() -= 2; t_cycle += 5; }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::CPDR() {
    // ED B9 - Compare, decrement and repeat (one iteration per step; see CPIR).
    // Repeats while BC != 0 and no match (Z clear). Interruptible between iterations.
    CPD();
    if (BC() != 0 && !(F() & Constants::Flags::ZERO)) { PC() -= 2; t_cycle += 5; }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::INDR() {
    // ED BA - Input, decrement and repeat (one iteration per step; see INIR).
    // Repeats while B != 0. Interruptible between iterations.
    IND();
    if (B() != 0) { PC() -= 2; t_cycle += 5; }
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OTDR() {
    // ED BB - Output, decrement and repeat (one iteration per step; see OTIR).
    // Repeats while B != 0. Interruptible between iterations.
    OUTD();
    if (B() != 0) { PC() -= 2; t_cycle += 5; }
}

// =============================================================================
// Individual I/O ED Instructions
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_B_C() {
    // ED 40 - Input from port C to B
    // I/O timing split (see FLOATING_BUS_DESIGN.md §5): the ED prefix M1 (4 T) was
    // charged by Step's dispatch; charge the opcode M1 (4 T) BEFORE the port read
    // so a device sampling the clock sees the I/O M-cycle, and the remaining 4 T
    // after. Prefix + handler total stays 12.
    t_cycle += 4;
    B() = io.In(BC());

    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (B() == 0) F() |= Constants::Flags::ZERO;
    if (B() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(B());

    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_C_B() {
    // ED 41 - Output B to port C
    t_cycle += 4;
    io.Out(BC(), B());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_C_C() {
    // ED 48 - Input from port C to C
    t_cycle += 4;                   // opcode M1 before the I/O read (see IN_B_C)
    C() = io.In(BC());

    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (C() == 0) F() |= Constants::Flags::ZERO;
    if (C() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(C());

    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_C_C() {
    // ED 49 - Output C to port C
    t_cycle += 4;
    io.Out(BC(), C());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_D_C() {
    // ED 50 - Input from port C to D
    t_cycle += 4;                   // opcode M1 before the I/O read (see IN_B_C)
    D() = io.In(BC());

    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (D() == 0) F() |= Constants::Flags::ZERO;
    if (D() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(D());

    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_C_D() {
    // ED 51 - Output D to port C
    t_cycle += 4;
    io.Out(BC(), D());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_E_C() {
    // ED 58 - Input from port C to E
    t_cycle += 4;                   // opcode M1 before the I/O read (see IN_B_C)
    E() = io.In(BC());

    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (E() == 0) F() |= Constants::Flags::ZERO;
    if (E() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(E());

    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_C_E() {
    // ED 59 - Output E to port C
    t_cycle += 4;
    io.Out(BC(), E());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_H_C() {
    // ED 60 - Input from port C to H
    t_cycle += 4;                   // opcode M1 before the I/O read (see IN_B_C)
    H() = io.In(BC());

    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (H() == 0) F() |= Constants::Flags::ZERO;
    if (H() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(H());

    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_C_H() {
    // ED 61 - Output H to port C
    t_cycle += 4;
    io.Out(BC(), H());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_L_C() {
    // ED 68 - Input from port C to L
    t_cycle += 4;                   // opcode M1 before the I/O read (see IN_B_C)
    L() = io.In(BC());

    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (L() == 0) F() |= Constants::Flags::ZERO;
    if (L() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(L());

    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_C_L() {
    // ED 69 - Output L to port C
    t_cycle += 4;
    io.Out(BC(), L());
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_F_C() {
    // ED 70 - Input from port C (undocumented - sets flags only, doesn't store value)
    t_cycle += 4;                   // opcode M1 before the I/O read (see IN_B_C)
    uint8_t value = io.In(BC());

    // Set flags based on input value but don't store it anywhere
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (value == 0) F() |= Constants::Flags::ZERO;
    if (value & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(value);

    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_C_0() {
    // ED 71 - Output 0 to port C (undocumented)
    t_cycle += 4;
    io.Out(BC(), 0);
    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::IN_A_C() {
    // ED 78 - Input from port C to A
    t_cycle += 4;                   // opcode M1 before the I/O read (see IN_B_C)
    A() = io.In(BC());

    // Set flags based on input value
    F() &= Constants::Flags::CARRY; // Preserve carry only
    if (A() == 0) F() |= Constants::Flags::ZERO;
    if (A() & 0x80) F() |= Constants::Flags::SIGN;
    F() |= CalculateParity(A());

    t_cycle += 4;
}

template <class Memory, class Io>
void CPUImpl<Memory, Io>::OUT_C_A() {
    // ED 79 - Output A to port C
    t_cycle += 4;
    io.Out(BC(), A());
    t_cycle += 4;
}

// =============================================================================
// Undocumented ED Instructions
// =============================================================================

template <class Memory, class Io>
void CPUImpl<Memory, Io>::SLL_mHL() {
    // ED 76 - Shift Left Logical (HL) - undocumented instruction
    // This is like SLA but always sets bit 0 to 1
    uint8_t value = memory[HL()];
    uint8_t bit7 = (value & 0x80) ? 1 : 0;
    uint8_t result = (value << 1) | 0x01; // Shift left and set bit 0
    
    memory[HL()] = result;
    
    // Set flags
    F() = Flags_SZXY(result);
    F() |= CalculateParity(result);
    if (bit7) F() |= Constants::Flags::CARRY;
    // H and N flags are reset (already 0)
    
    t_cycle += 11;
}

// =============================================================================
// Explicit Template Instantiations
// =============================================================================
// Emit the full CPU for each <Memory, Io> configuration used across the project
// so translation units that only see the declarations in z80_cpu.h link against
// these definitions. (Add a line here when a new configuration is introduced.)
//  - <FastMemory, OpenBusIo>                   : z80::CPU — production / benchmark
//  - <ObservableMemory, OpenBusIo>             : memory-observer tests
//  - <ObservableMemory, ObservableIo<LatchedIo>> : io_policy_test
//  - <ObservableMemory, ObservableIo<CallbackIo>> : the debugger AND the ZX
//      Spectrum (DebugCPU == SpectrumCpu — one config, so a DebugSession can
//      drive a running Spectrum; the ULA hooks the inner CallbackIo's ports)
template class CPUImpl<FastMemory, OpenBusIo>;
template class CPUImpl<ObservableMemory, OpenBusIo>;
template class CPUImpl<ObservableMemory, ObservableIo<LatchedIo>>;
template class CPUImpl<ObservableMemory, ObservableIo<CallbackIo>>;

} // namespace z80
