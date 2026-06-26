#pragma once

#include "AbstractReclamationTaskPlugin.h"

namespace asst
{
class RelaunchAnchorDeployTaskPlugin : public AbstractReclamationTaskPlugin
{
public:
    using AbstractReclamationTaskPlugin::AbstractReclamationTaskPlugin;
    virtual ~RelaunchAnchorDeployTaskPlugin() override = default;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual bool load_params(const json::value& params) override;

protected:
    virtual bool _run() override;

private:
    mutable std::string m_pending_task_name;
};
} // namespace asst
