#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace asst::infrast
{
class FiammettaTargetChoice
{
public:
    FiammettaTargetChoice(std::vector<std::string> targets, double threshold) :
        m_targets(std::move(targets)),
        m_lowest_mood(threshold)
    {
    }

    bool consider(std::string_view name, double mood)
    {
        if (mood >= m_lowest_mood || std::find(m_targets.begin(), m_targets.end(), name) == m_targets.end() ||
            !m_seen.emplace(name).second) {
            return false;
        }
        m_name = name;
        m_lowest_mood = mood;
        return true;
    }

    const std::string& name() const noexcept { return m_name; }

    double mood() const noexcept { return m_lowest_mood; }

private:
    std::vector<std::string> m_targets;
    std::unordered_set<std::string> m_seen;
    std::string m_name;
    double m_lowest_mood = 0;
};

class DormPageProgress
{
public:
    bool reached_end(size_t new_faces) noexcept
    {
        m_stalled = new_faces == 0 ? m_stalled + 1 : 0;
        return m_stalled >= 2;
    }

private:
    size_t m_stalled = 0;
};

enum class StationPresetDormStage
{
    Prepare,
    Preset,
    Rearrange,
};

inline bool station_preset_dorm_auxiliary_enabled(bool trust, bool notstationed) noexcept
{
    return trust || notstationed;
}

inline std::vector<StationPresetDormStage> station_preset_dorm_stages(bool fiammetta, bool dorm_auxiliary)
{
    std::vector<StationPresetDormStage> result;
    if (fiammetta) {
        result.emplace_back(StationPresetDormStage::Prepare);
    }
    result.emplace_back(StationPresetDormStage::Preset);
    if (dorm_auxiliary) {
        result.emplace_back(StationPresetDormStage::Rearrange);
    }
    return result;
}
} // namespace asst::infrast
