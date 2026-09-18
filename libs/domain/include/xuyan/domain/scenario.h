#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xuyan::domain {

enum class ErrorCode {
    validation_failed,
    revision_conflict,
    rule_conflict,
    missing_context,
    storage_error,
    command_conflict
};

struct Error {
    ErrorCode code{ErrorCode::validation_failed};
    std::string message;
    bool retryable{false};
    std::string suggested_action;
};

template <typename T>
struct Result {
    std::optional<T> value;
    std::optional<Error> error;

    static Result success(T result) { return Result{std::move(result), std::nullopt}; }
    static Result failure(Error failure) { return Result{std::nullopt, std::move(failure)}; }
    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
};

struct CharacterState {
    std::string id;
    std::string name;
    bool knows_gate_closure{false};
    bool knows_seal_forgery{false};
    int trust{0};
};

struct ScenarioState {
    int revision{0};
    int turn{0};
    int elapsed_ticks{0};
    std::string seal_holder_id{"actor-shentang"};
    bool seal_inspected{false};
    bool paused{false};
    bool completed{false};
    std::string narration;
    std::vector<CharacterState> characters;
};

struct CommitView {
    std::string branch_id;
    std::string commit_id;
    std::string parent_commit_id;
    std::string state_hash;
    ScenarioState state;
};

struct BranchInfo {
    std::string id;
    std::string name;
    std::string parent_id;
    std::string fork_commit_id;
    std::string head_commit_id;
};

enum class OperationType { inspect_seal, reveal_gate_secret, reveal_seal_forgery, transfer_seal };

struct ProposedOperation {
    OperationType type{OperationType::inspect_seal};
    std::string actor_id;
    std::string target_id;
    bool holder_consented{false};
};

ScenarioState makeGreyHarborInitialState();
Result<ScenarioState> applyOperation(const ScenarioState& input, const ProposedOperation& operation);
std::string canonicalState(const ScenarioState& state);
std::string stateHash(const ScenarioState& state);
const CharacterState* findCharacter(const ScenarioState& state, const std::string& id);

} // namespace xuyan::domain
