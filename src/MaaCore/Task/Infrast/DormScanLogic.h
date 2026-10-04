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
        if (std::find(m_targets.begin(), m_targets.end(), name) == m_targets.end() ||
            !m_recognized.emplace(name).second || mood >= m_lowest_mood) {
            return false;
        }
        m_name = name;
        m_lowest_mood = mood;
        return true;
    }

    const std::string& name() const noexcept { return m_name; }

    double mood() const noexcept { return m_lowest_mood; }

    size_t recognized_count() const noexcept { return m_recognized.size(); }

    bool is_complete() const noexcept { return m_recognized.size() == m_targets.size(); }

private:
    std::vector<std::string> m_targets;
    std::unordered_set<std::string> m_recognized;
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

inline bool fiammetta_target_needs_relocation(size_t configured_target_count) noexcept
{
    return configured_target_count > 1;
}

enum class TrustPageDecision
{
    ContinueScanning,
    Exhausted,
    MoreMayRemain,
};

inline TrustPageDecision decide_trust_page(
    bool room_full,
    bool saw_full_trust,
    bool has_extra_eligible_low_trust,
    bool has_unresolved_trust) noexcept
{
    // An explicit extra eligible low-trust operator always wins: the next dorm must
    // continue the destructive low-trust pass even if 200 is also visible later.
    if (has_extra_eligible_low_trust) {
        return TrustPageDecision::MoreMayRemain;
    }

    // Trust order is ascending. A reliable 200 boundary proves that no later page can
    // contain a lower-trust operator.
    if (saw_full_trust && !has_unresolved_trust) {
        return TrustPageDecision::Exhausted;
    }

    // Once the room is full there is no reason to swipe merely to prove list exhaustion.
    // Conservatively let the next dorm retry unless this page already proved exhaustion.
    if (room_full) {
        return TrustPageDecision::MoreMayRemain;
    }

    return TrustPageDecision::ContinueScanning;
}

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
