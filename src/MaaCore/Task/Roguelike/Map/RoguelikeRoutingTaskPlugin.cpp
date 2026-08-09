#include "RoguelikeRoutingTaskPlugin.h"

#include <limits>
#include <numeric>

#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/ProcessTask.h"
#include "Utils/DebugImageHelper.hpp"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/Miscellaneous/PixelAnalyzer.h"
#include "Vision/MultiMatcher.h"

bool asst::RoguelikeRoutingTaskPlugin::load_params([[maybe_unused]] const json::value& params)
{
    const std::string& theme = m_config->get_theme();
    const RoguelikeMode& mode = m_config->get_mode();

    // 水月/萨米刷藏品：只用 Stages 模板优先级 + CustomShopping，不启用寻路插件。
    if ((theme == RoguelikeTheme::Mizuki || theme == RoguelikeTheme::Sami) &&
        mode == RoguelikeMode::CollectibleFarm) {
        Log.info(__FUNCTION__, "| CollectibleFarm: routing disabled (Stages-only), theme:", theme);
        return false;
    }

    // 本插件仅：萨卡兹 / 界园
    if (theme != RoguelikeTheme::Sarkaz && theme != RoguelikeTheme::JieGarden) {
        return false;
    }

    const std::string config_task_name =
        Task.get(theme + "@RoguelikeRoutingConfig") != nullptr ? theme + "@RoguelikeRoutingConfig"
                                                               : "RoguelikeRoutingConfig";
    const TaskPtr config_task = Task.get(config_task_name);

    m_origin_x = config_task->special_params.at(0);
    m_middle_x = config_task->special_params.at(1);
    m_last_x = config_task->special_params.at(2);
    m_node_width = config_task->special_params.at(3);
    m_node_height = config_task->special_params.at(4);
    m_column_offset = config_task->special_params.at(5);
    m_nameplate_offset = config_task->special_params.at(6);
    m_roi_margin = config_task->special_params.at(7);
    m_direction_threshold = config_task->special_params.at(8);

    const std::string squad = params.get("squad", "");

    if (theme == RoguelikeTheme::Sarkaz && mode == RoguelikeMode::FastPass && squad == "蓝图测绘分队") {
        m_routing_strategy = RoutingStrategy::Sarkaz_FastPass;
        return true;
    }

    if (theme == RoguelikeTheme::Sarkaz && mode == RoguelikeMode::Investment && squad == "点刺成锭分队") {
        m_routing_strategy = RoutingStrategy::Sarkaz_FastInvestment;
        return true;
    }

    if (theme == RoguelikeTheme::JieGarden) {
        if (((mode == RoguelikeMode::Investment && squad == "指挥分队") ||
             (mode == RoguelikeMode::Collectible && params.get("collectible_mode_squad", squad) == "指挥分队")) &&
            m_config->get_difficulty() >= 3) {
            m_routing_strategy = RoutingStrategy::JieGarden_FastPassWithBattle;
            return true;
        }
    }

    return false;
}

void asst::RoguelikeRoutingTaskPlugin::reset_in_run_variables()
{
    m_map.reset();
    m_need_generate_map = true;
    // 新开局默认在一层：不扫全图，等 StrategyChange 再决定
    m_need_mizuki_full_scan = false;
    m_mizuki_skip_full_scan = true;
    m_mizuki_abandon_floor = false;
    m_mizuki_floor_changed = false;
    m_selected_column = 0;
    m_selected_x = 0;
}

bool asst::RoguelikeRoutingTaskPlugin::verify(const AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    std::string task_name = details.get("details", "task", "");

    // 换层时标记；一层 NoScan，二层起全图扫描（不在这里跑路由）
    if (m_routing_strategy == RoutingStrategy::Mizuki_CollectibleFarm) {
        std::string_view tv = task_name;
        const std::string prefix = m_config->get_theme() + "@";
        if (tv.starts_with(prefix)) {
            tv.remove_prefix(prefix.size());
        }
        if (tv == "Roguelike@StrategyChange") {
            m_mizuki_floor_changed = true;
            const std::string strategy_text = details.get("details", "result", "text", "");
            const bool abandon_floor = strategy_text.find("_restart") != std::string::npos ||
                                       strategy_text.find("_exit") != std::string::npos;
            m_mizuki_abandon_floor = abandon_floor;
            m_mizuki_skip_full_scan =
                abandon_floor || strategy_text.find("NoScan") != std::string::npos;
            Log.info(
                __FUNCTION__,
                "| StrategyChange, strategy:",
                strategy_text,
                "skip_full_scan:",
                m_mizuki_skip_full_scan,
                "abandon_floor:",
                abandon_floor);
            return false;
        }
        if (tv == "Roguelike@NextLevel" || tv == "Roguelike@DiceConfirmAfterNextLevel") {
            // 离开当前层：默认二层起要全图扫；若随后 StrategyChange 再识别到 NoScan 会覆盖
            m_mizuki_floor_changed = true;
            m_mizuki_skip_full_scan = false;
            Log.info(__FUNCTION__, "| NextLevel: require full scan on next floor");
            return false;
        }
    }

    // trigger 任务的名字可以为 "...@Roguelike@Routing-..." 的形式
    if (const size_t pos = task_name.find('-'); pos != std::string::npos) {
        task_name = task_name.substr(0, pos);
    }

    if (task_name == m_config->get_theme() + "@Roguelike@Routing") {
        return true;
    }

    return false;
}

bool asst::RoguelikeRoutingTaskPlugin::_run()
{
    LogTraceFunction;

    switch (m_routing_strategy) {
    case RoutingStrategy::Sarkaz_FastInvestment:
        if (m_need_generate_map) {
            // 随机点击一个第一列的节点，先随便写写，垃圾代码迟早要重构
            ProcessTask(*this, { "Sarkaz@RoguelikeRouting-CombatOps" }).run();
            // 刷新节点
            ProcessTask(*this, { "Sarkaz@RoguelikeRouting-RefreshNode" }).run();
            // 不识别了，进商店，Go!
            Task.set_task_base("RoguelikeRoutingAction", "Sarkaz@RoguelikeRoutingAction-StageTraderEnter");
            // 偷懒，直接用 m_need_generate_map 判断是否已进过商店
            m_need_generate_map = false;
        }
        else {
            Task.set_task_base("RoguelikeRoutingAction", "Sarkaz@RoguelikeRoutingAction-ExitThenAbandon");
        }
        break;
    case RoutingStrategy::JieGarden_FastPassWithBattle:
        if (m_need_generate_map) {
            // 向左滑动以检视前三列节点
            ProcessTask(*this, { "RoguelikeRouting-MoveRightToPeek" }).run();
            cv::Mat image = ctrler()->get_image();
            cv::Mat image_draw = image.clone();
            update_map(image, RoguelikeMap::INIT_INDEX + 1, image_draw);
#ifdef ASST_DEBUG
            utils::save_debug_image(
                image_draw,
                utils::path("debug") / "roguelikeMap",
                /*auto_clean=*/true,
                /*description=*/"bosky map draw",
                /*suffix=*/"draw");
#endif
            m_need_generate_map = false;

            // 根据第三列节点类型更新导航策略
            const size_t sample_node_of_last_column = m_map.size() - 1;
            const RoguelikeNodeType sample_node_type = m_map.get_node_type(sample_node_of_last_column);
            Log.info("RoguelikeRouting | Type of last node:", type2name(sample_node_type));
            if (sample_node_type == RoguelikeNodeType::RogueTrader) {
                m_routing_strategy = RoutingStrategy::JieGarden_FastPassWithoutBattle;
                m_config->set_skip_recruit_in_fast_pass(true);
                return _run();
            }
        }
        if (m_map.get_curr_pos() == RoguelikeMap::INIT_INDEX) {
            // 规划路线
            m_map.set_cost_fun([&](const RoguelikeNodePtr& node) {
                if (node->type == RoguelikeNodeType::CombatOps) {
                    return 10;
                }
                if (node->type == RoguelikeNodeType::EmergencyOps || node->type == RoguelikeNodeType::DreadfulFoe) {
                    return 11;
                }
                return 0;
            });
            m_map.update_node_costs();
            const size_t next_node = m_map.get_next_node();

            // 若无法避免超过三场战斗则重开
            if (m_map.get_node_cost(next_node) >= 30) {
                callback(
                    AsstMsg::TaskChainExtraInfo,
                    json::object {
                        { "what", "RoutingRestart" },
                        { "why", "TooManyBattlesAhead" },
                        { "node_cost", m_map.get_node_cost(next_node) },
                    });

                Task.set_task_base("RoguelikeRoutingAction", "JieGarden@RoguelikeRoutingAction-ExitThenAbandon");
            }
            else {
                const int next_node_x = m_left_most_column_x_in_view;
                const int next_node_y = m_map.get_node_y(next_node);
                Point next_node_center = Point(next_node_x + m_node_width / 2, next_node_y + m_node_height / 2);
                ctrler()->click(next_node_center);
                sleep(200);

                Task.set_task_base(
                    "RoguelikeRoutingAction",
                    "JieGarden@RoguelikeRoutingAction-StageCombatOpsEnterThenLeave");
                m_map.set_curr_pos(next_node);
            }
        }
        else {
            // 执行默认的避战策略
            Task.set_task_base("RoguelikeRoutingAction", "JieGarden@Roguelike@Stages_default");
        }
        break;

    case RoutingStrategy::JieGarden_FastPassWithoutBattle:
        if (m_need_generate_map) {
            cv::Mat image = ctrler()->get_image();
            cv::Mat image_draw = image.clone();
            update_map(image, RoguelikeMap::INIT_INDEX + 1, image_draw);
#ifdef ASST_DEBUG
            utils::save_debug_image(
                image_draw,
                utils::path("debug") / "roguelikeMap",
                /*auto_clean=*/true,
                /*description=*/"bosky map draw",
                /*suffix=*/"draw");
#endif
            m_need_generate_map = false;
        }
        if (m_map.get_curr_pos() == RoguelikeMap::INIT_INDEX) {
            m_map.set_cost_fun([&](const RoguelikeNodePtr& node) {
                if (node->type == RoguelikeNodeType::CombatOps || node->type == RoguelikeNodeType::EmergencyOps ||
                    node->type == RoguelikeNodeType::DreadfulFoe) {
                    return 1;
                }
                return 0;
            });
            m_map.update_node_costs();
            const size_t next_node = m_map.get_next_node();

            // 若无法避免超过两场战斗则重开
            if (m_map.get_node_cost(next_node) >= 2) {
                callback(
                    AsstMsg::TaskChainExtraInfo,
                    json::object {
                        { "what", "RoutingRestart" },
                        { "why", "TooManyBattlesAhead" },
                        { "node_cost", m_map.get_node_cost(next_node) },
                    });

                Task.set_task_base("RoguelikeRoutingAction", "JieGarden@RoguelikeRoutingAction-ExitThenAbandon");
            }
            else {
                const int next_node_x = m_left_most_column_x_in_view;
                const int next_node_y = m_map.get_node_y(next_node);
                Point next_node_center = Point(next_node_x + m_node_width / 2, next_node_y + m_node_height / 2);
                ctrler()->click(next_node_center);
                sleep(200);

                Task.set_task_base(
                    "RoguelikeRoutingAction",
                    "JieGarden@RoguelikeRoutingAction-StageCombatOpsEnterThenLeave");
                m_map.set_curr_pos(next_node);
            }
        }
        else {
            // 执行默认的避战策略
            Task.set_task_base("RoguelikeRoutingAction", "JieGarden@Roguelike@Stages_default");
        }
        break;
    case RoutingStrategy::Sarkaz_FastPass:
        if (m_need_generate_map) {
            generate_map();
            m_need_generate_map = false;
        }

        m_selected_column = m_map.get_node_column(m_map.get_curr_pos());
        update_selected_x();

        refresh_following_combat_nodes();
        navigate_route();
        break;

    case RoutingStrategy::Mizuki_CollectibleFarm:
        run_mizuki_collectible_farm();
        break;

    default:
        break;
    }

    return true;
}

bool asst::RoguelikeRoutingTaskPlugin::update_map(
    const cv::Mat& image,
    const size_t leftmost_column,
    std::optional<std::reference_wrapper<cv::Mat>> image_draw_opt)
{
    LogTraceFunction;

    if (leftmost_column == 0) {
        Log.error(__FUNCTION__, "| leftmost_column must be greater than zero");
        return false;
    }

    const std::string& theme = m_config->get_theme();

    size_t curr_col = leftmost_column - 1;
    int curr_x = -m_node_width - 1; // 第一列节点将触发 rect.x >= curr_x + m_node_width 并更新 curr_col 与 curr_x

    MultiMatcher node_analyzer(image);
    node_analyzer.set_task_info(theme + "@RoguelikeRoutingNodeAnalyze");
    if (!node_analyzer.analyze()) {
        Log.error(__FUNCTION__, "| no nodes are recognised");
        return false;
    }
    MultiMatcher::ResultsVec match_results = node_analyzer.get_result();
    sort_by_vertical_(match_results); // 按照水平方向从左到右排序各列节点，同一列节点按照垂直方向从上到下排序
    m_left_most_column_x_in_view = match_results.front().rect.x;

    const size_t old_num_columns = m_map.get_num_columns();
    for (const auto& [rect, score, templ_name] : match_results) {
        const RoguelikeNodeType type = RoguelikeMapInfo.templ2type(theme, templ_name);
#ifdef ASST_DEBUG
        if (image_draw_opt.has_value()) {
            cv::rectangle(image_draw_opt.value().get(), make_rect<cv::Rect>(rect), cv::Scalar(255, 255, 255), 2);
            cv::putText(
                image_draw_opt.value().get(),
                templ_name,
                cv::Point(rect.x, rect.y - m_roi_margin),
                cv::FONT_HERSHEY_DUPLEX,
                0.5,
                cv::Scalar(255, 255, 255));
        }
#endif
        if (rect.x >= curr_x + m_node_width) { // 识别到下一列的节点
            ++curr_col;
            curr_x = rect.x;
        }
        if (curr_col >= old_num_columns) // 仅更新新列节点
        {
            const size_t node = m_map.create_and_insert_node(type, curr_col, rect.y).value();
            generate_edges(node, image, rect.x, image_draw_opt);
        }
    }

    return true;
}

void asst::RoguelikeRoutingTaskPlugin::generate_map()
{
    LogTraceFunction;

    const std::string& theme = m_config->get_theme();

    m_map.reset();
    size_t curr_col = RoguelikeMap::INIT_INDEX + 1;
    Rect roi = Task.get<MatchTaskInfo>(theme + "@RoguelikeRoutingNodeAnalyze")->roi;

    // 第一列节点
    cv::Mat image = ctrler()->get_image();
    MultiMatcher node_analyzer(image);
    node_analyzer.set_task_info(theme + "@RoguelikeRoutingNodeAnalyze");
    if (!node_analyzer.analyze()) {
        Log.error(__FUNCTION__, "| no nodes found in the first column");
        return;
    }
    MultiMatcher::ResultsVec match_results = node_analyzer.get_result();
    sort_by_horizontal_(match_results); // 按照垂直方向排序（从上到下）
    for (const auto& [rect, score, templ_name] : match_results) {
        const RoguelikeNodeType type = RoguelikeMapInfo.templ2type(theme, templ_name);
        const size_t node = m_map.create_and_insert_node(type, curr_col, rect.y).value();
        generate_edges(node, image, rect.x);
    }

    // 第二列及以后的节点
    roi.x += m_column_offset;
    node_analyzer.set_roi(roi);
    while (!need_exit() && node_analyzer.analyze()) {
        ++curr_col;
        match_results = node_analyzer.get_result();
        sort_by_horizontal_(match_results);
        for (const auto& [rect, score, templ_name] : match_results) {
            const RoguelikeNodeType type = RoguelikeMapInfo.templ2type(theme, templ_name);
            const size_t node = m_map.create_and_insert_node(type, curr_col, rect.y).value();
            generate_edges(node, image, rect.x);
        }
        ProcessTask(*this, { "RoguelikeRouting-MoveRight" }).run();
        sleep(200);
        image = ctrler()->get_image();
        node_analyzer.set_image(image);
    }

    ProcessTask(*this, { theme + "@RoguelikeRouting-ExitThenContinue" }).run(); // 通过退出重进回到初始位置
}

void asst::RoguelikeRoutingTaskPlugin::generate_edges(
    const size_t& node,
    const cv::Mat& image,
    const int& node_x,
    [[maybe_unused]] std::optional<std::reference_wrapper<cv::Mat>> image_draw_opt)
{
    LogTraceFunction;

    const size_t node_column = m_map.get_node_column(node);

    if (node_column == RoguelikeMap::INIT_INDEX) {
        Log.error(__FUNCTION__, "| cannot generate edges for init node");
        return;
    }

    if (node_column == RoguelikeMap::INIT_INDEX + 1) {
        m_map.add_edge(RoguelikeMap::INIT_INDEX, node); // 第一列节点直接与 init 连接
        return;
    }

    // 将 image转换为二值图像后计算亮点
    PixelAnalyzer analyzer(image);

    const int center_x = node_x - (m_column_offset - m_node_width) / 2; // node 与 前一列节点的中点横坐标
    const int node_y = m_map.get_node_y(node);
    Rect roi(0, 0, m_roi_margin * 2, m_roi_margin * 2);

    // 遍历前一列节点
    const size_t pre_col_begin = m_map.get_column_begin(node_column - 1);
    const size_t pre_col_end = m_map.get_column_end(node_column - 1);
    for (size_t prev = pre_col_begin; prev < pre_col_end; ++prev) {
        const int prev_y = m_map.get_node_y(prev);
        const int center_y = (prev_y + node_y + m_node_height) / 2;
        roi.x = center_x - m_roi_margin;
        roi.y = center_y - m_roi_margin;
#ifdef ASST_DEBUG
        if (image_draw_opt.has_value()) {
            cv::rectangle(image_draw_opt.value().get(), make_rect<cv::Rect>(roi), cv::Scalar(255, 255, 255), 1);
        }
#endif
        analyzer.set_roi(roi);

        if (!analyzer.analyze()) { // 节点间没有连线
            continue;
        }

        // 按照水平方向排序（从左到右）
        std::vector<Point> brightPixels = analyzer.get_result();

        auto [x_min_p, x_max_p] = std::ranges::minmax(brightPixels, /*comp=*/ {}, [](const Point& p) { return p.x; });
        const int leftmost_x = x_min_p.x;
        const int rightmost_x = x_max_p.x;

        auto leftmostBrightPixels =
            brightPixels | std::views::filter([&](const Point& p) { return p.x == leftmost_x; });
        auto rightmostBrightPixels =
            brightPixels | std::views::filter([&](const Point& p) { return p.x == rightmost_x; });

        auto [leftmost_y_min_p, leftmost_y_max_p] =
            std::ranges::minmax(leftmostBrightPixels, /*comp=*/ {}, [](const Point& p) { return p.y; });
        const int leftmost_y = (leftmost_y_min_p.y + leftmost_y_max_p.y) / 2;

        auto [rightmost_y_min_p, rightmost_y_max_p] =
            std::ranges::minmax(rightmostBrightPixels, /*comp=*/ {}, [](const Point& p) { return p.y; });
        const int rightmost_y = (rightmost_y_min_p.y + rightmost_y_max_p.y) / 2;

        if ((std::abs(prev_y - node_y) < m_direction_threshold &&
             std::abs(leftmost_y - rightmost_y) < m_direction_threshold) ||
            (prev_y < node_y && leftmost_y < rightmost_y - m_direction_threshold) ||
            (prev_y > node_y && leftmost_y > rightmost_y + m_direction_threshold)) {
            m_map.add_edge(prev, node);
#ifdef ASST_DEBUG
            if (image_draw_opt.has_value()) {
                cv::line(
                    image_draw_opt.value().get(),
                    cv::Point(node_x - m_column_offset + m_node_width / 2, prev_y + m_node_height / 2),
                    cv::Point(node_x + m_node_width / 2, node_y + m_node_height / 2),
                    cv::Scalar(255, 255, 255),
                    2);
            }
#endif
        }
    }

    // 同列前一个节点
    if (node > m_map.get_column_begin(node_column)) {
        size_t prev = node - 1;
        roi.x = node_x + m_node_width / 2 - m_roi_margin;
        roi.y = (m_map.get_node_y(prev) + m_node_height + m_nameplate_offset + node_y) / 2 - m_roi_margin;
#ifdef ASST_DEBUG
        if (image_draw_opt.has_value()) {
            cv::rectangle(image_draw_opt.value().get(), make_rect<cv::Rect>(roi), cv::Scalar(255, 255, 255), 1);
        }
#endif
        analyzer.set_roi(roi);
        if (analyzer.analyze()) {
            m_map.add_edge(prev, node);
            m_map.add_edge(node, prev);
#ifdef ASST_DEBUG
            if (image_draw_opt.has_value()) {
                cv::line(
                    image_draw_opt.value().get(),
                    cv::Point(node_x + m_node_width / 2, m_map.get_node_y(prev) + m_node_height / 2),
                    cv::Point(node_x + m_node_width / 2, node_y + m_node_height / 2),
                    cv::Scalar(255, 255, 255),
                    2);
            }
#endif
        }
    }
}

void asst::RoguelikeRoutingTaskPlugin::refresh_following_combat_nodes()
{
    LogTraceFunction;

    const std::string& theme = m_config->get_theme();

    const size_t curr_node = m_map.get_curr_pos();
    const size_t curr_node_column = m_map.get_node_column(curr_node);

    for (size_t next_node : m_map.get_node_succs(curr_node)) {
        // 不刷新同一列的节点
        const size_t next_node_column = m_map.get_node_column(next_node);
        if (next_node_column <= curr_node_column) {
            continue;
        }
        // 每个节点仅刷新一次
        if (m_map.get_node_refresh_times(next_node)) {
            continue;
        }
        // 不刷新非战斗节点
        RoguelikeNodeType next_node_type = m_map.get_node_type(next_node);
        if (next_node_type != RoguelikeNodeType::CombatOps && next_node_type != RoguelikeNodeType::EmergencyOps &&
            next_node_type != RoguelikeNodeType::DreadfulFoe) {
            continue;
        }

        int next_node_x = m_selected_x + (next_node_column == m_selected_column ? 0 : m_column_offset);
        int next_node_y = m_map.get_node_y(next_node);
        Rect next_node_rect = Rect(next_node_x, next_node_y, m_node_width, m_node_height);

        // 点击节点
        ctrler()->click(next_node_rect);
        m_selected_column = m_map.get_node_column(next_node);
        update_selected_x();
        next_node_rect.x = m_selected_x;
        sleep(200);

        // 刷新节点
        ProcessTask(*this, { m_config->get_theme() + "@RoguelikeRouting-RefreshNode" }).run();
        m_map.set_node_refresh_times(next_node, m_map.get_node_refresh_times(next_node) + 1);

        // 识别并更新节点类型
        Matcher node_analyzer(ctrler()->get_image());
        node_analyzer.set_task_info(theme + "@RoguelikeRoutingNodeAnalyze");
        node_analyzer.set_roi(next_node_rect);
        if (node_analyzer.analyze()) {
            const Matcher::Result& match_results = node_analyzer.get_result();
            m_map.set_node_type(next_node, RoguelikeMapInfo.templ2type(theme, match_results.templ_name));
        }
    }
}

void asst::RoguelikeRoutingTaskPlugin::navigate_route()
{
    LogTraceFunction;

    const size_t curr_col = m_map.get_node_column(m_map.get_curr_pos());

    m_map.set_cost_fun([&](const RoguelikeNodePtr& node) {
        if (node->visited) {
            return 1000;
        }

        if (node->column == curr_col) {
            return 1000;
        }

        if (node->type == RoguelikeNodeType::CombatOps || node->type == RoguelikeNodeType::EmergencyOps ||
            node->type == RoguelikeNodeType::DreadfulFoe) {
            return 1 + (node->refresh_times ? 999 : 0);
        }

        return 0;
    });

    m_map.update_node_costs();

    const size_t next_node = m_map.get_next_node();

    if (m_map.get_node_cost(next_node) >= 1000) {
        Task.set_task_base("RoguelikeRoutingAction", "Sarkaz@RoguelikeRoutingAction-ExitThenAbandon");
        reset_in_run_variables();
        return;
    }

    const size_t next_node_column = m_map.get_node_column(next_node);
    const int next_node_x = m_selected_x + (next_node_column == m_selected_column ? 0 : m_column_offset);
    const int next_node_y = m_map.get_node_y(next_node);
    Point next_node_center = Point(next_node_x + m_node_width / 2, next_node_y + m_node_height / 2);
    ctrler()->click(next_node_center);
    sleep(200);

    if (m_map.get_node_type(next_node) == RoguelikeNodeType::Encounter) {
        Task.set_task_base("RoguelikeRoutingAction", "Sarkaz@RoguelikeRoutingAction-StageEncounterEnter");
        m_map.set_curr_pos(next_node);
    }
    else if (m_map.get_node_type(next_node) == RoguelikeNodeType::RogueTrader) {
        Task.set_task_base("RoguelikeRoutingAction", "Sarkaz@RoguelikeRoutingAction-StageTraderEnter");
        reset_in_run_variables();
    }
    else {
        Task.set_task_base("RoguelikeRoutingAction", "Sarkaz@RoguelikeRoutingAction-ExitThenAbandon");
        reset_in_run_variables();
    }
}

void asst::RoguelikeRoutingTaskPlugin::update_selected_x()
{
    if (m_selected_column == RoguelikeMap::INIT_INDEX) {
        m_selected_x = m_origin_x - m_column_offset;
    }
    else if (m_selected_column == RoguelikeMap::INIT_INDEX + 1) {
        m_selected_x = m_origin_x;
    }
    else if (m_selected_column == m_map.get_num_columns() - 1) [[unlikely]] {
        m_selected_x = m_last_x;
    }
    else {
        m_selected_x = m_middle_x;
    }
}

void asst::RoguelikeRoutingTaskPlugin::generate_fallback_edges_by_y()
{
    // 像素连线识别失败时，按 y 最近邻补边；距离过大则视为误检列，不连
    constexpr int kMaxYDist = 120;
    const size_t num_columns = m_map.get_num_columns();
    for (size_t col = RoguelikeMap::INIT_INDEX + 2; col < num_columns; ++col) {
        const size_t begin = m_map.get_column_begin(col);
        const size_t end = m_map.get_column_end(col);
        const size_t prev_begin = m_map.get_column_begin(col - 1);
        const size_t prev_end = m_map.get_column_end(col - 1);
        if (prev_begin >= prev_end) {
            continue;
        }
        for (size_t node = begin; node < end; ++node) {
            if (!m_map.get_node_preds(node).empty()) {
                continue;
            }
            const int node_y = m_map.get_node_y(node);
            size_t best_prev = prev_begin;
            int best_dist = std::numeric_limits<int>::max();
            for (size_t prev = prev_begin; prev < prev_end; ++prev) {
                const int dist = std::abs(m_map.get_node_y(prev) - node_y);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_prev = prev;
                }
            }
            if (best_dist > kMaxYDist) {
                Log.warn(__FUNCTION__, "| skip fallback edge, y_dist too large:", best_dist, "node", node);
                continue;
            }
            Log.info(__FUNCTION__, "| fallback edge", best_prev, "->", node, "y_dist", best_dist);
            m_map.add_edge(best_prev, node);
        }
    }
}

std::vector<std::vector<asst::MatchRect>>
    asst::RoguelikeRoutingTaskPlugin::analyze_mizuki_columns(const cv::Mat& image) const
{
    const std::string& theme = m_config->get_theme();
    MultiMatcher node_analyzer(image);
    node_analyzer.set_task_info(theme + "@RoguelikeRoutingNodeAnalyze");
    if (!node_analyzer.analyze()) {
        return {};
    }

    MultiMatcher::ResultsVec raw = node_analyzer.get_result();
    // 与 Routing-CollectibleFarm / NodeAnalyze templThreshold=0.7 对齐；0.75 会把战后常见 0.70 左右节点滤光
    std::erase_if(raw, [](const MultiMatcher::Result& r) { return r.rect.y < 150 || r.score < 0.70; });
    // 商店与作战模板易混：同位置有 RogueTrader 时丢掉 CombatOps，避免把店认成作战
    for (auto it = raw.begin(); it != raw.end();) {
        if (it->templ_name.find("CombatOps") == std::string::npos) {
            ++it;
            continue;
        }
        const int cx = it->rect.x + it->rect.width / 2;
        const int cy = it->rect.y + it->rect.height / 2;
        const bool trader_near = std::ranges::any_of(raw, [&](const MultiMatcher::Result& other) {
            if (other.templ_name.find("RogueTrader") == std::string::npos) {
                return false;
            }
            const int ox = other.rect.x + other.rect.width / 2;
            const int oy = other.rect.y + other.rect.height / 2;
            return std::abs(cx - ox) < 80 && std::abs(cy - oy) < 60;
        });
        if (trader_near) {
            it = raw.erase(it);
        }
        else {
            ++it;
        }
    }

    MultiMatcher::ResultsVec match_results = NMS(raw);
    if (match_results.empty()) {
        return {};
    }
    sort_by_vertical_(match_results);

    const int col_gap = std::max(m_node_width, m_column_offset / 2);
    std::vector<std::vector<MatchRect>> columns;
    for (const auto& mr : match_results) {
        if (columns.empty() || mr.rect.x >= columns.back().front().rect.x + col_gap) {
            columns.emplace_back();
        }
        columns.back().emplace_back(mr);
    }
    return columns;
}

void asst::RoguelikeRoutingTaskPlugin::append_mizuki_column(
    const std::vector<MatchRect>& column,
    const cv::Mat& image,
    cv::Mat* image_draw)
{
    if (column.empty()) {
        return;
    }
    const std::string& theme = m_config->get_theme();
    const size_t map_col = m_map.get_num_columns(); // 下一列 index（含 init 时即真实列号）
    for (const auto& [rect, score, templ_name] : column) {
        const RoguelikeNodeType type = RoguelikeMapInfo.templ2type(theme, templ_name);
#ifdef ASST_DEBUG
        if (image_draw) {
            cv::rectangle(*image_draw, make_rect<cv::Rect>(rect), cv::Scalar(0, 255, 0), 2);
            cv::putText(
                *image_draw,
                templ_name.substr(templ_name.find("MapNode")),
                cv::Point(rect.x, std::max(0, rect.y - 4)),
                cv::FONT_HERSHEY_DUPLEX,
                0.4,
                cv::Scalar(0, 255, 0));
        }
#endif
        const size_t node = m_map.create_and_insert_node(type, map_col, rect.y).value();
        // 第 1 列不接 INIT（稍后按候选列绑定）；后续列尝试连线
        if (map_col >= RoguelikeMap::INIT_INDEX + 2) {
            generate_edges(node, image, rect.x, image_draw ? std::optional { std::ref(*image_draw) } : std::nullopt);
        }
    }
}

int asst::RoguelikeRoutingTaskPlugin::scan_mizuki_map_by_swipes(cv::Mat& image_draw)
{
    LogTraceFunction;

    // 1) Peek：露出半截的第 3 列
    ProcessTask(*this, { "RoguelikeRouting-MoveRightToPeek" }).run();
    sleep(300);

    constexpr int kMaxSwipes = 5;
    constexpr int kNewColumnMinX = 620; // 只把偏右的列当作「新露出」
    int swipes_done = 0;
    int last_col_rep_y = -1;
    size_t last_col_size = 0;

    for (int attempt = 0; attempt <= kMaxSwipes && !need_exit(); ++attempt) {
        cv::Mat image = ctrler()->get_image();
        if (attempt == 0) {
            image_draw = image.clone();
        }
        auto columns = analyze_mizuki_columns(image);
        if (columns.empty()) {
            Log.warn(__FUNCTION__, "| no columns at attempt", attempt);
            break;
        }

        size_t added = 0;
        if (attempt == 0) {
            for (const auto& col : columns) {
                append_mizuki_column(col, image, &image_draw);
                ++added;
            }
            last_col_rep_y = columns.back().front().rect.y;
            last_col_size = columns.back().size();
            Log.info(__FUNCTION__, "| initial columns:", columns.size());
        }
        else {
            for (const auto& col : columns) {
                if (col.front().rect.x < kNewColumnMinX) {
                    continue;
                }
                // 与上一列过近且节点数相近 → 视为同一列未滑开，跳过
                const int y_dist = std::abs(col.front().rect.y - last_col_rep_y);
                if (y_dist < 40 && col.size() == last_col_size && m_map.get_num_columns() > 1) {
                    continue;
                }
                // 与地图最后一列按 y 对齐去重
                const size_t last_map_col = m_map.get_num_columns() - 1;
                const size_t b = m_map.get_column_begin(last_map_col);
                const size_t e = m_map.get_column_end(last_map_col);
                int same_y_hits = 0;
                for (const auto& mr : col) {
                    for (size_t i = b; i < e; ++i) {
                        if (std::abs(m_map.get_node_y(i) - mr.rect.y) < 35) {
                            ++same_y_hits;
                            break;
                        }
                    }
                }
                if (!col.empty() && same_y_hits * 2 >= static_cast<int>(col.size())) {
                    continue; // 大半都能对上，当作重复列
                }

                append_mizuki_column(col, image, &image_draw);
                last_col_rep_y = col.front().rect.y;
                last_col_size = col.size();
                ++added;
                Log.info(__FUNCTION__, "| appended new column at x", col.front().rect.x, "nodes", col.size());
            }
            if (added == 0) {
                Log.info(__FUNCTION__, "| no new columns after swipe, stop scan");
                break;
            }
        }

        if (attempt == kMaxSwipes) {
            break;
        }
        ProcessTask(*this, { "RoguelikeRouting-MoveRight" }).run();
        sleep(280);
        ++swipes_done;
    }

    generate_fallback_edges_by_y();
    Log.info(__FUNCTION__, "| scan done, map columns:", m_map.get_num_columns(), "swipes:", swipes_done);
    return swipes_done;
}

void asst::RoguelikeRoutingTaskPlugin::click_mizuki_node_and_set_action(
    const MatchRect& node,
    RoguelikeNodeType type)
{
    const Point icon_center(node.rect.x + node.rect.width / 2, node.rect.y + node.rect.height / 2);
    const Point nameplate_center(node.rect.x + node.rect.width / 2, node.rect.y + node.rect.height + 18);
    ctrler()->click(icon_center);
    sleep(400);
    ctrler()->click(nameplate_center);
    sleep(800);

    const char* action = "Mizuki@RoguelikeRoutingAction-AfterClick";
    switch (type) {
    case RoguelikeNodeType::RogueTrader:
        action = "Mizuki@RoguelikeRoutingAction-StageTraderEnter";
        break;
    case RoguelikeNodeType::Encounter:
    case RoguelikeNodeType::LostAndFound:
    case RoguelikeNodeType::Boons:
    case RoguelikeNodeType::Recreation:
    case RoguelikeNodeType::Scout:
    case RoguelikeNodeType::RegionalCommissions:
        action = "Mizuki@RoguelikeRoutingAction-StageEncounterEnter";
        break;
    case RoguelikeNodeType::CombatOps:
        action = "Mizuki@RoguelikeRoutingAction-StageCombatOpsEnter";
        break;
    case RoguelikeNodeType::EmergencyOps:
        action = "Mizuki@RoguelikeRoutingAction-StageEmergencyOpsEnter";
        break;
    case RoguelikeNodeType::SafeHouse:
        action = "Mizuki@RoguelikeRoutingAction-StageSafeHouseEnter";
        break;
    case RoguelikeNodeType::DreadfulFoe:
        action = "Mizuki@RoguelikeRoutingAction-StageDreadfulFoeEnter";
        break;
    default:
        break;
    }
    Task.set_task_base("RoguelikeRoutingAction", action);
}

bool asst::RoguelikeRoutingTaskPlugin::try_click_visible_mizuki_trader()
{
    const std::string& theme = m_config->get_theme();
    cv::Mat image = ctrler()->get_image();
    auto columns = analyze_mizuki_columns(image);

    // 只认「当前可选带」里的彩色商店：取含非灰节点的最左两列。
    // 回滑中途右侧远处店也会被彩色模板误匹配，点了进不去，视野还停在全灰区。
    std::vector<size_t> open_cols;
    for (size_t c = 0; c < columns.size(); ++c) {
        const bool has_open = std::ranges::any_of(columns[c], [](const MatchRect& mr) {
            return mr.templ_name.find("Grey") == std::string::npos;
        });
        if (has_open) {
            open_cols.push_back(c);
        }
    }
    if (open_cols.empty()) {
        return false;
    }
    const size_t col_limit = std::min<size_t>(2, open_cols.size());

    MatchRect best {};
    bool found = false;
    for (size_t i = 0; i < col_limit; ++i) {
        for (const auto& mr : columns[open_cols[i]]) {
            if (mr.templ_name.find("Grey") != std::string::npos) {
                continue;
            }
            if (RoguelikeMapInfo.templ2type(theme, mr.templ_name) != RoguelikeNodeType::RogueTrader) {
                continue;
            }
            // 同属可选带时取更靠左的（更可能是当前下一跳）
            if (!found || mr.rect.x < best.rect.x) {
                best = mr;
                found = true;
            }
        }
    }
    if (!found) {
        return false;
    }
    Log.info(__FUNCTION__, "| click selectable RogueTrader at", best.rect.to_string());
    click_mizuki_node_and_set_action(best, RoguelikeNodeType::RogueTrader);

    // 层内缓存：把当前位置推进到地图上的商店节点（按 y 最近）
    if (m_map.get_num_columns() > RoguelikeMap::INIT_INDEX + 1) {
        size_t best_node = RoguelikeMap::INIT_INDEX;
        int best_dist = std::numeric_limits<int>::max();
        for (size_t i = 0; i < m_map.size(); ++i) {
            if (m_map.get_node_type(i) != RoguelikeNodeType::RogueTrader) {
                continue;
            }
            const int dist = std::abs(m_map.get_node_y(i) - best.rect.y);
            if (dist < best_dist) {
                best_dist = dist;
                best_node = i;
            }
        }
        if (best_node != RoguelikeMap::INIT_INDEX) {
            m_map.set_curr_pos(best_node);
        }
    }
    return true;
}

void asst::RoguelikeRoutingTaskPlugin::apply_mizuki_collectible_farm_costs()
{
    m_map.set_cost_fun([&](const RoguelikeNodePtr& node) {
        switch (node->type) {
        case RoguelikeNodeType::Init:
        case RoguelikeNodeType::RogueTrader:
            return 0;
        case RoguelikeNodeType::Encounter:
            return 10;
        case RoguelikeNodeType::SafeHouse:
        case RoguelikeNodeType::Boons:
        case RoguelikeNodeType::Recreation:
        case RoguelikeNodeType::LostAndFound:
        case RoguelikeNodeType::Scout:
        case RoguelikeNodeType::RegionalCommissions:
            return 20;
        case RoguelikeNodeType::CombatOps:
            return 50;
        case RoguelikeNodeType::EmergencyOps:
        case RoguelikeNodeType::DreadfulFoe:
            return 80;
        default:
            return 30;
        }
    });
    m_map.update_node_costs();
}

void asst::RoguelikeRoutingTaskPlugin::rebind_mizuki_init_to_column(size_t map_col)
{
    if (map_col >= m_map.get_num_columns()) {
        return;
    }
    // 仅在 INIT 尚无出边时绑定，避免重复 add_edge
    if (!m_map.get_node_succs(RoguelikeMap::INIT_INDEX).empty()) {
        return;
    }
    const size_t begin = m_map.get_column_begin(map_col);
    const size_t end = m_map.get_column_end(map_col);
    for (size_t node = begin; node < end; ++node) {
        m_map.add_edge(RoguelikeMap::INIT_INDEX, node);
    }
}

void asst::RoguelikeRoutingTaskPlugin::run_mizuki_collectible_farm()
{
    LogTraceFunction;

    if (m_mizuki_floor_changed) {
        m_mizuki_floor_changed = false;
        m_map.reset();
        if (m_mizuki_skip_full_scan) {
            m_need_mizuki_full_scan = false;
            Log.info(__FUNCTION__, "| floor1/NoScan: skip full map scan");
        }
        else {
            m_need_mizuki_full_scan = true;
            Log.info(__FUNCTION__, "| floor changed: require full scan");
        }
    }

    // 二层起：只放弃，不寻店（对齐投资模式进二层后 Exit）
    if (m_mizuki_abandon_floor) {
        Log.info(__FUNCTION__, "| abandon floor: set Stages_restart");
        Task.set_task_base("RoguelikeRoutingAction", "Mizuki@Roguelike@Stages_restart");
        return;
    }

    // 视野里已有商店：直接点，避免 Peek/回滑把店滑走
    if (try_click_visible_mizuki_trader()) {
        return;
    }

    // 一层：不 Peek/全图滑，交给 Stages 模板优先级（不含 Routing，避免死循环）
    if (m_mizuki_skip_full_scan) {
        Log.info(__FUNCTION__, "| NoScan floor: fallback to Stages without routing");
        Task.set_task_base("RoguelikeRoutingAction", "Mizuki@Roguelike@Stages_collectibleFarm");
        return;
    }

    int swipes_done = 0;
    const bool do_full_scan =
        m_need_mizuki_full_scan || m_map.get_num_columns() <= RoguelikeMap::INIT_INDEX + 1;

    if (do_full_scan) {
        m_map.reset();
        cv::Mat image_draw;

        // Peek + 滑动全图扫描；必须完整回滑到起点，中途不点店（远处店易误点，会停在全灰页）
        swipes_done = scan_mizuki_map_by_swipes(image_draw);
        for (int i = 0; i < swipes_done && !need_exit(); ++i) {
            ProcessTask(*this, { "RoguelikeRouting-MoveLeft" }).run();
            sleep(250);
        }
        ProcessTask(*this, { "RoguelikeRouting-MoveLeftToUnpeek" }).run();
        sleep(350);
        if (try_click_visible_mizuki_trader()) {
            m_need_mizuki_full_scan = false;
            return;
        }

#ifdef ASST_DEBUG
        if (!image_draw.empty()) {
            utils::save_debug_image(
                image_draw,
                utils::path("debug") / "roguelikeMap",
                /*auto_clean=*/true,
                /*description=*/"mizuki collectible farm full scan",
                /*suffix=*/"draw");
        }
#endif
        m_need_mizuki_full_scan = false;
        Log.info(__FUNCTION__, "| full floor scan cached, map columns:", m_map.get_num_columns());
    }
    else {
        Log.info(
            __FUNCTION__,
            "| reuse floor map, columns:",
            m_map.get_num_columns(),
            "curr:",
            m_map.get_curr_pos());
    }

    // 识别当前视野，绑定/规划下一跳
    cv::Mat image = ctrler()->get_image();
    auto view_columns = analyze_mizuki_columns(image);
    if (view_columns.empty()) {
        Log.warn(__FUNCTION__, "| view empty, fallback to Stages_collectibleFarmLocal");
        Task.set_task_base("RoguelikeRoutingAction", "Mizuki@Roguelike@Stages_collectibleFarm");
        return;
    }

    // 开局视野左列就是第一可选列；中途若同时看到「已过列+下一列」才取右列
    size_t candidate_view_idx = 0;
    size_t candidate_map_col = 0;
    if (m_map.get_curr_pos() == RoguelikeMap::INIT_INDEX) {
        candidate_view_idx = 0;
        candidate_map_col = RoguelikeMap::INIT_INDEX + 1;
        rebind_mizuki_init_to_column(candidate_map_col);
    }
    else {
        candidate_view_idx = view_columns.size() >= 2 ? 1 : 0;
        candidate_map_col = m_map.get_node_column(m_map.get_curr_pos()) + 1;
    }

    Log.info(
        __FUNCTION__,
        "| view columns:",
        view_columns.size(),
        "candidate_view:",
        candidate_view_idx,
        "candidate_map_col:",
        candidate_map_col,
        "map_columns:",
        m_map.get_num_columns(),
        "curr:",
        m_map.get_curr_pos());

    if (candidate_map_col >= m_map.get_num_columns()) {
        Log.warn(__FUNCTION__, "| past cached map end, trigger rescan next time, fallback Local");
        m_need_mizuki_full_scan = true;
        Task.set_task_base("RoguelikeRoutingAction", "Mizuki@Roguelike@Stages_collectibleFarm");
        return;
    }

    apply_mizuki_collectible_farm_costs();

    if (m_map.get_node_succs(m_map.get_curr_pos()).empty()) {
        Log.warn(__FUNCTION__, "| no successor from curr, fallback to Stages_collectibleFarmLocal");
        m_need_mizuki_full_scan = true;
        Task.set_task_base("RoguelikeRoutingAction", "Mizuki@Roguelike@Stages_collectibleFarm");
        return;
    }

    const size_t next_node = m_map.get_next_node();
    const RoguelikeNodeType next_type = m_map.get_node_type(next_node);
    const int next_y = m_map.get_node_y(next_node);
    Log.info(
        __FUNCTION__,
        "| next node",
        next_node,
        "type",
        type2name(next_type),
        "cost",
        m_map.get_node_cost(next_node),
        "y",
        next_y);

    // 地图选了商店但视野没有：再右滑找回
    if (next_type == RoguelikeNodeType::RogueTrader) {
        constexpr int kMaxSeek = 4;
        for (int i = 0; i <= kMaxSeek && !need_exit(); ++i) {
            if (try_click_visible_mizuki_trader()) {
                return;
            }
            if (i == kMaxSeek) {
                break;
            }
            ProcessTask(*this, { "RoguelikeRouting-MoveRight" }).run();
            sleep(280);
        }
        Log.warn(__FUNCTION__, "| RogueTrader planned but not found in view, fallback to Stages_collectibleFarmLocal");
        Task.set_task_base("RoguelikeRoutingAction", "Mizuki@Roguelike@Stages_collectibleFarm");
        return;
    }

    // 非商店：在候选列优先按类型匹配，再按 y 最近；灰色节点不可进，跳过
    const std::string& theme = m_config->get_theme();
    const auto& cand_col = view_columns[candidate_view_idx];
    MatchRect best {};
    int best_dist = std::numeric_limits<int>::max();
    bool typed_hit = false;
    bool found = false;
    for (const auto& mr : cand_col) {
        if (mr.templ_name.find("Grey") != std::string::npos) {
            continue;
        }
        const auto mr_type = RoguelikeMapInfo.templ2type(theme, mr.templ_name);
        const int dist = std::abs(mr.rect.y - next_y);
        if (mr_type == next_type) {
            if (!typed_hit || dist < best_dist) {
                best_dist = dist;
                best = mr;
                typed_hit = true;
                found = true;
            }
        }
        else if (!typed_hit && (!found || dist < best_dist)) {
            best_dist = dist;
            best = mr;
            found = true;
        }
    }
    if (!found) {
        Log.warn(__FUNCTION__, "| no clickable (non-grey) node in candidate column, fallback Local");
        Task.set_task_base("RoguelikeRoutingAction", "Mizuki@Roguelike@Stages_collectibleFarm");
        return;
    }
    const auto click_type = RoguelikeMapInfo.templ2type(theme, best.templ_name);
    Log.info(__FUNCTION__, "| click view node type", type2name(click_type), "map planned", type2name(next_type));
    click_mizuki_node_and_set_action(best, click_type);
    m_map.set_curr_pos(next_node);
}
