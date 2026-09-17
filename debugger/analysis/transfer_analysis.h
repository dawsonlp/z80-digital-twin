// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "analysis_json.h"
#include "analysis_project.h"
#include "instruction_history.h"
#include <span>

namespace z80::dbg::analysis {

// Optional values are absent, never fabricated, for older or partial captures.
// This is instruction-entry context, not a resumable CPU snapshot.
struct TransferContext {
    std::optional<uint8_t> flags, b;
    std::optional<uint16_t> hl, ix, iy;
    bool operator==(const TransferContext&) const = default;
};
enum class DataAccessKind { Read, Write, RefusedWrite };
struct DataAccess {
    DataAccessKind kind = DataAccessKind::Read;
    uint16_t address = 0;
    uint8_t value = 0; // read value, committed write value, or refused attempted value
};
struct StackEvidence {
    uint16_t before_sp = 0, after_sp = 0;
    bool complete_data_accesses = false;
    // Producer attests no omitted execution or host mutation since this sample.
    // Numeric sequence adjacency alone never establishes continuity.
    std::optional<std::string> previous;
    std::vector<DataAccess> accesses;
};
struct TransferSample {
    std::string id; // supplied durable identity, not inferred from queue sequence
    InstructionObservation event;
    TransferContext before;
    std::optional<StackEvidence> stack = {};
};
struct TransferCapture {
    std::string source, limitations; // caller-supplied capture identity and limits
    std::vector<TransferSample> samples;
};
enum class TransferMechanism {
    Unknown, Sequential, Jump, IndirectJump, Call, Restart, Return,
    InterruptReturn, Repeat, Halt, MachineTransition
};
struct TransferFinding {
    std::string sample_id;
    TransferMechanism mechanism = TransferMechanism::Unknown;
    // Taken means the instruction's transfer was taken, NOT a logical call/return.
    std::optional<bool> taken;
    std::optional<uint16_t> encoded_target;
    std::string target_basis = "unresolved";
    std::vector<std::string> unresolved;
};

inline constexpr std::string_view kTransferTacticVersion = "z80-transfer-effects/1";
inline constexpr size_t kMaxTransferSamples = 8192;
inline constexpr size_t kMaxDataAccesses = 256;

struct ContinuationFinding {
    std::string sample_id, status = "unresolved", explanation;
    std::optional<std::string> call_sample;
    std::vector<std::string> unresolved;
};
inline constexpr std::string_view kContinuationTacticVersion = "z80-stack-continuations/1";
// Ordinary CALL/RST -> RET lineage only. Does not infer exclusive routines.
[[nodiscard]] std::vector<ContinuationFinding> AnalyzeContinuations(const TransferCapture& capture);

// Read-only tactics. Observed RET never implies a matched logical return.
[[nodiscard]] TransferFinding ClassifyTransfer(const TransferSample& sample);
[[nodiscard]] Result<TransferCapture> ReadTransferCapture(std::string_view text);
[[nodiscard]] Result<std::string> WriteTransferCapture(const TransferCapture& capture);
enum class AnalysisStage { Effects, Continuations, Values, Constructed };
// Occurrences are retained alongside grouped edges; destination sets stay open.
[[nodiscard]] Result<std::string> TransferReport(const TransferCapture& capture, AnalysisStage through = AnalysisStage::Constructed);

} // namespace z80::dbg::analysis
