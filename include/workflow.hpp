#pragma once

#include "config.hpp"

#include <string>

namespace qp {

// Full pipeline:
//   editor → select-all → copy → activate browser → new tab → open AI URL → paste prompt+text
class AiWorkflow {
public:
    explicit AiWorkflow(const WorkflowConfig& cfg);

    // promptBody = template text (instructions). Editor content is captured live.
    bool Run(const std::wstring& promptBody,
             const std::wstring& aiUrlOverride, // empty => cfg default
             std::wstring* error = nullptr);

    void SetConfig(const WorkflowConfig& cfg) { cfg_ = cfg; }

    // Build final message for the AI chat. Exposed for self-test.
    static std::wstring ComposePayload(const std::wstring& promptBody,
                                       const std::wstring& editorText,
                                       bool fenceEditorText);

private:
    WorkflowConfig cfg_;
};

} // namespace qp
