// P4 S8 (NATIVE-DESKTOP-PLAN.md section 20.4): ValidationPanel + the
// structure/process error-message formatting it displays.
#include <QtTest/QtTest>
#include <nlohmann/json.hpp>

#include "document/process_document.hpp"
#include "document/validation.hpp"
#include "shell/validation_panel.hpp"

using tcad::desktop::format_process_errors;
using tcad::desktop::parse_validation_errors;
using tcad::desktop::ProcessFlowDocument;
using tcad::desktop::ProcessStepData;
using tcad::desktop::structure_error_messages;
using tcad::desktop::ValidationErrorOut;
using tcad::desktop::ValidationPanel;

class TestValidation : public QObject {
    Q_OBJECT

private slots:
    void parsesTheRpcResultShape() {
        const auto errors = parse_validation_errors(nlohmann::json::parse(
            R"([{"message": "width_cm must be positive", "object_id": null},)"
            R"( {"message": "step needs a window", "object_id": "s1"}])"));
        QCOMPARE(errors.size(), std::size_t(2));
        QCOMPARE(errors[0].message, std::string("width_cm must be positive"));
        QVERIFY(errors[0].object_id.empty());
        QCOMPARE(errors[1].object_id, std::string("s1"));
    }

    void structureErrorsAreJustTheMessages() {
        std::vector<ValidationErrorOut> errors{{"a", ""}, {"b", "x"}};
        const auto msgs = structure_error_messages(errors);
        QCOMPARE(msgs.size(), std::size_t(2));
        QCOMPARE(msgs[0], std::string("a"));
        QCOMPARE(msgs[1], std::string("b"));  // object_id is irrelevant here
    }

    void processErrorsResolveTheStepLabelAndOneBasedIndex() {
        auto flow = ProcessFlowDocument::parse(R"({"steps": []})");
        ProcessStepData s0;
        s0.id = "s0";
        s0.operation = "substrate";
        flow.add_step(s0);
        ProcessStepData s1;
        s1.id = "s1";
        s1.operation = "implant";
        flow.add_step(s1);

        std::vector<ValidationErrorOut> errors{
            {"needs a window", "s1"},                  // resolves -> step 2
            {"flow-level: must start with substrate", ""},  // no object_id -> raw
            {"stale reference", "gone"},                // unresolved id -> raw
        };
        const auto formatted = format_process_errors(flow, errors);
        QCOMPARE(formatted.size(), std::size_t(3));
        QCOMPARE(formatted[0], std::string("Step 02 \xE2\x80\x94 Implant: needs a window"));
        QCOMPARE(formatted[1], std::string("flow-level: must start with substrate"));
        QCOMPARE(formatted[2], std::string("stale reference"));
    }

    void processErrorLabelUsesTheStepsCurrentPosition() {
        // The plan's own point (app_controller.py's comment): the index
        // is resolved from the step's CURRENT position, not wherever it
        // was when the error was generated -- reordering first, then
        // formatting the same error, must show the new position.
        auto flow = ProcessFlowDocument::parse(R"({"steps": []})");
        ProcessStepData s0;
        s0.id = "s0";
        s0.operation = "substrate";
        flow.add_step(s0);
        ProcessStepData s1;
        s1.id = "s1";
        s1.operation = "anneal";
        flow.add_step(s1);
        QVERIFY(flow.move_step("s1", -1));  // s1 is now index 0

        std::vector<ValidationErrorOut> errors{{"too hot", "s1"}};
        const auto formatted = format_process_errors(flow, errors);
        QCOMPARE(formatted[0], std::string("Step 01 \xE2\x80\x94 Anneal: too hot"));
    }

    void panelReflectsOkOrFailedAndTheMessageList() {
        ValidationPanel panel;
        QVERIFY(panel.ok());
        QCOMPARE(panel.statusText(), QStringLiteral("OK"));
        QCOMPARE(panel.errorCount(), std::size_t(0));

        panel.setErrors({"first problem", "second problem"});
        QVERIFY(!panel.ok());
        QCOMPARE(panel.statusText(), QStringLiteral("FAILED"));
        QCOMPARE(panel.errorCount(), std::size_t(2));
        QCOMPARE(panel.errorAt(0), std::string("first problem"));
        QCOMPARE(panel.errorAt(1), std::string("second problem"));

        panel.setErrors({});
        QVERIFY(panel.ok());
        QCOMPARE(panel.errorCount(), std::size_t(0));
    }
};

QTEST_MAIN(TestValidation)
#include "test_validation.moc"
