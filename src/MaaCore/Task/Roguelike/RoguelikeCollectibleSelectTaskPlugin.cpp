#include "RoguelikeCollectibleSelectTaskPlugin.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "Controller/Controller.h"
#include "Utils/DebugImageHelper.hpp"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/MultiMatcher.h"
#include "Vision/OCRer.h"

bool asst::RoguelikeCollectibleSelectTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    const std::string task = details.get("details", "task", "");
    return task.ends_with("Roguelike@GetDropSelect");
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

    if (select_by_shopping_list()) {
        return true;
    }

    Log.info(__FUNCTION__, "no shopping_list match, fallback to best select button");
    return click_best_select_button();
}

void asst::RoguelikeCollectibleSelectTaskPlugin::save_select_snapshot(std::string_view tag)
{
    const auto image = ctrler()->get_image();
    // 与商店截图路径平行：debug/roguelike/collectibleSelect；不自动清理
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

bool asst::RoguelikeCollectibleSelectTaskPlugin::select_by_shopping_list()
{
    auto image = ctrler()->get_image();
    const std::string& theme = m_config->get_theme();

    MultiMatcher buttons(image);
    buttons.set_task_info(theme + "@Roguelike@GetDropSelect");
    if (!buttons.analyze()) {
        Log.warn(__FUNCTION__, "no select buttons found");
        return false;
    }

    auto button_results = buttons.get_result();
    std::ranges::sort(button_results, [](const auto& a, const auto& b) { return a.rect.x < b.rect.x; });

    OCRer analyzer(image);
    analyzer.set_task_info("Roguelike@GetDropSelectCollectibleOcr");
    if (!analyzer.analyze()) {
        Log.warn(__FUNCTION__, "OCR found no collectible names");
        return false;
    }

    const auto& ocr_results = analyzer.get_result();
    for (const auto& target : m_shopping_list) {
        if (need_exit()) {
            return false;
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

        Log.info(__FUNCTION__, "prefer", target, "ocr:", ocr_it->text, "click", btn_it->rect.to_string());
        ctrler()->click(btn_it->rect);

        auto info = basic_info_with_what("RoguelikeCollectibleSelected");
        info["details"]["item"] = target;
        info["details"]["ocr"] = ocr_it->text;
        callback(AsstMsg::SubTaskExtraInfo, info);
        return true;
    }

    return false;
}

bool asst::RoguelikeCollectibleSelectTaskPlugin::click_best_select_button()
{
    auto image = ctrler()->get_image();
    const std::string& theme = m_config->get_theme();

    MultiMatcher buttons(image);
    buttons.set_task_info(theme + "@Roguelike@GetDropSelect");
    if (!buttons.analyze()) {
        // 退回 ProcessTask 已命中的「选择」；DoNothing 时用 last hit
        if (auto hit = get_hit_detail<Matcher::Result>()) {
            Log.info(__FUNCTION__, "click last hit", hit->rect.to_string());
            ctrler()->click(hit->rect);
            return true;
        }
        Log.warn(__FUNCTION__, "no select button to click");
        return false;
    }

    auto button_results = buttons.get_result();
    auto best = std::ranges::max_element(button_results, [](const auto& a, const auto& b) {
        return a.score < b.score;
    });
    Log.info(__FUNCTION__, "click best score", best->score, best->rect.to_string());
    ctrler()->click(best->rect);
    return true;
}
