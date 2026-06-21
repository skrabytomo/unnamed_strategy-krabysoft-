// Game implementation split into per-state files:
//   Game_Core.cpp     — init, run, shutdown, update/render dispatch, save/load, ImGui
//   Game_WorldMap.cpp — world map update/render, hero movement, tile events
//   Game_Combat.cpp   — combat update/render, enter/exit combat
//   Game_Town.cpp     — town update/render, enter/exit town
//   Game_Campaign.cpp — campaign update/render, enter/exit campaign
//   Game_Editor.cpp   — editor update/render, enter/exit editor
