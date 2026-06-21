-- campaign.lua — decision callbacks for "The Fracture" campaign
-- Called by CampaignManager.resolveDecision via LuaEngine.callFunction
-- ctx: { heroId, townId, q, r, playerSide }

-- ── Mission 0: The Border Burns ───────────────────────────────────────────────

function onDecision_evacuate(ctx)
    -- Halt siege, save civilians — Light +1, costs momentum
    game.print("You ordered the evacuation. The people are safe.")
    game.addGold(-200)   -- supply cost for the delay
end

function onDecision_negotiate(ctx)
    -- Prisoner exchange — Light +1
    game.print("The prisoner exchange is tense but successful.")
    game.addGold(150)    -- returned supplies
end

function onDecision_assault(ctx)
    -- Continue siege — Order +1, Light -1
    game.print("The fort falls. The cost is counted in blood you don't speak of.")
    game.addGold(400)    -- looted fort stores
end

function onDecision_arm(ctx)
    -- Arm the civilians as diversion — Order -1, Light -1
    game.print("Desperate. Effective. You'll never write this in a report.")
    game.addXP(120)      -- tactical bonus from unorthodox manoeuvre
end

function onDecision_release(ctx)
    -- Release Vorka on her word — Order -1, Light +1
    game.print("Vorka walks free. Intelligence received. Whether she honours it — time will tell.")
    game.addGold(300)    -- intelligence value
end

function onDecision_imprison(ctx)
    -- Take information, imprison her — Order +1, Light -1
    game.print("Practical. She'll escape. You both know it.")
    game.addGold(500)    -- full intelligence plus ransom value
end

function onDecision_refuse(ctx)
    -- Refuse the deal — Order +1, Light +1
    game.print("No deals with raiders. Your principles cost you intelligence and a bargaining chip.")
    game.addXP(80)       -- morale bonus from principled stand
end

-- ── Mission 1: The Thornwood Passage ─────────────────────────────────────────

function onDecision_groveOath(ctx)
    -- Bind to defend Thornkin lands — Order -1, Light +1
    game.print("You swear before the ancient roots. Briarwynd steps aside.")
    game.addSpell(31)    -- Entangle (Nature school) — gift from the elders
end

function onDecision_groveTrade(ctx)
    -- Trade VerdantSap for passage — neutral
    game.print("A fair exchange. The path opens.")
    game.addGold(-300)   -- VerdantSap cost
    game.addXP(60)
end

function onDecision_groveAuthority(ctx)
    -- Invoke Holy Order authority — Order +1, Light -1
    game.print("You outranked the forest. It remembers.")
    game.addGold(100)    -- saved trade cost
end

function onDecision_groveForce(ctx)
    -- Cut through the grove — Order -1, Light -1
    game.print("The path is clear. So is the new silence of this wood.")
    game.addXP(100)      -- swift advance bonus
    game.addGold(-500)   -- cost of reparations later
end

function onDecision_defectorAccept(ctx)
    -- Accept the cursed defector fully — Light +1
    game.print("She fights beside you. The curse is yours to witness.")
    game.addSpell(22)    -- Wither (Death school) — plans included this spell
    game.addXP(150)
end

function onDecision_defectorCure(ctx)
    -- Accept and attempt cure — Order +1, Light +1
    game.print("The ritual is costly. Partial success — she has months, not weeks.")
    game.addGold(-600)   -- FaithStone ritual cost
    game.addSpell(22)    -- Wither
    game.addXP(100)
end

function onDecision_defectorUse(ctx)
    -- Use her, send her away — Light -1
    game.print("The plans are excellent. You don't ask where she went.")
    game.addSpell(22)    -- Wither
    game.addGold(200)
end

function onDecision_defectorRefuse(ctx)
    -- Refuse entirely — Order +1
    game.print("The risk isn't worth it. The plans died with her.")
    game.addXP(50)
end

-- ── Mission 2: The Convergence Point ─────────────────────────────────────────

function onDecision_riftSeal(ctx)
    -- Seal the Rift — Light +2
    game.print("The Rift collapses. The silence that follows is absolute.")
    game.addXP(500)
    game.addGold(1000)   -- recovered Rift-touched resources
end

function onDecision_riftOrder(ctx)
    -- Channel through Holy Order doctrine — Order +2, Light +1
    game.print("Faith shapes the Rift's power. The Order's doctrine flows through it like light through a lens.")
    game.addSpell(4)     -- Radiance (Light school pinnacle)
    game.addXP(400)
    game.addGold(800)
end

function onDecision_riftShare(ctx)
    -- Share equally — neutral
    game.print("Every faction leaves with something. No one leaves satisfied. That might be peace.")
    game.addXP(350)
    game.addGold(600)
    game.addSpell(4)     -- Radiance
    game.addSpell(53)    -- Growth (Flesh school)
end

function onDecision_riftWeapon(ctx)
    -- Weaponise — Order +1, Light -2
    game.print("The war ends in an hour. The century of questions begins immediately after.")
    game.addXP(600)
    game.addGold(2000)   -- recovered after the cataclysm
end
