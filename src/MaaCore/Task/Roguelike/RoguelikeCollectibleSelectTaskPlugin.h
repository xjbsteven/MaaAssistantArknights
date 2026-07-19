#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "AbstractRoguelikeTaskPlugin.h"

namespace asst
{
// 刷藏品：在原 GetDropSelect（ClickSelf）触发时，先截图；若 OCR 命中列表则改写本次点击目标。
// 不改任务 action / next，无匹配则完全沿用原点击。
class RoguelikeCollectibleSelectTaskPlugin : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeCollectibleSelectTaskPlugin() override = default;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual bool load_params(const json::value& params) override;

protected:
    virtual bool _run() override;

private:
    /// 纯附加：选择前截图，不影响后续点击
    void save_select_snapshot(std::string_view tag);
    /// 按 shopping_list 找应对应点击的「选择」按钮；无匹配返回 nullopt
    std::optional<Rect> find_preferred_select_rect();

    std::vector<std::string> m_shopping_list;
    int m_select_snapshot_index = 0;
};
}
