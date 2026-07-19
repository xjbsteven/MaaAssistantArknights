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

    // 原流程已命中「选择」并将 ClickSelf 时介入；不改 action，只可能改写点击坐标
    return details.get("details", "task", "").ends_with("Roguelike@GetDropSelect");
}

bool asst::RoguelikeCollectibleSelectTaskPlugin::load_params(const json::value& params)
{
    m_shopping_list.clear();
    m_select_snapshot_index = 0;

    if (m_config->get_mode() != RoguelikeMode::CollectibleFarm) {
        return false;
    }

    if (auto opt = params.find<json::array>("refresh_trader_shopping_list"); opt) {
        for (const auto& name : *opt) {
            if (std::string name_str = name.as_string(); !name_str.empty()) {
                m_shopping_list.emplace_back(std::move(name_str));
            }
        }
    }

    if (m_shopping_list.empty()) {
        return false;
    }

    Log.info(__FUNCTION__, "CollectibleSelect enabled, shopping_list:", m_shopping_list);
    return true;
}

bool asst::RoguelikeCollectibleSelectTaskPlugin::_run()
{
    LogTraceFunction;

    ++m_select_snapshot_index;
    save_select_snapshot(std::format("before_{}", m_select_snapshot_index));

    const auto preferred = find_preferred_select_rect();
    if (!preferred) {
        Log.info(__FUNCTION__, "no shopping_list match, keep original GetDropSelect click");
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

    Log.info(
        __FUNCTION__,
        "rewrite GetDropSelect click",
        last_hit->rect.to_string(),
        "->",
        preferred->to_string());
    last_hit->rect = *preferred;

    auto info = basic_info_with_what("RoguelikeCollectibleSelected");
    info["details"]["rect"] = preferred->to_string();
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
}

std::optional<asst::Rect> asst::RoguelikeCollectibleSelectTaskPlugin::find_preferred_select_rect()
{
    auto image = ctrler()->get_image();
    const std::string& theme = m_config->get_theme();

    MultiMatcher buttons(image);
    buttons.set_task_info(theme + "@Roguelike@GetDropSelect");
    if (!buttons.analyze()) {
        Log.warn(__FUNCTION__, "no select buttons for OCR align");
        return std::nullopt;
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
