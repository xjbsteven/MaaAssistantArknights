#include "RelaunchAnchorDeployTaskPlugin.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <unordered_map>
#include <utility>

#include "Common/AsstBattleDef.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Utils/Logger.hpp"
#include "Utils/StringMisc.hpp"
#include "Vision/BestMatcher.h"
#include "Vision/MultiMatcher.h"
#include "Vision/OCRer.h"
#include "Vision/RegionOCRer.h"

namespace
{
const asst::Rect kDeployBarRoi { 0, 570, 1280, 150 };

const std::array<asst::Rect, 4> kFlagRois = { {
    { 35, 588, 1245, 18 },
    { 0, 555, 1280, 90 },
    { 0, 575, 1280, 30 },
    { 0, 570, 1280, 50 },
} };

struct GridScanConfig
{
    int bar_left = 8;
    int bar_top = 598;
    int slot_width = 76;
    int cost_x = 14;
    int cost_y = 6;
    int cost_w = 40;
    int cost_h = 22;
    int click_x = 2;
    int click_y = 22;
    int click_w = 72;
    int click_h = 96;
};

const std::array<GridScanConfig, 3> kGridConfigs = { {
    { 8, 598, 76, 14, 6, 40, 22, 2, 22, 72, 96 },
    { 6, 592, 78, 14, 8, 40, 22, 2, 24, 74, 96 },
    { 10, 604, 74, 14, 4, 40, 22, 2, 20, 70, 96 },
} };

struct DeploySlot
{
    asst::Rect click_rect;
    int cost = -1;
    asst::battle::Role role = asst::battle::Role::Unknown;
};

bool is_gathering_base_task(const std::string& task_name)
{
    return task_name.ends_with("DeployGatheringBaseSwipe");
}

bool is_operator_deploy_task(const std::string& task_name)
{
    return task_name.ends_with("DeployOperatorSwipe") || task_name.ends_with("DeployCX");
}

bool is_supported_deploy_task(const std::string& task_name)
{
    return is_gathering_base_task(task_name) || is_operator_deploy_task(task_name);
}

asst::Rect clamp_to_image(const asst::Rect& rect, const cv::Mat& image)
{
    if (image.empty() || image.cols <= 0 || image.rows <= 0) {
        return rect;
    }

    const asst::Rect main_roi { 0, 0, image.cols, image.rows };
    asst::Rect res = rect;
    if (res.x < 0) {
        res.x = 0;
        res.width = rect.width + rect.x;
    }
    if (res.y < 0) {
        res.y = 0;
        res.height = rect.height + rect.y;
    }
    if (res.x < main_roi.x) {
        res.width = res.width - (main_roi.x - res.x);
        res.x = main_roi.x;
    }
    if (res.y < main_roi.y) {
        res.height = res.height - (main_roi.y - res.y);
        res.y = main_roi.y;
    }
    if (res.x + res.width > main_roi.x + main_roi.width) {
        res.width = main_roi.x + main_roi.width - res.x;
    }
    if (res.y + res.height > main_roi.y + main_roi.height) {
        res.height = main_roi.y + main_roi.height - res.y;
    }
    return res;
}

asst::Rect flag_to_click_rect(const asst::Rect& flag_rect)
{
    return flag_rect.move(asst::Task.get("BattleOperClickRange")->rect_move);
}

asst::battle::Role detect_oper_role(const cv::Mat& image, const asst::Rect& flag_rect)
{
    static const std::unordered_map<std::string, asst::battle::Role> role_map = {
        { "Caster", asst::battle::Role::Caster },
        { "Medic", asst::battle::Role::Medic },
        { "Pioneer", asst::battle::Role::Pioneer },
        { "Sniper", asst::battle::Role::Sniper },
        { "Special", asst::battle::Role::Special },
        { "Support", asst::battle::Role::Support },
        { "Tank", asst::battle::Role::Tank },
        { "Warrior", asst::battle::Role::Warrior },
        { "Drone", asst::battle::Role::Drone },
    };

    static const std::string k_task_name = "BattleOperRole";
    static const std::string k_ext = ".png";

    const asst::Rect role_rect = clamp_to_image(
        flag_rect.move(asst::Task.get("BattleOperRoleRange")->rect_move),
        image);

    asst::BestMatcher role_analyzer(image);
    role_analyzer.set_log_tracing(false);
    role_analyzer.set_task_info(k_task_name);
    role_analyzer.set_roi(role_rect);
    for (const auto& role_name : role_map | std::views::keys) {
        role_analyzer.append_templ(k_task_name + role_name + k_ext);
    }

    const auto role_opt = role_analyzer.analyze();
    if (!role_opt) {
        return asst::battle::Role::Unknown;
    }

    const auto& templ_name = role_opt->templ_info.name;
    const std::string role_name =
        templ_name.substr(k_task_name.size(), templ_name.size() - k_task_name.size() - k_ext.size());
    const auto iter = role_map.find(role_name);
    return iter == role_map.end() ? asst::battle::Role::Unknown : iter->second;
}

int read_cost_at(const cv::Mat& image, const asst::Rect& cost_rect)
{
    const asst::Rect roi = clamp_to_image(cost_rect, image);
    if (roi.width <= 0 || roi.height <= 0) {
        return -1;
    }

    asst::RegionOCRer cost_analyzer(image, roi);
    cost_analyzer.set_replace(asst::Task.get<asst::OcrTaskInfo>("NumberOcrReplace")->replace_map);
    cost_analyzer.set_use_char_model(true);
    cost_analyzer.set_bin_threshold(60, 255);
    if (!cost_analyzer.analyze()) {
        return -1;
    }

    int cost = -1;
    if (!asst::utils::chars_to_number(cost_analyzer.get_result().text, cost)) {
        return -1;
    }
    return cost;
}

asst::Rect cost_text_to_click_rect(const asst::Rect& cost_rect)
{
    const asst::Rect& click_move = asst::Task.get("BattleOperClickRange")->rect_move;
    const asst::Rect& cost_move = asst::Task.get("BattleOperCost")->rect_move;

    asst::Rect flag_rect {
        cost_rect.x - cost_move.x,
        cost_rect.y - cost_move.y,
        cost_rect.width,
        cost_rect.height,
    };
    asst::Rect click_rect = flag_rect.move(click_move);
    if (click_rect.width <= 0) {
        click_rect.width = click_move.width;
    }
    if (click_rect.height <= 0) {
        click_rect.height = click_move.height;
    }
    return click_rect;
}

void dedupe_flags_by_x(std::vector<asst::MatchRect>& flags)
{
    if (flags.empty()) {
        return;
    }

    asst::sort_by_horizontal_(flags);
    std::vector<asst::MatchRect> deduped;
    deduped.reserve(flags.size());
    for (const auto& flag : flags) {
        if (!deduped.empty() && std::abs(flag.rect.x - deduped.back().rect.x) < 40) {
            if (flag.score > deduped.back().score) {
                deduped.back() = flag;
            }
            continue;
        }
        deduped.emplace_back(flag);
    }
    flags = std::move(deduped);
}

std::vector<asst::MatchRect> match_oper_flags(const cv::Mat& image)
{
    std::vector<asst::MatchRect> flags;
    for (const auto& roi : kFlagRois) {
        asst::MultiMatcher matcher(image, roi);
        matcher.set_task_info("BattleOpersFlag");
        matcher.set_threshold(0.45);
        matcher.set_log_tracing(true);
        if (const auto flag_opt = matcher.analyze()) {
            flags.insert(flags.end(), flag_opt->begin(), flag_opt->end());
        }
    }
    dedupe_flags_by_x(flags);
    return flags;
}

std::vector<DeploySlot> slots_from_flags(const cv::Mat& image, const std::vector<asst::MatchRect>& flags)
{
    std::vector<DeploySlot> slots;
    slots.reserve(flags.size());
    for (const auto& flag : flags) {
        DeploySlot slot;
        slot.click_rect = flag_to_click_rect(flag.rect);
        slot.role = detect_oper_role(image, flag.rect);
        slot.cost = read_cost_at(image, flag.rect.move(asst::Task.get("BattleOperCost")->rect_move));
        slots.emplace_back(std::move(slot));
    }
    return slots;
}

std::vector<DeploySlot> slots_from_grid_ocr(const cv::Mat& image, const GridScanConfig& config)
{
    std::vector<DeploySlot> slots;
    for (int slot_x = config.bar_left; slot_x + config.slot_width <= image.cols; slot_x += config.slot_width) {
        const asst::Rect cost_rect {
            slot_x + config.cost_x,
            config.bar_top + config.cost_y,
            config.cost_w,
            config.cost_h,
        };
        const int cost = read_cost_at(image, cost_rect);
        if (cost < 0) {
            continue;
        }

        DeploySlot slot;
        slot.cost = cost;
        slot.click_rect = {
            slot_x + config.click_x,
            config.bar_top + config.click_y,
            config.click_w,
            config.click_h,
        };
        slots.emplace_back(std::move(slot));
    }
    return slots;
}

void enrich_slots_with_roles(const cv::Mat& image, std::vector<DeploySlot>& slots, const std::vector<asst::MatchRect>& flags)
{
    constexpr int k_slot_match_threshold = 40;
    for (auto& slot : slots) {
        const int slot_center_x = slot.click_rect.x + slot.click_rect.width / 2;
        for (const auto& flag : flags) {
            const int flag_center_x = flag.rect.x + flag.rect.width / 2;
            if (std::abs(flag_center_x - slot_center_x) < k_slot_match_threshold) {
                slot.role = detect_oper_role(image, flag.rect);
                break;
            }
        }
    }
}

std::vector<DeploySlot> slots_from_cost_ocr(const cv::Mat& image)
{
    asst::OCRer analyzer(image, kDeployBarRoi);
    analyzer.set_replace(asst::Task.get<asst::OcrTaskInfo>("NumberOcrReplace")->replace_map);
    analyzer.set_use_char_model(true);
    const auto results_opt = analyzer.analyze();
    if (!results_opt) {
        return {};
    }

    std::vector<DeploySlot> slots;
    for (const auto& result : *results_opt) {
        int cost = -1;
        if (!asst::utils::chars_to_number(result.text, cost)) {
            continue;
        }

        DeploySlot slot;
        slot.cost = cost;
        slot.click_rect = cost_text_to_click_rect(result.rect);
        slots.emplace_back(std::move(slot));
    }

    std::ranges::sort(slots, [](const DeploySlot& lhs, const DeploySlot& rhs) {
        return lhs.click_rect.x < rhs.click_rect.x;
    });
    return slots;
}

std::vector<DeploySlot> detect_deploy_slots(const cv::Mat& image)
{
    const auto flags = match_oper_flags(image);

    std::vector<DeploySlot> slots;
    for (const auto& config : kGridConfigs) {
        slots = slots_from_grid_ocr(image, config);
        if (slots.size() >= 2) {
            enrich_slots_with_roles(image, slots, flags);
            Log.info(__FUNCTION__, "| detected", slots.size(), "slots from grid ocr");
            return slots;
        }
    }

    if (!flags.empty()) {
        slots = slots_from_flags(image, flags);
        Log.info(__FUNCTION__, "| detected", slots.size(), "slots from oper flags");
        return slots;
    }

    slots = slots_from_cost_ocr(image);
    if (!slots.empty()) {
        Log.info(__FUNCTION__, "| detected", slots.size(), "slots from cost ocr");
    }
    return slots;
}

int calc_slot_width(const std::vector<DeploySlot>& slots)
{
    if (slots.size() >= 2) {
        return slots[1].click_rect.x - slots[0].click_rect.x;
    }
    if (!slots.empty() && slots.front().click_rect.width > 0) {
        return slots.front().click_rect.width;
    }
    return 80;
}

bool is_operator_slot(const DeploySlot& slot)
{
    return slot.role != asst::battle::Role::Unknown || slot.cost >= 6;
}

std::optional<asst::Rect> find_gathering_base_rect(const std::vector<DeploySlot>& slots)
{
    if (slots.empty()) {
        return std::nullopt;
    }

    const auto& leftmost = slots.front();
    if (!is_operator_slot(leftmost)) {
        return leftmost.click_rect;
    }

    const int slot_width = calc_slot_width(slots);
    asst::Rect base_rect = leftmost.click_rect;
    base_rect.x = std::max(0, base_rect.x - slot_width);
    return base_rect;
}

std::optional<asst::Rect> find_rightmost_oper_rect(const std::vector<DeploySlot>& slots)
{
    std::optional<asst::Rect> best;
    for (const auto& slot : slots) {
        if (!is_operator_slot(slot)) {
            continue;
        }
        if (!best || slot.click_rect.x > best->x) {
            best = slot.click_rect;
        }
    }
    if (best) {
        return best;
    }

    if (!slots.empty()) {
        return slots.back().click_rect;
    }
    return std::nullopt;
}
} // namespace

bool asst::RelaunchAnchorDeployTaskPlugin::load_params(const json::value& /*params*/)
{
    return m_config->get_theme() == ReclamationTheme::RelaunchAnchor;
}

bool asst::RelaunchAnchorDeployTaskPlugin::verify(const AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    if (m_config->get_theme() != ReclamationTheme::RelaunchAnchor) {
        return false;
    }

    const std::string& task_name = details.get("details", "task", "");
    if (!is_supported_deploy_task(task_name)) {
        return false;
    }

    m_pending_task_name = task_name;
    return true;
}

bool asst::RelaunchAnchorDeployTaskPlugin::_run()
{
    LogTraceFunction;

    const std::string task_name = std::exchange(m_pending_task_name, {});
    if (task_name.empty()) {
        return true;
    }

    const auto task_ptr = Task.get(task_name);
    if (!task_ptr) {
        Log.error(__FUNCTION__, "| task not found:", task_name);
        return false;
    }

    const auto& image = ctrler()->get_image();
    const auto slots = detect_deploy_slots(image);
    std::optional<asst::Rect> target_rect;
    if (is_gathering_base_task(task_name)) {
        target_rect = find_gathering_base_rect(slots);
    }
    else {
        target_rect = find_rightmost_oper_rect(slots);
    }

    if (target_rect) {
        task_ptr->specific_rect = *target_rect;
        Log.info(__FUNCTION__, "| task:", task_name, ", deploy rect:", task_ptr->specific_rect);
    }
    else {
        Log.warn(__FUNCTION__, "| fallback to specificRect:", task_ptr->specific_rect);
    }

    return true;
}
