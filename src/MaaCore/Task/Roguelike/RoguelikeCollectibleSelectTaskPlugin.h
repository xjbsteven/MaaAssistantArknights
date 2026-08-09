#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "AbstractRoguelikeTaskPlugin.h"
#include "Vision/OCRer.h"

namespace asst
{
// 刷藏品战后几选一：
// - 截图：仅在 GetDropSelectReward（二选一「获得」界面）纯附加保存
// - OCR：命中列表则改写 ClickSelf 坐标；否则不改
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
    void save_select_snapshot(std::string_view tag);
    std::optional<Rect> find_preferred_select_rect();
    void log_ocr_results(const std::vector<OCRer::Result>& ocr_results);

    std::vector<std::string> m_shopping_list;
    int m_select_snapshot_index = 0;
};

} // namespace asst
