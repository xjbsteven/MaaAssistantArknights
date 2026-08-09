#include "RoguelikeConfig.h"

#include <algorithm>

#include "Config/TaskData.h"
#include "Utils/Logger.hpp"
#include "Utils/StringMisc.hpp"

bool asst::RoguelikeConfig::verify_and_load_params(const json::value& params)
{
    // ------------------ 肉鸽主题设置 ------------------
    std::string theme = params.get("theme", std::string(RoguelikeTheme::Phantom));
    if (!RoguelikeConfig::is_valid_theme(theme)) {
        Log.error("Unknown roguelike theme", theme);
        return false;
    }

    auto mode = static_cast<RoguelikeMode>(params.get("mode", 0));
    if (!RoguelikeConfig::is_valid_mode(mode, theme)) {
        Log.error(__FUNCTION__, "| Unknown mode", static_cast<int>(mode));
        return false;
    }

    m_theme = theme;
    m_mode = mode;
    m_difficulty = params.get("difficulty", -1);

    Log.info("Roguelike theme", m_theme, "| mode", static_cast<int>(m_mode), "| difficulty", m_difficulty);

    if (mode == RoguelikeMode::Collectible) {
        m_collectible_mode_shopping = params.get("collectible_mode_shopping", false);

        if (m_theme == RoguelikeTheme::Sami) {
            m_first_floor_foldartal = !params.get("first_floor_foldartal", "").empty();
        }
    }

    m_start_with_elite_two = params.get("start_with_elite_two", false);
    m_only_start_with_elite_two = params.get("only_start_with_elite_two", false);
    if (mode != RoguelikeMode::Collectible && (m_start_with_elite_two || m_only_start_with_elite_two)) {
        Log.error(__FUNCTION__, "| Invalid mode for start_with_elite_two", static_cast<int>(mode));
        return false;
    }
    if (!m_start_with_elite_two && m_only_start_with_elite_two) {
        Log.error(__FUNCTION__, "| only_start_with_elite_two can only be used together with start_with_elite_two");
        return false;
    }

    // 设置层数选点策略，相关逻辑在 RoguelikeStrategyChangeTaskPlugin
    {
        // 刷源石锭：Stages_investment；刷藏品：Stages_collectibleFarm；其余用 Stages_default
        const std::string stages_task = m_theme + "@Roguelike@Stages";
        const std::string investment_stages = m_theme + "@Roguelike@Stages_investment";
        const std::string collectible_farm_stages = m_theme + "@Roguelike@Stages_collectibleFarm";
        if (m_mode == RoguelikeMode::Investment && Task.get(investment_stages) != nullptr) {
            Task.set_task_base(stages_task, investment_stages);
        }
        else if (m_mode == RoguelikeMode::CollectibleFarm && Task.get(collectible_farm_stages) != nullptr) {
            Task.set_task_base(stages_task, collectible_farm_stages);
        }
        else {
            Task.set_task_base(stages_task, m_theme + "@Roguelike@Stages_default");
        }
        std::string strategy_task = m_theme + "@Roguelike@StrategyChange";
        std::string strategy_task_with_mode = strategy_task + "_mode" + std::to_string(static_cast<int>(mode));
        if (Task.get(strategy_task_with_mode) == nullptr) {
            strategy_task_with_mode = "#none"; // 没有对应的层数选点策略，使用默认策略（避战）
            Log.warn(__FUNCTION__, "No strategy for mode", static_cast<int>(mode));
        }
        Task.set_task_base(strategy_task, strategy_task_with_mode);

        // 萨卡兹点刺成锭分队特殊策略
        if (m_theme == "Sarkaz") {
            if (m_mode == RoguelikeMode::Investment && params.get("squad", "") == "点刺成锭分队") {
                // 启用特殊策略，联动 RoguelikeRoutingTaskPlugin
                Task.set_task_base(strategy_task, "Sarkaz@Roguelike@StrategyChange-FastInvestment");
                // 禁用前 2 层的 <思维负荷干员编队> 功能
                Task.set_task_base(
                    "Sarkaz@Roguelike@StageBurdenOperation",
                    "Sarkaz@Roguelike@StageBurdenOperation-None");
            }
            else {
                // 启用前 2 层的 <思维负荷干员编队> 功能
                Task.set_task_base(
                    "Sarkaz@Roguelike@StageBurdenOperation",
                    "Sarkaz@Roguelike@StageBurdenOperation-Start");
            }
        }
        // 界园指挥分队特殊策略
        if (m_theme == "JieGarden") {
            if (m_mode == RoguelikeMode::Investment && params.get("squad", "") == "指挥分队" && m_difficulty >= 3) {
                // 启用特殊策略，联动 RoguelikeRoutingTaskPlugin
                Task.set_task_base(strategy_task, "JieGarden@Roguelike@StrategyChange_mode1-FastPass");
            }
            if (m_mode == RoguelikeMode::Collectible &&
                params.get("collectible_mode_squad", params.get("squad", "")) == "指挥分队" && m_difficulty >= 3) {
                // 启用特殊策略，联动 RoguelikeRoutingTaskPlugin
                Task.set_task_base(strategy_task, "JieGarden@Roguelike@StrategyChange_mode4-FastPass");
            }
            if (m_mode == RoguelikeMode::FindPlaytime) {
                // 启用刷常乐节点策略，联动 RoguelikeRoutingTaskPlugin
                Task.set_task_base(strategy_task, "JieGarden@Roguelike@StrategyChange_mode20001");
            }
        }
    }

    if (m_mode == RoguelikeMode::CollectibleFarm) {
        if (parse_refresh_trader_shopping_list(params).empty()) {
            Log.error(__FUNCTION__, "| CollectibleFarm mode requires non-empty refresh_trader_shopping_list");
            return false;
        }
    }

    if (m_mode == RoguelikeMode::Investment) {
        bool investment_with_more_score = params.get("investment_with_more_score", false);
        if (params.contains("investment_enter_second_floor")) {
            Log.warn("================  DEPRECATED  ================");
            LogWarn << "`investment_enter_second_floor` has been deprecated since v5.2.1; Please use "
                       "'investment_with_more_score'";
            Log.warn("================  DEPRECATED  ================");
            return false;
        }
        m_invest_with_more_score = (investment_with_more_score);
    }

    if (m_mode == RoguelikeMode::Collectible && !m_only_start_with_elite_two) {
        m_run_for_collectible = true; // 烧开水模式下，如果不是只凹直升，第一轮游戏先烧水
    }

    if (m_mode == RoguelikeMode::FindPlaytime) {
        m_find_playTime_target = params.get("find_playTime_target", 0);
        if (m_find_playTime_target < 1 || m_find_playTime_target > 3) {
            Log.error(__FUNCTION__, "| Invalid find_playTime_target", m_find_playTime_target);
            return false;
        }
    }

    return true;
}

std::vector<std::string> asst::RoguelikeConfig::parse_refresh_trader_shopping_list(const json::value& params)
{
    std::vector<std::string> list;
    const auto opt = params.find<json::array>("refresh_trader_shopping_list");
    if (!opt) {
        return list;
    }

    // 把中文分号、换行统一成 ';'，再按 ';' 切开（兼容旧 GUI 未拆开的整段配置）
    constexpr std::string_view kCnSemicolon = "；"; // U+FF1B
    for (const auto& name : *opt) {
        std::string raw = name.as_string();
        if (raw.empty()) {
            continue;
        }
        utils::string_replace_all_in_place(raw, { { kCnSemicolon, ";" }, { "\r\n", ";" }, { "\n", ";" }, { "\r", ";" } });

        size_t start = 0;
        while (start <= raw.size()) {
            const size_t pos = raw.find(';', start);
            std::string part = raw.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
            // trim spaces
            const auto first = part.find_first_not_of(" \t");
            if (first == std::string::npos) {
                part.clear();
            }
            else {
                const auto last = part.find_last_not_of(" \t");
                part = part.substr(first, last - first + 1);
            }
            if (!part.empty()) {
                list.emplace_back(std::move(part));
            }
            if (pos == std::string::npos) {
                break;
            }
            start = pos + 1;
        }
    }
    return list;
}

void asst::RoguelikeConfig::clear()
{
    m_status = RoguelikeStatus();
    m_status.opers.reserve(m_status.formation_upper_limit);

    // ------------------ 通用参数 ------------------
    m_squad = std::string();
}

