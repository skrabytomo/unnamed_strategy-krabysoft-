// patch_08_ai_gold_spending_and_emergency_recruit.cpp
// Unnamed Strategy — AI Improvement: Spend Excess Gold / Emergency Recruit
// =============================================================================
// Addresses HANDOFF.md open issue (2026-07-20):
// "Gold-hoarding / trading when rich still unverified."
//
// Problem: AI accumulates five-figure gold treasuries but fields tiny armies
// because it only recruits during the weekly garrison phase and only builds
// until kMaxBuildsPerWeek is hit. A hero standing on a town with open
// dwellings on Day 3-6 ignores them; excess gold sits idle.
//
// Fix: After the normal build loop, if the AI still has "excess" gold
// (defined as >150% of next priority build cost OR >20k with no buildable
// target left), force immediate paid recruitment from that town into the
// standing garrison or a visiting hero. Also, if the town has a Marketplace
// and the owner is gold-rich but blocked on a key resource, trade down from
// gold even when the building isn't the very next priority — any legal build
// that moves gold -> units on the map.
// =============================================================================

// --- INSERT INTO: src/core/Game_WorldMap.cpp ---
// Location: inside the AI town build loop (around the kMaxBuildsPerWeek block),
// AFTER the market-trading pass (the existing 4:1 surplus swap) but BEFORE
// the loop ends for the week.

/*
    // ================================================================
    // PATCH 08: Emergency spend — excess gold -> troops / trades
    // ================================================================
    // Threshold: "excess" means we can afford the next priority building
    // twice over and still have 10k left, OR we have >20k and nothing in
    // the faction list is buildable this week.
    int gold = buildRes.get(ResourceType::Gold);
    bool hasExcessGold = false;
    if (gold > 20000) {
        // Check if any buildable target remains unaffordable only because
        // of non-gold resources (i.e. gold is not the blocker).
        bool nonGoldBlocked = false;
        for (int bid : kBuildOrder[fIdx]) {
            if (!town.canBuild(bid, allBuildings, m_turns.week())) continue;
            const BuildingDef* d2 = nullptr;
            for (const auto& d : allBuildings) if (d.id == bid) { d2 = &d; break; }
            if (!d2) continue;
            if (buildRes.canAfford(d2->cost)) continue; // already affordable
            bool needsNonGold = false;
            for (int rt = 1; rt < RESOURCE_COUNT; ++rt) { // skip gold (0)
                if (d2->cost.get(static_cast<ResourceType>(rt)) > buildRes.get(static_cast<ResourceType>(rt))) {
                    needsNonGold = true; break;
                }
            }
            if (!needsNonGold && d2->cost.get(ResourceType::Gold) > gold) {
                // gold-blocked, not excess
            } else if (needsNonGold) {
                nonGoldBlocked = true; break;
            }
        }
        hasExcessGold = !nonGoldBlocked;
    }

    // Also consider excess if we can afford top priority twice over
    if (!hasExcessGold && !kBuildOrder[fIdx].empty()) {
        const BuildingDef* topDef = nullptr;
        for (const auto& d : allBuildings)
            if (d.id == kBuildOrder[fIdx][0]) { topDef = &d; break; }
        if (topDef && town.canBuild(topDef->id, allBuildings, m_turns.week())) {
            int topGold = topDef->cost.get(ResourceType::Gold);
            if (gold > topGold * 2 + 10000)
                hasExcessGold = true;
        }
    }

    if (hasExcessGold) {
        // --- 8a: Force recruit everything available into garrison ---
        // (aiPaidRecruit already exists; call it with the town's pool)
        int recruited = aiPaidRecruit(town, town.garrison, buildRes, udefs);
        if (recruited > 0) {
            gLog("%s emergency recruit at %s: %d stacks into garrison (gold now %d)\n",
                 watchPlayerTown ? "Watch AI" : "AI",
                 town.name.c_str(), recruited, buildRes.get(ResourceType::Gold));
        }

        // --- 8b: If a hero is standing on this town, fill their army too ---
        for (auto& h : m_heroes) {
            if (h.pos == town.pos && h.ownerId == town.ownerId) {
                int heroRecruited = aiPaidRecruit(town, h.army, buildRes, udefs);
                if (heroRecruited > 0) {
                    gLog("%s emergency recruit at %s: %d stacks into hero %s (gold now %d)\n",
                         watchPlayerTown ? "Watch AI" : "AI",
                         town.name.c_str(), heroRecruited, h.name.c_str(),
                         buildRes.get(ResourceType::Gold));
                }
            }
        }

        // --- 8c: If still gold-heavy and Market exists, trade gold for ANY
        // missing resource that unlocks ANY buildable building, not just the
        // very next priority. This catches fallback buildings (Shipyard,
        // extra dwellings) that the priority list never reaches because a
        // cheap non-gold resource is missing.
        if (gold > 25000 && hasMarket) {
            for (const auto& d : allBuildings) {
                if (!town.canBuild(d.id, allBuildings, m_turns.week())) continue;
                if (buildRes.canAfford(d.cost)) continue; // already affordable
                // Only bother if gold is the ONLY thing we have in surplus
                bool goldOnlySurplus = true;
                for (int rt = 1; rt < RESOURCE_COUNT; ++rt) {
                    auto rtEnum = static_cast<ResourceType>(rt);
                    if (buildRes.get(rtEnum) > d.cost.get(rtEnum)) {
                        // we have surplus of this too, not gold-only
                        goldOnlySurplus = false; break;
                    }
                }
                if (!goldOnlySurplus) continue;

                // Try to buy the deficit with gold at 4:1
                constexpr int SELL_RATE = 4;
                for (int rt = 1; rt < RESOURCE_COUNT; ++rt) {
                    auto need = static_cast<ResourceType>(rt);
                    int deficit = d.cost.get(need) - buildRes.get(need);
                    if (deficit <= 0) continue;
                    int buy = std::min(deficit, buildRes.get(ResourceType::Gold) / SELL_RATE);
                    if (buy > 0) {
                        buildRes.add(ResourceType::Gold, -(buy * SELL_RATE));
                        buildRes.add(need, buy);
                        gLog("%s deep market: %d gold -> %d res%d for %s at %s\n",
                             watchPlayerTown ? "Watch AI" : "AI",
                             buy * SELL_RATE, buy, rt,
                             d.name.c_str(), town.name.c_str());
                    }
                }
                // After trading, try to build it immediately
                if (buildRes.canAfford(d.cost)) {
                    town.build(d.id, allBuildings);
                    buildRes.spend(d.cost);
                    gLog("%s emergency build: %s at %s (excess-gold triggered)\n",
                         watchPlayerTown ? "Watch AI" : "AI",
                         d.name.c_str(), town.name.c_str());
                    built = true;
                }
                if (built) break; // one emergency build per excess pass
            }
        }
    }
    // ================================================================
*/

// --- INSERT INTO: src/core/Game_WorldMap.cpp ---
// Location: inside the weekly AI reinforcement phase (where aiPaidRecruit is
// called for garrisons), AFTER the normal "best town's dwellings" recruit.
// This ensures a second sweep for any town that still has gold and open
// dwellings after the primary town has been exhausted.

/*
    // ================================================================
    // PATCH 08d: Weekly secondary recruit sweep
    // ================================================================
    // The primary reinforcement only recruits from the best town. If that
    // town's dwellings are exhausted but the AI has 20k+ gold, other towns
    // may still have available units that never get bought.
    for (auto& town : m_towns) {
        if (town.ownerId == 0) continue; // neutral
        if (town.ownerId != ownerId) continue;
        Resources& res = aiResources(ownerId);
        if (res.get(ResourceType::Gold) < 5000) continue; // not rich enough
        int before = res.get(ResourceType::Gold);
        int recruited = aiPaidRecruit(town, town.garrison, res, udefs);
        if (recruited > 0) {
            gLog("[ECONOMY] P%u secondary recruit at %s: %d stacks, gold %d -> %d\n",
                 ownerId, town.name.c_str(), recruited, before, res.get(ResourceType::Gold));
        }
    }
    // ================================================================
*/

// =============================================================================
// Verification
// =============================================================================
// Repro: ./build/bin/unnamed_strategy --watch-ai-test=6 --seed=123
// Check logs for:
//   - "emergency recruit" (should appear when a town has >20k gold and open dwellings)
//   - "deep market" (should appear when gold-heavy but blocked on iron/mercury/etc)
//   - "secondary recruit" (should appear in weekly phase for non-primary towns)
// Before patch: AI treasuries grow to 50k+ with minimal garrisons.
// After patch: excess gold converts into units on the map.
// =============================================================================
