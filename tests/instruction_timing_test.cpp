//
// Z80 Digital Twin - instruction T-state timing verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Checks the T-state cost of a representative opcode from every timing class
// against the documented Z80 values. The model: each M1 fetch (including a
// prefix byte) costs 4 T charged when that byte is fetched, and the instruction
// body adds the rest — so prefixed instructions (CB/ED/DD/FD and the DDCB/FDCB
// compounds) must NOT double-count the prefix fetch.
//

#include "z80_cpu.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using z80::CPU;

int failures = 0;

struct Case {
    const char* name;
    std::vector<uint8_t> bytes;
    int expected;       // documented T-states
    uint16_t bc = 1;    // BC before execution (block ops need a sane counter)
};

void run(const Case& c) {
    CPU cpu;
    cpu.Reset();
    cpu.LoadProgram(c.bytes, 0x0000);
    cpu.BC() = c.bc;
    const uint64_t t0 = cpu.GetCycleCount();
    do { cpu.Step(); } while (!cpu.InstructionComplete());
    const uint64_t got = cpu.GetCycleCount() - t0;
    const bool ok = static_cast<int>(got) == c.expected;
    std::cout << (ok ? "  ✓ " : "  ✗ ") << c.name << ": " << got
              << " T (expect " << c.expected << ")\n";
    if (!ok) ++failures;
}

} // namespace

int main() {
    std::cout << "Instruction timing verification\n===============================\n";

    std::cout << "\n[1] Base opcodes\n";
    for (const Case& c : std::vector<Case>{
        {"NOP",          {0x00},             4},
        {"LD A,B",       {0x78},             4},
        {"LD A,n",       {0x3E, 0x00},       7},
        {"LD A,(HL)",    {0x7E},             7},
        {"LD (HL),A",    {0x77},             7},
        {"LD A,(nn)",    {0x3A, 0, 0},      13},
        {"LD HL,nn",     {0x21, 0, 0},      10},
        {"INC A",        {0x3C},             4},
        {"INC HL",       {0x23},             6},
        {"INC (HL)",     {0x34},            11},
        {"ADD HL,BC",    {0x09},            11},
        {"ADD A,(HL)",   {0x86},             7},
        {"JP nn",        {0xC3, 0, 0},      10},
        {"CALL nn",      {0xCD, 0, 0},      17},
        {"RET",          {0xC9},            10},
        {"PUSH BC",      {0xC5},            11},
        {"POP BC",       {0xC1},            10},
        {"RST 38",       {0xFF},            11},
    }) run(c);

    std::cout << "\n[2] CB-prefixed\n";
    for (const Case& c : std::vector<Case>{
        {"RLC B",        {0xCB, 0x00},       8},
        {"RLC (HL)",     {0xCB, 0x06},      15},
        {"BIT 0,B",      {0xCB, 0x40},       8},
        {"BIT 0,(HL)",   {0xCB, 0x46},      12},
        {"SET 0,B",      {0xCB, 0xC0},       8},
        {"SET 0,(HL)",   {0xCB, 0xC6},      15},
        {"RES 0,(HL)",   {0xCB, 0x86},      15},
    }) run(c);

    std::cout << "\n[3] ED-prefixed\n";
    for (const Case& c : std::vector<Case>{
        {"LDI",          {0xED, 0xA0},      16},
        {"LDD",          {0xED, 0xA8},      16},
        {"LDIR (BC=1)",  {0xED, 0xB0},      16},
        {"CPI",          {0xED, 0xA1},      16},
        {"INI",          {0xED, 0xA2},      16, 0x0100},
        {"OUTI",         {0xED, 0xA3},      16, 0x0100},
        {"NEG",          {0xED, 0x44},       8},
        {"SBC HL,DE",    {0xED, 0x52},      15},
        {"ADC HL,DE",    {0xED, 0x5A},      15},
        {"IN B,(C)",     {0xED, 0x40},      12},
        {"OUT (C),B",    {0xED, 0x41},      12},
        {"LD A,I",       {0xED, 0x57},       9},
        {"IM 1",         {0xED, 0x56},       8},
        {"RETI",         {0xED, 0x4D},      14},
        {"LD (nn),BC",   {0xED, 0x43, 0, 0}, 20},
        {"RRD",          {0xED, 0x67},      18},
    }) run(c);

    std::cout << "\n[4] DD/FD-prefixed (IX/IY)\n";
    for (const Case& c : std::vector<Case>{
        {"INC IX",       {0xDD, 0x23},      10},
        {"ADD IX,BC",    {0xDD, 0x09},      15},
        {"LD IX,nn",     {0xDD, 0x21, 0, 0}, 14},
        {"LD A,(IX+0)",  {0xDD, 0x7E, 0},   19},
        {"LD (IX+0),A",  {0xDD, 0x77, 0},   19},
        {"INC (IX+0)",   {0xDD, 0x34, 0},   23},
        {"ADD A,(IX+0)", {0xDD, 0x86, 0},   19},
        {"LD (IX+0),n",  {0xDD, 0x36, 0, 0}, 19},
        {"JP (IX)",      {0xDD, 0xE9},       8},
        {"INC IY",       {0xFD, 0x23},      10},
        {"LD A,(IY+0)",  {0xFD, 0x7E, 0},   19},
    }) run(c);

    std::cout << "\n[5] DDCB/FDCB-prefixed\n";
    for (const Case& c : std::vector<Case>{
        {"RLC (IX+0)",   {0xDD, 0xCB, 0, 0x06}, 23},
        {"BIT 0,(IX+0)", {0xDD, 0xCB, 0, 0x46}, 20},
        {"SET 0,(IX+0)", {0xDD, 0xCB, 0, 0xC6}, 23},
        {"RES 0,(IX+0)", {0xDD, 0xCB, 0, 0x86}, 23},
        {"RLC (IY+0)",   {0xFD, 0xCB, 0, 0x06}, 23},
    }) run(c);

    std::cout << "\n===============================\n";
    if (failures == 0) {
        std::cout << "✅ ALL TIMING CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " timing check(s) FAILED\n";
    return 1;
}
