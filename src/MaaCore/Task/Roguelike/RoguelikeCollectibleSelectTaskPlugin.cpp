#include "RoguelikeCollectibleSelectTaskPlugin.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "Controller/Controller.h"
#include "Task/ProcessTask.h"
#include "Utils/DebugImageHelper.hpp"
#include "Utils/Logger.hpp"
#include "Vision/MultiMatcher.h"
#include "Vision/OCRer.h"

bool asst::RoguelikeCollectibleSelectTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    const std::string task = details.get("details", "task", "");
    // 实测多选链路：GetDropBoxOpen →（界面出现）→ GetDropSelectReward（「获得」标题误点）
    // GetDropSelect 有时分不够高，进不来；必须挂到真实走过的任务上，且不改任务链/action
    if (task.ends_with("Roguelike@GetDropBoxOpen")) {
        m_trigger = Trigger::BoxOpen;
        return true;
    }
    if (task.ends_with("Roguelike@GetDropSelect")) {
        m_trigger = Trigger::Select;
        return true;
    }
    if (task.ends_with("Roguelike@GetDropSelectReward")) {
        m_trigger = Trigger::SelectReward;
        return true;
    }
    return false;
}

bool asst::RoguelikeCollectibleSelectTaskPlugin::load_params(const json::value& params)
{
    m_shopping_list.clear();
    m_select_snapshot_index = 0;

    if (m_config->get_mode() != RoguelikeMode::CollectibleFarm) {
        return false;
    }

    m_shopping_list = RoguelikeConfig::parse_refresh_trader_shopping_list(params);
    if (m_shopping_list.empty()) {
        return false;
    }

    Log.info(__FUNCTION__, "CollectibleSelect enabled, shopping_list:", m_shopping_list);
    return true;
}

void asst::RoguelikeCollectibleSelectTaskPlugin::reset_in_run_variables()
{
    m_select_snapshot_index = 0;
}

bool asst::RoguelikeCollectibleSelectTaskPlugin::_run()
{
    LogTraceFunction;

    ++m_select_snapshot_index;
    const char* trigger_tag = "select";
    switch (m_trigger) {
    case Trigger::BoxOpen:
        trigger_tag = "box_open";
        break;
    case Trigger::SelectReward:
        trigger_tag = "select_reward";
        break;
    case Trigger::Select:
        trigger_tag = "select";
        break;
    }
    save_select_snapshot(std::format("{}_{}", trigger_tag, m_select_snapshot_index));

    // 列表点开入口：只截图，点击仍由原 GetDropBoxOpen 执行
    if (m_trigger == Trigger::BoxOpen) {
        Log.info(__FUNCTION__, "GetDropBoxOpen: snapshot only");
        return true;
    }

    // SelectReward：若画面上没有「选择」按钮，是真·获得确认，不改坐标
    if (m_trigger == Trigger::SelectReward && !has_select_buttons()) {
        Log.info(__FUNCTION__, "GetDropSelectReward without select buttons, keep original 获得 click");
        return true;
    }

    const auto preferred = find_preferred_select_rect();
    if (!preferred) {
        Log.info(__FUNCTION__, "no shopping_list match, keep original click");
        return true;
    }

    auto* process = dynamic_cast<ProcessTask*>(m_task_ptr);
    if (process == nullptr) {
        Log.warn(__FUNCTION__, "task_ptr is not ProcessTask, cannot rewrite click");
        return true;
    }

    const auto& last_hit = process->get_last_hit();
    if (!last_hit) {
        Log.warn(__FUNCTION__, "no last hit, cannot rewrite click");
        return true;
    }

    Log.info(__FUNCTION__, "rewrite click", last_hit->rect.to_string(), "->", preferred->to_string());
    last_hit->rect = *preferred;

    auto info = basic_info_with_what("RoguelikeCollectibleSelected");
    info["details"]["rect"] = preferred->to_string();
    info["details"]["trigger"] = trigger_tag;
    callback(AsstMsg::SubTaskExtraInfo, info);
    return true;
}

void asst::RoguelikeCollectibleSelectTaskPlugin::save_select_snapshot(std::string_view tag)
{
    const auto image = ctrler()->get_image();
    const bool ok = utils::save_debug_image(
        image,
        utils::path("debug") / "roguelike" / "collectibleSelect",
        false,
        "collectible farm drop select",
        tag);
    if (!ok) {
        Log.warn(__FUNCTION__, "failed to save select snapshot", tag);
    }
    else {
        Log.info(__FUNCTION__, "saved", tag);
    }
}

bool asst::RoguelikeCollectibleSelectTaskPlugin::has_select_buttons()
{
    auto image = ctrler()->get_image();
    const std::string& theme = m_config->get_theme();

    MultiMatcher buttons(image);
    buttons.set_task_info(theme + "@Roguelike@GetDropSelect");
    if (buttons.analyze()) {
        return true;
    }
    // 水月多选「选择」有时更接近密匣打开模板
    buttons.set_task_info(theme + "@Roguelike@GetDropBoxOpen");
    return buttons.analyze().has_value();
}

std::optional<asst::Rect> asst::RoguelikeCollectibleSelectTaskPlugin::find_preferred_select_rect()
{
    auto image = ctrler()->get_image();
    const std::string& theme = m_config->get_theme();

    MultiMatcher buttons(image);
    buttons.set_task_info(theme + "@Roguelike@GetDropSelect");
    if (!buttons.analyze()) {
        buttons.set_task_info(theme + "@Roguelike@GetDropBoxOpen");
        if (!buttons.analyze()) {
            Log.warn(__FUNCTION__, "no select buttons for OCR align");
            return std::nullopt;
        }
    }

    auto button_results = buttons.get_result();
    std::ranges::sort(button_results, [](const auto& a, const auto& b) { return a.rect.x < b.rect.x; });

    OCRer analyzer(image);
    analyzer.set_task_info("Roguelike@GetDropSelectCollectibleOcr");
    if (!analyzer.analyze()) {
        Log.warn(__FUNCTION__, "OCR found no collectible names");
        return std::nullopt;
    }

    const auto& ocr_results = analyzer.get_result();
    for (const auto& target : m_shopping_list) {
        if (need_exit()) {
            return std::nullopt;
        }

        auto ocr_it = std::ranges::find_if(ocr_results, [&](const OCRer::Result& tr) {
            return tr.text.find(target) != std::string::npos || target.find(tr.text) != std::string::npos;
        });
        if (ocr_it == ocr_results.cend()) {
            continue;
        }

        const int name_cx = ocr_it->rect.x + ocr_it->rect.width / 2;
        auto btn_it = std::ranges::min_element(button_results, [&](const auto& a, const auto& b) {
            const int ca = a.rect.x + a.rect.width / 2;
            const int cb = b.rect.x + b.rect.width / 2;
            return std::abs(ca - name_cx) < std::abs(cb - name_cx);
        });
        if (btn_it == button_results.end()) {
            continue;
        }

        Log.info(__FUNCTION__, "prefer", target, "ocr:", ocr_it->text, "button", btn_it->rect.to_string());
        return btn_it->rect;
    }

    return std::nullopt;
}
