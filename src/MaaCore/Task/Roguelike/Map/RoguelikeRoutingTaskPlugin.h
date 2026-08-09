#pragma once

#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCVMat.hpp"
#include "RoguelikeMap.h"
#include "Task/Roguelike/AbstractRoguelikeTaskPlugin.h"

namespace asst
{
class RoguelikeRoutingTaskPlugin : public AbstractRoguelikeTaskPlugin
{
public:
    using AbstractRoguelikeTaskPlugin::AbstractRoguelikeTaskPlugin;
    virtual ~RoguelikeRoutingTaskPlugin() override = default;
    virtual bool verify(AsstMsg msg, const json::value& details) const override;
    virtual bool load_params(const json::value& params) override;
    virtual void reset_in_run_variables() override;

    enum class RoutingStrategy
    {
        None,
        Sarkaz_FastPass,                 // 实验模式，暂未开放给用户
        Sarkaz_FastInvestment,           // 点刺成锭分队快速投资
        JieGarden_FastPassWithBattle,    // 指挥分队一战快速投资/烧水
        JieGarden_FastPassWithoutBattle, // 指挥分队无战快速投资/烧水
        Mizuki_CollectibleFarm,          // 刷藏品：可见地图内优先走通往商店的支路
    };

protected:
    virtual bool _run() override;

private:
    /// <summary>
    /// 识别画面中节点并更新地图信息。
    /// </summary>
    /// <param name="image">截图。</param>
    /// <param name="leftmost_column">
    ///     画面最左侧节点 (忽视 init node) 所在列的 index。
    ///     按照定义，要求 leftmost_column >= 1。
    /// </param>
    /// <param name="image_draw_opt">ASST_DEBUG 模式下用于标注识别结果的截图。若为 std::nullopt
    /// 则不标注识别结果。</param> <returns> 若有新地图信息，则返回 true, 反之则返回 false。
    /// </returns>
    /// <remarks>
    /// 画面中最多同时存在三列节点。
    /// </remarks>
    bool update_map(
        const cv::Mat& image,
        size_t leftmost_column = RoguelikeMap::INIT_INDEX + 1,
        std::optional<std::reference_wrapper<cv::Mat>> image_draw_opt = std::nullopt);

    void generate_map();
    void generate_edges(
        const size_t& node,
        const cv::Mat& image,
        const int& node_x,
        std::optional<std::reference_wrapper<cv::Mat>> image_draw_opt = std::nullopt);
    void refresh_following_combat_nodes();
    void navigate_route();
    void update_selected_x();
    /// 刷藏品：每层全图扫描一次，层内复用地图选通往商店的下一跳
    void run_mizuki_collectible_farm();
    /// 连线识别失败时，按 y 距离给无前驱节点补边
    void generate_fallback_edges_by_y();
    /// 识别并过滤、分列当前画面节点
    std::vector<std::vector<MatchRect>> analyze_mizuki_columns(const cv::Mat& image) const;
    /// Peek 后滑动扫描右侧未露出列，去重合并进 m_map；返回需要回滑的次数（不含 peek）
    int scan_mizuki_map_by_swipes(cv::Mat& image_draw);
    /// 将一列节点写入 m_map（不自动接 INIT）
    void append_mizuki_column(
        const std::vector<MatchRect>& column,
        const cv::Mat& image,
        cv::Mat* image_draw);
    /// 当前画面是否已有商店；有则点击并设置进店 action
    bool try_click_visible_mizuki_trader();
    /// 点击节点并按类型设置 RoguelikeRoutingAction
    void click_mizuki_node_and_set_action(const MatchRect& node, RoguelikeNodeType type);
    /// 应用刷藏品路径代价并刷新
    void apply_mizuki_collectible_farm_costs();
    /// 将 INIT 接到指定列（先清掉 INIT 旧出边，避免重复）
    void rebind_mizuki_init_to_column(size_t map_col);

    inline static std::function<std::string(RoguelikeNodeType)> type2name = &RoguelikeMapConfig::type2name;

    // ———————— constants and variables ———————————————————————————————————————————————
    RoutingStrategy m_routing_strategy = RoutingStrategy::None;
    RoguelikeMap m_map;
    bool m_need_generate_map = true;
    /// 水月刷藏品：本层是否还需要 Peek+全图扫描（二层起换层后为 true；一层 NoScan）
    bool m_need_mizuki_full_scan = false;
    /// 当前层是否跳过全图扫描（一层 / StrategyChange=_collectibleFarmNoScan）
    mutable bool m_mizuki_skip_full_scan = true;
    /// 二层起 StrategyChange=_restart/_exit：只放弃，不再寻店
    mutable bool m_mizuki_abandon_floor = false;
    /// verify 里看到 StrategyChange/NextLevel 时置位，下次选路再应用
    mutable bool m_mizuki_floor_changed = false;
    size_t m_selected_column = 0;  // 当前选中节点所在列
    int m_selected_x = 0;          // 当前选中节点的横坐标 (Rect.x)

    int m_origin_x = 0;            // 第一列节点的默认横坐标 (Rect.x)
    int m_middle_x = 0;            // 中间列节点的默认横坐标 (Rect.x)
    int m_last_x = 0;              // 最后列节点的默认横坐标 (Rect.x)
    int m_node_width = 0;          // 节点 Rect.width
    int m_node_height = 0;         // 节点 Rect.height
    int m_column_offset = 0;       // 两列节点之间的距离
    int m_nameplate_offset = 0;    // 节点 Rect 下边缘到节点铭牌下边缘的距离
    int m_roi_margin = 0;          // roi 的 margin offset
    int m_direction_threshold = 0; // 节点间连线方向判定的阈值

    // view-related
    int m_left_most_column_x_in_view = 0;
};
}
