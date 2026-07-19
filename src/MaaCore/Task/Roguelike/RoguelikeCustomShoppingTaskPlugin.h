#pragma once

#include <string>
#include <vector>

#include "AbstractRoguelikeTaskPlugin.h"

namespace asst
{
// 自定义藏品刷取：进店后先按用户列表 OCR→购买→刷新，刷新用尽后再交给投资/常规购物。
// 当前水月骰子刷新已完整实现；萨米/萨卡兹免费刷新路径已预留，便于后续扩展。
class RoguelikeCustomShoppingTaskPlugin : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeCustomShoppingTaskPlugin() override = default;

    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual bool load_params(const json::value& params) override;
    virtual void reset_in_run_variables() override;

protected:
    virtual bool _run() override;

private:
    enum class RefreshResult
    {
        Success,     // 刷新成功，货架已更新
        Exhausted,   // 本商店刷新次数已用完
        Unavailable, // 当前主题/界面无可用刷新
        Failed,      // 识别或点击失败
    };

    enum class BuyResult
    {
        Bought,
        CannotAfford, // 已识别到目标但买不起；会暂停任务等用户手动处理
        Failed,
    };

    // 识别当前货架上仍可购买的商品（带价签）
    std::vector<TextRect> recognize_goods();
    // 按优先级尝试购买一件；成功则返回 Bought
    BuyResult try_buy_once(const std::vector<TextRect>& goods);
    BuyResult confirm_or_cancel_purchase(const std::string& goods_name);
    // 主题相关的刷新；水月走骰子，萨米/萨卡兹走免费刷新（预留）
    RefreshResult try_refresh();
    RefreshResult refresh_with_dice();
    RefreshResult refresh_free();

    bool click_matched_task(const std::string& task_name, int retry = 3);
    bool wait_and_click_matched_task(const std::string& task_name, int retry = 8);
    bool ocr_matched_task(const std::string& task_name);
    /// 是否已回到商店主界面（离店/刷新/货架可见），用于判断骰子刷新弹窗已关闭
    bool is_trader_main_ui();

    void notify_goods_bought(const std::string& goods_name);
    // 钱不够时暂停任务，停留在商店供用户手动操作（不放弃本局）
    void stop_for_insufficient_funds(const std::string& goods_name);
    /// 保存商店界面截图到 debug/roguelike/collectibleFarm，便于核对漏识别
    void save_trader_snapshot(std::string_view tag);
    /// 离店并放弃本局（不依赖 LeaveConfirm 的 fragile next，避免 TaskChainError）
    void leave_trader_and_abandon();

    std::vector<std::string> m_shopping_list;
    // 本店是否还允许尝试刷新（用尽或不可用后置 false）
    bool m_refresh_enabled = true;
    int m_refresh_snapshot_index = 0;
};
}
