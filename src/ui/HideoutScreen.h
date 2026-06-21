#pragma once
#include "../meta/HideoutDB.h"

// ImGui window for the persistent Hideout meta-layer.
// Call draw() each frame when the screen is open.
class HideoutScreen
{
public:
    // Renders the window. Sets open=false when the user closes it.
    void draw(HideoutDB& db, bool& open);

private:
    void drawXPBar(HideoutDB& db);
    void drawBranch(HideoutDB& db, const char* branch, const char* label,
                    int maxTiers, const int tierCosts[]);
    void drawMilestones(HideoutDB& db);
};
