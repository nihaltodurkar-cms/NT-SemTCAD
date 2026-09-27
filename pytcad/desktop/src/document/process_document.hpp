// gui/services/process_model.py's ProcessFlow.to_dict() shape: a single
// "steps" array of {id, name, operation, enabled, parameters}.
// `parameters` is a free-form dict whose shape depends on `operation`
// (substrate/implant/anneal/oxidize -- ProcessPanel.qml documents the
// four fixed schemas); it is carried as opaque JSON here and never
// interpreted, so a step this build has no editor for still round-trips
// exactly.
//
// duplicate_step() (P4 S5): a fresh `uuid.uuid4().hex[:8]`-shaped id
// (via QUuid, same convention RegionListWidget::addRegion() uses), so
// -- as S1 disclosed when this class was first built -- it is gated
// structurally (a new id, distinct from the original, with every other
// field copied and inserted immediately after), never by exact-output
// equality against a second, independent call.
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace tcad::desktop {

struct ProcessStepData {
    std::string id;
    std::string name;
    std::string operation;   // "substrate" | "implant" | "anneal" | "oxidize"
    bool enabled = true;
    nlohmann::ordered_json parameters = nlohmann::ordered_json::object();
};

class ProcessFlowDocument {
public:
    using Json = nlohmann::ordered_json;

    static ProcessFlowDocument parse(const std::string& text);
    static ProcessFlowDocument load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
    std::string dump(int indent = -1) const;
    const Json& json() const { return doc_; }

    std::size_t step_count() const;
    ProcessStepData step(std::size_t index) const;
    void add_step(const ProcessStepData& s);
    bool remove_step(const std::string& id);
    bool move_step(const std::string& id, int offset);
    bool set_step_enabled(const std::string& id, bool enabled);
    // Replaces the WHOLE parameters object for `id` (matching how the
    // QML editors call `setProcessStepParameters` -- see
    // ImplantEditor.qml etc.: each field's `onEditingFinished` builds a
    // full merged copy of `parameters` client-side, then sends that
    // whole object, never a single-key patch). `id`/`name`/`operation`/
    // `enabled` are untouched.
    bool set_step_parameters(const std::string& id, const Json& parameters);
    // Returns the new step's id ("" if `id` was not found). Inserted
    // immediately after the original, name suffixed " (copy)" -- see
    // the class comment on why this cannot be exact-equality-gated.
    std::string duplicate_step(const std::string& id);

private:
    Json doc_;
};

}  // namespace tcad::desktop
