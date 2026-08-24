// patch_13_ai_artifact_economy.cpp
// Unnamed Strategy — AI Improvement: Intelligent Artifact Shopping & Equipment
// =============================================================================
// Addresses the gap between AI artifact auto-equip (shipped) and intelligent
// economic decisions around buying, selling, and prioritizing artifacts.
//
// Problem: The AI auto-equips picked-up artifacts but never visits Artifact
// Merchants or Taverns to BUY artifacts, even with six-figure gold. It also
// never sells obsolete low-tier artifacts to fund better ones. Human players
// shop every week; the AI ignores the entire economy layer.
//
// Fix: Three modules:
//   A. ARTIFACT VALUATION — score each artifact for a specific hero based on
//      class, build, and current stats. A Warrior hero values +ATK > +DEF;
//      a Mage values +mana/spell power > +ATK; a fast hero values +speed.
//   B. SHOP VISIT LOGIC — when a hero has >5000 gold and passes within 5 hexes
//      of an Artifact Merchant or a town with a Tavern, detour to shop if any
//      offered artifact scores higher than the hero's WORST equipped item.
//   C. SELL & UPGRADE — after shopping, if the hero bought a new artifact and
//      has no empty slots, sell the lowest-scoring equipped artifact for 50%
//      of its shopPrice (if the shop has buyback) or simply discard it.
// =============================================================================

// --- A. ARTIFACT VALUATION ---
// INSERT INTO: src/hero/Hero.cpp (or a new AI utility file)

/*
    // ================================================================
    // PATCH 13A: Artifact valuation for AI heroes
    // ================================================================
    float aiScoreArtifactForHero(const Artifact& art, const Hero& hero) {
        float score = 0.0f;

        // --- Base stat weights by hero class archetype ---
        float wAtk = 1.0f, wDef = 1.0f, wSpd = 0.5f, wMana = 0.3f, wSpell = 0.3f, wHp = 0.4f;

        const HeroClass& cls = hero.getClass();
        if (cls.archetype == HeroArchetype::Warrior) {
            wAtk = 2.0f; wDef = 1.5f; wSpd = 1.0f; wMana = 0.1f; wSpell = 0.1f; wHp = 1.0f;
        } else if (cls.archetype == HeroArchetype::Mage) {
            wAtk = 0.5f; wDef = 0.8f; wSpd = 0.5f; wMana = 2.5f; wSpell = 2.0f; wHp = 0.5f;
        } else if (cls.archetype == HeroArchetype::Ranger) {
            wAtk = 1.5f; wDef = 1.0f; wSpd = 2.0f; wMana = 0.3f; wSpell = 0.5f; wHp = 0.6f;
        } else if (cls.archetype == HeroArchetype::Tank) {
            wAtk = 0.8f; wDef = 2.5f; wSpd = 0.3f; wMana = 0.1f; wSpell = 0.1f; wHp = 2.0f;
        }

        // --- Flat stat bonuses ---
        score += art.bonusAttack  * wAtk;
        score += art.bonusDefense * wDef;
        score += art.bonusSpeed   * wSpd;
        score += art.bonusMana    * wMana;
        score += art.bonusSpellPower * wSpell;
        score += art.bonusHp      * wHp;

        // --- Percentage bonuses (more valuable late-game when base stats are high) ---
        int baseAtk  = hero.getBaseAttack();
        int baseDef  = hero.getBaseDefense();
        int baseMana = hero.getBaseMana();
        if (baseAtk > 0)  score += (art.pctAttack  * baseAtk  / 100.0f) * wAtk;
        if (baseDef > 0)  score += (art.pctDefense * baseDef  / 100.0f) * wDef;
        if (baseMana > 0) score += (art.pctMana    * baseMana / 100.0f) * wMana;

        // --- Special effects ---
        if (art.effect == ArtifactEffect::Regeneration)     score += 5.0f * wHp;
        if (art.effect == ArtifactEffect::ExtraMove)        score += 8.0f * wSpd;
        if (art.effect == ArtifactEffect::SpellPierce)      score += 6.0f * wSpell;
        if (art.effect == ArtifactEffect::GoldPerDay)       score += art.value * 0.02f; // 2% of daily value
        if (art.effect == ArtifactEffect::MoraleBonus)      score += 3.0f;
        if (art.effect == ArtifactEffect::LuckBonus)        score += 3.0f;

        // --- Set bonus potential ---
        // If the hero already owns 1-2 pieces of a set, the 3rd piece is extra valuable
        if (art.setId != ArtifactSetId::None) {
            int ownedInSet = 0;
            for (const auto& eq : hero.equippedArtifacts) {
                if (eq.setId == art.setId) ++ownedInSet;
            }
            if (ownedInSet == 1) score *= 1.3f; // 2nd piece
            else if (ownedInSet == 2) score *= 2.0f; // 3rd piece (completes set)
        }

        // --- Diminishing returns on stacked stats ---
        // If hero already has +20 ATK from artifacts, another +2 ATK is less valuable
        int currentAtkBonus = 0;
        for (const auto& eq : hero.equippedArtifacts) currentAtkBonus += eq.bonusAttack;
        if (currentAtkBonus > 20 && art.bonusAttack > 0) score -= art.bonusAttack * 0.3f;

        return score;
    }
    // ================================================================
*/

// --- B. SHOP VISIT LOGIC ---
// INSERT INTO: src/core/Game_WorldMap.cpp — inside the AI hero turn loop,
// AFTER normal target scoring but BEFORE movement execution.

/*
    // ================================================================
    // PATCH 13B: AI artifact shop detour logic
    // ================================================================
    // If this hero has significant gold and there's a shop within reasonable
    // detour distance, evaluate whether shopping is worth the movement cost.
    bool shouldShopThisTurn = false;
    HexCoord shopHex;
    float bestShopScore = 0.0f;

    if (aiResources(hero.ownerId).get(ResourceType::Gold) >= 5000) {
        // Check Artifact Merchants on the map
        for (const auto& obj : m_worldObjects) {
            if (obj.type != WorldObjectType::ArtifactMerchant) continue;
            int dist = HexGrid::distance(hero.pos, obj.pos);
            if (dist > 8) continue; // too far to detour

            // Evaluate each artifact the merchant offers
            for (const auto& art : obj.merchantWares) {
                float val = aiScoreArtifactForHero(art, hero);
                // Compare against worst equipped artifact (or empty slot = 0)
                float worstEquipped = 9999.0f;
                bool hasEmptySlot = (hero.equippedArtifacts.size() < hero.maxArtifactSlots);
                if (!hasEmptySlot) {
                    for (const auto& eq : hero.equippedArtifacts) {
                        float s = aiScoreArtifactForHero(eq, hero);
                        if (s < worstEquipped) worstEquipped = s;
                    }
                } else {
                    worstEquipped = 0.0f; // empty slot is "free upgrade"
                }

                if (val > worstEquipped * 1.2f && art.shopPrice <= aiResources(hero.ownerId).get(ResourceType::Gold)) {
                    float detourCost = dist * 0.5f; // opportunity cost of movement
                    float netScore = val - worstEquipped - detourCost;
                    if (netScore > bestShopScore) {
                        bestShopScore = netScore;
                        shopHex = obj.pos;
                        shouldShopThisTurn = true;
                    }
                }
            }
        }

        // Also check Tavern Specials in nearby owned towns
        for (const auto& t : m_towns) {
            if (t.ownerId != hero.ownerId) continue;
            if (!t.hasBuilding(BID::TAVERN)) continue;
            int dist = HexGrid::distance(hero.pos, t.pos);
            if (dist > 5) continue;
            for (const auto& art : t.tavernWares) {
                float val = aiScoreArtifactForHero(art, hero);
                float worstEquipped = 9999.0f;
                bool hasEmptySlot = (hero.equippedArtifacts.size() < hero.maxArtifactSlots);
                if (!hasEmptySlot) {
                    for (const auto& eq : hero.equippedArtifacts) {
                        float s = aiScoreArtifactForHero(eq, hero);
                        if (s < worstEquipped) worstEquipped = s;
                    }
                } else {
                    worstEquipped = 0.0f;
                }
                if (val > worstEquipped * 1.2f && art.shopPrice <= aiResources(hero.ownerId).get(ResourceType::Gold)) {
                    float detourCost = dist * 0.3f; // town is usually on the way
                    float netScore = val - worstEquipped - detourCost;
                    if (netScore > bestShopScore) {
                        bestShopScore = netScore;
                        shopHex = t.pos;
                        shouldShopThisTurn = true;
                    }
                }
            }
        }
    }

    if (shouldShopThisTurn) {
        // Override the hero's movement goal for this turn to shop
        hero.overrideGoal = shopHex;
        hero.overrideReason = "shop for artifact";
        gLog("[SHOP] %s detours to %d,%d for artifact (score=%.1f, gold=%d)\n",
             hero.name.c_str(), shopHex.x, shopHex.y,
             bestShopScore, aiResources(hero.ownerId).get(ResourceType::Gold));
    }
    // ================================================================
*/

// --- C. SELL & UPGRADE ---
// INSERT INTO: src/core/Game_WorldMap.cpp — inside the artifact purchase handler,
// or as a post-purchase cleanup step.

/*
    // ================================================================
    // PATCH 13C: AI artifact sell-and-equip on purchase
    // ================================================================
    void aiPurchaseArtifact(Hero& hero, const Artifact& art, Resources& res) {
        if (art.shopPrice > res.get(ResourceType::Gold)) return;

        res.add(ResourceType::Gold, -art.shopPrice);

        // If slots are full, find the worst equipped artifact to remove
        if (hero.equippedArtifacts.size() >= hero.maxArtifactSlots) {
            int worstIdx = -1;
            float worstScore = 9999.0f;
            for (size_t i = 0; i < hero.equippedArtifacts.size(); ++i) {
                float s = aiScoreArtifactForHero(hero.equippedArtifacts[i], hero);
                if (s < worstScore) { worstScore = s; worstIdx = (int)i; }
            }
            if (worstIdx >= 0) {
                const Artifact& old = hero.equippedArtifacts[worstIdx];
                gLog("[SHOP] %s sells %s (score=%.1f) to equip %s (score=%.1f)\n",
                     hero.name.c_str(), old.name.c_str(), worstScore,
                     art.name.c_str(), aiScoreArtifactForHero(art, hero));
                // Sell back for 50% if merchant supports buyback, else discard
                int sellPrice = old.shopPrice / 2;
                if (sellPrice > 0) {
                    res.add(ResourceType::Gold, sellPrice);
                    gLog("[SHOP] %s gains %d gold from selling %s\n",
                         hero.name.c_str(), sellPrice, old.name.c_str());
                }
                hero.equippedArtifacts.erase(hero.equippedArtifacts.begin() + worstIdx);
            }
        }

        hero.equippedArtifacts.push_back(art);
        gLog("[SHOP] %s buys and equips %s (cost %d, new score %.1f)\n",
             hero.name.c_str(), art.name.c_str(), art.shopPrice,
             aiScoreArtifactForHero(art, hero));
    }
    // ================================================================
*/

// =============================================================================
// Verification
// =============================================================================
// Repro: ./build/bin/unnamed_strategy --watch-ai-test=6 --seed=123
// Check logs for:
//   - "[SHOP] <hero> detours to <x>,<y> for artifact" — AI visiting merchants
//   - "[SHOP] <hero> sells <old> to equip <new>" — upgrade decisions
//   - "[SHOP] <hero> buys and equips <artifact>" — successful purchases
// Before: AI never visits artifact shops, never buys, never sells.
// After: AI shops intelligently, equips class-appropriate artifacts, upgrades.
// =============================================================================
