#pragma once

#include "config.hpp"

#include <string>

namespace qp {

struct WorkflowRequest {
    std::wstring promptBody;
    std::wstring aiUrl;          // empty => cfg.defaultAiUrl
    std::wstring pageTitleHint;  // empty => derive from URL / cfg
    bool captureEditor = true;
    bool requireClipboardImage = false;
    bool fenceEditorText = true;
    std::wstring service;        // for logs
    std::wstring label;
};

class AiWorkflow {
public:
    explicit AiWorkflow(const WorkflowConfig& cfg);

    bool Run(const WorkflowRequest& req, std::wstring* error = nullptr);

    // Back-compat wrapper
    bool Run(const std::wstring& promptBody,
             const std::wstring& aiUrlOverride,
             std::wstring* error = nullptr);

    void SetConfig(const WorkflowConfig& cfg) { cfg_ = cfg; }

    static std::wstring ComposePayload(const std::wstring& promptBody,
                                       const std::wstring& editorText,
                                       bool fenceEditorText);

private:
    WorkflowConfig cfg_;
};

} // namespace qp
