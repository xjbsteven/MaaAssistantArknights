#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "AbstractRoguelikeTaskPlugin.h"

namespace asst
{
// 刷藏品：战后几选一按 refresh_trader_shopping_list 优先点选；无匹配则点最高分「选择」
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
    bool select_by_shopping_list();
    bool click_best_select_button();
    /// 选择前截图到 debug/roguelike/collectibleSelect，便于低出现率验收
    void save_select_snapshot(std::string_view tag);

    std::vector<std::string> m_shopping_list;
    int m_select_snapshot_index = 0;
};
}
