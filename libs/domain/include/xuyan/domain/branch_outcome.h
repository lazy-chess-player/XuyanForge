#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/world_version.h"

#include <string>
#include <vector>

namespace xuyan::domain {

struct BranchDifference {
    std::string field;
    std::string left_value;
    std::string right_value;
};

struct BranchComparison {
    std::string left_branch_id;
    std::string right_branch_id;
    std::string common_commit_id;
    std::string left_head_commit_id;
    std::string right_head_commit_id;
    std::vector<std::string> left_commit_ids;
    std::vector<std::string> right_commit_ids;
    std::vector<BranchDifference> differences;
    int left_calls{0};
    int right_calls{0};
    int left_input_tokens{0};
    int right_input_tokens{0};
    int left_output_tokens{0};
    int right_output_tokens{0};
    int left_unknown_calls{0};
    int right_unknown_calls{0};
};

} // namespace xuyan::domain
