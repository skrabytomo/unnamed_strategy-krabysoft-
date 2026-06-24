#include "FullGameSim.h"
#include "SimDB.h"
#include "../core/DevLog.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <array>
#include <sstream>
#include <iomanip>

static const char* factionName(FactionId f)
{
    switch (f) {
    case FactionId::HolyOrder:      return "HolyOrder";
    case FactionId::CrimsonWardens: return "CrimsonWardens";
    case FactionId::Thornkin:       return "Thornkin";
    case FactionId::EternalEmpire:  return "EternalEmpire";
    case FactionId::Bloodsworn:     return "Bloodsworn";
    case FactionId::Voidkin:        return "Voidkin";
    case FactionId::IronAssembly:   return "IronAssembly";
    case FactionId::Amalgamate:     return "Amalgamate";
    case FactionId::Convergence:    return "Convergence";
    default:                        return "Unknown";
    }
}

static void printUsage()
{
    printf(
        "Usage: fullgame_sim [options]\n"
        "  --games N        Games per matchup (default: 50)\n"
        "  --seed S         Base RNG seed (default: 42)\n"
        "  --max-weeks W    Max weeks per game (default: 30)\n"
        "  --all-vs-all     Run all 9x9 faction matchups (default: on)\n"
        "  --factions F1 F2 Run single matchup (faction index 0-8)\n"
        "  --snapshots      Record per-turn data in DB\n"
        "  --db PATH        Output SQLite DB path (default: fullgame_results.db)\n"
        "  --no-db          Don't write to database\n"
        "\nFaction indices: 0=HolyOrder 1=CrimsonWardens 2=Thornkin 3=EternalEmpire\n"
        "                  4=Bloodsworn 5=Voidkin 6=IronAssembly 7=Amalgamate 8=Convergence\n"
    );
}

int main(int argc, char* argv[])
{
    int      gamesPerMatchup = 50;
    uint32_t baseSeed        = 42;
    int      maxWeeks        = 30;
    bool     allVsAll        = true;
    bool     useSnapshots    = false;
    bool     useDB           = true;
    int      f1Idx           = 0;
    int      f2Idx           = 1;
    std::string dbPath       = "fullgame_results.db";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--games") == 0 && i+1 < argc)
            gamesPerMatchup = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i+1 < argc)
            baseSeed = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--max-weeks") == 0 && i+1 < argc)
            maxWeeks = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--all-vs-all") == 0)
            allVsAll = true;
        else if (std::strcmp(argv[i], "--factions") == 0 && i+2 < argc) {
            f1Idx   = std::atoi(argv[++i]);
            f2Idx   = std::atoi(argv[++i]);
            allVsAll = false;
        }
        else if (std::strcmp(argv[i], "--snapshots") == 0)
            useSnapshots = true;
        else if (std::strcmp(argv[i], "--db") == 0 && i+1 < argc)
            dbPath = argv[++i];
        else if (std::strcmp(argv[i], "--no-db") == 0)
            useDB = false;
        else if (std::strcmp(argv[i], "--help") == 0) {
            printUsage(); return 0;
        }
    }

    DevLog::setSilent(true);

    printf("=== Full AI vs AI Game Simulator ===\n");
    printf("Games per matchup: %d | Base seed: %u | Max weeks: %d\n\n",
           gamesPerMatchup, baseSeed, maxWeeks);

    SimDB db;
    if (useDB) {
        if (!db.open(dbPath)) {
            fprintf(stderr, "Warning: could not open DB '%s' — results won't be stored.\n",
                    dbPath.c_str());
            useDB = false;
        } else {
            printf("Storing results in '%s'\n\n", dbPath.c_str());
        }
    }

    FullGameSim sim;
    auto t0 = std::chrono::steady_clock::now();

    // Build list of matchups to run
    std::vector<std::pair<int,int>> matchups;
    if (allVsAll) {
        for (int i = 0; i < 9; ++i)
            for (int j = i+1; j < 9; ++j)
                matchups.emplace_back(i, j);
    } else {
        matchups.emplace_back(f1Idx, f2Idx);
    }

    int totalMatchups = static_cast<int>(matchups.size());
    int doneMatchups  = 0;

    for (auto [fi, fj] : matchups) {
        FactionId f1 = static_cast<FactionId>(fi);
        FactionId f2 = static_cast<FactionId>(fj);

        int w1 = 0, w2 = 0, draws = 0;
        int lastPct = -1;

        for (int g = 0; g < gamesPerMatchup; ++g) {
            int pct = g * 100 / gamesPerMatchup;
            if (pct != lastPct) {
                printf("\r  [%d/%d] %s vs %s: %d%%   ",
                    doneMatchups+1, totalMatchups,
                    factionName(f1), factionName(f2), pct);
                fflush(stdout);
                lastPct = pct;
            }

            FullGameSim::Config cfg;
            cfg.f1              = f1;
            cfg.f2              = f2;
            cfg.seed            = baseSeed + static_cast<uint32_t>(g * 997 + fi * 31 + fj);
            cfg.maxWeeks        = maxWeeks;
            cfg.recordSnapshots = useSnapshots;

            FullGameSim::Result res = sim.run(cfg);

            if (res.winner == 1) w1++;
            else if (res.winner == 2) w2++;
            else draws++;

            if (useDB) {
                int64_t matchId = db.insertMatch(f1, f2, res.winner, res.endWeek, cfg.seed);
                if (matchId > 0 && useSnapshots && !res.snapshots.empty())
                    db.insertSnapshots(matchId, res.snapshots);
            }
        }

        float wr1 = 100.f * w1 / gamesPerMatchup;
        float wr2 = 100.f * w2 / gamesPerMatchup;
        printf("\r  %s vs %s: %.1f%% / %.1f%% (draws: %d)%s\n",
            factionName(f1), factionName(f2), wr1, wr2, draws,
            (wr1 > 65.f || wr2 > 65.f) ? "  *** IMBALANCED" : "");

        ++doneMatchups;
    }

    auto t1  = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("\nTotal time: %.0f ms  (%.1f games/sec)\n\n",
           ms, (gamesPerMatchup * totalMatchups * 1000.0) / ms);

    if (useDB) {
        std::string report = db.buildBalanceReport();
        printf("%s\n", report.c_str());
        db.close();
    }

    return 0;
}
