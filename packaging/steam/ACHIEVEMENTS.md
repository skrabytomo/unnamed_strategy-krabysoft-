# Achievements

`steam::unlockAchievement("API_NAME")` calls are already placed in the game at
the events below. They're **no-ops** until you (1) build with Steamworks on and
(2) create each achievement with the **exact API Name** in the Steamworks admin
(*Stats & Achievements → New Achievement*). Steam ignores re-unlocks, so it's
safe that some fire every time the event happens.

## Wired now

| API Name | Fires when | Suggested display name |
|---|---|---|
| `ACH_FIRST_WIN` | You win your first battle | *First Blood* |
| `ACH_FIRST_TOWN` | You capture your first town | *Land Grab* |
| `ACH_HERO_LEVEL_5` | A hero reaches level 5 | *Seasoned* |
| `ACH_HERO_LEVEL_10` | A hero reaches level 10 | *Veteran* |
| `ACH_WIN_GAME` | You win a game (all enemies defeated / last human standing) | *Conqueror* |

Set these up in Steamworks with matching API Names, upload icons (256×256 for
the earned icon + a grey locked version), and they'll start unlocking.

## Adding more

Two steps: create it in the admin, then drop one line at the event. Examples of
good events already in the code that just need a call added — tell me which and
I'll place them:

- Equip your first artifact → in the artifact-equip path
- Finish a campaign map → `CampaignManager` completion
- Finish Conquest / win an Arena run → `onConquestBattleEnd` / `onArenaBattleEnd`
- Build a full town (tree complete) → the town build loop
- Reach week 20 / survive N weeks → weekly tick
- Field 7 full unit stacks → recruitment/garrison pickup

The call is always the same one-liner:

```cpp
#include "platform/SteamIntegration.h"
steam::unlockAchievement("ACH_YOUR_ID");
```

Keep API Names `ACH_UPPER_SNAKE`, stable once shipped (renaming an API Name
orphans everyone's earned achievement).
