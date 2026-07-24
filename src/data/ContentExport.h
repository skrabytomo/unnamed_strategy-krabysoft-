#pragma once
#include <string>

// ── Content export ────────────────────────────────────────────────────────────
// Dumps the hardcoded content registries (BuildingRegistry: buildings + units,
// plus factions/resources/terrain enums) to JSON so non-C++ front-ends can share
// the same balance data instead of re-typing it.
//
// Driven by `--export-content=<dir>` in main.cpp; runs before SDL/GL init, so it
// needs no window and is safe to call from CI.
//
// Writes into <dir>:
//   factions.json   9 factions, id + name
//   resources.json  resource enum, id + display name
//   terrain.json    terrain enum, id + name
//   buildings.json  every BuildingDef (cost, income, growth, prereqs, upgrades)
//   units.json      every UnitDef (combat stats, tags, recruit + craft cost)
//   assets.json     inventory of assets/ (relative paths, grouped by folder)
//
// Returns false if the directory could not be written.
bool exportContentJson(const std::string& outDir);
