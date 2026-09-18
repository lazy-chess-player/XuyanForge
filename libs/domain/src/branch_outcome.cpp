#include "xuyan/domain/branch_outcome.h"

// Branch outcome records are plain immutable transfer objects; comparison rules
// live in the application service because they require persisted commit chains.
