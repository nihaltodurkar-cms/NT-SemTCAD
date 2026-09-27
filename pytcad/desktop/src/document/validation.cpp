#include "document/validation.hpp"

#include <cctype>
#include <map>

namespace tcad::desktop {

std::vector<ValidationErrorOut> parse_validation_errors(const nlohmann::json& array) {
    std::vector<ValidationErrorOut> out;
    out.reserve(array.size());
    for (const auto& e : array) {
        ValidationErrorOut v;
        v.message = e.at("message").get<std::string>();
        if (e.contains("object_id") && !e.at("object_id").is_null())
            v.object_id = e.at("object_id").get<std::string>();
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<std::string> structure_error_messages(const std::vector<ValidationErrorOut>& errors) {
    std::vector<std::string> out;
    out.reserve(errors.size());
    for (const auto& e : errors) out.push_back(e.message);
    return out;
}

namespace {

std::string operation_label(const std::string& operation) {
    static const std::map<std::string, std::string> labels{
        {"substrate", "Substrate"}, {"implant", "Implant"}, {"anneal", "Anneal"},
        {"oxidize", "Oxidize"}};
    auto it = labels.find(operation);
    if (it != labels.end()) return it->second;
    // AppController's own fallback, str.title() -- every operation this
    // schema defines is one of the four above (ProcessPanel.qml's own
    // fixed set), so this path is not expected to run; still not left
    // unhandled.
    std::string title = operation;
    if (!title.empty()) title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
    return title;
}

}  // namespace

std::vector<std::string> format_process_errors(const ProcessFlowDocument& flow,
                                               const std::vector<ValidationErrorOut>& errors) {
    std::vector<std::string> out;
    out.reserve(errors.size());
    for (const auto& e : errors) {
        int found_index = -1;
        ProcessStepData found_step;
        if (!e.object_id.empty()) {
            for (std::size_t i = 0; i < flow.step_count(); ++i) {
                ProcessStepData s = flow.step(i);
                if (s.id == e.object_id) {
                    found_index = static_cast<int>(i);
                    found_step = s;
                    break;
                }
            }
        }
        if (found_index >= 0) {
            out.push_back("Step " + [](int n) {
                std::string s = std::to_string(n);
                return s.size() < 2 ? std::string(2 - s.size(), '0') + s : s;
            }(found_index + 1) + " \xE2\x80\x94 " + operation_label(found_step.operation) + ": " +
                          e.message);
        } else {
            out.push_back(e.message);
        }
    }
    return out;
}

}  // namespace tcad::desktop
