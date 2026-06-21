#pragma once
#include <algorithm>
#include "../hero/Hero.h"   // FactionId

// ── 2-axis moral alignment ─────────────────────────────────────────────────────
//
//  Light (+)                   faction unlock grid:
//    │   HolyOrder  IronAssem  Thornkin
//    │   EtrnEmpire Convergence Voidkin    ← Convergence requires HideoutDB too
//    │   CrimWardens Amalgamate Bloodsworn
//   Dark (-)
//       Order(+) ── Neutral ── Chaos(-)
//
// Both axes clamped to [-3, +3]. Faction resolved at campaign end.

class AlignmentSystem
{
public:
    static constexpr int AXIS_MAX = 3;

    void reset() { m_order = 0; m_light = 0; }

    void apply(int orderDelta, int lightDelta)
    {
        m_order = std::clamp(m_order + orderDelta, -AXIS_MAX, AXIS_MAX);
        m_light = std::clamp(m_light + lightDelta, -AXIS_MAX, AXIS_MAX);
    }

    int orderScore() const { return m_order; }
    int lightScore() const { return m_light; }

    // Normalised to [-1, 1] for UI compass drawing
    float orderNorm() const { return static_cast<float>(m_order) / AXIS_MAX; }
    float lightNorm() const { return static_cast<float>(m_light) / AXIS_MAX; }

    // Faction unlock at campaign end.
    // convergenceEligible = HideoutDB convergence check passed.
    FactionId resolveUnlock(bool convergenceEligible = false) const
    {
        // Classify axes into: Positive(>=2), Neutral(-1..1), Negative(<=-2)
        auto cls = [](int v) -> int {
            if (v >=  2) return  1;  // Order / Light
            if (v <= -2) return -1;  // Chaos / Dark
            return 0;                // Neutral
        };

        int o = cls(m_order);
        int l = cls(m_light);

        // Grid lookup (light row, order col)
        if (l ==  1 && o ==  1) return FactionId::HolyOrder;
        if (l ==  1 && o ==  0) return FactionId::IronAssembly;
        if (l ==  1 && o == -1) return FactionId::Thornkin;
        if (l ==  0 && o ==  1) return FactionId::EternalEmpire;
        if (l ==  0 && o ==  0) return convergenceEligible
                                       ? FactionId::Convergence
                                       : FactionId::Thornkin;  // fallback neutral
        if (l ==  0 && o == -1) return FactionId::Voidkin;
        if (l == -1 && o ==  1) return FactionId::CrimsonWardens;
        if (l == -1 && o ==  0) return FactionId::Amalgamate;
        if (l == -1 && o == -1) return FactionId::Bloodsworn;

        return FactionId::Thornkin; // should never reach
    }

    // Human-readable alignment title
    const char* getTitle() const
    {
        auto cls = [](int v) -> int {
            if (v >=  2) return  1;
            if (v <= -2) return -1;
            return 0;
        };
        int o = cls(m_order), l = cls(m_light);

        if (l ==  1 && o ==  1) return "Lawful Good";
        if (l ==  1 && o ==  0) return "Neutral Good";
        if (l ==  1 && o == -1) return "Chaotic Good";
        if (l ==  0 && o ==  1) return "Lawful Neutral";
        if (l ==  0 && o ==  0) return "True Neutral";
        if (l ==  0 && o == -1) return "Chaotic Neutral";
        if (l == -1 && o ==  1) return "Lawful Evil";
        if (l == -1 && o ==  0) return "Neutral Evil";
        if (l == -1 && o == -1) return "Chaotic Evil";
        return "Unknown";
    }

    const char* getDescription() const
    {
        auto cls = [](int v) -> int {
            if (v >=  2) return  1;
            if (v <= -2) return -1;
            return 0;
        };
        int o = cls(m_order), l = cls(m_light);

        if (l ==  1 && o ==  1) return "Faith and law above all. The Order stands eternal.";
        if (l ==  1 && o ==  0) return "Machines remember. Build something that outlasts empires.";
        if (l ==  1 && o == -1) return "The wild has its own justice. Ancient and feral.";
        if (l ==  0 && o ==  1) return "Power through discipline. Death is merely a promotion.";
        if (l ==  0 && o ==  0) return "The balance holds. Neither destroyer nor saviour.";
        if (l ==  0 && o == -1) return "Entropy is freedom. The void has no allegiances.";
        if (l == -1 && o ==  1) return "Order through domination. Undeath is immortal hierarchy.";
        if (l == -1 && o ==  0) return "Neither flesh nor machine — both, and neither.";
        if (l == -1 && o == -1) return "Blood is the only currency. Drown everything else.";
        return "";
    }

private:
    int m_order = 0;
    int m_light = 0;
};
