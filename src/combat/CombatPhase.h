#pragma once

enum class CombatPhase
{
    Setup,          // place units, pre-battle
    PlayerTurn,     // waiting for player input
    EnemyTurn,      // AI taking its turn
    Animation,      // playing an action animation (input locked)
    SpellTarget,    // player selecting spell target
    Victory,
    Defeat,
    Retreat,
};
