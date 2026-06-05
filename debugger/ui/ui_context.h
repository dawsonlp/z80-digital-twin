//
// Z80 Digital Twin Debugger - UiContext
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The shared context handed to every panel each frame. It exposes the debug
// session, symbol table, and disassembler, a one-shot command buffer panels can
// post into (applied by the app), and the status line. Convenience helpers
// build a byte reader / symbol resolver bound to the current session/symbols.
//

#ifndef Z80_DBG_UI_CONTEXT_H
#define Z80_DBG_UI_CONTEXT_H

#include "debug_session.h"
#include "disassembler.h"
#include "symbol_table.h"

#include <cstdint>
#include <string>

namespace z80::dbg {

/// @brief One-shot debug commands a panel can request; the app applies and
///        clears them each frame.
struct DebugCommands {
    bool step = false;
    bool step_over = false;
    bool run = false;
    bool pause = false;
    bool reset = false;

    void Clear() { step = step_over = run = pause = reset = false; }
};

/// @brief Everything a panel needs to render and drive the debugger.
struct UiContext {
    DebugSession& session;
    SymbolTable& symbols;
    const Disassembler& disasm;
    DebugCommands& commands;
    std::string& status;

    [[nodiscard]] DebugCPU& cpu() const { return session.Cpu(); }

    [[nodiscard]] ByteReader reader() const {
        DebugSession* s = &session;
        return [s](uint16_t a) { return s->Cpu().ReadMemory(a); };
    }

    [[nodiscard]] SymbolResolver resolver() const { return symbols.MakeResolver(); }
};

} // namespace z80::dbg

#endif // Z80_DBG_UI_CONTEXT_H
