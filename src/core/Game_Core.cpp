#include "Game.h"
#include "../magic/SpellRegistry.h"
#include "../hero/SkillRegistry.h"
#include "../hero/LevelUpSystem.h"
#include "../world/WorldGen.h"
#include "../world/HexGrid.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>
#include <stdio.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#  include <direct.h>
#else
#  include <unistd.h>
#endif
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

static constexpr const char* HIDEOUT_PATH = "saves/hideout.db";

// ── Init ──────────────────────────────────────────────────────────────────────
bool Game::init(const std::string& title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    // Default to the desktop's native resolution instead of the fixed
    // 1280x720 the caller passes — that's a comically small, non-native box
    // on modern high-res displays, and everything looks soft/stretched once
    // the window is resized or maximized to fill the actual screen.
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
        width  = dm.w;
        height = dm.h;
    }
    m_width  = width;
    m_height = height;

    // Resolve executable directory for asset loading
    {
        char* base = SDL_GetBasePath();
        if (base) {
            m_basePath = base;
            SDL_free(base);
        }
        // Also try to chdir there so saves/ scripts/ etc. resolve correctly
        if (!m_basePath.empty()) {
#ifdef _WIN32
            _chdir(m_basePath.c_str());
#else
            chdir(m_basePath.c_str());
#endif
        }
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    m_window = SDL_CreateWindow(title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    if (!m_window) { fprintf(stderr, "Window: %s\n", SDL_GetError()); return false; }

    m_glCtx = SDL_GL_CreateContext(m_window);
    if (!m_glCtx) { fprintf(stderr, "GL context: %s\n", SDL_GetError()); return false; }
#ifdef _WIN32
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { fprintf(stderr, "GLEW init failed\n"); return false; }
#endif

    SDL_GL_SetSwapInterval(1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_camera.setViewport(width, height);
    m_camera.setPosition(0.0f, 0.0f);

    // Bring ImGui + the menu backdrop up FIRST (they need only the GL context),
    // so the ENTIRE asset load — including the hundreds of hex terrain sheets
    // right below — runs under the progress bar instead of a black window.
    if (!m_batch.init())                          { fprintf(stderr, "SpriteBatch failed\n"); return false; }
    initImGui();
    // false = GL_LINEAR (smooth) filtering — this is painterly illustration
    // art, not pixel art, so nearest-neighbor filtering just looks blocky
    // when stretched to fill the screen.
    m_menuBgTex.load(m_basePath + "assets/ui/menu_bg.png", false, false);
    m_menuHeaderEmblemTex.load(m_basePath + "assets/ui/menu_header_emblem.png", false, false);
    m_menuButtonFrameTex.load(m_basePath + "assets/ui/menu_button_frame.png", false, false);
    {
        static const char* kMenuIconFiles[NUM_MENU_ICONS] = {
            "sword", "chest", "book", "crossedswords", "eye", "gear", "compass", "door"
        };
        for (int i = 0; i < NUM_MENU_ICONS; ++i) {
            std::string rel = std::string("assets/ui/menu_icon_") + kMenuIconFiles[i] + ".png";
            m_menuIconTex[i].load(m_basePath + rel, false, false);
        }
    }
    renderLoadingScreen(0.01f, "Starting up");

    // Hex terrain sheets (the biggest early load) — driven onto the bar per type.
    if (!m_hexRenderer.init(40.0f, m_basePath,
            [this](float f){ renderLoadingScreen(0.02f + 0.12f * f, "Loading terrain"); }))
                                                  { fprintf(stderr, "HexRenderer failed\n"); return false; }
    if (!m_ui.init(width, height))                { fprintf(stderr, "UIRenderer failed\n"); return false; }
    renderLoadingScreen(0.15f, "Preparing renderer");

    // Audio next — bring music up early so it plays over the loading screen
    // (background + progress bar) while the heavier sprite art streams in. The
    // world-map track + UI sounds load first and start playing immediately; the
    // larger tracks follow and drive the "Loading music" portion of the bar.
    if (m_audio.init()) {
        m_audio.loadWav("click",   "assets/sounds/click.wav");
        m_audio.loadWav("pickup",  "assets/sounds/pickup.wav");
        m_audio.loadWav("levelup", "assets/sounds/levelup.wav");
        m_audio.loadWav("hit",     "assets/sounds/hit.wav");
        m_audio.loadWav("spell",   "assets/sounds/spell.wav");
        m_audio.loadWav("buy",     "assets/sounds/buy.wav");
        m_audio.loadWav("worldmap_music", "assets/sounds/worldmap_music.wav");
        m_audio.playMusic("worldmap_music");          // start ASAP, over the bar
        renderLoadingScreen(0.16f, "Loading music");
        m_audio.loadWav("combat_music_1", "assets/sounds/combat_music.wav");
        m_audio.loadWav("combat_music_2", "assets/sounds/combat_music_2.wav");
        m_audio.loadWav("combat_music_3", "assets/sounds/combat_music_3.wav");
        m_audio.loadWav("combat_music_4", "assets/sounds/combat_music_4.wav");
        m_audio.loadWav("town_music",     "assets/sounds/town_music.wav");
        for (int fi = 0; fi < 9; ++fi) {
            char key[32], path[64];
            std::snprintf(key,  sizeof(key),  "faction_music_%d", fi);
            std::snprintf(path, sizeof(path), "assets/sounds/faction_music_%d.wav", fi);
            m_audio.loadWav(key, path);
            renderLoadingScreen(0.18f + 0.26f * (fi + 1) / 9.0f, "Loading music");
        }
    }

    m_iconTex.load(m_basePath + "assets/icons.png", true, false);
    m_spellIconTex.load(m_basePath + "assets/icons_spells.png", true, false);
    renderLoadingScreen(0.46f, "Loading interface");

    // Per-unit sprite sheets (optional — falls back to circles if missing)
    // File: assets/sprites/faction_F_tT.png  (F=faction 0-8, T=tier 1-6)
    for (int i = 0; i < NUM_FACTIONS; ++i)
        for (int t = 0; t < NUM_UNIT_TIERS; ++t) {
            char rel[80];
            std::snprintf(rel, sizeof(rel), "assets/sprites/faction_%d_t%d.png", i, t + 1);
            if (!m_unitTex[i][t].load(m_basePath + rel, false, false) && t > 0) {
                // Missing sheet — reuse one tier lower as a fallback
                char fb[80];
                std::snprintf(fb, sizeof(fb), "assets/sprites/faction_%d_t%d.png", i, t);
                m_unitTex[i][t].load(m_basePath + fb, false, false);
                gLog("WARN: Missing sprite %s, using tier-%d fallback\n", rel, t);
            }
            // Derive actual frame count from pixel dimensions (standard sheets are ~square frames)
            if (m_unitTex[i][t].ok() && m_unitTex[i][t].height() > 0) {
                int nc = static_cast<int>(
                    std::round(static_cast<float>(m_unitTex[i][t].width()) /
                               static_cast<float>(m_unitTex[i][t].height())));
                m_unitTexCols[i][t] = std::max(1, nc);
            } else {
                m_unitTexCols[i][t] = 8;
            }
        }

    renderLoadingScreen(0.48f, "Loading unit sprites");

    // Summoned-unit sheets (skeletons, ghosts) + per-faction hero figures.
    // Frame count auto-derived from dimensions, same as unit sheets.
    auto colsOf = [](const Texture& t) {
        if (t.ok() && t.height() > 0)
            return std::max(1, (int)std::round((float)t.width() / (float)t.height()));
        return 8;
    };
    if (m_summonSkelTex.load(m_basePath + "assets/sprites/summon_skeleton.png", false, false))
        m_summonSkelCols = colsOf(m_summonSkelTex);
    if (m_summonGhostTex.load(m_basePath + "assets/sprites/summon_ghost.png", false, false))
        m_summonGhostCols = colsOf(m_summonGhostTex);
    for (int i = 0; i < NUM_FACTIONS; ++i) {
        char hp[80];
        std::snprintf(hp, sizeof(hp), "assets/sprites/hero_%d.png", i);
        if (m_heroTex[i].load(m_basePath + hp, false, false))
            m_heroTexCols[i] = colsOf(m_heroTex[i]);
    }

    // Siege art: per-faction defensive towers + attacker engines (see ART_SIEGE.md).
    // Falls back to the existing procedural placeholder (drawn in Game_Combat.cpp)
    // whenever a sheet is missing, so this is safe even if assets are incomplete.
    for (int i = 0; i < NUM_FACTIONS; ++i) {
        char rel[80];
        std::snprintf(rel, sizeof(rel), "assets/sprites/tower_%d.png", i);
        if (m_towerTex[i].load(m_basePath + rel, false, false))
            m_towerTexCols[i] = colsOf(m_towerTex[i]);
    }
    static const char* kEngineKeys[NUM_ENGINE_KEYS] = {
        "catapult", "ram", "trebuchet", "tower",
        "divine_trebuchet", "silver_trebuchet", "living_tower", "bone_crusher",
        "blood_catapult", "void_caster", "iron_ram", "iron_catapult",
        "iron_trebuchet", "flesh_drill",
    };
    for (int i = 0; i < NUM_ENGINE_KEYS; ++i) {
        char rel[96];
        std::snprintf(rel, sizeof(rel), "assets/sprites/engine_%s.png", kEngineKeys[i]);
        if (m_engineTex[i].load(m_basePath + rel, false, false))
            m_engineTexCols[i] = colsOf(m_engineTex[i]);
    }
    for (int i = 0; i < NUM_FACTIONS; ++i) {
        char rel[80];
        std::snprintf(rel, sizeof(rel), "assets/siege/wall_%d.png", i);
        m_wallTex[i].load(m_basePath + rel, false, false);
        std::snprintf(rel, sizeof(rel), "assets/siege/wall_%d_damaged.png", i);
        m_wallDamagedTex[i].load(m_basePath + rel, false, false);
        std::snprintf(rel, sizeof(rel), "assets/siege/gate_%d.png", i);
        m_gateTex[i].load(m_basePath + rel, false, false);
        std::snprintf(rel, sizeof(rel), "assets/siege/moat_%d.png", i);
        m_moatTex[i].load(m_basePath + rel, false, false);
    }

    // SDL cursors
    m_cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    m_cursorFight = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);

    renderLoadingScreen(0.56f, "Loading heroes");

    // Building registry
    m_registry.init();

    // Hero class registry
    m_classRegistry.init();

    // Artifact registry
    m_artifactRegistry.init();

    renderLoadingScreen(0.62f, "Generating world");
    startNewGame();
    m_state = GameState::MainMenu;

    // Load hero faction portraits (first idle frame of each faction's t1 sprite)
    for (int i = 0; i < NUM_FACTIONS; ++i) {
        char rel[80];
        std::snprintf(rel, sizeof(rel), "assets/portraits/faction_%d.png", i);
        m_portraitTex[i].load(m_basePath + rel, false, false);
    }

    // Load faction town art (user-provided or placeholder)
    for (int i = 0; i < NUM_FACTIONS; ++i) {
        char rel[80];
        std::snprintf(rel, sizeof(rel), "assets/towns/faction_%d.png", i);
        m_townTex[i].load(m_basePath + rel, false, false);
    }

    renderLoadingScreen(0.70f, "Loading towns");

    // Load building category icon atlas
    m_buildingIconTex.load(m_basePath + "assets/buildings/icons_buildings.png", true, false);

    // Load per-faction single-tier building art (3x3 spritesheet, row-major faction order)
    static const struct { const char* dir; const char* base; } kFactionBuildings[] = {
        { "fort",      "fort"      },
        { "market",    "market"    },
        { "town_hall", "town_hall" },
        { "city_hall", "city_hall" },
    };
    auto loadFactionTiles = [&](Texture* tex, const char* dir, const char* base) {
        for (int f = 0; f < NUM_FACTIONS; ++f) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "assets/buildings/%s/%s_f%d.png", dir, base, f);
            tex[f].load(m_basePath + buf, false, false);
        }
    };
    loadFactionTiles(m_fortTex,     "fort",      "fort");
    loadFactionTiles(m_marketTex,   "market",    "market");
    loadFactionTiles(m_townHallTex, "town_hall", "town_hall");
    loadFactionTiles(m_cityHallTex, "city_hall", "city_hall");

    // Load per-faction mage guild art: mage_guild/mage_guild_f{0-8}_t{1-4}.png
    for (int f = 0; f < NUM_FACTIONS; ++f)
        for (int t = 0; t < MAGE_GUILD_TIERS; ++t) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "assets/buildings/mage_guild/mage_guild_f%d_t%d.png", f, t + 1);
            m_mageGuildTex[f][t].load(m_basePath + buf, false, false);
        }

    // Load per-faction warehouse art: warehouse/warehouse_f{0-8}_t{1-3}.png
    for (int f = 0; f < NUM_FACTIONS; ++f)
        for (int t = 0; t < WAREHOUSE_TIERS; ++t) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "assets/buildings/warehouse/warehouse_f%d_t%d.png", f, t + 1);
            m_warehouseTex[f][t].load(m_basePath + buf, false, false);
        }

    renderLoadingScreen(0.78f, "Loading structures");

    // Load HolyOrder dwelling art (base + A/B upgrade per tier)
    // Files: assets/units/holy_order/<DwellingName>[— Variant].png
    // nullptr entries = no art uploaded yet for that variant (falls back to icon)
    static const struct { const char* base; const char* varA; const char* varB; } kHODwellings[6] = {
        { "Squire Barracks",  "Squire Barracks \xe2\x80\x94 Fast Death",  "Squire Barracks \xe2\x80\x94 Hardened"     },
        { "Paladin Hall",     "Paladin Hall \xe2\x80\x94 Arsonist",       "Paladin Hall \xe2\x80\x94 Devoted"         },
        { "Crusader Chapel",  "Crusader \xe2\x80\x94 Sacrifice",          "Crusader \xe2\x80\x94 Toxic Cloud"         },
        { nullptr,           nullptr,                                     nullptr                                    }, // T4 no art yet
        { "Holy Champion",   "Holy Champion \xe2\x80\x94 Wide Aura",     "Holy Champion \xe2\x80\x94 Unchained"      },
        { "Archangel",       "Archangel \xe2\x80\x94 Desperation",       "Archangel \xe2\x80\x94 Both Meters"        },
    };
    for (int t = 0; t < HO_DWELLING_TIERS; ++t) {
        const char* names[3] = { kHODwellings[t].base, kHODwellings[t].varA, kHODwellings[t].varB };
        for (int v = 0; v < HO_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/holy_order/%s.png", names[v]);
            m_hoDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // CrimsonWardens dwelling art
    static const struct { const char* base; const char* varA; const char* varB; } kCWDwellings[6] = {
        { "Scout Camp",     "Scout (A)",              "Scout (B)"               },
        { "Ranger Lodge",   "Ranger (A)",             "Ranger (B)"              },
        { "Hunter's Lodge", "Hunter (A)",             "Hunter (B)"              },
        { "Berserker Hall", "Berserker (A)",          "Berserker (B)"           },
        { "Warden's Tower", "Warden Commander (A)",   "Warden Commander (B)"    },
        { "Warlord's Bastion", "Warlord (A)",         "Warlord (B)"             },
    };
    for (int t = 0; t < CW_DWELLING_TIERS; ++t) {
        const char* names[3] = { kCWDwellings[t].base, kCWDwellings[t].varA, kCWDwellings[t].varB };
        for (int v = 0; v < CW_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/crimson_wardens/%s.png", names[v]);
            m_cwDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // EternalEmpire dwelling art (base + A "Eternal Command" + B "Necromantic")
    static const struct { const char* base; const char* varA; const char* varB; } kEEDwellings[6] = {
        { "Skeleton Soldier",   "Skeleton Soldier (A)",   "Skeleton Soldier (B)"   },
        { "Armoured Skeleton",  "Armoured Skeleton (A)",  "Armoured Skeleton (B)"  },
        { "Zombie Warrior",     "Zombie Warrior (A)",     "Zombie Warrior (B)"     },
        { "Death Knight",       "Death Knight (A)",       "Death Knight (B)"       },
        { "Lich",               "Lich (A)",                "Lich (B)"               },
        { "Eternal Emperor",    "Eternal Emperor (A)",    "Eternal Emperor (B)"    },
    };
    for (int t = 0; t < EE_DWELLING_TIERS; ++t) {
        const char* names[3] = { kEEDwellings[t].base, kEEDwellings[t].varA, kEEDwellings[t].varB };
        for (int v = 0; v < EE_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/eternal_empire/%s.png", names[v]);
            m_eeDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // Thornkin dwelling art
    static const struct { const char* base; const char* varA; const char* varB; } kTKDwellings[6] = {
        { "Sprout Hollow",  "Seedling Twin Hollow",   "Ironroot Den"        },
        { "Briar Thicket",  "Briar Pair Thicket",     "Thornwall Thicket"   },
        { "Vine Den",       "Vine Duo Den",           "Elder Vine Den"      },
        { "Guardian Grove", "Grove Bonded Sanctuary", "Ironwood Golem Grove"},
        { "Elder Circle",   "Ancient Pair Circle",    "World Root Circle"   },
        { "World Tree Root","Twin Thorn Canopy",      "Elder Thorn Root"    },
    };
    for (int t = 0; t < TK_DWELLING_TIERS; ++t) {
        const char* names[3] = { kTKDwellings[t].base, kTKDwellings[t].varA, kTKDwellings[t].varB };
        for (int v = 0; v < TK_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/thornkin/%s.png", names[v]);
            m_tkDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // Bloodsworn dwelling art
    static const struct { const char* base; const char* varA; const char* varB; } kBSDwellings[6] = {
        { "Bloodling Pen",    "Blood Fanatic Den",       "Pact Warrior Den"       },
        { "Berserker Pits",   "Blood Berserker Pits",    "Ritual Guard Pits"      },
        { "Shaman Hut",       "High Shaman Hut",         "Pact Shaman Hut"        },
        { "Ravager Corral",   "Blood Ravager Corral",    "Pact Ravager Corral"    },
        { "Warlord Pavilion", "Blood Avatar Pavilion",   "Ritual Champion Pavilion"},
        { "Avatar Shrine",    "Blood God Shrine",        "Pact Titan Shrine"      },
    };
    for (int t = 0; t < BS_DWELLING_TIERS; ++t) {
        const char* names[3] = { kBSDwellings[t].base, kBSDwellings[t].varA, kBSDwellings[t].varB };
        for (int v = 0; v < BS_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/bloodsworn/%s.png", names[v]);
            m_bsDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // Voidkin dwelling art
    static const struct { const char* base; const char* varA; const char* varB; } kVKDwellings[6] = {
        { "Wisp Hollow",  "Phase Wisp Hollow",  "Void Anchor Hollow"  },
        { "Phase Den",    "Flicker Den",        "Void Bulwark Den"    },
        { "Rift Arch",    "Void Sniper Arch",   "Anchor Archer Arch"  },
        { "Stalker Gate", "Phase Hunter Gate",  "Void Monolith Gate"  },
        { "Wraith Spire", "Chaos Wraith Spire", "Entropy Anchor Spire"},
        { "Colossus Rift","Void Specter Rift",  "Void Titan Rift"     },
    };
    for (int t = 0; t < VK_DWELLING_TIERS; ++t) {
        const char* names[3] = { kVKDwellings[t].base, kVKDwellings[t].varA, kVKDwellings[t].varB };
        for (int v = 0; v < VK_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/voidkin/%s.png", names[v]);
            m_vkDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // IronAssembly dwelling art
    static const struct { const char* base; const char* varA; const char* varB; } kIADwellings[6] = {
        { "Automaton Works",     "Runic Automaton Works",     "Salvage Bot Works"        },
        { "Gun Construct Bay",   "Runic Gunner Bay",          "Scrap Gunner Bay"         },
        { "Steam Walker Depot",  "Runic Walker Depot",        "Salvage Walker Depot"     },
        { "Siege Bot Foundry",   "Runic Siege Bot Foundry",   "Salvage Bot MkII Foundry" },
        { "Titan Assembly",      "Runic Titan Assembly",      "Salvage Titan Assembly"   },
        { "Colossus Prime Dock", "Runic Colossus Dock",       "Salvage Prime Dock"       },
    };
    for (int t = 0; t < IA_DWELLING_TIERS; ++t) {
        const char* names[3] = { kIADwellings[t].base, kIADwellings[t].varA, kIADwellings[t].varB };
        for (int v = 0; v < IA_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/iron_assembly/%s.png", names[v]);
            m_iaDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // Amalgamate dwelling art
    static const struct { const char* base; const char* varA; const char* varB; } kAMDwellings[6] = {
        { "Flesh Crawler Vat", "Rapid Crawler Vat",   "Fused Crawler Vat"  },
        { "Graft Soldier Bay", "Rapid Soldier Bay",   "Fused Soldier Bay"  },
        { "Bone Machine Works","Rapid Machine Works", "Fused Machine Works"},
        { "Fleshwork Forge",   "Rapid Knight Forge",  "Fused Knight Forge" },
        { "Juggernaut Pit",    "Rapid Juggernaut Pit","Fused Juggernaut Pit"},
        { "Spawn Chamber",     "Rapid Spawn Chamber", "Fused Spawn Chamber"},
    };
    for (int t = 0; t < AM_DWELLING_TIERS; ++t) {
        const char* names[3] = { kAMDwellings[t].base, kAMDwellings[t].varA, kAMDwellings[t].varB };
        for (int v = 0; v < AM_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/amalgamate/%s.png", names[v]);
            m_amDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // Convergence dwelling art
    static const struct { const char* base; const char* varA; const char* varB; } kCVDwellings[6] = {
        { "Awakening Chamber", "Mirror Awakening Chamber", "Harmony Seeker Chamber" },
        { "Synthesis Lab",     "Mirror Synthesis Lab",     "Harmony Bound Lab"      },
        { "Harmony Hall",      "Mirror Harmony Hall",      "Resonance Core Hall"    },
        { "Resonance Spire",   "Mirror Resonance Spire",   "Harmony Knight Spire"   },
        { "Transcendence Gate","Mirror Form Gate",         "Transcendent Prime Gate"},
        { "Unity Forge",       "Mirror Unity Forge",       "Harmonic Unity Forge"   },
    };
    for (int t = 0; t < CV_DWELLING_TIERS; ++t) {
        const char* names[3] = { kCVDwellings[t].base, kCVDwellings[t].varA, kCVDwellings[t].varB };
        for (int v = 0; v < CV_DWELLING_VARIANTS; ++v) {
            if (!names[v]) continue;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "assets/units/convergence/%s.png", names[v]);
            m_cvDwellingTex[t][v].load(m_basePath + buf, false, false);
        }
    }

    // Capitol buildings (one per faction)
    static const char* kCapitolNames[NUM_FACTIONS] = {
        "Sacred Sanctum", "Grand Necropolis", "Ancient Heartwood", "Eternal Citadel",
        "Bloodspire Fortress", "Void Core Nexus", "Grand Megaforge", "Grand Fleshpit", "Synthesis Nexus",
    };
    for (int f = 0; f < NUM_FACTIONS; ++f) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "assets/buildings/capitol/%s.png", kCapitolNames[f]);
        m_capitolTex[f].load(m_basePath + buf, false, false);
    }

    // Faction power/support special buildings (2 per faction)
    static const char* kPowerSpecialNames[NUM_FACTIONS] = {
        "Light Shrine", "Death Altar", "Ancient Circle", "Necropolis Gate",
        "Blood Altar", "Rift Gate", "Blueprint Vault", "Flesh Vault", "Resonance Well",
    };
    static const char* kSupportSpecialNames[NUM_FACTIONS] = {
        "Reliquary", "Warden's Brand Chamber", "Symbiosis Web", "Monument of Eternity",
        "War Shrine", "Void Lens", "Overclock Chamber", "Merge Chamber", "Mirror Chamber",
    };
    for (int f = 0; f < NUM_FACTIONS; ++f) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "assets/buildings/special/%s.png", kPowerSpecialNames[f]);
        m_powerSpecialTex[f].load(m_basePath + buf, false, false);
        std::snprintf(buf, sizeof(buf), "assets/buildings/special/%s.png", kSupportSpecialNames[f]);
        m_supportSpecialTex[f].load(m_basePath + buf, false, false);
    }
    m_bastionTex.load(m_basePath + "assets/buildings/special/Bastion.png", false, false);
    m_shipyardTex.load(m_basePath + "assets/buildings/special/Shipyard.png", false, false);

    renderLoadingScreen(0.90f, "Loading creatures");

    // Load combat board terrain backgrounds (assets/terrain/combat/NAME.png)
    static const char* kTerrainBgName[NUM_TERRAIN_TYPES] = {
        "plains", "forest", "highland", "corrupted", "toxic",
        "sacred", "industrial", "rocky", "swamp", "plains",
        "volcanic", "barren", "wasteland", "corrupted_forest", "flesh_zone",
    };
    for (int i = 0; i < NUM_TERRAIN_TYPES; ++i) {
        char rel[96];
        std::snprintf(rel, sizeof(rel), "assets/terrain/combat/%s.png", kTerrainBgName[i]);
        m_combatBgTex[i].load(m_basePath + rel, false, false);
    }

    // Wire WorldMapHUD callbacks
    m_worldHUD.init(width, height);
    m_worldHUD.setNumHumanPlayers(m_numHumanPlayers);
    if (m_iconTex.ok())
        m_worldHUD.setIconTex((ImTextureID)(uintptr_t)m_iconTex.id());
    for (int i = 0; i < NUM_FACTIONS; ++i)
        if (m_portraitTex[i].ok())
            m_worldHUD.setPortraitTex(i, (ImTextureID)(uintptr_t)m_portraitTex[i].id());
    m_worldHUD.onEndTurn     = [this]() { doEndTurn(); };
    m_worldHUD.onWorldSpells = [this]() { m_showWorldSpellPanel = !m_showWorldSpellPanel; };
    m_worldHUD.onKingdom     = [this]() { m_showKingdomPanel    = !m_showKingdomPanel; };
    m_worldHUD.onOptions     = [this]() { m_showPauseMenu       = !m_showPauseMenu; };
    m_worldHUD.onHeroClicked = [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(m_heroes.size())) {
            if (idx == m_activeHeroIdx) {
                // Second click on same hero: open/close the inspect panel
                m_showHeroInspect = !m_showHeroInspect;
                return;
            }
            m_activeHeroIdx = idx;
            const Hero& h = m_heroes[idx];
            float hx2, hy2;
            m_hexRenderer.grid().hexToWorld(h.pos, hx2, hy2);
            m_camera.setPosition(hx2, hy2);
            m_selected = {-999, -999};
            auto costFn = [this, &h](HexCoord c) -> int {
                const HexTile* t = m_map.getTile(c);
                if (!t || !h.canEnter(t->terrain)) return 999;
                return h.moveCost(t->terrain);
            };
            m_reachable = Pathfinder::reachable(m_map, h.pos, costFn, h.movePool);
        }
    };

    m_worldHUD.onTownClicked = [this](int idx) {
        // Find the idx-th player-owned town and jump to it
        int count = 0;
        for (auto& t : m_towns) {
            if (t.ownerId != static_cast<uint32_t>(currentPlayerId())) continue;
            if (count == idx) {
                float tx, ty;
                m_hexRenderer.grid().hexToWorld(t.pos, tx, ty);
                m_camera.setPosition(tx, ty);
                enterTown(&t);
                return;
            }
            ++count;
        }
    };

    // Wire TownScreen callbacks
    m_townScreen.init(width, height);
    m_townScreen.onClose = [this]() { exitTown(); };
    m_townScreen.onUpgradePathChoice = [this](int pathA, int pathB) {
        m_upgradePathA = pathA;
        m_upgradePathB = pathB;
        m_showUpgradePathPopup = true;
    };

    // Wire CombatHUD callbacks
    m_combatHUD.init(width, height);
    m_combatHUD.onWait      = [this]() { m_combat.wait(); };
    m_combatHUD.onDefend    = [this]() { m_combat.skipUnit(); };
    m_combatHUD.onEndCombat = [this]() { exitCombat(false); };
    m_combatHUD.onSpells    = [this]() { m_showSpellPanel = !m_showSpellPanel; };

    // Open hideout DB (non-fatal if it fails)
    m_hideout.open(HIDEOUT_PATH);

    // Open save DB
    m_saveDB.open("saves/saves.db");

    // Scripting
    if (m_lua.init()) {
        m_triggers.setEngine(&m_lua);
        bindLuaAPI();
        m_lua.execFile("scripts/autoload.lua");
    }

    // Map editor (ImGui already initialized above, before asset loading)
    if (m_imguiReady)
        m_editor.init(width, height);

    renderLoadingScreen(0.99f, "Finalizing");
    loadSettings();   // apply persisted volume / fullscreen settings (music already playing)

    m_running = true;
    gLog("Game initialized: %dx%d\n", width, height);
    return true;
}

// ── Menu backdrop ────────────────────────────────────────────────────────────
// Aspect-correct "cover": scale the art to fill W×H at ANY resolution without
// stretching (overflow is cropped, clipped by the framebuffer). Falls back to a
// vertical gradient if the art is missing. scrimAlpha darkens it for legibility.
void Game::drawMenuBackdrop(float W, float H, int scrimAlpha)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (m_menuBgTex.ok() && m_menuBgTex.width() > 0 && m_menuBgTex.height() > 0) {
        float iw = (float)m_menuBgTex.width(), ih = (float)m_menuBgTex.height();
        float scale = std::max(W / iw, H / ih);          // cover
        float dw = iw * scale, dh = ih * scale;
        float x = (W - dw) * 0.5f, y = (H - dh) * 0.5f;  // centre, crop overflow
        dl->AddImage((ImTextureID)(uintptr_t)m_menuBgTex.id(), ImVec2(x, y), ImVec2(x + dw, y + dh));
    } else {
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H),
            IM_COL32(20, 16, 34, 255), IM_COL32(20, 16, 34, 255),
            IM_COL32(40, 20, 14, 255), IM_COL32(40, 20, 14, 255));
    }
    if (scrimAlpha > 0)
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, scrimAlpha));
}

// ── Loading screen ──────────────────────────────────────────────────────────
// Draws one full-screen frame with the menu backdrop and a gold progress bar.
// Called between asset-load phases in init() so startup isn't a black window.
void Game::renderLoadingScreen(float progress, const char* label)
{
    if (!m_imguiReady) return;
    progress = std::clamp(progress, 0.0f, 1.0f);

    // CRITICAL: pump the OS event queue. Asset loading is a long synchronous
    // block; without servicing events the window goes "not responding", never
    // gets shown/sized (so ImGui's DisplaySize can read 0 and mis-place the menu),
    // and the window manager draws a draggable ghost. Draining + forwarding to
    // ImGui keeps the window live and its size current between load phases.
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        ImGui_ImplSDL2_ProcessEvent(&ev);
        if (ev.type == SDL_WINDOWEVENT &&
            ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            m_width  = ev.window.data1;   // logical size, matching the main loop
            m_height = ev.window.data2;
        }
    }

    // GL viewport uses the drawable (pixel) size — kept in locals so the logical
    // m_width/m_height members (used by the HUDs/camera) aren't clobbered on HiDPI.
    int fbW = m_width, fbH = m_height;
    SDL_GL_GetDrawableSize(m_window, &fbW, &fbH);
    if (fbW <= 0) fbW = 1;
    if (fbH <= 0) fbH = 1;

    glViewport(0, 0, fbW, fbH);
    glClearColor(0.04f, 0.03f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    beginImGuiFrame();
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    drawMenuBackdrop(W, H, 90);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImFont* font = ImGui::GetFont();

    // Title
    const char* title = "UNNAMED STRATEGY";
    const float ts = 34.0f;
    ImVec2 tsz = font->CalcTextSizeA(ts, 1e9f, 0.0f, title);
    dl->AddText(font, ts, ImVec2((W - tsz.x) * 0.5f, H * 0.5f - 92.0f),
                IM_COL32(240, 210, 120, 255), title);

    // Progress bar
    const float barW = std::min(560.0f, W - 120.0f);
    const float barH = 22.0f;
    const float bx = (W - barW) * 0.5f;
    const float by = H * 0.5f + 8.0f;
    dl->AddRectFilled(ImVec2(bx - 2, by - 2), ImVec2(bx + barW + 2, by + barH + 2), IM_COL32(0, 0, 0, 180), 4.0f);
    dl->AddRect(ImVec2(bx - 2, by - 2), ImVec2(bx + barW + 2, by + barH + 2), IM_COL32(120, 95, 40, 255), 4.0f, 0, 1.5f);
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + barW, by + barH), IM_COL32(30, 26, 22, 255), 3.0f);
    const float fw = barW * progress;
    if (fw > 1.0f)
        dl->AddRectFilledMultiColor(ImVec2(bx, by), ImVec2(bx + fw, by + barH),
            IM_COL32(150, 110, 40, 255), IM_COL32(232, 182, 72, 255),
            IM_COL32(232, 182, 72, 255), IM_COL32(150, 110, 40, 255));

    char pct[16]; std::snprintf(pct, sizeof(pct), "%d%%", (int)std::round(progress * 100.0f));
    ImVec2 psz = font->CalcTextSizeA(15.0f, 1e9f, 0.0f, pct);
    dl->AddText(font, 15.0f, ImVec2((W - psz.x) * 0.5f, by + barH + 8.0f), IM_COL32(232, 222, 192, 255), pct);
    if (label && *label) {
        ImVec2 lsz = font->CalcTextSizeA(15.0f, 1e9f, 0.0f, label);
        dl->AddText(font, 15.0f, ImVec2((W - lsz.x) * 0.5f, by - 26.0f), IM_COL32(200, 190, 160, 255), label);
    }

    endImGuiFrame();
    SDL_GL_SwapWindow(m_window);
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void Game::run()
{
    Uint64 prev = SDL_GetPerformanceCounter();
    const Uint64 freq = SDL_GetPerformanceFrequency();
    while (m_running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - prev) / static_cast<float>(freq);
        if (dt > 0.1f) dt = 0.1f;
        prev = now;
        m_input.beginFrame();
        processEvents();
        update(dt);
        render();
    }
}

// ── Events ────────────────────────────────────────────────────────────────────
void Game::processEvents()
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (m_imguiReady) ImGui_ImplSDL2_ProcessEvent(&e);
        m_input.handleEvent(e);

        if (e.type == SDL_QUIT) m_running = false;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            if (m_state == GameState::Town)
                exitTown();
            else if (m_state == GameState::Combat)
                exitCombat(false);
            else if (m_state == GameState::WorldMap || m_state == GameState::Campaign)
                m_showPauseMenu = !m_showPauseMenu;
            else
                m_running = false;
        }

        if (e.type == SDL_WINDOWEVENT &&
            e.window.event == SDL_WINDOWEVENT_RESIZED) {
            m_width  = e.window.data1;
            m_height = e.window.data2;
            m_camera.setViewport(m_width, m_height);
            m_ui.resize(m_width, m_height);
            m_worldHUD.resize(m_width, m_height);
            m_combatHUD.resize(m_width, m_height);
            glViewport(0, 0, m_width, m_height);
        }
    }
}

// ── Update dispatch ───────────────────────────────────────────────────────────
void Game::update(float dt)
{
    m_audio.update();
    if (m_input.keyDown(SDLK_F5)) saveGame();
    if (m_input.keyDown(SDLK_F9)) { if (m_activeSaveId) loadGame(m_activeSaveId); }
    if (m_input.keyDown(SDLK_F2)) {
        if (m_state == GameState::Editor) exitEditor();
        else enterEditor();
    }


    switch (m_state) {
        case GameState::MainMenu: updateMainMenu(dt);  break;
        case GameState::Conquest: updateConquest(dt);  break;
        case GameState::WorldMap: updateWorldMap(dt);  break;
        case GameState::Combat:   updateCombat(dt);    break;
        case GameState::Town:     updateTown(dt);      break;
        case GameState::Editor:   updateEditor(dt);    break;
        case GameState::Campaign: updateCampaign(dt);  break;
        default: break;
    }
}

// ── Render dispatch ───────────────────────────────────────────────────────────
void Game::render()
{
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.04f, 0.03f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    switch (m_state) {
        case GameState::MainMenu: renderMainMenu();  break;
        case GameState::Conquest: renderConquest();  break;
        case GameState::WorldMap: renderWorldMap();  break;
        case GameState::Combat:   renderCombat();    break;
        case GameState::Town:     renderTown();      break;
        case GameState::Editor:   renderEditor();    break;
        case GameState::Campaign: renderCampaign();  break;
        default: break;
    }

    SDL_GL_SwapWindow(m_window);
}

// ── Faction name helper (mirrors factionShortName in Game_MainMenu.cpp) ──────
static const char* factionNameStr(int f) {
    switch (f) {
    case 0: return "Holy Order";     case 1: return "Crimson Wardens";
    case 2: return "Thornkin";       case 3: return "Eternal Empire";
    case 4: return "Bloodsworn";     case 5: return "Voidkin";
    case 6: return "Iron Assembly";  case 7: return "Amalgamate";
    case 8: return "Convergence";    default: return "Unknown";
    }
}

// ── Save / Load ───────────────────────────────────────────────────────────────
void Game::saveGame(const std::string& customName)
{
    if (!m_saveDB.isOpen()) return;

    GameSaveData data = SaveLoad::packState(
        m_map, m_heroes, m_enemyHeroes,
        m_players.empty() ? std::vector<Hero>{} : m_players[0].defeatedPool,
        m_towns, m_worldObjects, m_resources, m_nextObjId,
        m_playerResources,
        m_turns.day(), m_turns.week(),
        m_mapSize,
        m_newGameDifficulty, m_activeHeroIdx);
    data.enemyResourceAmounts = m_enemyResources.amounts;
    data.campaign = m_campaign.toSaveState();

    // N-player hotseat — pack all player states
    data.numHumanPlayers  = m_numHumanPlayers;
    data.currentPlayerIdx = m_currentPlayerIdx;
    if (m_numHumanPlayers >= 2) {
        data.playerStates.resize(m_numHumanPlayers);
        for (int pi = 0; pi < m_numHumanPlayers; ++pi) {
            auto& ps = data.playerStates[pi];
            if (pi == m_currentPlayerIdx) {
                ps.heroes          = SaveLoad::packHeroes(m_heroes);
                ps.resourceAmounts = m_playerResources.amounts;
                ps.activeHeroIdx   = m_activeHeroIdx;
            } else {
                ps.heroes          = SaveLoad::packHeroes(m_players[pi].heroes);
                ps.resourceAmounts = m_players[pi].resources.amounts;
                ps.activeHeroIdx   = m_players[pi].activeHeroIdx;
            }
            if (pi < (int)m_players.size())
                ps.defeatedHeroes = SaveLoad::packHeroes(m_players[pi].defeatedPool);
        }
    }

    std::string jsonStr = SaveLoad::saveGameToString(data);
    if (jsonStr.empty()) { fprintf(stderr, "Save serialization failed\n"); return; }

    bool isCampaign = data.campaign.active;
    std::string heroName   = (m_heroes.empty() || m_heroes[0].name.empty()) ? "Save" : m_heroes[0].name;
    std::string factionStr = m_heroes.empty() ? "" : factionNameStr(static_cast<int>(m_heroes[0].faction));

    // Auto-generate name if not provided
    std::string name = customName;
    if (name.empty()) {
        if (isCampaign) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Campaign - Week %d", m_turns.week());
            name = buf;
        } else {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s - Week %d", heroName.c_str(), m_turns.week());
            name = buf;
        }
    }

    m_activeSaveId = m_saveDB.upsert(m_activeSaveId, name, jsonStr, isCampaign,
                                     data.campaign.missionIdx,
                                     heroName, factionStr,
                                     data.day, data.week);
    if (m_activeSaveId)
        gLog("Game saved (id %lld, \"%s\")\n", (long long)m_activeSaveId, name.c_str());
    else
        fprintf(stderr, "Save DB write failed\n");
}

Hero* Game::currentActiveHero()
{
    // m_heroes always holds the current player's roster (N-player handoff swaps it).
    if (m_heroes.empty()) return nullptr;
    return &m_heroes[m_activeHeroIdx];
}
const Hero* Game::currentActiveHero() const
{
    return const_cast<Game*>(this)->currentActiveHero();
}

bool Game::loadGameApply(GameSaveData& data)
{
    m_mapSize = static_cast<MapSize>(data.mapSizeEnum);
    m_map.create(m_mapSize);

    int day = 1, week = 1;
    std::vector<Hero> tempP1Defeated;
    SaveLoad::unpackState(data, m_map, m_heroes, m_enemyHeroes, tempP1Defeated,
                          m_towns, m_worldObjects, m_resources, m_nextObjId,
                          m_playerResources, day, week);
    for (const auto& wo : m_worldObjects)
        if (wo.type == WorldObjectType::Barrier && !wo.collected)
            if (HexTile* t = m_map.getTile(wo.pos)) t->blocked = true;

    m_newGameDifficulty = data.difficulty;

    // AI team pool (v5+). Older saves carry no pool — seed the new-game
    // default so the fair-economy AI isn't broke on a legacy load.
    m_enemyResources = Resources{};
    m_enemyResources.amounts = data.enemyResourceAmounts;
    if (data.version < 5) {
        int aiStarts = std::max(1, (int)m_enemyHeroes.size());
        m_enemyResources.set(ResourceType::Gold, 5000 * aiStarts);
        m_enemyResources.set(ResourceType::Iron,   20 * aiStarts);
    }

    m_activeHeroIdx = (!m_heroes.empty())
        ? std::min(data.activeHeroIdx, (int)m_heroes.size() - 1)
        : 0;
    if (!m_heroes.empty()) {
        float hx, hy;
        m_hexRenderer.grid().hexToWorld(m_heroes[m_activeHeroIdx].pos, hx, hy);
        m_camera.setPosition(hx, hy);
    }

    for (auto& t : m_towns)
        if (HexTile* tile = m_map.getTile(t.pos)) tile->townId = t.id;

    // Restore turn counter
    m_turns.setDayWeek(day, week);

    // Rebuild road network (was lost when map was recreated)
    m_roadHexes.clear();
    if (m_towns.size() >= 2) {
        auto roadCost = [this](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            return (t && t->terrain != Terrain::Water) ? 1 : 999;
        };
        for (size_t ti = 0; ti < m_towns.size(); ++ti)
            for (size_t tj = ti + 1; tj < m_towns.size(); ++tj) {
                auto path = Pathfinder::find(m_map, m_towns[ti].pos, m_towns[tj].pos, roadCost);
                for (auto& h : path) m_roadHexes.insert(h);
                m_roadHexes.insert(m_towns[ti].pos);
                m_roadHexes.insert(m_towns[tj].pos);
            }
    }

    m_moveT    = 1.0f;
    m_state    = GameState::WorldMap;
    m_selected = {-999,-999};
    m_reachable.clear();

    // Clear any transient UI / encounter state that must not survive a load
    m_showEncounterPrompt    = false;
    m_showVictory            = false;
    m_showDefeat             = false;
    m_finalDefeat            = false;
    m_showCombatResult       = false;
    m_showWeekSummary        = false;
    m_pendingMineId          = 0;
    m_pendingNeutralOutpostId= 0;
    m_pendingTownCaptureId   = 0;
    m_pendingTownDefenseId   = 0;
    m_defenseAttackerId      = 0;
    m_showDefensePrepPopup   = false;
    m_siegePrepChoice        = -1;
    m_watchMovedThisDay.clear();
    m_lastBanditCampId       = 0;
    m_pendingCryptId         = 0;
    m_pendingPandoraId       = 0;
    m_pendingUtopiaId        = 0;
    m_lastCombatEnemyId      = 0;

    if (data.campaign.active) {
        m_campaign.init();
        m_campaign.fromSaveState(data.campaign);
        m_state = GameState::Campaign;
    }

    // N-player hotseat — restore all player states
    m_numHumanPlayers  = data.numHumanPlayers;
    m_currentPlayerIdx = data.currentPlayerIdx;
    // Hot-seat isn't stored explicitly in saves; any 2+ human game IS hot-seat.
    m_hotSeatMode      = (m_numHumanPlayers >= 2);
    m_hotSeatHandoff   = false;
    m_players.assign(m_numHumanPlayers, PlayerState{});
    m_playerNotifs.assign(m_numHumanPlayers, PlayerNotifs{});

    if (!data.playerStates.empty()) {
        int numPs = std::min((int)data.playerStates.size(), m_numHumanPlayers);
        for (int pi = 0; pi < numPs; ++pi) {
            auto& ps = data.playerStates[pi];
            m_players[pi].heroes            = SaveLoad::unpackHeroes(ps.heroes);
            m_players[pi].resources.amounts = ps.resourceAmounts;
            m_players[pi].activeHeroIdx     = ps.activeHeroIdx;
            m_players[pi].defeatedPool      = SaveLoad::unpackHeroes(ps.defeatedHeroes);
        }
    } else if (m_numHumanPlayers >= 2) {
        // Legacy v3 fallback
        auto backupHeroes = SaveLoad::unpackHeroes(data.p2Heroes);
        Resources backupRes; backupRes.amounts = data.p2ResourceAmounts;
        if (m_currentPlayerIdx == 0) {
            m_players[0].heroes = m_heroes; m_players[0].resources = m_playerResources;
            m_players[0].activeHeroIdx = m_activeHeroIdx;
            m_players[1].heroes = std::move(backupHeroes); m_players[1].resources = std::move(backupRes);
            m_players[1].activeHeroIdx = data.p2ActiveHeroIdx;
        } else {
            m_players[1].heroes = m_heroes; m_players[1].resources = m_playerResources;
            m_players[1].activeHeroIdx = m_activeHeroIdx;
            m_players[0].heroes = std::move(backupHeroes); m_players[0].resources = std::move(backupRes);
            m_players[0].activeHeroIdx = data.p2ActiveHeroIdx;
        }
        m_players[0].defeatedPool = std::move(tempP1Defeated);
        m_players[1].defeatedPool = SaveLoad::unpackHeroes(data.p2DefeatedHeroes);
    } else {
        m_players[0].defeatedPool = std::move(tempP1Defeated);
    }

    m_worldHUD.setCurrentPlayerId(currentPlayerId());
    m_worldHUD.setNumHumanPlayers(m_numHumanPlayers);

    {
        uint32_t maxId = 299;
        auto scanHero = [&](const Hero& h){ if (h.id > maxId) maxId = h.id; };
        for (const auto& h : m_heroes)      scanHero(h);
        for (const auto& h : m_enemyHeroes) scanHero(h);
        for (int pi = 0; pi < m_numHumanPlayers; ++pi) {
            for (const auto& h : m_players[pi].heroes)      scanHero(h);
            for (const auto& h : m_players[pi].defeatedPool) scanHero(h);
        }
        m_nextHeroId = maxId + 1;
    }

    {
        uint32_t cid = static_cast<uint32_t>(currentPlayerId());
        m_cachedWeeklyIncome = m_turns.calculateWeeklyIncome(m_towns, cid);
        for (const auto& r : m_resources)
            if (r.ownedBy == cid) m_cachedWeeklyIncome.add(r.type, mineYield(r));
    }

    gLog("Game loaded (day %d week %d)\n", day, week);
    return true;
}

bool Game::loadGame(int64_t saveId)
{
    if (!m_saveDB.isOpen()) return false;
    std::string jsonStr;
    if (!m_saveDB.load(saveId, jsonStr)) {
        fprintf(stderr, "LoadGame: row %lld not found\n", (long long)saveId);
        return false;
    }
    GameSaveData data;
    if (!SaveLoad::loadGameFromString(jsonStr, data)) {
        fprintf(stderr, "LoadGame: JSON parse failed for id %lld\n", (long long)saveId);
        return false;
    }
    m_activeSaveId = saveId;
    return loadGameApply(data);
}

bool Game::loadGameFile(const std::string& path)
{
    GameSaveData data;
    if (!SaveLoad::loadGame(path, data)) {
        fprintf(stderr, "Load failed: %s\n", path.c_str());
        return false;
    }
    m_activeSaveId = 0; // file load doesn't have a DB id
    return loadGameApply(data);
}

// ── New game (reset + world gen) ──────────────────────────────────────────────
void Game::startNewGame()
{
    // Clear all runtime state
    m_heroes.clear();
    m_enemyHeroes.clear();
    m_towns.clear();
    m_resources.clear();
    m_worldObjects.clear();
    m_heroMapAnimators.clear();
    m_pickupEffects.clear();
    m_reachable.clear();
    m_activeHeroIdx   = 0;
    m_moveT           = 1.0f;
    m_selected        = {-999, -999};
    m_hovered         = {-999, -999};
    m_nextObjId       = 1;
    m_nextHeroId      = 300;
    m_turns           = TurnManager{};
    m_playerResources = Resources{};
    m_showVictory     = false;
    m_showDefeat      = false;
    m_finalDefeat     = false;
    m_mageGuildT4BonusGiven.clear();
    m_showCapturePopup = false;
    m_showTownLostPopup = false;
    m_showCombatResult = false;

    // Reset campaign state so leftover campaign data doesn't affect skirmish
    m_campaign.reset();

    // ── Resolve the setup slots (HoMM-style lobby) ────────────────────────────
    // Slot 0 = you. Humans are packed first (owner ids 1..N), bots follow —
    // downstream code (towns, AI start index) relies on that ordering.
    int  slotCount = std::clamp(m_setupPlayerCount, 2, 4);
    int  sortedFaction[4], sortedBonus[4];
    int  numHumansResolved = 0;
    {
        m_slotType[0] = 0;  // slot 0 is always you
        uint32_t frng = static_cast<uint32_t>(SDL_GetTicks()) ^ 0xC0FFEE11u;
        auto resolveFac = [&](int f) {
            if (f >= 0 && f <= 8) return f;
            frng = frng * 1664525u + 1013904223u;
            return static_cast<int>(frng % 9);
        };
        int idx = 0;
        for (int s = 0; s < slotCount; ++s)         // humans first
            if (m_slotType[s] == 0) {
                sortedFaction[idx] = resolveFac(m_slotFaction[s]);
                sortedBonus[idx]   = m_slotBonus[s];
                ++idx;
            }
        numHumansResolved = idx;
        for (int s = 0; s < slotCount; ++s)         // bots after
            if (m_slotType[s] != 0) {
                sortedFaction[idx] = resolveFac(m_slotFaction[s]);
                sortedBonus[idx]   = m_slotBonus[s];
                ++idx;
            }
        // Keep the legacy fields coherent with the slots
        m_newGameFaction = sortedFaction[0];
        m_newGameHotSeat = (numHumansResolved >= 2);
        if (numHumansResolved >= 2) m_p2Faction = sortedFaction[1];
    }

    // Multiplayer reset — resolved here (before m_players is sized) so world
    // gen, m_players.assign(), and AI start-index math all see the final count.
    m_numHumanPlayers      = numHumansResolved;
    m_currentPlayerIdx     = 0;
    m_players.assign(m_numHumanPlayers, PlayerState{});
    m_playerNotifs.assign(m_numHumanPlayers, PlayerNotifs{});
    m_showPlayerTurnBanner = false;
    m_playerTurnBannerT    = 0.0f;

    // Generate world procedurally using selected settings
    static constexpr MapSize kMapSizes[] = {
        MapSize::Small, MapSize::Medium, MapSize::Large, MapSize::XLarge
    };
    m_mapSize = kMapSizes[std::clamp(m_newGameMapSize, 0, 3)];
    m_map.create(m_mapSize);

    WorldGenParams wgp;
    wgp.seed        = static_cast<uint32_t>(SDL_GetTicks()) ^ 0x5A5A5A5Au;
    wgp.size        = m_mapSize;
    wgp.playerCount = slotCount;   // exactly as many zones/towns as setup slots
    wgp.waterRatio  = 0.18f;
    auto wgResult   = WorldGen::generate(m_map, wgp);

    // Every player zone's town takes its slot's faction — enemy heroes, hall
    // pre-builds, terrain painting, and faction mines all read town.faction.
    for (int ti = 0; ti < (int)wgResult.towns.size() && ti < slotCount; ++ti)
        wgResult.towns[ti].faction =
            static_cast<FactionId>(std::clamp(sortedFaction[ti], 0, 8));

    m_resources = std::move(wgResult.resources);
    m_nextObjId = static_cast<uint32_t>(m_resources.size()) + 1;

    // Starting resources scale with difficulty: the AI always gets a full
    // player share (5000g + 20 iron per AI hero); the human starts with
    // 100% / 90% / 80% of that on Easy / Normal / Hard.
    {
        static const int kStartPct[3] = {100, 90, 80};
        int sp = kStartPct[std::clamp(m_newGameDifficulty, 0, 2)];
        m_playerResources.set(ResourceType::Gold, 5000 * sp / 100);
        m_playerResources.set(ResourceType::Iron,   20 * sp / 100);
    }

    static constexpr FactionId kFactions[] = {
        FactionId::HolyOrder, FactionId::CrimsonWardens, FactionId::Thornkin,
        FactionId::EternalEmpire, FactionId::Bloodsworn, FactionId::Voidkin,
        FactionId::IronAssembly, FactionId::Amalgamate, FactionId::Convergence
    };
    static constexpr int kFactionStartSpell[] = {
        SPL::BLESS, SPL::BLOOD_FRENZY, SPL::ENTANGLE,
        SPL::CURSE, SPL::BLOOD_FRENZY, SPL::ENTANGLE,
        SPL::REINFORCE, SPL::MEND_FLESH, SPL::BLESS
    };
    static const char* kFactionHeroNames[] = {
        "Alara", "Dren", "Korvas", "Mira", "Seld",
        "Thayne", "Vex", "Lyra", "Cael"
    };
    int fi = std::clamp(m_newGameFaction, 0, 8);

    Hero hero;
    hero.id       = 1;
    hero.name     = kFactionHeroNames[fi];
    hero.faction  = kFactions[fi];
    hero.pos      = wgResult.startPositions.empty() ? HexCoord{0,0}
                                                    : wgResult.startPositions[0];
    hero.movePool = hero.maxMove;
    hero.lightPower = (fi == 0 || fi == 8) ? 3 : 0;
    hero.knownSpells = { kFactionStartSpell[fi] };

    // Assign chosen hero class (or first available for faction)
    {
        const HeroClassDef* chosenCls = nullptr;
        if (m_newGameClassId != 0)
            chosenCls = m_classRegistry.getClass(m_newGameClassId);
        if (!chosenCls) {
            auto cls4fac = m_classRegistry.getClassesForFaction(hero.faction);
            if (!cls4fac.empty()) chosenCls = cls4fac[0];
        }
        if (chosenCls) {
            hero.classId = chosenCls->id;
            hero.efficientSpecialty  = (chosenCls->specialty == SpecialtyType::Efficient);
            hero.bloodScentSpecialty = (chosenCls->specialty == SpecialtyType::BloodScent);
            hero.infestationSpecialty = (chosenCls->specialty == SpecialtyType::Infestation);
            hero.ghostWalkSpecialty   = (chosenCls->specialty == SpecialtyType::GhostWalk);
            hero.blightAuraSpecialty  = (chosenCls->specialty == SpecialtyType::BlightAura);
            // Grant first skill from class pool at Basic tier
            if (!chosenCls->skillPool.empty()) {
                int startSkillId = chosenCls->skillPool[0];
                hero.skills.learn(startSkillId);
                // Apply immediate world-map bonuses for passive skills
                if (const SkillDef* sd = findSkillDef(startSkillId)) {
                    int v = sd->values[0]; // Basic tier value
                    if (sd->effectType == SkillEffectType::MovementBonus) {
                        hero.maxMove += v;
                        hero.movePool = hero.maxMove;
                    } else if (sd->effectType == SkillEffectType::VisionBonus) {
                        hero.visionRange += v;
                    } else if (sd->effectType == SkillEffectType::MagicSchoolBonus) {
                        if      (sd->statName == "lightPower")  hero.lightPower  += v;
                        else if (sd->statName == "bloodPower")  hero.bloodPower  += v;
                        else if (sd->statName == "deathPower")  hero.deathPower  += v;
                        else if (sd->statName == "naturePower") hero.naturePower += v;
                        else if (sd->statName == "forgePower")  hero.forgePower  += v;
                        else if (sd->statName == "fleshPower")  hero.fleshPower  += v;
                    }
                }
            }
        }
    }

    // Apply Hideout permanent upgrades to the starting hero and resources
    if (m_hideout.isOpen()) {
        // Castle: bonus starting gold (+200 / +400 / +700 per tier)
        static constexpr int CASTLE_GOLD[] = { 0, 200, 600, 1300 }; // cumulative per tier
        int castleTier = m_hideout.getUpgradeLevel(HideoutBranch::CASTLE);
        if (castleTier > 0)
            m_playerResources.add(ResourceType::Gold, CASTLE_GOLD[std::min(castleTier, 3)]);

        // Barracks: bonus hero ATK (T1: +1 ATK, T2: +1 ATK +1 DEF)
        int barracksTier = m_hideout.getUpgradeLevel(HideoutBranch::BARRACKS);
        if (barracksTier >= 1) hero.attack++;
        if (barracksTier >= 2) hero.defense++;

        // Vault: bonus rare resources
        int vaultTier = m_hideout.getUpgradeLevel(HideoutBranch::VAULT);
        if (vaultTier >= 1) {
            m_playerResources.add(ResourceType::Iron, 1);
            m_playerResources.add(ResourceType::Mercury, 1);
        }
        if (vaultTier >= 2) {
            m_playerResources.add(ResourceType::VerdantSap, 1);
            m_playerResources.add(ResourceType::BloodEssence, 1);
            m_playerResources.add(ResourceType::FaithStones, 1);
        }

        // Shrine: second starting spell (faction-appropriate)
        if (m_hideout.getUpgradeLevel(HideoutBranch::SHRINE) >= 1) {
            static constexpr int kShrineSpell[] = {
                SPL::DIVINE_SHIELD, SPL::DRAIN_LIFE, SPL::SERPENT_VENOM,
                SPL::WITHER,        SPL::ENERVATE,   SPL::CURSE,
                SPL::SHRAPNEL,      SPL::FESTER,     SPL::REINFORCE
            };
            int shrineSpell = kShrineSpell[fi];
            bool alreadyKnown = false;
            for (int s : hero.knownSpells) if (s == shrineSpell) { alreadyKnown = true; break; }
            if (!alreadyKnown) hero.knownSpells.push_back(shrineSpell);
        }

        // Sanctum: +10 max mana
        if (m_hideout.getUpgradeLevel(HideoutBranch::SANCTUM) >= 1) {
            hero.maxMana += 10;
            hero.mana = hero.maxMana;
        }
    }

    // Difficulty bonuses for the player hero
    int diff = std::clamp(m_newGameDifficulty, 0, 2);
    if (diff == 0) { hero.attack += 2; hero.defense += 2; } // Easy: +2 ATK/DEF

    m_heroes.push_back(hero);
    if (HexTile* ht = m_map.getTile(hero.pos)) ht->heroId = hero.id;

    // Army sizes: Easy=larger player army, Normal/Hard=base
    static const int kT1Count[] = {24, 20, 20};
    static const int kT2Count[] = {10,  8,  8};

    auto giveStartingArmy = [&](Hero& h, int t1, int t2) {
        for (int tier : {1, 2}) {
            int cnt = (tier == 1) ? t1 : t2;
            for (const auto& ud : m_registry.units()) {
                if (ud.faction == h.faction && ud.tier == tier
                    && ud.path == UpgradePath::None) {
                    h.army.push_back({ud.id, cnt});
                    break;
                }
            }
        }
    };
    giveStartingArmy(m_heroes[0], kT1Count[diff], kT2Count[diff]);

    // Faction-appropriate enemy hero names (9 factions × 3 names each)
    static const char* kEnemyHeroNames[9][3] = {
        {"Seraphiel", "Ardent Inquisitor", "Blessed Blade"},       // HolyOrder
        {"Vael Bonechant", "Mortis Raider", "Crypt Sovereign"},    // CrimsonWardens
        {"Root-Elder", "Thornweave", "Briar Sovereign"},           // Thornkin
        {"Shade Marshal", "Revenant Warden", "Iron Phantom"},      // EternalEmpire
        {"Kael Bloodfang", "Ravager Lord", "Warlord Gruk"},        // Bloodsworn
        {"Vex Nullform", "Phase Stalker", "Rift Caller"},          // Voidkin
        {"Cogmaster Rex", "Iron Overseer", "Steam Baron"},         // IronAssembly
        {"Flesh-Weave", "Graft Sovereign", "Marrow Sculptor"},     // Amalgamate
        {"Synth-One", "Accord Delegate", "Unity Seeker"},          // Convergence
    };
    // Player 2+ setup (hotseat): use startPositions[pi] and a different faction each
    for (int pi = 1; pi < m_numHumanPlayers && pi < (int)wgResult.startPositions.size(); ++pi) {
        int pfi = std::clamp(sortedFaction[pi], 0, 8);
        {
            static const int kStartPct[3] = {100, 90, 80};
            int sp = kStartPct[std::clamp(m_newGameDifficulty, 0, 2)];
            m_players[pi].resources.set(ResourceType::Gold, 5000 * sp / 100);
            m_players[pi].resources.set(ResourceType::Iron,   20 * sp / 100);
        }
        m_players[pi].activeHeroIdx = 0;

        Hero phero;
        phero.id      = static_cast<uint32_t>(pi + 1);
        phero.name    = "Player " + std::to_string(pi + 1);
        phero.faction = kFactions[pfi];
        phero.pos     = wgResult.startPositions[pi];
        phero.movePool = phero.maxMove;
        phero.knownSpells = { kFactionStartSpell[pfi] };
        {
            auto cls4fac = m_classRegistry.getClassesForFaction(phero.faction);
            if (!cls4fac.empty()) phero.classId = cls4fac[0]->id;
        }
        giveStartingArmy(phero, kT1Count[diff], kT2Count[diff]);
        if (HexTile* ht = m_map.getTile(phero.pos)) ht->heroId = phero.id;
        m_players[pi].heroes.push_back(phero);
    }

    uint32_t nameRng = wgp.seed ^ 0xABCD1234u;
    int aiStartIdx = m_numHumanPlayers;  // skip P2's position slot in 2P mode
    for (int i = aiStartIdx; i < static_cast<int>(wgResult.startPositions.size()) && i <= 3; ++i) {
        FactionId ef = (i < static_cast<int>(wgResult.towns.size()))
                       ? wgResult.towns[i].faction : FactionId::EternalEmpire;
        int efi = std::clamp(static_cast<int>(ef), 0, 8);
        nameRng = nameRng * 1664525u + 1013904223u;
        const char* eName = kEnemyHeroNames[efi][nameRng % 3];
        Hero eHero;
        eHero.id       = 99u + static_cast<uint32_t>(i);
        eHero.name     = eName;
        eHero.faction  = ef;
        eHero.pos      = wgResult.startPositions[i];
        eHero.movePool = eHero.maxMove;
        // No difficulty stat bonuses: the AI plays by the same rules as the
        // player; difficulty only changes behavior (aggression, combat AI).
        // Faction-specific spells and school power for the enemy hero
        // Two spells: one offensive/debuff + one DoT or heavy hitter
        static const int kEnemySpells[9][2] = {
            {SPL::SMITE,         SPL::RADIANCE},         // HolyOrder
            {SPL::WITHER,        SPL::VENOMOUS_CLOUD},   // CrimsonWardens
            {SPL::ENTANGLE,      SPL::SERPENT_VENOM},    // Thornkin
            {SPL::CURSE,         SPL::WITHER},            // EternalEmpire
            {SPL::DRAIN_LIFE,    SPL::ENERVATE},          // Bloodsworn
            {SPL::CURSE,         SPL::PLAGUE},            // Voidkin
            {SPL::SHRAPNEL,      SPL::NAPALM},            // IronAssembly
            {SPL::FESTER,        SPL::ACID_SPRAY},        // Amalgamate
            {SPL::SMITE,         SPL::SHRAPNEL},          // Convergence
        };
        eHero.knownSpells = { kEnemySpells[efi][0], kEnemySpells[efi][1] };
        eHero.mana    = 20;
        eHero.maxMana = 20;
        // Assign a random class from the enemy's faction pool and set persistent specialty flags
        {
            auto eCls = m_classRegistry.getClassesForFaction(ef);
            if (!eCls.empty()) {
                int pick = static_cast<int>(i % eCls.size());
                const HeroClassDef* ecls = eCls[pick];
                eHero.classId = ecls->id;
                eHero.ghostWalkSpecialty  = (ecls->specialty == SpecialtyType::GhostWalk);
                eHero.blightAuraSpecialty = (ecls->specialty == SpecialtyType::BlightAura);
                eHero.infestationSpecialty = (ecls->specialty == SpecialtyType::Infestation);
                // Grant first two skills from class pool at Basic tier (mirrors player hero creation)
                auto applyStartSkill = [&eHero](int sid) {
                    eHero.skills.learn(sid);
                    if (const SkillDef* def = findSkillDef(sid)) {
                        int v = def->values[0];
                        if (def->effectType == SkillEffectType::MovementBonus) {
                            eHero.maxMove += v; eHero.movePool = eHero.maxMove;
                        } else if (def->effectType == SkillEffectType::VisionBonus) {
                            eHero.visionRange += v;
                        } else if (def->effectType == SkillEffectType::MagicSchoolBonus) {
                            if      (def->statName == "lightPower")  eHero.lightPower  += v;
                            else if (def->statName == "bloodPower")  eHero.bloodPower  += v;
                            else if (def->statName == "deathPower")  eHero.deathPower  += v;
                            else if (def->statName == "naturePower") eHero.naturePower += v;
                            else if (def->statName == "forgePower")  eHero.forgePower  += v;
                            else if (def->statName == "fleshPower")  eHero.fleshPower  += v;
                        }
                    }
                };
                if (ecls->skillPool.size() >= 1) applyStartSkill(ecls->skillPool[0]);
                if (ecls->skillPool.size() >= 2) applyStartSkill(ecls->skillPool[1]);
            }
        }
        // School power scales enemy hero spells (roughly half player's starting tier)
        switch (ef) {
            case FactionId::HolyOrder:      eHero.lightPower  = 2; break;
            case FactionId::CrimsonWardens: eHero.deathPower  = 2; break;
            case FactionId::Thornkin:       eHero.naturePower = 2; break;
            case FactionId::EternalEmpire:  eHero.deathPower  = 2; break;
            case FactionId::Bloodsworn:     eHero.bloodPower  = 2; break;
            case FactionId::Voidkin:        eHero.deathPower  = 2; break;
            case FactionId::IronAssembly:   eHero.forgePower  = 2; break;
            case FactionId::Amalgamate:     eHero.fleshPower  = 2; break;
            case FactionId::Convergence:    eHero.lightPower  = 1; eHero.forgePower = 1; break;
            default: break;
        }
        // Same starting army as the player at every difficulty — no unit
        // bonuses, the AI's only advantage is information.
        giveStartingArmy(eHero, 20, 8);
        m_enemyHeroes.push_back(eHero);
        if (HexTile* ht = m_map.getTile(eHero.pos)) ht->heroId = eHero.id;
    }

    // AI team economy pool: each AI "player" starts with the same resources
    // a human gets (5000g + 20 iron); the team shares one pool.
    m_enemyResources = Resources{};
    {
        int aiStarts = static_cast<int>(m_enemyHeroes.size());
        m_enemyResources.set(ResourceType::Gold, 5000 * std::max(1, aiStarts));
        m_enemyResources.set(ResourceType::Iron,   20 * std::max(1, aiStarts));
    }

    for (int i = 0; i < static_cast<int>(wgResult.towns.size()); ++i) {
        Town& wt = wgResult.towns[i];
        if (i == 0) {
            wt.ownerId = 1;
            // Force the player's starting town to match their chosen faction
            wt.faction = static_cast<FactionId>(std::clamp(m_newGameFaction, 0, 8));
            // Pre-build Mage Guild and faction town hall so income starts immediately
            int hallId = (static_cast<int>(wt.faction) + 1) * 100;
            wt.builtBuildings.push_back(BID::MAGE_GUILD);
            wt.builtBuildings.push_back(hallId);
            // Rebuild weeklyIncome from pre-built buildings
            for (int bid : wt.builtBuildings) {
                const BuildingDef* def = m_registry.getBuildingDef(bid);
                if (def) wt.weeklyIncome.addAll(def->weeklyIncome);
            }
        } else if (i >= 1 && i < m_numHumanPlayers) {
            // Human player i's starting town — faction already set from slots
            wt.ownerId = static_cast<uint32_t>(i + 1);
            int hallId = (static_cast<int>(wt.faction) + 1) * 100;
            wt.builtBuildings.push_back(BID::MAGE_GUILD);
            wt.builtBuildings.push_back(hallId);
            for (int bid : wt.builtBuildings) {
                const BuildingDef* def = m_registry.getBuildingDef(bid);
                if (def) wt.weeklyIncome.addAll(def->weeklyIncome);
            }
        } else {
            // Assign enemy town to the corresponding AI hero (heroes are 99+i)
            uint32_t aiId = 99u + static_cast<uint32_t>(i);
            bool assignedToAI = false;
            for (const auto& eh : m_enemyHeroes)
                if (eh.id == aiId) { assignedToAI = true; break; }
            if (assignedToAI) {
                wt.ownerId = aiId;
                // Pre-build same starting buildings as the player
                int hallId = (static_cast<int>(wt.faction) + 1) * 100;
                wt.builtBuildings.push_back(BID::MAGE_GUILD);
                wt.builtBuildings.push_back(hallId);
                for (int bid : wt.builtBuildings) {
                    const BuildingDef* def = m_registry.getBuildingDef(bid);
                    if (def) wt.weeklyIncome.addAll(def->weeklyIncome);
                }
            } else {
                wt.ownerId = 0;
                for (const auto& ud : m_registry.units()) {
                    if (ud.faction == wt.faction && ud.tier == 1
                        && ud.path == UpgradePath::None) {
                        wt.garrison.push_back({ud.id, 15});
                        break;
                    }
                }
            }
        }
        // Starting defensive garrison for every OWNED town (player, human,
        // AI) so a hero wandering off doesn't hand the undefended town to an
        // early raider on turn 1 — the fight now happens at the walls.
        if (wt.ownerId != 0 && wt.garrison.empty()) {
            for (const auto& ud : m_registry.units())
                if (ud.faction == wt.faction && ud.tier == 1
                    && ud.path == UpgradePath::None) { wt.garrison.push_back({ud.id, 20}); break; }
            for (const auto& ud : m_registry.units())
                if (ud.faction == wt.faction && ud.tier == 2
                    && ud.path == UpgradePath::None) { wt.garrison.push_back({ud.id, 8}); break; }
        }

        if (HexTile* ht = m_map.getTile(wt.pos)) ht->townId = wt.id;
        m_towns.push_back(wt);

        // Paint faction-appropriate terrain around the town (radius 3)
        auto factionTerrain = [](FactionId f) -> Terrain {
            switch (f) {
            case FactionId::HolyOrder:     return Terrain::Sacred;
            case FactionId::CrimsonWardens:return Terrain::Highland;
            case FactionId::Thornkin:      return Terrain::Forest;
            case FactionId::EternalEmpire: return Terrain::Toxic;
            case FactionId::Bloodsworn:    return Terrain::Corrupted;
            case FactionId::Voidkin:       return Terrain::CorruptedForest;
            case FactionId::IronAssembly:  return Terrain::Industrial;
            case FactionId::Amalgamate:    return Terrain::Wasteland;
            case FactionId::Convergence:   return Terrain::Plains;
            default:                       return Terrain::Plains;
            }
        };
        Terrain ft = factionTerrain(wt.faction);
        for (auto& nc : HexGrid::range(wt.pos, 3)) {
            HexTile* nt = m_map.getTile(nc);
            if (nt && nt->terrain != Terrain::Water)
                nt->terrain = ft;
        }
    }

    // Guarantee every town has a mine of its faction's key resource within
    // 7 hexes. World gen placed faction mines by zone index, but chosen
    // factions (and the weighted global mine roll) are only known now —
    // retype the nearest mine if none matches.
    {
        auto factionResource = [](FactionId f) -> ResourceType {
            switch (f) {
            case FactionId::HolyOrder:
            case FactionId::CrimsonWardens: return ResourceType::FaithStones;
            case FactionId::Thornkin:
            case FactionId::Voidkin:        return ResourceType::VerdantSap;
            case FactionId::EternalEmpire:
            case FactionId::Convergence:    return ResourceType::Mercury;
            case FactionId::Bloodsworn:
            case FactionId::Amalgamate:     return ResourceType::BloodEssence;
            default:                        return ResourceType::Iron;
            }
        };
        for (const auto& t : m_towns) {
            ResourceType want = factionResource(t.faction);
            bool has = false;
            ResourceNode* closest = nullptr;
            int closestD = 9999;
            for (auto& r : m_resources) {
                int d = HexGrid::distance(r.pos, t.pos);
                if (d > 7) continue;
                if (r.type == want) { has = true; break; }
                // Prefer retyping a non-gold mine so gold stays the commonest
                if (r.type != ResourceType::Gold && d < closestD) { closestD = d; closest = &r; }
            }
            if (has) continue;
            if (!closest) {
                for (auto& r : m_resources) {
                    int d = HexGrid::distance(r.pos, t.pos);
                    if (d <= 7 && d < closestD) { closestD = d; closest = &r; }
                }
            }
            if (closest) {
                closest->type   = want;
                closest->amount = 2 + (closest->amount % 4);
            }
        }
    }

    // ── Starting bonuses (per setup slot): artifact, +5 faction resource, or
    // +1500 gold. Humans and bots alike — same rules for everyone.
    {
        auto facRes = [](FactionId f) -> ResourceType {
            switch (f) {
            case FactionId::HolyOrder:
            case FactionId::CrimsonWardens: return ResourceType::FaithStones;
            case FactionId::Thornkin:
            case FactionId::Voidkin:        return ResourceType::VerdantSap;
            case FactionId::EternalEmpire:
            case FactionId::Convergence:    return ResourceType::Mercury;
            case FactionId::Bloodsworn:
            case FactionId::Amalgamate:     return ResourceType::BloodEssence;
            default:                        return ResourceType::Iron;
            }
        };
        uint32_t brng = wgp.seed ^ 0xB07705E5u;
        auto randomArtifactId = [&]() -> int {
            const auto& arts = m_artifactRegistry.artifacts();
            if (arts.empty()) return 0;
            brng = brng * 1664525u + 1013904223u;
            return arts[brng % arts.size()].id;
        };
        for (int si = 0; si < slotCount; ++si) {
            int bonus = std::clamp(sortedBonus[si], 0, 2);
            FactionId sfac = static_cast<FactionId>(std::clamp(sortedFaction[si], 0, 8));
            bool isHumanSlot = (si < m_numHumanPlayers);
            Resources* pool = nullptr;
            Hero*      bhero = nullptr;
            if (si == 0) {
                pool = &m_playerResources;
                bhero = m_heroes.empty() ? nullptr : &m_heroes[0];
            } else if (isHumanSlot) {
                pool = &m_players[si].resources;
                bhero = m_players[si].heroes.empty() ? nullptr : &m_players[si].heroes[0];
            } else {
                pool = &m_enemyResources;
                uint32_t aiId = 99u + static_cast<uint32_t>(si);
                for (auto& eh : m_enemyHeroes) if (eh.id == aiId) { bhero = &eh; break; }
            }
            switch (bonus) {
            case 0:  // Artifact
                if (bhero) {
                    int aid = randomArtifactId();
                    if (aid > 0) bhero->artifactInventory.push_back(aid);
                }
                break;
            case 1:  // +5 of the slot faction's key resource
                if (pool) pool->add(facRes(sfac), 5);
                break;
            default: // +1500 gold
                if (pool) pool->add(ResourceType::Gold, 1500);
                break;
            }
        }
    }

    m_worldObjects.clear();
    {
        uint32_t rng = wgp.seed ^ 0xF00DBABE;
        auto lcg = [&]() { return (rng = rng * 1664525u + 1013904223u); };

        auto allCoords = m_map.coords();
        for (size_t ci = allCoords.size() - 1; ci > 0; --ci)
            std::swap(allCoords[ci], allCoords[lcg() % (ci + 1)]);

        HexCoord startPos = m_heroes.empty() ? HexCoord{0,0} : m_heroes[0].pos;
        // Pick a tile — prefer tiles within minDist..maxDist of start (for first N objects)
        int nearPickCount = 0;
        auto pickTile = [&](int minDist = 0, int maxDist = 999) -> HexCoord {
            for (auto& c : allCoords) {
                const HexTile* t = m_map.getTile(c);
                if (!t || t->terrain == Terrain::Water) continue;
                if (t->heroId || t->townId || t->resourceId) continue;
                bool used = false;
                for (auto& o : m_worldObjects) if (o.pos == c) { used = true; break; }
                if (used) continue;
                int d = HexGrid::distance(c, startPos);
                if (d < minDist || d > maxDist) continue;
                return c;
            }
            // Fallback: any valid tile
            for (auto& c : allCoords) {
                const HexTile* t = m_map.getTile(c);
                if (!t || t->terrain == Terrain::Water) continue;
                if (t->heroId || t->townId || t->resourceId) continue;
                bool used = false;
                for (auto& o : m_worldObjects) if (o.pos == c) { used = true; break; }
                if (!used) return c;
            }
            return {0, 0};
        };

        // Scale object count to map size
        int mapR  = static_cast<int>(m_map.radius());
        int scale = std::max(1, mapR / 16); // Small=1, Medium=1, Large=2, XL=3

        static const int kScrollSpells[] = {
            SPL::SMITE, SPL::REGROWTH, SPL::CURSE, SPL::BLESS, SPL::CALL_LIGHTNING,
            SPL::REINFORCE, SPL::OVERCLOCK, SPL::WITHER, SPL::BARKSKIN
        };
        // First scroll + cache guaranteed near start (visible from turn 1)
        {
            HexCoord p = pickTile(6, 10);
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::SpellScroll, p,
                kScrollSpells[lcg() % 9], ResourceType::Gold, false});
        }
        {
            ResourceType rtype = ResourceType::Gold;
            HexCoord p = pickTile(6, 10);
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::ResourceCache, p,
                500 + static_cast<int>(lcg() % 1500), rtype, false});
        }

        // Guarantee 2 resource mines visible from the start (within 5-9 hexes)
        {
            static const ResourceType kNearRes[] = { ResourceType::Gold, ResourceType::Iron };
            for (int ri = 0; ri < 2; ++ri) {
                for (auto& c : allCoords) {
                    HexTile* t = m_map.getTile(c);
                    if (!t || t->terrain == Terrain::Water) continue;
                    if (t->heroId || t->townId || t->resourceId) continue;
                    bool usedByObj = false;
                    for (auto& o : m_worldObjects) if (o.pos == c) { usedByObj = true; break; }
                    if (usedByObj) continue;
                    int d = HexGrid::distance(c, startPos);
                    if (d < 5 || d > 9) continue;
                    bool tooClose = false;
                    for (auto& r : m_resources)
                        if (HexGrid::distance(c, r.pos) < 4) { tooClose = true; break; }
                    if (tooClose) continue;
                    ResourceNode node;
                    node.id     = m_nextObjId++;
                    node.pos    = c;
                    node.type   = kNearRes[ri];
                    node.amount = (node.type == ResourceType::Gold)
                                 ? 250
                                 : 3 + static_cast<int>(lcg() % 3);
                    t->resourceId = node.id;
                    m_resources.push_back(node);
                    break;
                }
            }
        }

        // Guarantee 1 faction-specific resource mine within 10 hexes of start
        {
            FactionId playerFaction = FactionId::None;
            for (const auto& t : m_towns)
                if (t.ownerId == 1) { playerFaction = t.faction; break; }

            auto factionPrimaryRes = [](FactionId f) -> ResourceType {
                switch (f) {
                case FactionId::HolyOrder:      return ResourceType::FaithStones;
                case FactionId::CrimsonWardens: return ResourceType::FaithStones;
                case FactionId::Thornkin:       return ResourceType::VerdantSap;
                case FactionId::EternalEmpire:  return ResourceType::Mercury;
                case FactionId::Bloodsworn:     return ResourceType::BloodEssence;
                case FactionId::Voidkin:        return ResourceType::VerdantSap;
                case FactionId::IronAssembly:   return ResourceType::Iron;
                case FactionId::Amalgamate:     return ResourceType::BloodEssence;
                case FactionId::Convergence:    return ResourceType::Mercury;
                default:                        return ResourceType::Gold;
                }
            };

            ResourceType fres = factionPrimaryRes(playerFaction);
            for (auto& c : allCoords) {
                HexTile* t = m_map.getTile(c);
                if (!t || t->terrain == Terrain::Water) continue;
                if (t->heroId || t->townId || t->resourceId) continue;
                bool usedByObj = false;
                for (auto& o : m_worldObjects) if (o.pos == c) { usedByObj = true; break; }
                if (usedByObj) continue;
                int d = HexGrid::distance(c, startPos);
                if (d < 4 || d > 10) continue;
                bool tooClose = false;
                for (auto& r : m_resources)
                    if (HexGrid::distance(c, r.pos) < 3) { tooClose = true; break; }
                if (tooClose) continue;
                ResourceNode node;
                node.id     = m_nextObjId++;
                node.pos    = c;
                node.type   = fres;
                node.amount = (fres == ResourceType::Gold) ? 250 : 3 + static_cast<int>(lcg() % 3);
                t->resourceId = node.id;
                m_resources.push_back(node);
                gLog("Placed faction mine (%s) at (%d,%d) dist=%d\n",
                       resourceName(fres), c.q, c.r, d);
                break;
            }
        }
        for (int s = 0; s < 4 * scale; ++s) {
            HexCoord p = pickTile();
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::SpellScroll, p,
                kScrollSpells[lcg() % 9], ResourceType::Gold, false});
        }
        for (int a = 0; a < 3 * scale; ++a) {
            HexCoord p = pickTile();
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::ArtifactChest, p,
                1 + static_cast<int>(lcg() % 8), ResourceType::Gold, false});
        }
        // Artifact Merchants — permanent shops stocking 3 Special artifacts each
        for (int am = 0; am < 2 + scale; ++am) {
            HexCoord p = pickTile();
            WorldObject wo;
            wo.id    = m_nextObjId++;
            wo.type  = WorldObjectType::ArtifactMerchant;
            wo.pos   = p;
            wo.value = static_cast<int>(lcg() & 0x7FFFFFFF);  // per-merchant RNG seed
            m_worldObjects.push_back(wo);
        }
        // Arenas — 2-3 per map
        for (int a = 0; a < 2 + (scale > 0 ? 1 : 0); ++a) {
            WorldObject awo; awo.id = m_nextObjId++; awo.type = WorldObjectType::Arena;
            awo.pos = pickTile(); m_worldObjects.push_back(awo);
        }
        // Experience Wells — 3-4 per map
        for (int w = 0; w < 3 + scale; ++w) {
            WorldObject ewo; ewo.id = m_nextObjId++; ewo.type = WorldObjectType::ExperienceWell;
            ewo.pos = pickTile(); m_worldObjects.push_back(ewo);
        }
        for (int x = 0; x < 3 * scale; ++x) {
            HexCoord p = pickTile();
            // XP shrines: a boost, not a game-decider. 200-599 XP let a hero
            // hit level 3 in week 1 and snowball the whole match; 60-180 is
            // roughly half a level — meaningful but not swingy.
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::XPShrine, p,
                60 + static_cast<int>(lcg() % 120), ResourceType::Gold, false});
        }
        for (int rc = 0; rc < 4 * scale; ++rc) {
            HexCoord p = pickTile();
            static const ResourceType kRTypes[] = {
                ResourceType::Gold, ResourceType::Iron,
                ResourceType::FaithStones, ResourceType::BloodEssence,
                ResourceType::VerdantSap, ResourceType::Mercury
            };
            ResourceType rtype = kRTypes[lcg() % 6];
            int rval = (rtype == ResourceType::Gold) ? 500 + static_cast<int>(lcg() % 2000)
                                                     : 5   + static_cast<int>(lcg() % 10);
            m_worldObjects.push_back({m_nextObjId++, WorldObjectType::ResourceCache, p, rval, rtype, false});
        }

        // Landmarks — named historical sites; permanent XP on first visit
        for (int lm = 0; lm < 3 * scale; ++lm) {
            HexCoord p = pickTile();
            WorldObject wo;
            wo.id    = m_nextObjId++;
            wo.type  = WorldObjectType::Landmark;
            wo.pos   = p;
            wo.value = 300 + static_cast<int>(lcg() % 500); // XP amount
            m_worldObjects.push_back(wo);
        }

        // Cursed Ground — damages army each crossing; questState = charges (3-5)
        for (int cg = 0; cg < 2 * scale; ++cg) {
            HexCoord p = pickTile();
            WorldObject wo;
            wo.id         = m_nextObjId++;
            wo.type       = WorldObjectType::CursedGround;
            wo.pos        = p;
            wo.value      = 10 + static_cast<int>(lcg() % 20); // dmg per trigger
            wo.questState = 3 + static_cast<int>(lcg() % 3);   // charges
            m_worldObjects.push_back(wo);
        }

        // Neutral Outposts — guarded; capture gives weekly T1 production
        for (int no = 0; no < 2 * scale; ++no) {
            HexCoord p = pickTile();
            WorldObject wo;
            wo.id      = m_nextObjId++;
            wo.type    = WorldObjectType::NeutralOutpost;
            wo.pos     = p;
            wo.faction = static_cast<uint8_t>(lcg() % 9);
            wo.value   = 1; // T1 dwellings
            m_worldObjects.push_back(wo);
        }

        // Pandora's Boxes — guarded gamble (late-game content), away from start
        for (int pb = 0; pb < 2 + scale; ++pb) {
            HexCoord p = pickTile(10, 999);
            WorldObject wo;
            wo.id    = m_nextObjId++;
            wo.type  = WorldObjectType::PandoraBox;
            wo.pos   = p;
            wo.value = static_cast<int>(lcg() & 0x7FFFFFFF);  // reward seed
            m_worldObjects.push_back(wo);
        }
    }

    for (auto& wo : wgResult.worldObjects) m_worldObjects.push_back(wo);
    for (const auto& wo : m_worldObjects)
        if (wo.id >= m_nextObjId) m_nextObjId = wo.id + 1;

    // Build road network connecting all towns via shortest land paths
    m_roadHexes.clear();
    if (m_towns.size() >= 2) {
        auto roadCost = [this](HexCoord c) -> int {
            const HexTile* t = m_map.getTile(c);
            return (t && t->terrain != Terrain::Water) ? 1 : 999;
        };
        // Connect each town to every other town
        for (size_t ti = 0; ti < m_towns.size(); ++ti) {
            for (size_t tj = ti + 1; tj < m_towns.size(); ++tj) {
                auto path = Pathfinder::find(m_map, m_towns[ti].pos, m_towns[tj].pos, roadCost);
                for (auto& h : path) m_roadHexes.insert(h);
                // Also insert the town tile itself
                m_roadHexes.insert(m_towns[ti].pos);
                m_roadHexes.insert(m_towns[tj].pos);
            }
        }
    }

    FogOfWar::hideAll(m_map);
    FogOfWar::updateVision(m_map, m_heroes[0]);

    // ── Hot-seat: configure P2 hero from menu choices ─────────────────────────
    // Hot-seat runs on the N-player system: P2 is m_players[1] (hero id=2), created
    // in the multiplayer loop above. Here we just overlay P2's menu picks (faction,
    // class) onto that hero and their starting town. m_enemyHeroes stays pure AI.
    m_hotSeatMode    = m_newGameHotSeat;
    m_worldHUD.setNumHumanPlayers(m_numHumanPlayers);
    m_hotSeatHandoff = false;

    if (m_hotSeatMode && m_numHumanPlayers >= 2 && !m_players[1].heroes.empty()) {
        Hero& p2Hero = m_players[1].heroes[0];
        FactionId p2f = kFactions[std::clamp(m_p2Faction, 0, 8)];
        p2Hero.faction = p2f;
        p2Hero.name    = "Player 2";
        p2Hero.army.clear();
        giveStartingArmy(p2Hero, kT1Count[diff], kT2Count[diff]);
        // Apply P2's chosen class
        const HeroClassDef* p2cls = nullptr;
        if (m_p2ClassId != 0) p2cls = m_classRegistry.getClass(m_p2ClassId);
        if (!p2cls) {
            auto cp = m_classRegistry.getClassesForFaction(p2f);
            if (!cp.empty()) p2cls = cp[0];
        }
        if (p2cls) {
            p2Hero.classId = p2cls->id;
            p2Hero.ghostWalkSpecialty   = (p2cls->specialty == SpecialtyType::GhostWalk);
            p2Hero.blightAuraSpecialty  = (p2cls->specialty == SpecialtyType::BlightAura);
            p2Hero.infestationSpecialty = (p2cls->specialty == SpecialtyType::Infestation);
        }
        // Fix P2's starting town faction to match (P2's towns have ownerId == 2)
        for (auto& t : m_towns) {
            if (t.ownerId == 2u) {
                t.faction = p2f;
                break;
            }
        }
    }

    float hx, hy;
    m_hexRenderer.grid().hexToWorld(m_heroes[0].pos, hx, hy);
    m_camera.setPosition(hx, hy);

    // Sync HUD with final player count (may differ from what was set at init time)
    m_worldHUD.setNumHumanPlayers(m_numHumanPlayers);
    m_worldHUD.setCurrentPlayerId(1);

    // Initial income cache
    m_cachedWeeklyIncome = m_turns.calculateWeeklyIncome(m_towns, 1);
    for (const auto& r : m_resources)
        if (r.ownedBy == 1u) m_cachedWeeklyIncome.add(r.type, mineYield(r));
}

// ── Settings persistence ──────────────────────────────────────────────────────
void Game::saveSettings()
{
    nlohmann::json j;
    j["sfxVol"]        = m_settingsSfxVol;
    j["musVol"]        = m_settingsMasVol;
    j["fullscreen"]    = m_settingsFullscreen;
    j["autoSave"]      = m_settingsAutoSave;
    j["animSpeed"]     = m_settingsAnimSpeed;
    j["showDmgNums"]   = m_settingsShowDmgNums;
    std::ofstream f("settings.json");
    if (f) f << j.dump(2);
}

void Game::loadSettings()
{
    std::ifstream f("settings.json");
    if (!f) return;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        m_settingsSfxVol        = j.value("sfxVol",      0.7f);
        m_settingsMasVol        = j.value("musVol",       0.35f);
        m_settingsFullscreen    = j.value("fullscreen",   false);
        m_settingsAutoSave      = j.value("autoSave",     true);
        m_settingsAnimSpeed     = j.value("animSpeed",    1.0f);
        m_settingsShowDmgNums   = j.value("showDmgNums",  true);
        m_audio.setSfxVolume(m_settingsSfxVol);
        m_audio.setMusicVolume(m_settingsMasVol);
        if (m_settingsFullscreen) {
            if (SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
                m_settingsFullscreen = false;  // bad display mode — fall back to windowed
                saveSettings();
            }
        }
    } catch (...) {}
}

// ── ImGui integration ─────────────────────────────────────────────────────────
bool Game::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // ── Medieval dark-stone / gold / crimson theme ────────────────────────────────────────
    ImGuiStyle& sty = ImGui::GetStyle();
    sty.WindowRounding    = 2.0f;
    sty.ChildRounding     = 2.0f;
    sty.PopupRounding     = 2.0f;
    sty.FrameRounding     = 2.0f;
    sty.ScrollbarRounding = 2.0f;
    sty.GrabRounding      = 2.0f;
    sty.TabRounding       = 2.0f;
    sty.WindowBorderSize  = 1.0f;
    sty.FrameBorderSize   = 0.0f;
    sty.FramePadding      = {6.0f, 4.0f};
    sty.ItemSpacing       = {8.0f, 6.0f};
    sty.WindowPadding     = {10.0f, 10.0f};
    sty.ScrollbarSize     = 12.0f;

    ImVec4* C = sty.Colors;
    C[ImGuiCol_Text]                  = {0.92f, 0.86f, 0.70f, 1.00f}; // parchment
    C[ImGuiCol_TextDisabled]          = {0.52f, 0.46f, 0.34f, 1.00f};
    C[ImGuiCol_WindowBg]              = {0.10f, 0.08f, 0.06f, 0.96f}; // dark stone
    C[ImGuiCol_ChildBg]               = {0.08f, 0.06f, 0.05f, 0.90f};
    C[ImGuiCol_PopupBg]               = {0.10f, 0.08f, 0.06f, 0.97f};
    C[ImGuiCol_Border]                = {0.42f, 0.32f, 0.10f, 0.72f}; // dark gold
    C[ImGuiCol_BorderShadow]          = {0.00f, 0.00f, 0.00f, 0.50f};
    C[ImGuiCol_FrameBg]               = {0.18f, 0.14f, 0.10f, 0.90f}; // dark iron
    C[ImGuiCol_FrameBgHovered]        = {0.28f, 0.22f, 0.12f, 0.90f};
    C[ImGuiCol_FrameBgActive]         = {0.38f, 0.28f, 0.12f, 0.90f};
    C[ImGuiCol_TitleBg]               = {0.20f, 0.08f, 0.06f, 1.00f}; // deep burgundy
    C[ImGuiCol_TitleBgActive]         = {0.38f, 0.14f, 0.08f, 1.00f};
    C[ImGuiCol_TitleBgCollapsed]      = {0.12f, 0.05f, 0.04f, 1.00f};
    C[ImGuiCol_MenuBarBg]             = {0.14f, 0.11f, 0.08f, 1.00f};
    C[ImGuiCol_ScrollbarBg]           = {0.06f, 0.05f, 0.04f, 0.90f};
    C[ImGuiCol_ScrollbarGrab]         = {0.38f, 0.28f, 0.10f, 0.80f};
    C[ImGuiCol_ScrollbarGrabHovered]  = {0.52f, 0.40f, 0.14f, 0.90f};
    C[ImGuiCol_ScrollbarGrabActive]   = {0.65f, 0.50f, 0.18f, 1.00f};
    C[ImGuiCol_CheckMark]             = {0.85f, 0.65f, 0.20f, 1.00f}; // gold
    C[ImGuiCol_SliderGrab]            = {0.58f, 0.44f, 0.14f, 0.90f};
    C[ImGuiCol_SliderGrabActive]      = {0.78f, 0.58f, 0.18f, 1.00f};
    C[ImGuiCol_Button]                = {0.22f, 0.18f, 0.12f, 0.90f}; // dark iron
    C[ImGuiCol_ButtonHovered]         = {0.48f, 0.35f, 0.12f, 1.00f}; // burnished bronze
    C[ImGuiCol_ButtonActive]          = {0.68f, 0.50f, 0.16f, 1.00f}; // bright gold
    C[ImGuiCol_Header]                = {0.38f, 0.13f, 0.08f, 0.85f}; // deep crimson
    C[ImGuiCol_HeaderHovered]         = {0.52f, 0.19f, 0.10f, 0.90f};
    C[ImGuiCol_HeaderActive]          = {0.64f, 0.24f, 0.12f, 1.00f};
    C[ImGuiCol_Separator]             = {0.38f, 0.28f, 0.08f, 0.80f};
    C[ImGuiCol_SeparatorHovered]      = {0.55f, 0.40f, 0.12f, 0.90f};
    C[ImGuiCol_SeparatorActive]       = {0.70f, 0.52f, 0.15f, 1.00f};
    C[ImGuiCol_ResizeGrip]            = {0.35f, 0.25f, 0.08f, 0.40f};
    C[ImGuiCol_ResizeGripHovered]     = {0.52f, 0.38f, 0.12f, 0.70f};
    C[ImGuiCol_ResizeGripActive]      = {0.70f, 0.52f, 0.16f, 0.90f};
    C[ImGuiCol_Tab]                   = {0.18f, 0.14f, 0.10f, 0.85f};
    C[ImGuiCol_TabHovered]            = {0.48f, 0.35f, 0.12f, 0.90f};
    C[ImGuiCol_TabActive]             = {0.36f, 0.25f, 0.10f, 1.00f};
    C[ImGuiCol_TabUnfocused]          = {0.14f, 0.10f, 0.07f, 0.85f};
    C[ImGuiCol_TabUnfocusedActive]    = {0.28f, 0.20f, 0.10f, 1.00f};
    C[ImGuiCol_PlotLines]             = {0.80f, 0.60f, 0.20f, 1.00f};
    C[ImGuiCol_PlotLinesHovered]      = {1.00f, 0.80f, 0.30f, 1.00f};
    C[ImGuiCol_PlotHistogram]         = {0.65f, 0.48f, 0.15f, 1.00f};
    C[ImGuiCol_PlotHistogramHovered]  = {0.85f, 0.65f, 0.20f, 1.00f};
    C[ImGuiCol_TableHeaderBg]         = {0.22f, 0.10f, 0.06f, 1.00f};
    C[ImGuiCol_TableBorderStrong]     = {0.38f, 0.28f, 0.10f, 1.00f};
    C[ImGuiCol_TableBorderLight]      = {0.28f, 0.20f, 0.08f, 0.70f};
    C[ImGuiCol_TableRowBg]            = {0.00f, 0.00f, 0.00f, 0.00f};
    C[ImGuiCol_TableRowBgAlt]         = {0.12f, 0.10f, 0.07f, 0.30f};
    C[ImGuiCol_TextSelectedBg]        = {0.55f, 0.40f, 0.10f, 0.40f};
    C[ImGuiCol_DragDropTarget]        = {0.85f, 0.65f, 0.20f, 0.90f};
    C[ImGuiCol_NavHighlight]          = {0.80f, 0.60f, 0.20f, 0.90f};
    C[ImGuiCol_NavWindowingHighlight] = {0.85f, 0.65f, 0.20f, 0.70f};
    C[ImGuiCol_NavWindowingDimBg]     = {0.00f, 0.00f, 0.00f, 0.50f};
    C[ImGuiCol_ModalWindowDimBg]      = {0.00f, 0.00f, 0.00f, 0.72f};

    if (!ImGui_ImplSDL2_InitForOpenGL(m_window, m_glCtx)) {
        fprintf(stderr, "ImGui SDL2 backend init failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        fprintf(stderr, "ImGui OpenGL3 backend init failed\n");
        return false;
    }
    m_imguiReady = true;
    gLog("ImGui %s ready\n", ImGui::GetVersion());
    return true;
}

void Game::shutdownImGui()
{
    if (!m_imguiReady) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    m_imguiReady = false;
}

void Game::beginImGuiFrame()
{
    if (!m_imguiReady) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void Game::endImGuiFrame()
{
    if (!m_imguiReady) return;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Lua scripting API ─────────────────────────────────────────────────────────
void Game::luaAddSpell(int spellId)
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    for (int s : hero.knownSpells) if (s == spellId) return;
    hero.knownSpells.push_back(spellId);
}

void Game::luaAddXP(int amount)
{
    if (m_heroes.empty()) return;
    Hero& hero = m_heroes[m_activeHeroIdx];
    int oldLvl = hero.level;
    if (hero.addXp(amount)) {
        const HeroClassDef* cls = m_classRegistry.getClass(hero.classId);
        if (cls) {
            std::vector<SkillDef> allSkills(SKILL_DEFS, SKILL_DEFS + SKILL_DEF_COUNT);
            m_levelUpOffers = LevelUpSystem::generateOffers(
                *cls, hero.skills, hero.level, allSkills, hero.faction);
        }
        if (m_levelUpOffers.empty())
            m_levelUpOffers.push_back({SkillID::OFFENSE, false, false, "Learn Offense"});
        m_pendingLevelUps = hero.level - oldLvl;
        m_showLevelUpModal = true;
    }
}

void Game::bindLuaAPI()
{
    lua_State* L = m_lua.state();
    if (!L) return;

    // Store this in registry so non-capturing callbacks can reach Game state
    lua_pushlightuserdata(L, static_cast<void*>(this));
    lua_setfield(L, LUA_REGISTRYINDEX, "Game");

    m_lua.registerGameFunc("getDay", [](lua_State* L) -> int {
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        lua_pushinteger(L, g ? g->luaGetDay() : 0);
        return 1;
    });

    m_lua.registerGameFunc("getWeek", [](lua_State* L) -> int {
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        lua_pushinteger(L, g ? g->luaGetWeek() : 0);
        return 1;
    });

    m_lua.registerGameFunc("getGold", [](lua_State* L) -> int {
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        lua_pushinteger(L, g ? g->luaGetGold() : 0);
        return 1;
    });

    m_lua.registerGameFunc("getHeroLevel", [](lua_State* L) -> int {
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        lua_pushinteger(L, g ? g->luaGetHeroLevel() : 1);
        return 1;
    });

    m_lua.registerGameFunc("addGold", [](lua_State* L) -> int {
        int n = static_cast<int>(luaL_checkinteger(L, 1));
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        if (g) g->luaAddGold(n);
        return 0;
    });

    m_lua.registerGameFunc("addSpell", [](lua_State* L) -> int {
        int id = static_cast<int>(luaL_checkinteger(L, 1));
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        if (g) g->luaAddSpell(id);
        return 0;
    });

    m_lua.registerGameFunc("addXP", [](lua_State* L) -> int {
        int n = static_cast<int>(luaL_checkinteger(L, 1));
        lua_getfield(L, LUA_REGISTRYINDEX, "Game");
        auto* g = static_cast<Game*>(lua_touserdata(L, -1)); lua_pop(L, 1);
        if (g) g->luaAddXP(n);
        return 0;
    });
}

// ── Shutdown ──────────────────────────────────────────────────────────────────
void Game::shutdown()
{
    SDL_FreeCursor(m_cursorArrow);
    SDL_FreeCursor(m_cursorFight);
    m_audio.shutdown();
    m_lua.shutdown();
    shutdownImGui();
    m_editor.shutdown();
    m_hideout.close();
    if (m_glCtx) SDL_GL_DeleteContext(m_glCtx);
    if (m_window) SDL_DestroyWindow(m_window);
    SDL_Quit();
    gLog("Shutdown\n");
}
