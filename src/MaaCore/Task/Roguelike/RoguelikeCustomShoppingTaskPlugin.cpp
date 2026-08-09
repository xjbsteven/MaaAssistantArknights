#include "RoguelikeCustomShoppingTaskPlugin.h"

#include <algorithm>
#include <format>

#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Task/Roguelike/RoguelikeTraderGoodsHelper.h"
#include "Utils/DebugImageHelper.hpp"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/OCRer.h"

bool asst::RoguelikeCustomShoppingTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    return details.get("details", "task", "").ends_with("Roguelike@StageTraderCustomShopping");
}

bool asst::RoguelikeCustomShoppingTaskPlugin::load_params(const json::value& params)
{
    m_shopping_list.clear();

    // 仅「刷藏品」策略启用
    if (m_config->get_mode() != RoguelikeMode::CollectibleFarm) {
        return false;
    }

    m_shopping_list = RoguelikeConfig::parse_refresh_trader_shopping_list(params);
    if (m_shopping_list.empty()) {
        return false;
    }

    Log.info(__FUNCTION__, "CollectibleFarm enabled, shopping_list:", m_shopping_list);
    return true;
}

void asst::RoguelikeCustomShoppingTaskPlugin::reset_in_run_variables()
{
    m_refresh_enabled = true;
    m_refresh_snapshot_index = 0;
}

bool asst::RoguelikeCustomShoppingTaskPlugin::_run()
{
    LogTraceFunction;
    Log.info(__FUNCTION__, "start custom shopping, list:", m_shopping_list);

    // 防止同一次进店被多次触发时重复刷新；每次进入商店重新允许刷新
    m_refresh_enabled = true;
    m_refresh_snapshot_index = 0;

    constexpr int kMaxLoops = 30;
    bool saved_enter_snapshot = false;
    for (int loop = 0; loop < kMaxLoops && !need_exit(); ++loop) {
        const auto goods = recognize_goods();
        Log.info(__FUNCTION__, "loop", loop, "goods_on_shelf:", goods.size());

        // 进店首次货架：刷新前基准图，便于事后对照是否漏 OCR
        if (!saved_enter_snapshot) {
            save_trader_snapshot("enter");
            saved_enter_snapshot = true;
        }

        const BuyResult buy_ret = try_buy_once(goods);
        if (buy_ret == BuyResult::Bought) {
            sleep(800);
            continue;
        }
        if (buy_ret == BuyResult::CannotAfford) {
            // 已暂停任务，等待用户手动处理
            return true;
        }

        // 本架已无可买目标（找不到）→ 尝试刷新
        if (!m_refresh_enabled) {
            Log.info(__FUNCTION__, "no target on shelf and refresh disabled, done");
            break;
        }

        ++m_refresh_snapshot_index;
        save_trader_snapshot(std::format("before_refresh_{}", m_refresh_snapshot_index));

        const RefreshResult refresh_ret = try_refresh();
        switch (refresh_ret) {
        case RefreshResult::Success:
            Log.info(__FUNCTION__, "refresh success, re-OCR");
            sleep(1000);
            save_trader_snapshot(std::format("after_refresh_{}", m_refresh_snapshot_index));
            continue;
        case RefreshResult::Exhausted:
            Log.info(__FUNCTION__, "refresh exhausted, hand over to invest/shopping");
            m_refresh_enabled = false;
            break;
        case RefreshResult::Unavailable:
            Log.info(__FUNCTION__, "refresh unavailable for theme", m_config->get_theme());
            m_refresh_enabled = false;
            break;
        case RefreshResult::Failed:
            Log.warn(__FUNCTION__, "refresh failed, stop custom shopping loop");
            m_refresh_enabled = false;
            break;
        }
        break;
    }

    Log.info(__FUNCTION__, "custom shopping finished, next=StageTraderLeave");
    return true;
}

std::vector<asst::TextRect> asst::RoguelikeCustomShoppingTaskPlugin::recognize_goods()
{
    return RoguelikeTraderGoodsHelper::recognize_goods(ctrler()->get_image());
}

asst::RoguelikeCustomShoppingTaskPlugin::BuyResult asst::RoguelikeCustomShoppingTaskPlugin::try_buy_once(
    const std::vector<TextRect>& goods)
{
    for (const auto& target : m_shopping_list) {
        if (need_exit()) {
            return BuyResult::Failed;
        }

        auto find_it = std::ranges::find_if(goods, [&](const TextRect& tr) {
            return tr.text.find(target) != std::string::npos || target.find(tr.text) != std::string::npos;
        });
        if (find_it == goods.cend()) {
            continue;
        }

        Log.info(__FUNCTION__, "ready to buy", target, "ocr:", find_it->text);
        ctrler()->click(find_it->rect);
        sleep(500);

        const BuyResult confirm_ret = confirm_or_cancel_purchase(target);
        if (confirm_ret == BuyResult::Bought) {
            m_config->status().collections.emplace_back(target);
            notify_goods_bought(target);
            Log.info(__FUNCTION__, "bought", target);
            return BuyResult::Bought;
        }
        if (confirm_ret == BuyResult::CannotAfford) {
            // 刷到目标但钱不够：暂停等用户手动买，不再试列表后续或刷新
            stop_for_insufficient_funds(target);
            return BuyResult::CannotAfford;
        }
        Log.warn(__FUNCTION__, "buy failed for", target);
    }
    return BuyResult::Failed;
}

asst::RoguelikeCustomShoppingTaskPlugin::BuyResult
    asst::RoguelikeCustomShoppingTaskPlugin::confirm_or_cancel_purchase(const std::string& goods_name)
{
    // 不走 ProcessTask 的 next 链，避免 Confirm 成功后误入离店/常规购物
    for (int i = 0; i < 8 && !need_exit(); ++i) {
        auto image = ctrler()->get_image();

        Matcher confirm(image);
        confirm.set_task_info("Roguelike@TraderRandomShoppingConfirm");
        if (auto conf = confirm.analyze()) {
            ctrler()->click(conf->rect);
            sleep(Task.get("Roguelike@TraderRandomShoppingConfirm")->post_delay);
            Log.info(__FUNCTION__, "confirmed purchase:", goods_name);
            return BuyResult::Bought;
        }

        Matcher cancel(image);
        cancel.set_task_info("Roguelike@TraderRandomShoppingCancel");
        if (cancel.analyze()) {
            // 确认键未出现而取消键在，通常为钱不够。
            // 不点取消，保留购买界面，便于用户手动处理。
            Log.info(__FUNCTION__, "cannot afford (confirm missing, cancel present):", goods_name);
            return BuyResult::CannotAfford;
        }

        sleep(200);
    }

    // 兜底：尝试点取消，避免卡在商品详情
    click_matched_task("Roguelike@TraderRandomShoppingCancel", 2);
    return BuyResult::Failed;
}

void asst::RoguelikeCustomShoppingTaskPlugin::notify_goods_bought(const std::string& goods_name)
{
    auto info = basic_info_with_what("RoguelikeCustomGoodsBought");
    info["details"]["item"] = goods_name;
    callback(AsstMsg::SubTaskExtraInfo, info);
}

void asst::RoguelikeCustomShoppingTaskPlugin::stop_for_insufficient_funds(const std::string& goods_name)
{
    auto info = basic_info_with_what("RoguelikeCustomGoodsCannotAfford");
    info["details"]["item"] = goods_name;
    callback(AsstMsg::SubTaskExtraInfo, info);
    Log.warn(__FUNCTION__, "insufficient funds for", goods_name, ", pause for manual operation");
    // 仅停任务，不放弃本局，界面停留在商店购买弹窗
    m_task_ptr->set_enable(false);
}

void asst::RoguelikeCustomShoppingTaskPlugin::save_trader_snapshot(std::string_view tag)
{
    const auto image = ctrler()->get_image();
    // 不做自动清理：低掉率排查需要留一整天的图，由用户自行删目录
    const bool ok = utils::save_debug_image(
        image,
        utils::path("debug") / "roguelike" / "collectibleFarm",
        false,
        "collectible farm trader",
        tag);
    if (!ok) {
        Log.warn(__FUNCTION__, "failed to save trader snapshot", tag);
    }
}

void asst::RoguelikeCustomShoppingTaskPlugin::leave_trader_and_abandon()
{
    const std::string& theme = m_config->get_theme();
    const std::string stages_task = theme + "@Roguelike@Stages";
    const std::string restart_task = theme + "@Roguelike@Stages_restart";

    // 地图侧兜底：若仍回到选点，也只走放弃（含骰子确认）
    if (Task.get(restart_task) != nullptr) {
        Task.set_task_base(stages_task, restart_task);
    }
    else {
        Task.set_task_base(stages_task, "Roguelike@Stages_restart");
    }

    // 分步离店：不要把希望寄托在 LeaveConfirm.next 上（离店后常先出骰子结果，Exit 被挡住）
    click_matched_task(theme + "@Roguelike@StageTraderLeave", 3);
    sleep(600);
    click_matched_task(theme + "@Roguelike@StageTraderLeaveConfirm", 3);
    sleep(1500);

    // 骰子结果 → 退出 → 放弃；提高重试，避免中途 TaskChainError
    const bool ok = ProcessTask(
                        *this,
                        {
                            theme + "@Roguelike@DiceConfirmThenAbandon",
                            theme + "@Roguelike@ExitThenAbandon",
                            theme + "@Roguelike@Abandon",
                            theme + "@Roguelike@AbandonConfirm",
                            theme + "@Roguelike@MissionFailedFlag2",
                            theme + "@Roguelike@StartExplore",
                            // 仍停在离店确认时再点一次
                            theme + "@Roguelike@StageTraderLeaveConfirm",
                            theme + "@Roguelike@StageTraderLeave",
                        })
                        .set_retry_times(20)
                        .run();
    Log.info(__FUNCTION__, "leave/abandon process", ok ? "ok" : "failed");
}

asst::RoguelikeCustomShoppingTaskPlugin::RefreshResult asst::RoguelikeCustomShoppingTaskPlugin::try_refresh()
{
    const auto& theme = m_config->get_theme();
    if (theme == RoguelikeTheme::Mizuki) {
        return refresh_with_dice();
    }
    if (theme == RoguelikeTheme::Sami || theme == RoguelikeTheme::Sarkaz) {
        // 预留：与现有 RoguelikeShoppingTaskPlugin 一致的免费刷新
        return refresh_free();
    }

    Log.info(__FUNCTION__, "no refresh implementation for theme", theme);
    return RefreshResult::Unavailable;
}

bool asst::RoguelikeCustomShoppingTaskPlugin::is_trader_main_ui()
{
    // 离开骰子确认/动画后，商店主界面应能看到离店或刷新按钮
    auto image = ctrler()->get_image();
    {
        Matcher leave(image);
        leave.set_task_info("Roguelike@StageTraderLeave");
        if (auto result = leave.analyze(); result && result->score >= 0.7) {
            return true;
        }
    }
    {
        Matcher refresh(image);
        refresh.set_task_info("Roguelike@StageTraderRefreshWithDice");
        if (auto result = refresh.analyze(); result && result->score >= 0.85) {
            return true;
        }
    }
    return !recognize_goods().empty();
}

asst::RoguelikeCustomShoppingTaskPlugin::RefreshResult asst::RoguelikeCustomShoppingTaskPlugin::refresh_with_dice()
{
    Log.info(__FUNCTION__, "try Mizuki dice refresh");

    // 已用完提示可能在点击前就存在（例如按钮灰了仍可点）
    if (ocr_matched_task("Roguelike@StageTraderRefreshWithDiceNotAvaiable")) {
        return RefreshResult::Exhausted;
    }

    if (!click_matched_task("Roguelike@StageTraderRefreshWithDice", 3)) {
        // 无刷新按钮：视为本店不可再刷
        if (ocr_matched_task("Roguelike@StageTraderRefreshWithDiceNotAvaiable")) {
            return RefreshResult::Exhausted;
        }
        Log.warn(__FUNCTION__, "refresh button not found");
        return RefreshResult::Unavailable;
    }

    sleep(800);

    const auto& dbl = Task.get("Roguelike@StageTraderRefreshWithDiceDoubleConfirm");
    bool clicked_confirm = false;

    // 官方链是 Confirm（可多次）→ DoubleConfirm；这里循环直到回到商店主界面，
    // 避免点完确认就当成功、实际仍停在骰子弹窗上。
    for (int i = 0; i < 25 && !need_exit(); ++i) {
        if (ocr_matched_task("Roguelike@StageTraderRefreshWithDiceNotAvaiable")) {
            return RefreshResult::Exhausted;
        }

        if (is_trader_main_ui()) {
            Log.info(__FUNCTION__, "back to trader UI after dice refresh, attempts", i);
            return RefreshResult::Success;
        }

        auto image = ctrler()->get_image();
        Matcher confirm(image);
        confirm.set_task_info("Roguelike@StageTraderRefreshWithDiceConfirm");
        if (auto result = confirm.analyze()) {
            ctrler()->click(result->rect);
            clicked_confirm = true;
            sleep(600);
            ctrler()->click(dbl->specific_rect);
            sleep(std::max(600, dbl->post_delay));
            continue;
        }

        // 确认键暂时看不见：可能在骰子动画中，补点二次确认坐标
        if (clicked_confirm) {
            ctrler()->click(dbl->specific_rect);
            sleep(600);
        }
        sleep(400);
    }

    Log.warn(__FUNCTION__, "dice refresh did not return to trader UI");
    return RefreshResult::Failed;
}

asst::RoguelikeCustomShoppingTaskPlugin::RefreshResult asst::RoguelikeCustomShoppingTaskPlugin::refresh_free()
{
    const auto& theme = m_config->get_theme();
    Log.info(__FUNCTION__, "try free refresh for", theme);

    if (!ProcessTask(*this, { theme + "@Roguelike@StageTraderRefresh" }).set_retry_times(3).run()) {
        Log.info(__FUNCTION__, "free refresh button not found");
        return RefreshResult::Unavailable;
    }

    const bool confirmed =
        ProcessTask(*this, { "Roguelike@StageTraderRefreshConfirm" }).set_retry_times(RetryTimesDefault).run();
    if (!confirmed) {
        Log.warn(__FUNCTION__, "free refresh confirm failed");
        return RefreshResult::Failed;
    }

    sleep(1000);
    return RefreshResult::Success;
}

bool asst::RoguelikeCustomShoppingTaskPlugin::click_matched_task(const std::string& task_name, int retry)
{
    for (int i = 0; i < retry && !need_exit(); ++i) {
        auto image = ctrler()->get_image();
        Matcher matcher(image);
        matcher.set_task_info(task_name);
        if (auto result = matcher.analyze()) {
            ctrler()->click(result->rect);
            if (auto task = Task.get(task_name)) {
                sleep(task->post_delay);
            }
            return true;
        }
        sleep(200);
    }
    return false;
}

bool asst::RoguelikeCustomShoppingTaskPlugin::wait_and_click_matched_task(const std::string& task_name, int retry)
{
    return click_matched_task(task_name, retry);
}

bool asst::RoguelikeCustomShoppingTaskPlugin::ocr_matched_task(const std::string& task_name)
{
    auto image = ctrler()->get_image();
    OCRer ocr(image);
    ocr.set_task_info(task_name);
    return ocr.analyze().has_value();
}
