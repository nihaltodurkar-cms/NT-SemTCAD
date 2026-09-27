// P4 S8 (NATIVE-DESKTOP-PLAN.md section 20.4): the validation-message
// formatting `AppController.structureValidationErrors`/
// `processValidationErrors` (gui/controllers/app_controller.py) apply
// to the backend's `structure.validate`/`process.validate` RPC results
// (S1). Qt-free: no widget/window needed to format a string.
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "document/process_document.hpp"

namespace tcad::desktop {

// One {"message", "object_id"} entry from structure.validate/
// process.validate's RPC result -- object_id is empty/absent for a
// flow-level error not scoped to any one step.
struct ValidationErrorOut {
    std::string message;
    std::string object_id;  // "" = none (matches Python's object_id=None)
};

// Parses the RPC result's JSON array (a list of {"message", "object_id"}
// objects) into ValidationErrorOut. Throws nlohmann::json::exception on
// a malformed entry -- the same "fail loudly, don't guess" convention
// every other parser in this data layer follows.
std::vector<ValidationErrorOut> parse_validation_errors(const nlohmann::json& array);

// AppController.structureValidationErrors: just the messages, in order
// -- structure errors are never scoped to a step.
std::vector<std::string> structure_error_messages(const std::vector<ValidationErrorOut>& errors);

// AppController._format_process_error + processValidationErrors,
// exactly: an error whose object_id resolves to a step currently in
// `flow` is formatted "Step NN — Label: message" (1-based index, the
// step's CURRENT position -- not wherever it was when the error was
// generated); anything else (no object_id, or one that no longer
// resolves) is the raw message.
std::vector<std::string> format_process_errors(const ProcessFlowDocument& flow,
                                                const std::vector<ValidationErrorOut>& errors);

}  // namespace tcad::desktop
