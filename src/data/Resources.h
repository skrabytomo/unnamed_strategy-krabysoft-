#pragma once
#include <array>
#include <string>
#include <cstdint>

// ── Resource types ─────────────────────────────────────────────────────────────
enum class ResourceType : uint8_t
{
    Gold = 0,
    Iron,           // raw — all factions
    FaithStones,    // Holy Order, Crimson Wardens
    BloodEssence,   // Bloodsworn, Eternal Empire
    VerdantSap,     // Thornkin, Voidkin
    Mercury,        // Eternal Empire
    COUNT
};

static constexpr int RESOURCE_COUNT = static_cast<int>(ResourceType::COUNT);

inline const char* resourceName(ResourceType r) {
    switch (r) {
        case ResourceType::Gold:         return "Gold";
        case ResourceType::Iron:         return "Iron";
        case ResourceType::FaithStones:  return "Faith Stones";
        case ResourceType::BloodEssence: return "Blood Essence";
        case ResourceType::VerdantSap:   return "Verdant Sap";
        case ResourceType::Mercury:      return "Mercury";
        default:                         return "Unknown";
    }
}

// ── Resource pool ──────────────────────────────────────────────────────────────
struct Resources
{
    std::array<int, RESOURCE_COUNT> amounts = {};

    int  get(ResourceType r) const { return amounts[static_cast<int>(r)]; }
    void set(ResourceType r, int v) { amounts[static_cast<int>(r)] = v; }
    void add(ResourceType r, int v) { amounts[static_cast<int>(r)] += v; }
    bool sub(ResourceType r, int v) {
        if (amounts[static_cast<int>(r)] < v) return false;
        amounts[static_cast<int>(r)] -= v;
        return true;
    }

    // Check if this pool can afford a cost
    bool canAfford(const Resources& cost) const {
        for (int i = 0; i < RESOURCE_COUNT; ++i)
            if (amounts[i] < cost.amounts[i]) return false;
        return true;
    }

    // Subtract cost, returns false if insufficient
    bool spend(const Resources& cost) {
        if (!canAfford(cost)) return false;
        for (int i = 0; i < RESOURCE_COUNT; ++i)
            amounts[i] -= cost.amounts[i];
        return true;
    }

    void addAll(const Resources& other) {
        for (int i = 0; i < RESOURCE_COUNT; ++i)
            amounts[i] += other.amounts[i];
    }

    // Helper constructors
    static Resources gold(int g) {
        Resources r; r.set(ResourceType::Gold, g); return r;
    }
    static Resources make(ResourceType t, int v) {
        Resources r; r.set(t, v); return r;
    }
};
