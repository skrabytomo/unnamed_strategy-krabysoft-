#include "FullGameSim.h"
#include "SimDB.h"
#include "../core/DevLog.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <array>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <vector>

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
        "  --mcts           Use MCTS goal selection (slower, smarter AI)\n"
        "  --threads N      Run N games concurrently (default: 1)\n"
        "  --state-hash     Print a deterministic fingerprint of every game's\n"
        "                   final state, plus a combined digest. The digest for a\n"
        "                   given --seed must be IDENTICAL at any --threads value;\n"
        "                   if it is not, threads are sharing state that they\n"
        "                   should not. This is the primary race check on Windows,\n"
        "                   where ThreadSanitizer does not exist.\n"
        "\nFaction indices: 0=HolyOrder 1=CrimsonWardens 2=Thornkin 3=EternalEmpire\n"
        "                  4=Bloodsworn 5=Voidkin 6=IronAssembly 7=Amalgamate 8=Convergence\n"
    );
}

// One scheduled game. Built up front in a fixed order so that neither the job
// list nor the combined digest depends on thread timing.
struct Job
{
    int                 matchupIdx = 0;
    FullGameSim::Config cfg;
};

int main(int argc, char* argv[])
{
    int      gamesPerMatchup = 50;
    uint32_t baseSeed        = 42;
    int      maxWeeks        = 30;
    bool     allVsAll        = true;
    bool     useSnapshots    = false;
    bool     useDB           = true;
    bool     useMCTS         = false;
    int      numThreads      = 1;
    bool     showStateHash   = false;
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
        else if (std::strcmp(argv[i], "--mcts") == 0)
            useMCTS = true;
        else if (std::strcmp(argv[i], "--threads") == 0 && i+1 < argc)
            numThreads = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--state-hash") == 0)
            showStateHash = true;
        else if (std::strcmp(argv[i], "--help") == 0) {
            printUsage(); return 0;
        }
    }
    if (numThreads < 1) numThreads = 1;

    DevLog::setSilent(true);

    printf("=== Full AI vs AI Game Simulator ===\n");
    printf("Games per matchup: %d | Base seed: %u | Max weeks: %d | Threads: %d\n\n",
           gamesPerMatchup, baseSeed, maxWeeks, numThreads);

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

    // ── Schedule every game up front ─────────────────────────────────────────
    // The seed is derived purely from the matchup and game index (never from a
    // thread id or a clock), so which worker picks a job up cannot influence
    // its outcome.
    std::vector<Job> jobs;
    jobs.reserve(static_cast<size_t>(totalMatchups) * gamesPerMatchup);
    for (int m = 0; m < totalMatchups; ++m) {
        auto [fi, fj] = matchups[m];
        for (int g = 0; g < gamesPerMatchup; ++g) {
            Job job;
            job.matchupIdx      = m;
            job.cfg.f1              = static_cast<FactionId>(fi);
            job.cfg.f2              = static_cast<FactionId>(fj);
            job.cfg.seed            = baseSeed + static_cast<uint32_t>(g * 997 + fi * 31 + fj);
            job.cfg.maxWeeks        = maxWeeks;
            job.cfg.recordSnapshots = useSnapshots;
            job.cfg.useMCTS         = useMCTS;
            jobs.push_back(std::move(job));
        }
    }

    // ── Run them ─────────────────────────────────────────────────────────────
    // results[] is sized before any thread starts and each worker writes only
    // the slot it claimed, so no two threads ever touch the same element and no
    // reallocation can occur. Aggregation and all DB writes happen after the
    // join, on this thread.
    std::vector<FullGameSim::Result> results(jobs.size());
    std::atomic<size_t> nextJob{0};
    std::atomic<int>    completed{0};
    const int totalGames = static_cast<int>(jobs.size());

    auto worker = [&]() {
        FullGameSim sim;   // stateless, but one per thread costs nothing
        for (;;) {
            size_t i = nextJob.fetch_add(1, std::memory_order_relaxed);
            if (i >= jobs.size()) break;
            results[i] = sim.run(jobs[i].cfg);
            int n = completed.fetch_add(1, std::memory_order_relaxed) + 1;
            printf("\r  running: %d/%d games   ", n, totalGames);
            fflush(stdout);
        }
    };

    if (numThreads <= 1) {
        worker();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(numThreads);
        for (int t = 0; t < numThreads; ++t) pool.emplace_back(worker);
        for (auto& t : pool) t.join();
    }
    printf("\r%*s\r", 32, "");   // clear the progress line

    // ── Aggregate per matchup (serial, fixed order) ──────────────────────────
    for (int m = 0; m < totalMatchups; ++m) {
        auto [fi, fj] = matchups[m];
        FactionId f1 = static_cast<FactionId>(fi);
        FactionId f2 = static_cast<FactionId>(fj);

        int w1 = 0, w2 = 0, draws = 0;
        for (size_t i = 0; i < jobs.size(); ++i) {
            if (jobs[i].matchupIdx != m) continue;
            const auto& res = results[i];
            if (res.winner == 1)      w1++;
            else if (res.winner == 2) w2++;
            else                      draws++;

            if (useDB) {
                int64_t matchId = db.insertMatch(f1, f2, res.winner, res.endWeek,
                                                 jobs[i].cfg.seed,
                                                 res.combatDecided ? 1 : 0);
                if (matchId > 0 && useSnapshots && !res.snapshots.empty())
                    db.insertSnapshots(matchId, res.snapshots);
            }
        }

        float wr1 = 100.f * w1 / gamesPerMatchup;
        float wr2 = 100.f * w2 / gamesPerMatchup;
        printf("  %s vs %s: %.1f%% / %.1f%% (draws: %d)%s\n",
            factionName(f1), factionName(f2), wr1, wr2, draws,
            (wr1 > 65.f || wr2 > 65.f) ? "  *** IMBALANCED" : "");
    }

    auto t1  = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("\nTotal time: %.0f ms  (%.1f games/sec)\n\n",
           ms, (gamesPerMatchup * totalMatchups * 1000.0) / ms);

    // ── Determinism digest ───────────────────────────────────────────────────
    // Folded in job order, never completion order, so the digest is a pure
    // function of (--seed, --games, --max-weeks, --mcts). Compare it across
    // --threads values: any difference means shared state leaked between
    // threads.
    if (showStateHash) {
        uint64_t combined = 1469598103934665603ull;
        auto fold = [&combined](uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                combined ^= static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
                combined *= 1099511628211ull;
            }
        };
        for (size_t i = 0; i < results.size(); ++i) {
            fold(results[i].stateHash);
            printf("  game %-5zu seed %-10u  state %016llx\n",
                   i, jobs[i].cfg.seed,
                   static_cast<unsigned long long>(results[i].stateHash));
        }
        printf("\nSTATE-HASH %016llx  (%d games, %d threads)\n\n",
               static_cast<unsigned long long>(combined), totalGames, numThreads);
    }

    if (useDB) {
        std::string report = db.buildBalanceReport();
        printf("%s\n", report.c_str());
        db.close();
    }

    return 0;
}
