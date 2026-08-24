// patch_10_ai_adaptive_build_order.cpp
// Unnamed Strategy — AI Improvement: Dynamic Build Priority Based on Game State
// =============================================================================
// Addresses the static kBuildOrder[9] arrays in Game_WorldMap.cpp.
//
// Problem: Every AI faction follows the same rigid build order regardless of
// whether it is under siege, resource-starved, dominating, or on the ropes.
// A faction losing badly still saves for T5/T6 dwellings while its last town
// has no walls; a gold-rich faction with no Market still can't trade surplus.
//
// Fix: Before each build pass, dynamically reorder the faction priority list
// based on four game-state signals:
//   1. SIEGE THREAT    — enemy hero within 8 hexes of any owned town
//                        → boost Fort→Citadel→Castle to top priority
//   2. RESOURCE BLOCK  — have 2x+ gold but missing a key non-gold resource
//                        → boost Market (if not built) to top priority
//   3. DESPERATION     — 1 town left OR army strength < 30% of strongest rival
//                        → boost T1/T2 dwellings and cheap production buildings
//   4. DOMINANCE       — 3+ towns AND army strength > 2x strongest rival
//                        → boost T5/T6, Mage Guild T3/T4, and special buildings
//
// The dynamic reorder is a std::vector<int> copy of kBuildOrder[fIdx] that
// gets std::stable_partition'd into priority tiers. No new data structures;
// the existing build loop reads the vector unchanged.
// =============================================================================

// --- INSERT INTO: src/core/Game_WorldMap.cpp ---
// Location: inside the AI town build loop, BEFORE the kBuildOrder scan,
// around where fIdx is determined.

/*
    // ================================================================
    // PATCH 10: Adaptive build order — dynamic priority per game state
    // ================================================================
    std::vector<int> adaptiveOrder;
    if (fIdx >= 0 && fIdx < 9) {
        adaptiveOrder = kBuildOrder[fIdx]; // copy static list

        // --- Signal 1: Siege threat ---
        bool underThreat = false;
        int threatCount = 0;
        for (const auto& t : m_towns) {
            if (t.ownerId != town.ownerId) continue;
            for (const auto& eh : m_enemyHeroes) {
                if (eh.ownerId == town.ownerId) continue;
                int dist = HexGrid::distance(t.pos, eh.pos);
                if (dist <= 8) { underThreat = true; ++threatCount; }
            }
        }

        // --- Signal 2: Resource block (gold-rich, key-poor) ---
        bool resourceBlocked = false;
        int gold = buildRes.get(ResourceType::Gold);
        if (gold > 15000) {
            for (int rt = 1; rt < RESOURCE_COUNT; ++rt) {
                auto rtEnum = static_cast<ResourceType>(rt);
                if (buildRes.get(rtEnum) < 3) { resourceBlocked = true; break; }
            }
        }

        // --- Signal 3: Desperation (1 town or tiny army) ---
        int myTowns = 0;
        for (const auto& t : m_towns) if (t.ownerId == town.ownerId) ++myTowns;
        int myArmyStr = 0;
        for (const auto& h : m_heroes) {
            if (h.ownerId == town.ownerId) myArmyStr += heroStrength(h, udefs);
        }
        int bestRivalStr = 0;
        for (const auto& eh : m_enemyHeroes) {
            if (eh.ownerId == town.ownerId) continue;
            int s = heroStrength(eh, udefs);
            if (s > bestRivalStr) bestRivalStr = s;
        }
        bool desperate = (myTowns <= 1) || (bestRivalStr > 0 && myArmyStr * 3 < bestRivalStr);

        // --- Signal 4: Dominance (3+ towns, army 2x+ best rival) ---
        bool dominant = (myTowns >= 3 && bestRivalStr > 0 && myArmyStr >= bestRivalStr * 2);

        // --- Reorder: stable_partition into tiers ---
        auto isFort = [&](int bid) {
            return bid == BID::FORT || bid == BID::CITADEL || bid == BID::CASTLE;
        };
        auto isMarket = [&](int bid) {
            return bid == BID::MARKET;
        };
        auto isCheapUnit = [&](int bid) {
            // T1/T2 base dwellings for this faction
            static const int cheapIds[] = {
                BID::HO_T1_BASE, BID::CW_T1, BID::TK_T1, BID::EE_T1,
                BID::BS_T1, BID::VK_T1, BID::IA_T1, BID::AM_T1, BID::CV_T1,
                BID::HO_T2_BASE, BID::CW_T2, BID::TK_T2, BID::EE_T2,
                BID::BS_T2, BID::VK_T2, BID::IA_T2, BID::AM_T2, BID::CV_T2
            };
            for (int id : cheapIds) if (bid == id) return true;
            return false;
        };
        auto isExpensiveUnit = [&](int bid) {
            // T5/T6 and upgrade paths
            static const int expIds[] = {
                BID::HO_T5_BASE, BID::HO_T6_BASE, BID::HO_T6_A,
                BID::CW_T5, BID::CW_T5_A, BID::CW_T6, BID::CW_T6_A,
                BID::TK_T5, BID::TK_T5_A, BID::TK_T6, BID::TK_T6_A,
                BID::EE_T5, BID::EE_T5_A, BID::EE_T6, BID::EE_T6_A,
                BID::BS_T5, BID::BS_T5_A, BID::BS_T6, BID::BS_T6_A,
                BID::VK_T5, BID::VK_T5_A, BID::VK_T6, BID::VK_T6_A,
                BID::IA_T5, BID::IA_T5_A, BID::IA_T6, BID::IA_T6_A,
                BID::AM_T5, BID::AM_T5_A, BID::AM_T6, BID::AM_T6_A,
                BID::CV_T5, BID::CV_T5_A, BID::CV_T6, BID::CV_T6_A
            };
            for (int id : expIds) if (bid == id) return true;
            return false;
        };
        auto isMageGuild = [&](int bid) {
            return bid == BID::MAGE_GUILD || bid == BID::MAGE_GUILD_T2 ||
                   bid == BID::MAGE_GUILD_T3 || bid == BID::MAGE_GUILD_T4;
        };
        auto isSpecial = [&](int bid) {
            // faction-unique power buildings (Blood Altar, Blueprint Vault, etc.)
            static const int specialIds[] = {
                BID::HO_LIGHT_SHRINE, BID::HO_RELIQUARY,
                BID::CW_WARDEN_BRAND,
                BID::TK_SYMBIOSIS_WEB, BID::TK_ANCIENT_CIRCLE,
                BID::EE_NECROPOLIS, BID::EE_MONUMENT,
                BID::BS_BLOOD_ALTAR, BID::BS_WAR_SHRINE,
                BID::VK_VOID_RIFT,
                BID::IA_BLUEPRINT_VAULT, BID::IA_OVERCLOCK,
                BID::AM_MERGE_CHAMBER,
                BID::CV_SYNTHESIS_HUB
            };
            for (int id : specialIds) if (bid == id) return true;
            return false;
        };

        // Partition order: priority items float to front, deprioritized sink
        if (underThreat) {
            std::stable_partition(adaptiveOrder.begin(), adaptiveOrder.end(), isFort);
            gLog("[BUILD] P%u adaptive: siege threat (%d heroes), fortifications boosted\n",
                 town.ownerId, threatCount);
        }
        else if (desperate) {
            std::stable_partition(adaptiveOrder.begin(), adaptiveOrder.end(), isCheapUnit);
            gLog("[BUILD] P%u adaptive: desperation (%d towns, army %d vs rival %d), cheap units boosted\n",
                 town.ownerId, myTowns, myArmyStr, bestRivalStr);
        }
        else if (resourceBlocked && !town.hasBuilding(BID::MARKET)) {
            std::stable_partition(adaptiveOrder.begin(), adaptiveOrder.end(), isMarket);
            gLog("[BUILD] P%u adaptive: resource block (gold %d), Market boosted\n",
                 town.ownerId, gold);
        }
        else if (dominant) {
            std::stable_partition(adaptiveOrder.begin(), adaptiveOrder.end(),
                [&](int bid){ return isExpensiveUnit(bid) || isMageGuild(bid) || isSpecial(bid); });
            gLog("[BUILD] P%u adaptive: dominance (%d towns, army %d vs rival %d), T5/T6/Mage boosted\n",
                 town.ownerId, myTowns, myArmyStr, bestRivalStr);
        }
    }
    // ================================================================
*/

// --- MODIFY EXISTING LOOP in Game_WorldMap.cpp ---
// Location: the existing `for (int bid : kBuildOrder[fIdx])` inside the
// kMaxBuildsPerWeek loop. Replace with:
/*
    const std::vector<int>& buildList = adaptiveOrder.empty() ? kBuildOrder[fIdx] : adaptiveOrder;
    for (int bid : buildList) {
        // ... existing build logic unchanged ...
    }
*/

// =============================================================================
// Verification
// =============================================================================
// Repro: ./build/bin/unnamed_strategy --watch-ai-test=6 --seed=123
// Check logs for:
//   - "[BUILD] P<n> adaptive: siege threat" when enemy near town
//   - "[BUILD] P<n> adaptive: desperation" when 1 town or tiny army
//   - "[BUILD] P<n> adaptive: resource block" when gold-rich, key-poor
//   - "[BUILD] P<n> adaptive: dominance" when crushing
// Before: all 9 factions follow identical static order every game.
// After: build order shifts dynamically based on actual board state.
// =============================================================================
