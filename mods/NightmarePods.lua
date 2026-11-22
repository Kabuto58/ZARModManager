-- Nightmare Pods (a ZARModManager mod): the Pods mission gets tougher. In Pods, destroying the boss nest releases the boss.
-- The loader (za.pods_bosses) adds more bosses to that: the nest's death also spawns extra bosses from the mission's own boss
-- pool, so the number chosen in Mod Settings comes out of it together. It applies from the next mission loaded, and only the
-- host's game builds the mission, so it works on your own and as the leader of a party.

za.mod{
    name = 'Nightmare Pods',
    version = '1.0.2',
    description = 'Pods gets extremely tough: the boss nest releases several bosses instead of one. Works alone and as the leader of your party.',
    settings = {
        {id = 'bosses', type = 'choice', label = 'Bosses from the boss nest', options = {'1 boss (vanilla)', '2 bosses', '3 bosses'}, default = 0, lock = 'mission'},
    },
}

local MAX_EXTRA = 2   -- at most 3 bosses (the vanilla one and two more), whatever an older saved setting says
local applied = nil
local next_at = 0
za.on_frame(function()
    local now = za.time()
    if now < next_at then return end
    next_at = now + 1
    local v = za.setting('bosses')
    local n = type(v) == 'number' and math.max(0, math.min(MAX_EXTRA, math.floor(v))) or 0   -- 0 is the vanilla boss
    if n == applied then return end
    applied = n
    za.pods_bosses(n + 1)
end)
