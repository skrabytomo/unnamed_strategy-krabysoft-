#!/usr/bin/env python3
"""Unnamed Strategy — Auto-Patch v2. Targets exact patterns from the repo."""

import os, sys, re, shutil, argparse
from pathlib import Path

PATCH_BACKUP_SUFFIX = ".orig"

PATCHES = [
    {
        "name": "AI Early Game Buildup",
        "file": "src/core/Game_WorldMap.cpp",
        "search": r'gLog\s*\(\s*"P%d %s committing to march on %s \(P%d\) dist %d"',
        "insert_before": True,
        "code": """
    // === AUTO-PATCH: Early game buildup gate ===
    {
        int currentWeek = m_turns.week();
        int mineCount = 0;
        for (const auto& r : m_resources)
            if (r.ownedBy == ehi.ownerId) mineCount++;
        bool inBuildup = (currentWeek <= 3);
        bool poorEconomy = (mineCount < 4);
        if (inBuildup && poorEconomy) {
            int bestMineId = -1;
            int bestDist = 9999;
            for (const auto& r : m_resources) {
                if (r.ownedBy != 0) continue;
                int d = HexGrid::distance(ehi.pos, r.pos);
                if (d < bestDist && d <= ehi.movePool) {
                    bestDist = d;
                    bestMineId = (int)r.id;
                }
            }
            if (bestMineId >= 0) {
                gLog("[BUILDUP] P%d forced to mine %d (dist %d) before town rush\n",
                     ehi.ownerId, bestMineId, bestDist);
            }
        }
    }
    // === END AUTO-PATCH ===
"""
    },
    {
        "name": "Mine Transfer on Elimination",
        "file": "src/core/Game_Combat.cpp",
        "search": r'// Release all mines owned by the defeated hero',
        "insert_before": True,
        "code": """
    // === AUTO-PATCH: Transfer mines to conqueror on final blow ===
    {
        uint32_t conquerorId = 0;
        if (!m_heroes.empty())
            conquerorId = m_heroes[m_activeHeroIdx].ownerId;
        else if (m_currentPlayerIdx >= 0 && m_currentPlayerIdx < m_numHumanPlayers)
            conquerorId = static_cast<uint32_t>(m_currentPlayerIdx + 1);
        int minesTransferred = 0;
        for (auto& r : m_resources) {
            if (r.ownedBy == m_lastCombatEnemyId) {
                if (conquerorId != 0 && conquerorId != m_lastCombatEnemyId) {
                    r.ownedBy = conquerorId;
                    minesTransferred++;
                } else {
                    r.ownedBy = 0;
                    r.guardBeaten = false;
                }
            }
        }
        if (minesTransferred > 0)
            gLog("P%d inherits %d mines from defeated P%d\n",
                 conquerorId, minesTransferred, m_lastCombatEnemyId);
    }
    // === END AUTO-PATCH ===
"""
    },
    {
        "name": "Dynamic Town Capture Owner",
        "file": "src/core/Game_Combat.cpp",
        "search": r'captured->ownerId\s*=\s*1;',
        "replace": True,
        "code": """
    // === AUTO-PATCH: Dynamic town capture owner ===
    {
        uint32_t newOwner = 1;
        if (!m_heroes.empty())
            newOwner = m_heroes[m_activeHeroIdx].ownerId;
        else if (m_currentPlayerIdx >= 0)
            newOwner = static_cast<uint32_t>(m_currentPlayerIdx + 1);
        uint32_t prevOwner = captured->ownerId;
        captured->ownerId = newOwner;
        bool victimHasOtherTowns = false;
        for (const auto& t : m_towns)
            if (t.ownerId == prevOwner && t.id != captured->id) { victimHasOtherTowns = true; break; }
        if (!victimHasOtherTowns && prevOwner != newOwner) {
            int minesTransferred = 0;
            for (auto& r : m_resources) {
                if (r.ownedBy == prevOwner) {
                    r.ownedBy = newOwner;
                    minesTransferred++;
                }
            }
            if (minesTransferred > 0)
                gLog("P%d conquered last town of P%d -- %d mines transferred\n",
                     newOwner, prevOwner, minesTransferred);
        }
    }
    // === END AUTO-PATCH ===
"""
    },
    {
        "name": "Watch Mode Victory Condition",
        "file": "src/core/Game_Core.cpp",
        "search": r'if\s*\(\s*m_watchAiAutoExit\s*&&\s*m_watchingAI\s*\)\s*\{',
        "insert_after": True,
        "code": """
        // === AUTO-PATCH: Continue until 1 player remains ===
        {
            int aliveCount = 0;
            for (int o = 1; o <= m_setupPlayerCount; ++o) {
                bool hasHero = false, hasTown = false;
                for (const auto& h : m_enemyHeroes)
                    if (h.ownerId == static_cast<uint32_t>(o)) { hasHero = true; break; }
                for (const auto& t : m_towns)
                    if (t.ownerId == static_cast<uint32_t>(o)) { hasTown = true; break; }
                if (hasHero || hasTown) aliveCount++;
            }
            for (int pi = 0; pi < m_numHumanPlayers; ++pi) {
                bool hasHero = !m_players[pi].heroes.empty();
                bool hasTown = false;
                for (const auto& t : m_towns)
                    if (t.ownerId == static_cast<uint32_t>(pi + 1)) { hasTown = true; break; }
                if (hasHero || hasTown) aliveCount++;
            }
            if (aliveCount <= 1) {
                gLog("[WATCH-AI] game over (day %d week %d): last survivor wins\n",
                     m_turns.day(), m_turns.week());
                m_running = false;
            } else if (m_turns.week() > m_watchAiMaxWeeks && m_watchAiMaxWeeks > 0) {
                gLog("[WATCH-AI] week cap reached (%d weeks)\n", m_watchAiMaxWeeks);
                m_running = false;
            }
            continue;
        }
        // === END AUTO-PATCH ===
"""
    },
    {
        "name": "Boat Texture Loading",
        "file": "src/core/Game_Core.cpp",
        "search": r'm_heroTex\[NUM_FACTIONS\s*-\s*1\]\.load',
        "insert_before": True,
        "code": """
    // === AUTO-PATCH: Load boat texture ===
    m_boatTex.load(m_basePath + "assets/sprites/transport_boat.png", false, false);
    if (!m_boatTex.ok()) {
        gLog("WARN: Boat texture missing, using fallback\n");
    }
    // === END AUTO-PATCH ===
"""
    },
    {
        "name": "Boat Rendering",
        "file": "src/core/Game_WorldMap.cpp",
        "search": r'dl->AddImage\(\s*\(ImTextureID\)\(uintptr_t\)m_heroTex\[ehi\.faction\]\.id\(\),',
        "insert_before": True,
        "code": """
        // === AUTO-PATCH: Draw boat if hero is embarked ===
        if (ehi.isEmbarked && m_boatTex.ok()) {
            float boatScale = 1.2f;
            ImVec2 boatMin{sx - hexR * boatScale, sy - hexR * boatScale};
            ImVec2 boatMax{sx + hexR * boatScale, sy + hexR * boatScale};
            ImU32 boatTint = getPlayerColor(ehi.ownerId);
            dl->AddImage((ImTextureID)(uintptr_t)m_boatTex.id(), boatMin, boatMax,
                         ImVec2(0,0), ImVec2(1,1), boatTint);
        }
        // === END AUTO-PATCH ===
"""
    },
    {
        "name": "Score Table Largest Army",
        "file": "src/core/Game_WorldMap.cpp",
        "search": r'm_watchSummary\.push_back\(row\);',
        "insert_before": True,
        "code": """
        // === AUTO-PATCH: Compute largest army ===
        row.largestArmy = 0;
        for (const auto& h : m_enemyHeroes)
            if (h.ownerId == static_cast<uint32_t>(o)) {
                int sz = 0;
                for (const auto& s : h.army) sz += s.count;
                if (sz > row.largestArmy) row.largestArmy = sz;
            }
        for (int pi = 0; pi < m_numHumanPlayers; ++pi)
            for (const auto& h : m_players[pi].heroes)
                if (h.ownerId == static_cast<uint32_t>(o)) {
                    int sz = 0;
                    for (const auto& s : h.army) sz += s.count;
                    if (sz > row.largestArmy) row.largestArmy = sz;
                }
        for (const auto& t : m_towns)
            if (t.ownerId == static_cast<uint32_t>(o)) {
                int sz = 0;
                for (const auto& g : t.garrison) sz += g.count;
                if (sz > row.largestArmy) row.largestArmy = sz;
            }
        // === END AUTO-PATCH ===
"""
    },
    {
        "name": "UI Null Guard",
        "file": "src/core/Game_WorldMap.cpp",
        "search": r'ImGui::Begin\(\s*"World Info"\s*\)',
        "insert_after": True,
        "code": """
    // === AUTO-PATCH: Null guard for empty selection ===
    if (!m_selectedHero && m_hoveredHero < 0 && !m_showEncounterPrompt) {
        ImGui::TextUnformatted("Hover over a hero, town, or mine to see details.");
        ImGui::Text("Watch mode speed: %.1fx", m_watchAISpeed);
        ImGui::End();
        return;
    }
    // === END AUTO-PATCH ===
"""
    },
]

def apply_patch(file_path, patch, dry_run=False):
    if not file_path.exists():
        return False, f"File not found: {file_path}"
    content = file_path.read_text(encoding="utf-8")
    search_rx = re.compile(patch["search"], re.MULTILINE)
    match = search_rx.search(content)
    if not match:
        return False, f"Search pattern not found: {patch['search'][:80]}..."
    insert_code = patch["code"]
    if patch.get("replace"):
        new_content = content[:match.start()] + insert_code + content[match.end():]
    elif patch.get("insert_after"):
        end = match.end()
        new_content = content[:end] + "\n" + insert_code + content[end:]
    elif patch.get("insert_before"):
        start = match.start()
        new_content = content[:start] + insert_code + "\n" + content[start:]
    else:
        new_content = content[:match.start()] + insert_code + content[match.end():]
    if dry_run:
        return True, f"[DRY-RUN] Would patch {file_path.name}"
    backup = file_path.with_suffix(file_path.suffix + PATCH_BACKUP_SUFFIX)
    if not backup.exists():
        shutil.copy2(file_path, backup)
    file_path.write_text(new_content, encoding="utf-8")
    return True, f"Patched {file_path.name}"

def main():
    parser = argparse.ArgumentParser(description="Apply Unnamed Strategy patches v2")
    parser.add_argument("--dry-run", action="store_true", help="Preview changes")
    parser.add_argument("--repo", type=str, default=".", help="Repo root")
    args = parser.parse_args()
    root = Path(args.repo).resolve()
    applied = 0
    failed = 0
    print(f"Applying patches in: {root}")
    for patch in PATCHES:
        file_path = root / patch["file"]
        print(f"\n>>> {patch['name']}")
        ok, msg = apply_patch(file_path, patch, dry_run=args.dry_run)
        print(f"    {'OK' if ok else 'FAIL'}: {msg}")
        if ok: applied += 1
        else: failed += 1
    print(f"\n{'='*50}")
    print(f"Applied: {applied} / {len(PATCHES)}")
    print(f"Failed:  {failed}")
    if not args.dry_run and applied > 0:
        print(f"Backups: *{PATCH_BACKUP_SUFFIX}")
        print("Review:  git diff")

if __name__ == "__main__":
    main()