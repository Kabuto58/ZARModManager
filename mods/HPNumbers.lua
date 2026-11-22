-- HP Numbers (a ZARModManager mod): the health of whatever your crosshair is on, in a panel under the map at the top left:
-- its name, current / max health, a health bar (and its shield, if it has one). Read-only: it asks the loader which entity is
-- under the cursor (za.hover) and reads that one entity's health (za.entity), so nothing is drawn over the other entities
-- and the cost stays small however many there are.

za.mod{
    name = 'HP Numbers',
    version = '1.4.0',
    description = 'Shows the name and health (current / max, with a bar) of the enemy, explosive barrel or sentry under your crosshair, in a panel under the map at the top left. Aim at a boss and it adds a live damage-per-second estimate.',
    settings = {
        {id = 'barrels', type = 'toggle', label = 'Explosive barrels', default = true},
        {id = 'sentries', type = 'toggle', label = 'Your sentries and drones', default = true},
        {id = 'dps', type = 'toggle', label = 'DPS estimate on bosses', default = true},
    },
}

local WHITE, GREY = {0.95, 0.95, 0.93, 1}, {0.6, 0.63, 0.65, 1}
local PANEL = {0.043, 0.051, 0.063, 0.82}
local SHIELD = {0.45, 0.8, 1.0, 1}
local X, Y = 25, 250            -- the panel's top left: under the map (the same place the HUD mod keeps its readouts)
local MIN_W, MAX_W, H = 230, 520, 70
local HOVER_RADIUS = 1.3        -- metres around the crosshair (more for big, tough targets)
local KEEP = 0.35               -- seconds the panel stays after the crosshair leaves (no flicker between two targets)
local DPS_MIN = 0.5             -- seconds of aiming at the same boss before there is enough to estimate from
local DPS_WINDOW = 4            -- the estimate is the damage taken over the last this-many seconds
local GOLD = {1.0, 0.78, 0.25, 1}

local function health_colour(f)
    if f > 0.6 then return {0.45, 1.0, 0.45, 1} end    -- green
    if f > 0.3 then return {1.0, 0.85, 0.3, 1} end     -- yellow
    return {1.0, 0.35, 0.3, 1}                         -- red
end
local function whole(x) return string.format('%d', math.ceil(x)) end

-- an entity's object name -> something to read: "PF_Whiplash_3_IG(Clone)" -> "Whiplash 3", "PF_Shambler_Stage4(Clone)" ->
-- "Shambler (Stage 4)", "LevelObject_123 (Zombie_BossPod)" -> "Zombie Boss Pod"
local function pretty(raw)
    local s = tostring(raw or ''):gsub('%(Clone%)', ''):gsub('%s+$', '')
    s = s:gsub('LevelObject_%-?%d+%s*%((.-)%)', '%1')
    s = s:gsub('^PF_', ''):gsub('_IG$', ''):gsub('[-_:]entity$', '')
    local stage = s:match('[_ ]?[Ss]tage[_ ]?(%d+)')
    s = s:gsub('[_ ]?[Ss]tage[_ ]?%d+', ''):gsub('_AUTOGEN', '')
    s = s:gsub('[-_:]+', ' ')
    s = s:gsub('(%l)(%u)', '%1 %2')
    s = s:gsub('(%a)([%w]*)', function(a, b) return a:upper() .. b end)
    s = s:gsub('%s+', ' '):gsub('^%s+', ''):gsub('%s+$', '')
    if s == '' then s = 'Unknown' end
    return stage and (s .. ' (Stage ' .. stage .. ')') or s
end

-- ------------------------------------------------------------------------------------------ panel --
local E, built_gen = nil, nil
local shown = false
local function build()
    local g = za.ui.group{anchor = 'tl', x = X, y = Y, w = MIN_W, h = H, visible = false}
    local function text(spec) spec.parent = g; spec.outline = spec.outline or 0.2; spec.color = spec.color or WHITE; return za.ui.text(spec) end
    local function image(spec) spec.parent = g; spec.sprite = spec.sprite or 'none'; return za.ui.image(spec) end
    E = {g = g}
    E.bg = image{anchor = 'tl', x = 0, y = 0, w = MIN_W, h = H, color = PANEL}
    E.stripe = image{anchor = 'tl', x = 0, y = 0, w = 3, h = H, color = WHITE}
    E.name = text{anchor = 'tl', x = 14, y = 9, w = MIN_W - 28, h = 24, size = 19, align = 'left', text = ''}
    E.dps = text{anchor = 'tl', x = MIN_W - 174, y = 11, w = 160, h = 22, size = 16, align = 'right', color = GOLD, text = ''}
    E.hp = text{anchor = 'tl', x = 14, y = 36, w = 160, h = 22, size = 16, align = 'left', text = ''}
    E.shield = text{anchor = 'tl', x = MIN_W - 174, y = 37, w = 160, h = 20, size = 13, align = 'right', color = SHIELD, text = ''}
    E.track = image{anchor = 'tl', x = 14, y = H - 14, w = MIN_W - 28, h = 5, color = {0, 0, 0, 0.6}}
    E.bar = image{anchor = 'tl', x = 14, y = H - 14, w = MIN_W - 28, h = 5, fill = 'horizontal', amount = 1, color = WHITE}
    shown = false
end
local last_sig = nil
local function draw(name, ent, dps)
    local w = math.max(MIN_W, math.min(MAX_W, math.floor(#name * 10.4) + 44 + (dps and 120 or 0)))
    local f = math.max(0, math.min(1, ent.hp / ent.max_hp))
    local col = health_colour(f)
    local hp = whole(ent.hp) .. ' / ' .. whole(ent.max_hp)
    local sh = ent.shield_max > 0 and ('SHIELD ' .. whole(ent.shield)) or ''
    if sh ~= '' then w = math.max(w, math.min(MAX_W, 14 + math.floor(#hp * 9.8) + 24 + math.floor(#sh * 8) + 14)) end   -- (health numbers on the left, shield on the right, never over each other)
    local dps_text = dps and ('DPS ' .. string.format('%d', math.floor(dps + 0.5))) or ''
    local sig = name .. '|' .. w .. '|' .. hp .. '|' .. sh .. '|' .. string.format('%.3f', f) .. '|' .. dps_text
    if sig == last_sig then return end   -- (only touches the UI when something changed)
    last_sig = sig
    za.ui.set(E.g, {w = w})
    za.ui.set(E.bg, {w = w})
    za.ui.set(E.stripe, {color = col})
    za.ui.set(E.name, {w = w - 28 - (dps and 120 or 0), text = name})
    za.ui.set(E.dps, {x = w - 174, text = dps_text})
    za.ui.set(E.hp, {text = hp, color = col})
    za.ui.set(E.shield, {x = w - 174, text = sh})
    za.ui.set(E.track, {w = w - 28})
    za.ui.set(E.bar, {w = w - 28, amount = f, color = col})
end
local function show(on)
    if on ~= shown then shown = on; za.ui.set(E.g, {visible = on}) end
    if not on then last_sig = nil end
end

-- ------------------------------------------------------------------------------------------ hover --
-- Where the crosshair is on the ground: the game's aim record (the controller's lastAimReadInfo) holds the player's weapon
-- position (aimSourcePos) and how far the cursor is from it on the ground (aimCursorWorldPositionOffset: x, z), so the point
-- is the sum. Only for a mouse (a pointer): there is no crosshair to hover with a controller.
local function aim_point()
    local r = za.static('GameRunner', '<Instance>k__BackingField')
    local cim = r and r:get('characterInputManager')
    local ctrl = cim and cim:get('mainPlayer')
    local ai = ctrl and ctrl:get('lastAimReadInfo')
    if not ai or ai:get('hasAimCursor') ~= true then return nil end
    local src, off = ai:get('aimSourcePos'), ai:get('aimCursorWorldPositionOffset')
    if not src or not off then return nil end
    local sx, sz, ox, oy = src:get('x'), src:get('z'), off:get('x'), off:get('y')
    if type(sx) ~= 'number' or type(sz) ~= 'number' or type(ox) ~= 'number' or type(oy) ~= 'number' then return nil end
    return sx + ox, sz + oy
end
-- The game's own health bar for the target it is showing (a boss): the entity it is for and the title it shows, which is the
-- name the native client uses with its modifiers ("Elite Ruinous Regurgitator").
local function native_name(id)
    local r = za.static('GameRunner', '<Instance>k__BackingField')
    local hud = r and r:get('gameHud')
    local hb = hud and hud:get('healthBarDisplay')
    if not hb then return nil end
    local gid = hb:get('gameEntityId')
    if not gid or gid:get('id') ~= id then return nil end
    if hb:get('initialisedForEntity') == false then return nil end   -- (the bar is not set up for it yet)
    local t = hb:get('title')
    local text = t and table.concat(t:texts(), ' ')
    text = text and text:gsub('^%s+', ''):gsub('%s+$', '')
    if not text or text == '' or text:lower() == 'title' then return nil end   -- ("Title" is the bar's placeholder until it is filled in)
    return text
end
-- an entity's name: the native one once the game's bar has shown it twice the same (its title scrambles in as it appears),
-- else the tidied object name
-- Is this target a boss? The game's own boss bar is showing it, or (in the seconds after it spawns, before that bar has its
-- name) its object name is one of the game's bosses. Once seen as a boss it stays one.
local BOSS_NAMES = {'bulwark', 'reanimator', 'regurgitator', 'shredder', 'whiplash'}
local boss_ids = {}
local function has_boss_bar(id)
    local r = za.static('GameRunner', '<Instance>k__BackingField')
    local hud = r and r:get('gameHud')
    local hb = hud and hud:get('healthBarDisplay')
    if not hb then return false end
    local gid = hb:get('gameEntityId')
    return gid ~= nil and gid:get('id') == id and hb:get('initialisedForEntity') ~= false
end
local function is_boss(ent)
    if boss_ids[ent.id] then return true end
    local boss = has_boss_bar(ent.id)
    if not boss then
        local raw = tostring(ent.name or ''):lower()
        for _, b in ipairs(BOSS_NAMES) do if raw:find(b, 1, true) then boss = true; break end end
    end
    if boss then boss_ids[ent.id] = true end
    return boss
end
-- Damage dealt to the target you are aiming at: what its health and shield lost over the last DPS_WINDOW seconds (healing is
-- not counted). It is everything that hurt it (a squad's, a turret's), not only your own shots.
local focus_id, samples = nil, {}
local function dps_of(ent, now)
    samples[#samples + 1] = {t = now, v = ent.hp + (ent.shield or 0)}
    while #samples > 1 and now - samples[1].t > DPS_WINDOW do table.remove(samples, 1) end
    if now - samples[1].t < DPS_MIN then return nil end
    local dmg = 0
    for i = 2, #samples do local d = samples[i - 1].v - samples[i].v; if d > 0 then dmg = dmg + d end end
    return dmg / (now - samples[1].t)
end
local names, pending = {}, {}
local function name_of(ent)
    local n = names[ent.id]
    if n and n.native then return n.text end
    local nat = native_name(ent.id)
    if nat then
        if pending[ent.id] == nat then names[ent.id] = {text = nat, native = true}; return nat end
        pending[ent.id] = nat
    end
    if not n then n = {text = pretty(ent.name)}; names[ent.id] = n end
    return n.text
end
local next_at, hide_at, last_id = 0, 0, nil
za.on_frame(function()
    if not za.ui.ready() then return end
    if not E or built_gen ~= za.ui.generation() then build(); built_gen = za.ui.generation() end
    local now = za.time()
    if now < next_at then return end
    next_at = now + 0.07
    if not za.in_mission() then show(false); names, pending, boss_ids = {}, {}, {}; return end
    local types = {Enemy = true, Companion = true, ExplosiveBarrel = za.setting('barrels') ~= false,
                   Turret = za.setting('sentries') ~= false, Drone = za.setting('sentries') ~= false}
    local wx, wz = aim_point()
    local id = wx and za.hover(types, wx, wz, HOVER_RADIUS) or nil
    if id then
        last_id, hide_at = id, now + KEEP
        if id ~= focus_id then focus_id, samples = id, {} end   -- (a new target: its estimate starts from scratch)
    end
    if last_id and now < hide_at then
        local ent = za.entity(last_id)
        if ent and not ent.dead and ent.has_health and ent.max_hp > 0 and ent.hp > 0 then
            local dps = nil
            if ent.id == focus_id and za.setting('dps') ~= false and is_boss(ent) then
                dps = dps_of(ent, now)
            end
            draw(name_of(ent), ent, dps)
            show(true)
            return
        end
    end
    show(false)
    focus_id, samples = nil, {}   -- (the crosshair left the target: the next one starts from scratch)
end)
