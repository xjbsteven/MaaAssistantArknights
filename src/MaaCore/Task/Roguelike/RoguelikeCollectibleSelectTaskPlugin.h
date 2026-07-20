#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "AbstractRoguelikeTaskPlugin.h"

namespace asst
{
// 刷藏品战后几选一：
// - 截图：纯附加，挂在实际会走到的 GetDropBoxOpen / GetDropSelect / GetDropSelectReward
// - OCR：仅在即将 ClickSelf 时，若画面上有「选择」按钮且命中列表，则改写点击坐标；否则不改
class RoguelikeCollectibleSelectTaskPlugin : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeCollectibleSelectTaskPlugin() override = default;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual bool load_params(const json::value& params) override;
    virtual void reset_in_run_variables() override;

protected:
    virtual bool _run() override;

private:
    enum class Trigger
    {
        BoxOpen,      // 战利品列表点开藏品入口：只截图
        Select,       // 标准「选择」
        SelectReward, // 实际多选时常被「获得」标题误命中：截图 + 尝试改写到「选择」
    };

    void save_select_snapshot(std::string_view tag);
    std::optional<Rect> find_preferred_select_rect();
    /// 画面上是否存在可点的「选择」按钮（多选界面）；用于区分真·获得确认
    bool has_select_buttons();

    std::vector<std::string> m_shopping_list;
    int m_select_snapshot_index = 0;
    mutable Trigger m_trigger = Trigger::Select;
};
}
