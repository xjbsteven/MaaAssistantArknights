#pragma once
#include <filesystem>

#include "Common/AsstInfrastDef.h"
#include "Task/InterfaceTask.h"

namespace asst
{
class ProcessTask;
class InfrastInfoTask;
class InfrastMfgTask;
class InfrastTradeTask;
class InfrastPowerTask;
class InfrastControlTask;
class InfrastReceptionTask;
class InfrastReceptionPresetTask;
class InfrastOfficeTask;
class InfrastTrainingTask;
class InfrastDormTask;
class ReplenishOriginiumShardTaskPlugin;
class InfrastProcessingTask;
class InfrastRecycleTask;
class InfrastAssistantChangeTask;
class InfrastPresetTask;
class InfrastProductionTask;

class InfrastTask final : public InterfaceTask
{
    enum class Mode
    {
        Default = 0,
        Custom = 10'000,
        Rotation = 20'000,
    };

public:
    inline static constexpr std::string_view TaskType = "Infrast";

    InfrastTask(const AsstCallback& callback, Assistant* inst);
    virtual ~InfrastTask() override = default;

    // 整轮基建入口：清空编组可用集后再跑各设施子任务
    virtual bool run() override;

    virtual bool set_params(const json::value& params) override;

private:
    bool parse_and_set_custom_config(const std::filesystem::path& path, int index);
    bool parse_station_preset_config(const std::filesystem::path& path, int index);
    bool apply_station_preset_plan(const json::object& plan);
    void append_station_preset_auxiliary_subtasks();
    void insert_collect_after_first_entry();

    std::shared_ptr<ProcessTask> m_infrast_begin_task_ptr = nullptr;
    std::shared_ptr<ProcessTask> m_collect_task_ptr = nullptr;
    std::shared_ptr<ProcessTask> m_queue_rotation_task = nullptr;
    std::shared_ptr<InfrastInfoTask> m_info_task_ptr = nullptr;
    std::shared_ptr<InfrastMfgTask> m_mfg_task_ptr = nullptr;
    std::shared_ptr<InfrastMfgTask> m_mfg_info_task_ptr = nullptr;
    std::shared_ptr<InfrastTradeTask> m_trade_task_ptr = nullptr;
    std::shared_ptr<InfrastPowerTask> m_power_task_ptr = nullptr;
    std::shared_ptr<InfrastControlTask> m_control_task_ptr = nullptr;
    std::shared_ptr<InfrastReceptionTask> m_reception_task_ptr = nullptr;
    std::shared_ptr<InfrastReceptionPresetTask> m_reception_preset_task_ptr = nullptr;
    std::shared_ptr<InfrastOfficeTask> m_office_task_ptr = nullptr;
    std::shared_ptr<InfrastProcessingTask> m_processing_task_ptr = nullptr;
    std::shared_ptr<InfrastTrainingTask> m_training_task_ptr = nullptr;
    std::shared_ptr<InfrastRecycleTask> m_recycle_task_ptr = nullptr;
    std::shared_ptr<InfrastDormTask> m_dorm_task_ptr = nullptr;
    std::shared_ptr<InfrastDormTask> m_dorm_task_ptr_post = nullptr;
    std::shared_ptr<InfrastAssistantChangeTask> m_assistant_change_task_ptr = nullptr;
    std::shared_ptr<InfrastPresetTask> m_preset_task_ptr = nullptr;
    std::shared_ptr<ReplenishOriginiumShardTaskPlugin> m_replenish_task_ptr = nullptr;
    std::shared_ptr<infrast::TaskData> m_task_data;

    bool m_collect_enabled = true;
    bool m_facility_preset_dorm_enabled = false;
    bool m_facility_preset_replenish_enabled = false;
    bool m_facility_preset_training_enabled = false;
    bool m_rotation_station_preset = false;
    bool m_station_preset_fiammetta_enabled = false;

    bool m_reception_message_board = true;
    bool m_reception_receive_clue = true;
    bool m_reception_clue_exchange = true;
    bool m_reception_send_clue = true;
};
}
