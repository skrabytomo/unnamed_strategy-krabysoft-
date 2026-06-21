-- autoload.lua — loaded at game start
-- Loads sub-scripts and registers trigger callbacks

-- Load campaign decision callbacks
local ok, err = pcall(function() dofile("scripts/campaign.lua") end)
if not ok then
    game.print("Warning: campaign.lua failed — " .. tostring(err))
end

-- ── Global trigger callbacks ───────────────────────────────────────────────────

function onEnterTown(ctx)
    game.print("Hero " .. ctx.heroId .. " entered a town (week " .. game.getWeek() .. ")")
end

function onBattleWon(ctx)
    local level = game.getHeroLevel()
    game.print("Victory! Hero " .. ctx.heroId .. " is level " .. level
               .. " (gold: " .. game.getGold() .. ")")
end

function onBattleLost(ctx)
    game.print("Defeat — retreating to the last safe position (week " .. game.getWeek() .. ")")
end

function onWeekStart(ctx)
    game.print("Week " .. game.getWeek() .. " begins  |  gold: " .. game.getGold()
               .. "  |  hero level: " .. game.getHeroLevel())
end

game.print("Scripts loaded — v" .. game.version()
           .. "  (day " .. game.getDay() .. ")")
