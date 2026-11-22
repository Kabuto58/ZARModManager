-- Minimalistic Mod (a ZARModManager mod; formerly "HUD B2"). Claude Design concept B2 · Corner clusters with the
-- Concept A tactical strip. Top: the mission strip (objective + progress, TIME, MISSION, ALIVE, KILLS). Bottom-left:
-- the squad panel (you at the bottom). Right edge: your three abilities as named rings. Bottom centre: the weapon
-- carousel; bottom right: turret and grenade. It reads game state and draws; the only things it changes are native
-- HUD visibility and the text sounds of the (hidden) dialogue box.
--
-- Settings (Settings > MODS): Kill Count and Enemies Left in the strip, and the size of the weapon area
-- (with turret / grenade) and of the player status area.

za.mod{
    name = 'Minimalistic Mod',
    version = '0.11.2',
    description = 'Removes clutter from Vanilla, leaving only the essentials. Repositions vital information onscreen to maximize viewing space. Only recommend for use by players accustomed to the game.',
    settings = {
        {id = 'kills', type = 'toggle', label = 'Kill Count', default = true},
        {id = 'enemies', type = 'toggle', label = 'Enemies Left', default = true},
        {id = 'banners', type = 'toggle', label = 'Game banners in the top bar', default = true},
        {id = 'notices', type = 'toggle', label = 'Hide the New Equipment / Bounties / Upgrade notices', default = true},
        {id = 'weapon_scale', type = 'slider', label = 'Weapon Area Size (scaling factor)', min = 0.1, max = 2, step = 0.05, default = 1},
        {id = 'status_scale', type = 'slider', label = 'Player Status Area Size (scaling factor)', min = 0.1, max = 2, step = 0.05, default = 1},
    },
}

local OPT = {
    ability_names = 'on_change',  -- 'always' | 'on_change' (3 s after a state change, or with no enemies) | 'off'
    self_row = true,              -- show yourself in the squad panel (replaces the native player card)
    hide_native = true,           -- hide the native ability hexagons, weapon panel, gear slots and player cards
    strip = true,                 -- the Concept A mission strip across the top
    hide_objectives = true,       -- hide the native objective stack once the strip is showing its text
    hide_dialogue = true,         -- hide in-mission dialogue (portrait + text box) and silence its text sounds
}

-- game enums (NativeBuildInterop.MissionType, GameRunner.GameState), in declaration order
local MISSION_HUB = 4             -- fallback only: GameRunner.IsInHub() is asked first
local STATE_INGAME = 3
local SQUAD_X, SQUAD_BOTTOM = 25, 25   -- squad panel: bottom-left corner, rows stacking upwards (you at the bottom)
local ALERT_H = 36                -- the top bar's alert row (the game's banners), under the strip
local STRIP_X = 285               -- the strip starts right of the minimap's connection-strength bars
local WY = {name = 20, icon = 52, reload = 130, ammo = 136, tag = 184}  -- weapon readout rows, up from the bottom

-- design system (round 1 / round 2)
local WHITE, AMBER, GREY = {0.95, 0.95, 0.93, 1}, {1, 0.745, 0.031, 1}, {0.6, 0.63, 0.65, 1}
local PANEL = {0.043, 0.051, 0.063, 0.6}
-- The game's classes and their specialisations (GameRunner.loadedPlayerClasses[].subclasses[]), by internal id:
--   assault: blackops, squadcommander    heavy: shock-trooper, Dreadnought    medic: Science Ops, Field Surgeon
local CLASS = {
    heavy = {'HEAVY', {0.35, 0.61, 1, 1}}, medic = {'MEDIC', {0.5, 0.89, 0.63, 1}},
    assault = {'ASSAULT', {0.91, 0.45, 0.23, 1}},
}
local SUBCLASS = {  -- id (lower case) -> {display name (Capitalised Each Word), its class, the game's title key}
    blackops = {'Black Ops', 'assault', 'blackops_Title'},
    squadcommander = {'Squad Commander', 'assault', 'squadcommander_Title'},
    ['shock-trooper'] = {'Shock Trooper', 'heavy', 'shock_trooper_Title'},
    dreadnought = {'Dreadnought', 'heavy', 'dreadnought_Title'},
    ['science ops'] = {'Science Ops', 'medic', 'Science_Ops_Title'},
    ['field surgeon'] = {'Field Surgeon', 'medic', 'Field_Surgeon_Title'},
}
local function title_case(s)  -- "BLACK OPS" / "black-ops" / "black_ops" -> "Black Ops"
    s = s:gsub('[_%-]', ' '):lower()
    return (s:gsub("(%a)([%w']*)", function(a, b) return a:upper() .. b end))
end
-- A specialisation's display name: the game's own title (Capitalised Each Word), else the table, else the id itself
local function subclass_name(id)
    local S = SUBCLASS[id:lower()]
    local t = S and za.loc(S[3])
    if t and t ~= '' then return title_case(t) end
    return S and S[1] or title_case(id)
end
local KEYS = {'CTRL', 'SHIFT', 'SPACE'}

local function hex(c) return string.format('#%02X%02X%02X', math.floor(c[1] * 255), math.floor(c[2] * 255), math.floor(c[3] * 255)) end
local function get(ref, ...)
    for _, k in ipairs({...}) do if not ref then return nil end ref = ref:get(k) end
    return ref
end
local function trunc(s, n) s = s or '' if utf8.len(s) and utf8.len(s) > n then return s:sub(1, utf8.offset(s, n + 1) - 1) .. '…' end return s end
local function hp_colour(f) if f > 0.6 then return {0.45, 1, 0.45, 1} elseif f > 0.3 then return {1, 0.85, 0.3, 1} end return {1, 0.35, 0.3, 1} end

-- A localisation key -> the game's own text, else a readable form of the key ("heavy_hold_the_line_Title" -> HOLD THE LINE)
local function title_of(key, class)
    if not key or key == '' then return nil end
    local text = za.loc(key)
    if text and text ~= '' then return text:upper() end
    local s = key:lower():gsub('_?title$', ''):gsub('^' .. (class or '') .. '[_%-]', '')
    return (s:gsub('[_%-]', ' ')):upper()
end

-- ------------------------------------------------------------------------------------------ elements --
local E = nil
local built_gen = -1

local GROUP = nil  -- elements created while this is set go into that group (scaled together)
local function text(spec) spec.outline = spec.outline or 0.2; spec.color = spec.color or WHITE; spec.parent = spec.parent or GROUP; return za.ui.text(spec) end
local function image(spec) spec.color = spec.color or WHITE; spec.parent = spec.parent or GROUP; return za.ui.image(spec) end
local function show(id, on) if id then za.ui.set(id, {visible = on}) end end

local function build()
    E = {squad = {}, rings = {}}
    -- scalable areas: groups pinned to their screen corner / edge (they scale around that point)
    E.gs = za.ui.group{anchor = 'bl', x = 0, y = 0, w = 1000, h = 600}
    E.gw = za.ui.group{anchor = 'b', x = 0, y = 0, w = 1400, h = 320}
    E.gg = za.ui.group{anchor = 'br', x = 0, y = 0, w = 300, h = 200}
    -- squad rows: anchor 'bl', vertical positions set each frame by row_geometry (rows grow to fit their content)
    GROUP = E.gs
    for i = 1, 4 do
        local X = SQUAD_X
        local R = {
            bg = image{anchor = 'bl', x = X, y = 0, w = 340, h = 70, sprite = 'none', color = PANEL, visible = false},
            stripe = image{anchor = 'bl', x = X, y = 0, w = 4, h = 70, sprite = 'none', visible = false},
            name = text{anchor = 'bl', x = X + 12, y = 0, w = 316, h = 24, size = 18, align = 'left', visible = false},
            readout = text{anchor = 'bl', x = X + 130, y = 0, w = 198, h = 22, size = 16, align = 'right', visible = false},
            class = text{anchor = 'bl', x = X + 12, y = 0, w = 260, h = 22, size = 16, align = 'left', visible = false},
            pips = {}, status = {},
            hpbg = image{anchor = 'bl', x = X, y = 0, w = 340, h = 4, sprite = 'none', color = {0, 0, 0, 0.6}, visible = false},
            hp = image{anchor = 'bl', x = X, y = 0, w = 340, h = 4, sprite = 'none', fill = 'horizontal', amount = 1, visible = false},
            sh = image{anchor = 'bl', x = X, y = 0, w = 340, h = 2, sprite = 'none', fill = 'horizontal', amount = 0,
                       color = {0.12, 0.56, 1, 1}, visible = false},
        }
        for p = 1, 3 do R.pips[p] = image{anchor = 'bl', x = X + 290 + (p - 1) * 16, y = 0, w = 10, h = 10, sprite = 'disc', color = GREY, visible = false} end
        for k = 1, 3 do R.status[k] = image{anchor = 'bl', x = X + 300, y = 0, w = 22, h = 22, sprite = 'none', visible = false} end
        E.squad[i] = R
    end
    GROUP = nil
    -- the strip: up to 1540 x 50 at (285, 25), corner brackets, segments laid out left to right each frame
    local S = {x = STRIP_X, y = 25, w = 1825 - STRIP_X, h = 50}
    E.strip = {bg = image{anchor = 'tl', x = S.x, y = S.y, w = S.w, h = S.h, sprite = 'none', color = {0.043, 0.051, 0.063, 0.78}},
               brk = {}, segs = {}, divs = {}}
    local BR = {0.95, 0.95, 0.93, 0.9}
    for _, c in ipairs({{0, 0}, {1, 0}, {0, 1}, {1, 1}}) do
        local cx, cy = S.x + c[1] * (S.w - 12), S.y + c[2] * (S.h - 2)
        E.strip.brk[#E.strip.brk + 1] = {right = c[1], bottom = c[2], w = 12, h = 2, id = image{anchor = 'tl', x = cx, y = cy, w = 12, h = 2, sprite = 'none', color = BR}}
        cx, cy = S.x + c[1] * (S.w - 2), S.y + c[2] * (S.h - 12)
        E.strip.brk[#E.strip.brk + 1] = {right = c[1], bottom = c[2], w = 2, h = 12, id = image{anchor = 'tl', x = cx, y = cy, w = 2, h = 12, sprite = 'none', color = BR}}
    end
    for _, k in ipairs({'obj', 'time', 'mission', 'alive', 'kills'}) do
        E.strip.segs[k] = text{anchor = 'tl', x = S.x, y = S.y + 13, w = 100, h = 24, size = 18, align = 'left', text = ''}
    end
    for i = 1, 5 do E.strip.divs[i] = image{anchor = 'tl', x = S.x, y = S.y + 8, w = 1, h = S.h - 16, sprite = 'none', color = {0.95, 0.95, 0.93, 0.22}} end
    E.strip.pbg = image{anchor = 'tl', x = S.x, y = S.y + S.h - 10, w = 100, h = 3, sprite = 'none', color = {0.95, 0.95, 0.93, 0.18}}
    E.strip.pfill = image{anchor = 'tl', x = S.x, y = S.y + S.h - 10, w = 100, h = 3, sprite = 'none', fill = 'horizontal', amount = 0, color = AMBER}
    E.strip_geom = S
    -- the alert row: the game's banners, joined under the strip while any is up (up to 3 side by side)
    local AY = S.y + S.h
    E.alert = {bg = image{anchor = 'tl', x = S.x, y = AY, w = S.w, h = ALERT_H, sprite = 'none', color = {0.043, 0.051, 0.063, 0.78}, visible = false},
               rule = image{anchor = 'tl', x = S.x + 12, y = AY, w = S.w - 24, h = 1, sprite = 'none', color = {0.95, 0.95, 0.93, 0.14}, visible = false},
               mark = image{anchor = 'tl', x = S.x, y = AY + 6, w = 3, h = ALERT_H - 12, sprite = 'none', color = AMBER, visible = false},
               segs = {}, divs = {}, hbg = {}, hfill = {}}
    for i = 1, 3 do
        E.alert.segs[i] = text{anchor = 'tl', x = S.x, y = AY + 8, w = 100, h = 22, size = 16, align = 'left', text = '', visible = false}
        E.alert.hbg[i] = image{anchor = 'tl', x = S.x, y = AY + ALERT_H - 6, w = 100, h = 2, sprite = 'none', color = {0.95, 0.95, 0.93, 0.18}, visible = false}
        E.alert.hfill[i] = image{anchor = 'tl', x = S.x, y = AY + ALERT_H - 6, w = 100, h = 2, sprite = 'none', fill = 'horizontal', amount = 0,
                                 color = {1, 0.35, 0.3, 1}, visible = false}
    end
    for i = 1, 2 do E.alert.divs[i] = image{anchor = 'tl', x = S.x, y = AY + 7, w = 1, h = ALERT_H - 14, sprite = 'none', color = {0.95, 0.95, 0.93, 0.22}, visible = false} end
    for i = 1, 3 do
        local top = 504 + (i - 1) * 90
        E.rings[i] = {
            disc = image{anchor = 'tr', x = 29, y = top, w = 72, h = 72, sprite = 'disc', color = {0.043, 0.051, 0.063, 0.6}},
            halo = image{anchor = 'tr', x = 25, y = top - 4, w = 80, h = 80, sprite = 'ring_thin', color = {1, 0.745, 0.031, 0.4}},
            track = image{anchor = 'tr', x = 29, y = top, w = 72, h = 72, sprite = 'ring', color = {0.95, 0.95, 0.93, 0.2}},
            ring = image{anchor = 'tr', x = 29, y = top, w = 72, h = 72, sprite = 'ring', fill = 'radial', clockwise = true, amount = 1},
            icon = image{anchor = 'tr', x = 47, y = top + 18, w = 36, h = 36, sprite = 'none'},
            num = text{anchor = 'tr', x = 29, y = top + 22, w = 72, h = 30, size = 24, align = 'center', text = ''},
            chip = image{anchor = 'tr', x = 61, y = top - 8, w = 50, h = 18, sprite = 'none', color = {0.043, 0.051, 0.063, 0.85}},
            key = text{anchor = 'tr', x = 61, y = top - 8, w = 50, h = 18, size = 13, align = 'center', text = KEYS[i], color = AMBER},
            label = text{anchor = 'tr', x = 115, y = top + 12, w = 320, h = 48, size = 18, align = 'right', text = ''},
        }
    end
    -- weapons, bottom centre (anchor 'b': x from the screen centre, y up from the bottom edge). Glow first (behind).
    GROUP = E.gw
    E.wglow = image{anchor = 'b', x = 0, y = WY.icon - 26, w = 320, h = 124, sprite = 'scrim', color = {1, 1, 1, 0.3}, visible = false}
    E.wslots = {}
    for i = 1, 5 do
        E.wslots[i] = {
            icon = image{anchor = 'b', x = 0, y = WY.icon, w = 168, h = 72, sprite = 'none', visible = false},
            name = text{anchor = 'b', x = 0, y = WY.name, w = 380, h = 22, size = 17, align = 'center', text = '', visible = false},
        }
    end
    E.reloadbg = image{anchor = 'b', x = 0, y = WY.reload, w = 168, h = 3, sprite = 'none', color = {0, 0, 0, 0.6}, visible = false}
    E.reload = image{anchor = 'b', x = 0, y = WY.reload, w = 168, h = 3, sprite = 'none', fill = 'horizontal', amount = 0, color = AMBER, visible = false}
    E.ammo = text{anchor = 'b', x = 0, y = WY.ammo, w = 290, h = 46, size = 43, align = 'center', text = '', visible = false}
    E.wtag = text{anchor = 'b', x = 0, y = WY.tag, w = 240, h = 19, size = 14, align = 'center', text = '', visible = false}
    -- gear (turret, grenade), bottom right
    GROUP = E.gg
    E.gear = {}
    for i = 1, 2 do
        local x = 25 + (2 - i) * 66
        E.gear[i] = {
            bg = image{anchor = 'br', x = x, y = 25, w = 56, h = 56, sprite = 'none', color = PANEL},
            icon = image{anchor = 'br', x = x + 6, y = 31, w = 44, h = 44, sprite = 'none'},
            key = text{anchor = 'br', x = x + 36, y = 63, w = 18, h = 16, size = 12, align = 'left', text = '', color = AMBER},
            num = text{anchor = 'br', x = x + 4, y = 27, w = 40, h = 20, size = 16, align = 'right', text = ''},
        }
    end
    GROUP = nil
end

-- ------------------------------------------------------------------------------------------ state --
local hud_key = nil
local loadout, wcache, ammo_max   -- weapons state (defined below, reset per HUD)
local loadout_base, exact, exact_why, weapons_since, loadout_sig
local silenced = false
local id_field = nil
local slot = {}            -- per own ability: cooldown / active tracking
local downed_since = {}
local mission_start = nil
local banner_seen = {}     -- banner field -> {text, since}: when its text last changed
local banners_merged = nil -- whether the native banners are hidden now (they are restored when the setting is off)
local obj_seen = false     -- the strip has read the native objective text at least once (only then is it hidden)
local id_cache = {}        -- card name -> the last identity found for it (this HUD)
local clear = {}           -- native dialog field -> {shift (screen fraction moved down), last top seen, ph (parent height)}

local function on_new_hud(runner, hud)
    slot, downed_since, id_field, mission_start, obj_seen = {}, {}, nil, nil, false
    loadout, wcache, ammo_max, loadout_base, exact = nil, {}, {}, nil, nil
    exact_why, weapons_since, loadout_sig = nil, nil, nil
    silenced = false
    za.hud.nudge_reset()
    clear, id_cache, banners_merged, banner_seen = {}, {}, nil, {}
    local nameText = get(hud, 'weaponDisplay', 'info', 'nameText')
    if nameText then za.ui.use_font(nameText) end
end

-- The game's own dialogs that can show at the top of the screen (GameHud fields). What each shows (its text and images,
-- za.hud.content_rect) is measured every frame while it is up and, if it reaches up into the strip, moved down just
-- clear of it (remembered, so it appears clear next time).
-- Dialogs are moved by their anchors: some are animated, and the game sets their position every frame.
--   ingameToastDisplay: objective toasts, including "get to the extraction zone" with its countdown (the objective
--     container's objectiveToast is the same object);  healthBarDisplayRoot: boss health bar;
--   introDisplay: mission intro / mission complete and failed header;  notificationDisplayContainer: notifications;
--   bountyCompleteDisplayContainer: bounty complete;  tutorialHintDisplay: tutorial hints;  revivePopup: revive prompt;
--   selfRevives / teamRevives: revive counters.
-- Hidden instead (the custom HUD replaces them): the objective rows (incl. ExtractionDisplay) and the dialogue box.
local NATIVE_DIALOGS = {'ingameToastDisplay', 'healthBarDisplayRoot', 'introDisplay', 'notificationDisplayContainer',
                        'bountyCompleteDisplayContainer', 'tutorialHintDisplay', 'revivePopup', 'selfRevives', 'teamRevives'}
local STRIP_BOTTOM = (25 + 50 + 8) / 1080   -- the strip's bottom edge (plus a gap), as a fraction of the screen height
local STRIP_LEFT, STRIP_RIGHT = 285 / 1920, 1825 / 1920   -- and its sides, as fractions of the width
-- The game's banners that the top bar takes over (the "Game banners in the top bar" setting): their text shows in an
-- alert row under the strip, and the native ones are hidden. The revive popup has buttons, so it stays (kept clear).
--   ingameToastDisplay: objective toasts ("Get to the ship 00:30");  healthBarDisplayRoot: boss / objective health bar (hidden,
--   not shown in the strip: the HP Numbers mod shows the health of what you aim at);
--   introDisplay: mission intro, mission complete / failed;  notificationDisplayContainer: notifications;
--   tutorialHintDisplay: tutorial hints;  bountyCompleteDisplayContainer: bounty complete.
local BANNERS = {{k = 'ingameToastDisplay'}, {k = 'healthBarDisplayRoot', hide_only = true}, {k = 'introDisplay'},
                 {k = 'notificationDisplayContainer'}, {k = 'tutorialHintDisplay'}, {k = 'bountyCompleteDisplayContainer'}}
local BANNER_FIELD = {}
for _, B in ipairs(BANNERS) do BANNER_FIELD[B.k] = true end
local function keep_clear(hud, merged)
    for _, k in ipairs(NATIVE_DIALOGS) do
        local ref = not (merged and BANNER_FIELD[k]) and hud:get(k) or nil
        if ref then
            local C = clear[k]
            if not C then C = {shift = 0}; clear[k] = C end
            local now = za.time()
            if ref:active() and now - (C.measured or 0) >= 0.3 then  -- (measured 3 times a second: it is many calls)
                C.measured = now
                -- what the dialog shows (its text and images), not its container, which can span the whole screen
                local l, t, r, b = za.hud.content_rect(ref)
                -- moved only once it has settled (two readings in a row agree: it may be sliding in) and is fully on
                -- screen (a toast rests above the top edge while it waits), and only while it overlaps the strip
                -- sideways; it is moved down just clear of the strip, or back up if it ended up lower than needed
                if t and t >= 0 and C.last and math.abs(t - C.last) < 0.001 and r > STRIP_LEFT and l < STRIP_RIGHT then
                    if t < STRIP_BOTTOM or (C.shift > 0 and t > STRIP_BOTTOM + 0.005) then
                        C.shift = math.max(0, math.min(0.3, C.shift + STRIP_BOTTOM - t))
                    end
                end
                C.last = t
            end
            if C.shift > 0 then
                -- by its anchors (some dialogs are animated: their position is set by the game every frame), as a
                -- fraction of the parent's height
                if not C.ph then
                    local par = ref:parent()
                    local _, pt, _, pb = za.hud.rect(par or ref)
                    C.ph = (pt and pb and pb - pt > 0.05) and (pb - pt) or 1
                end
                za.hud.nudge_anchor(ref, C.shift / C.ph)
            end
        end
    end
end

local function merge_banners(hud, merge)
    if merge == banners_merged then return end
    banners_merged = merge
    for _, B in ipairs(BANNERS) do local ref = hud:get(B.k); if ref then za.hud.scale(ref, merge and 0 or 1) end end
end

local function hide_native(hud, hub)
    -- the notices at the top right ("New equipment available", "New bounties available", "Upgrade available")
    if za.setting('notices') ~= false then
        local n = hud:get('notificationDisplayContainer')
        if n then
            za.hud.scale(n, 0)
        end
    end
    if not OPT.hide_native then return end
    for _, k in ipairs({'activatedPowersList', 'weaponInfoList'}) do
        local t = hud:get(k); if t then za.hud.scale(t, 0) end
    end
    local roots = hud:get('deployableSlotRoots')
    if roots then for i = 1, roots:len() do local t = roots:at(i); if t then za.hud.scale(t, 0) end end end
    -- the native player cards (yours too when the squad panel has a row for you); their data is still read below
    local cards = hud:get('playerInfos')
    if cards then
        for i = (OPT.self_row and 1 or 2), cards:len() do
            local c = cards:at(i) and cards:at(i):get('info'); if c then za.hud.scale(c, 0) end
        end
    end
    -- the game's own top dialogs (toasts, extraction countdown, boss bar, ...) clear of the strip
    local merge = OPT.strip and not hub and za.setting('banners') ~= false
    if merge then for _, B in ipairs(BANNERS) do local ref = hud:get(B.k); if ref then za.hud.scale(ref, 0) end end end
    merge_banners(hud, merge)
    if OPT.strip and not hub then keep_clear(hud, merge) end
    if OPT.hide_dialogue then
        -- the dialogue box (portrait and text) hidden, and its text sounds silenced (the game still runs the dialogue,
        -- unskipped; the narrator's voice is the game's own setting)
        local d = hud:get('dialogue')
        if d then
            za.hud.scale(d, 0)
            if not silenced then
                silenced = true
                za.hud.silence(d, 'pageAppearEvent', 'letterAppearEvent')
            end
        end
    end
    if OPT.strip and OPT.hide_objectives and obj_seen then
        local o = hud:get('objectiveDisplayContainer'); if o then za.hud.scale(o, 0) end
    end
end

-- ------------------------------------------------------------------------------------------ squad --
-- Rows come from the native player cards (hub and missions alike), you at the bottom. Each card's name is matched to
-- the game's player data for class / specialisation / level: the mission squad, else the server's client list (the
-- hub). Your card is the one the game marks as the local player. HP: yours from your own entity, teammates' from their
-- native health bar. Status icons (extraction, etc.) are the card's own.
local function plain(s) return ((s or ''):gsub('<[^>]->', ''):gsub('^%s+', ''):gsub('%s+$', '')) end
local function est(s, size) local p = plain(s); return math.floor((utf8.len(p) or #p) * size * 0.56 + 0.5) end

-- Server client list: by lower-case display name, plus the count
local function client_index(runner)
    local cur = runner:get('clientUpdateResult')
    local clients = cur and cur:get('clients')
    local n = cur and cur:get('client_count') or 0
    local byName = {}
    if clients then
        for i = 1, math.min(n, 64) do
            local c = clients:at(i)
            local dn = c and c:get('display_name')
            if dn then byName[dn:lower()] = c end
        end
    end
    return byName, n, clients
end

-- The native cards in use: {card, info (PlayerInfoDisplay), name, isLocal}
local function native_cards(hud)
    local out = {}
    local cards = hud:get('playerInfos')
    for i = 1, cards and math.min(cards:len(), 8) or 0 do
        local card = cards:at(i) and cards:at(i):get('info')
        local info = card and card:get('info')
        local nt = info and info:get('nameText')
        local name = nt and plain(nt:text())
        -- a player who left: the game hides their card, or keeps it with its "disconnected" icon on; either way no row
        local gone = card and card:get('disconnectIcon')
        gone = gone and gone:active() or false
        if card and card:active() and not gone and name and name ~= '' then
            local li = info:get('localPlayerIndicator')
            -- (texts: what the card shows, read once per update; matching cards to players reads it many times)
            out[#out + 1] = {card = card, info = info, name = name, isLocal = li and li:active() or false, slot = i,
                             texts = info:texts() or {}}
        end
    end
    return out
end

local my_name = nil       -- your display name, once known
-- Your card. The game marks it (localPlayerIndicator) in missions, but not in the hub's party cards. There it is the
-- card showing your party entry's level and class (the cards show account names, the party character names, so names
-- can't be compared); once found, your name is remembered for the session.
local function card_fits(c, level, className, subclassName)
    local texts = c.texts or c.info:texts() or {}
    local lvl, cls, sub = tostring(level or ''), (className or ''):lower(), (subclassName or ''):lower()
    local hasLevel, hasClass = false, cls == '' and sub == ''
    for _, t in ipairs(texts) do
        local p = plain(t):lower()
        if p == lvl then hasLevel = true end
        if (cls ~= '' and p:find(cls, 1, true)) or (sub ~= '' and p:find(sub, 1, true)) then hasClass = true end
    end
    return hasLevel, hasClass, texts
end
local function card_matches(c, pi) return card_fits(c, pi:get('level'), pi:get('className'), pi:get('subclassName')) end
local function find_me(list, runner)
    for _, c in ipairs(list) do if c.isLocal then my_name = c.name; return c end end
    for _, c in ipairs(list) do if my_name and c.name:lower() == my_name:lower() then return c end end
    if #list == 1 then my_name = list[1].name; return list[1] end
    -- no card marked: match your party entry's level (and class, when two cards share it)
    local party = runner and get(runner, 'party', 'players')
    local mine = nil
    for i = 1, party and math.min(party:len(), 8) or 0 do
        local tp = party:at(i)
        if tp and tp:get('isLocalPlayer') == true then mine = tp:get('playerInfo') end
    end
    if mine then
        local byLevel, byBoth = {}, {}
        for _, c in ipairs(list) do
            local lv, cl = card_matches(c, mine)
            if lv then byLevel[#byLevel + 1] = c end
            if lv and cl then byBoth[#byBoth + 1] = c end
        end
        local pick = (#byLevel == 1 and byLevel[1]) or (#byBoth == 1 and byBoth[1]) or nil
        if pick then my_name = pick.name; return pick end
    end
    return list[1]
end

-- Your own entry in the server client list (KILLS, and your class in the hub). The list names players by character
-- name, not by the account name the cards show, so it is matched by the game's own id for this client.
local function my_client(runner)
    local cur = runner:get('clientUpdateResult')
    local clients = cur and cur:get('clients')
    local n = cur and cur:get('client_count') or 0
    if not clients or n < 1 then return nil end
    local myId = cur:get('client_id')
    local found = nil
    for i = 1, math.min(n, 64) do
        local c = clients:at(i)
        if c and myId and c:get('id') == myId then found = c; break end
    end
    if not found and n == 1 then found = clients:at(1) end
    return found
end

-- A card's player: the mission squad and the server client list are both in card order (you first), while their names
-- (character names) differ from the account names the cards show, so cards are matched by slot. In the hub (no squad;
-- the client list is everyone in the hub) only your own entry is known, by client id.
-- The team the cards belong to: the mission squad in a mission, else your party (the hub). Both list each player
-- (Team.TeamPlayer: isLocalPlayer, playerInfo with class / specialisation / level); re-read every frame, so players
-- joining or leaving the party show up at once.
local function team_entries(runner, hub)
    -- (in the hub the mission squad is the last mission's, kept after it ends: the party is used there)
    local list = not hub and get(runner, 'lobby', 'missionSquad', 'players') or nil
    local squad = list and list:len() > 0
    if not squad then list = get(runner, 'party', 'players') end
    local out = {squad = squad}
    for i = 1, list and math.min(list:len(), 8) or 0 do
        local tp = list:at(i)
        local pi = tp and tp:get('playerInfo')
        if pi then out[#out + 1] = {pi = pi, isLocal = tp:get('isLocalPlayer') == true, slot = i} end
    end
    return out
end

-- A card's player. In a mission the cards are in the squad's own order, so a card's slot is its squad entry. In the
-- hub (party): by name, else yours by isLocalPlayer, else the others in order. Without a team (single player before
-- the squad exists), from the server's client list (yours by client id).
local function identity(runner, team, list, me, c)
    local found, how = nil, nil
    -- 1. the same name (where the team's names are the ones the cards show)
    for _, e in ipairs(team) do
        local dn = e.pi:get('displayName')
        if dn and plain(dn):lower() == c.name:lower() then found, how = e.pi, 'name' end
    end
    -- 2. your card: the entry the game marks as you
    if not found and c == me then for _, e in ipairs(team) do if e.isLocal then found, how = e.pi, 'you' end end end
    -- 3. a teammate's card: the one entry showing the card's level (and class, when two share the level). The cards and
    --    the team list are not always in the same order, so this comes before position.
    if not found and c ~= me then
        local byLevel, byBoth = {}, {}
        for _, e in ipairs(team) do
            if not e.isLocal then
                local lv, cl = card_matches(c, e.pi)
                if lv then byLevel[#byLevel + 1] = e.pi end
                if lv and cl then byBoth[#byBoth + 1] = e.pi end
            end
        end
        if #byLevel == 1 then found, how = byLevel[1], 'level'
        elseif #byBoth == 1 then found, how = byBoth[1], 'level+class' end
    end
    -- 4. position in the mission squad (only while the squad and the cards agree on how many players there are)
    if not found and team.squad and #team == #list then
        for _, e in ipairs(team) do if e.slot == c.slot then found, how = e.pi, 'slot' end end
    end
    -- 5. the others in order
    if not found and c ~= me then
        local others, mates = {}, {}
        for _, e in ipairs(team) do if not e.isLocal then others[#others + 1] = e end end
        for _, x in ipairs(list) do if x ~= me then mates[#mates + 1] = x end end
        if #others == #mates then for i, x in ipairs(mates) do if x == c then found, how = others[i].pi, 'order' end end end
    end
    if found then
        return {className = found:get('className'), subclassName = found:get('subclassName'), level = found:get('level'),
                isLeader = found:get('isLeader'), how = how}
    end
    local cl = c == me and my_client(runner) or nil  -- (the client list is in join order, not card order: yours only)
    if cl then return {className = cl:get('class_name'), subclassName = cl:get('subclass_name'), level = cl:get('player_level'), how = 'client'} end
    return {}
end

local function own_hp(runner)
    local cur = runner:get('clientUpdateResult')
    local id = cur and cur:get('client_controlling_entity_id')
    id = id and id:get('id')
    if not id then return nil end
    for _, e in ipairs(za.entities('Player')) do
        if e.id == id and e.max_hp and e.max_hp > 0 then return e.hp / e.max_hp, e.shield or 0, e.shield_max or 0 end
    end
    return nil
end

-- Teammates' live health. Player characters are created in join order, which is also the order of the teammate cards,
-- so they are paired by order (yours left out); a pairing is only used while that character's max HP matches the
-- card's, else the card's own bar is shown.
local function teammate_hp(runner, list, me, c)
    local cur = runner:get('clientUpdateResult')
    local selfId = cur and cur:get('client_controlling_entity_id')
    selfId = selfId and selfId:get('id')
    local players, byId = {}, {}
    for _, e in ipairs(za.entities('Player')) do
        if e.max_hp and e.max_hp > 0 then byId[e.id] = e; if e.id ~= selfId then players[#players + 1] = e end end
    end
    table.sort(players, function(a, b) return a.id < b.id end)
    local mates = {}
    for _, x in ipairs(list) do if x ~= me then mates[#mates + 1] = x end end
    local e = nil
    for i, x in ipairs(mates) do if x == c then e = (#players == #mates) and players[i] or nil end end
    if not e then return nil end
    local cardMax = c.card:get('lastHealthMaxValue') or 0
    if cardMax > 0 and math.abs(cardMax - e.max_hp) > 1 then return nil end
    return e.hp / e.max_hp, e.shield or 0, e.shield_max or 0
end

local function row_geometry(R, r, w)
    -- anchor 'bl': y is the distance from the screen bottom to the element's bottom edge; rows stack upwards
    local y0 = SQUAD_BOTTOM + (r - 1) * 76
    local function at(oy, h) return y0 + 70 - oy - h end
    za.ui.set(R.bg, {y = at(0, 70), w = w})
    za.ui.set(R.stripe, {y = at(0, 70)})
    za.ui.set(R.name, {y = at(6, 24)})
    za.ui.set(R.class, {y = at(31, 22)})
    za.ui.set(R.readout, {y = at(7, 22), x = SQUAD_X + w - 210})
    for p = 1, 3 do za.ui.set(R.pips[p], {y = at(37, 10), x = SQUAD_X + w - 54 + (p - 1) * 16}) end
    for k = 1, 3 do za.ui.set(R.status[k], {y = at(5, 22)}) end
    za.ui.set(R.hpbg, {y = at(66, 4), w = w}); za.ui.set(R.hp, {y = at(66, 4), w = w}); za.ui.set(R.sh, {y = at(64, 2), w = w})
end

-- The other players actually present, as {level, className, subclassName}: in the hub, your party's other members; in
-- a mission, the server's connected clients other than you. The game's cards can outlive players (someone who left
-- the party or the match, the last match's squad after it ends), so these decide which cards get a row. nil when
-- neither list can be read (the cards are then shown as they are).
local function others_present(runner, hub)
    local out = {}
    if hub then
        local party = get(runner, 'party', 'players')
        if not party then return nil end
        for i = 1, math.min(party:len(), 8) do
            local tp = party:at(i)
            local pi = tp and tp:get('isLocalPlayer') ~= true and tp:get('playerInfo')
            if pi then out[#out + 1] = {pi:get('level'), pi:get('className'), pi:get('subclassName')} end
        end
        return out
    end
    local cur = runner:get('clientUpdateResult')
    local clients, n, myId = cur and cur:get('clients'), cur and cur:get('client_count') or 0, cur and cur:get('client_id')
    if not clients or n < 1 then return nil end
    for i = 1, math.min(n, 16) do
        local c = clients:at(i)
        if c and c:get('id') ~= myId and c:get('disconnected') ~= true then
            out[#out + 1] = {c:get('player_level'), c:get('class_name'), c:get('subclass_name')}
        end
    end
    return out
end

-- The cards to show: yours, then one per other player present, each taking the card that shows its level and class
-- (else its level; else the next card left). Never more rows than players present.
local function present_cards(runner, list, hub)
    local others = others_present(runner, hub)
    if not others then return list end
    local me = #list > 0 and find_me(list, runner) or nil
    local taken, out = {}, {}
    if me then taken[me] = true; out[1] = me end
    local function claim(o, needClass)
        for _, c in ipairs(list) do
            if not taken[c] then
                local lv, cl = card_fits(c, o[1], o[2], o[3])
                if lv and (cl or not needClass) then taken[c] = true; return c end
            end
        end
    end
    local picked = {}
    for i, o in ipairs(others) do picked[i] = claim(o, true) end
    for i, o in ipairs(others) do picked[i] = picked[i] or claim(o, false) end
    for i = 1, #others do
        if not picked[i] then
            for _, c in ipairs(list) do if not taken[c] then taken[c] = true; picked[i] = c; break end end
        end
    end
    for i = 1, #others do if picked[i] then out[#out + 1] = picked[i] end end
    return out
end

-- sprites that are plain backdrops (Unity's built-in UI sprites), never a status picture
local GENERIC_SPRITE = {white = true, uisprite = true, background = true, knob = true, inputfieldbackground = true,
                        uimask = true, checkmark = true, dropdownarrow = true, none = true}
local function update_squad(runner, hud, hub)
    local list = present_cards(runner, native_cards(hud), hub)
    local me = find_me(list, runner)
    local rows = {}
    if me then rows[1] = me end
    for _, c in ipairs(list) do if c ~= me and #rows < 4 then rows[#rows + 1] = c end end
    local team = team_entries(runner, hub)
    local byName = client_index(runner)
    local now = za.time()
    for r, c in ipairs(rows) do
        local R = E.squad[r]
        -- a player's class, specialisation and level don't change during a mission: once found they are kept, so a
        -- frame where the squad list and the cards disagree (players joining, leaving, re-syncing) shows the last ones
        local id = identity(runner, team, list, me, c)
        if id.className and id.className ~= '' then id_cache[c.name] = id else id = id_cache[c.name] or id end
        local cls = (id.className or ''):lower()
        local sub = id.subclassName or ''
        -- some players report a specialisation as their class: show its class, with the specialisation beside it
        if SUBCLASS[cls] then
            if sub == '' then sub = id.className end
            cls = SUBCLASS[cls][2]
        end
        local level = id.level or 0
        local col = CLASS[cls] or {cls ~= '' and cls:upper() or '', WHITE}
        local card = c.card
        -- health
        local frac, sh, shmax
        if c == me then frac, sh, shmax = own_hp(runner) else frac, sh, shmax = teammate_hp(runner, list, me, c) end
        if not frac then
            local bar = card:get('healthBar')
            local f = bar and bar:fill()
            local maxhp = math.max(1, card:get('lastHealthMaxValue') or 1)
            frac = f or ((card:get('lastHealthValue') or 0) / maxhp)
            local sbar = card:get('shieldHealthBar')
            sh, shmax = sbar and sbar:active() and (sbar:fill() or 0) or 0, 1
        end
        frac = math.max(0, math.min(1, frac or 0))
        local revive = card:get('reviveIcon')
        local downed = revive and revive:active() or false
        if downed then downed_since[c.name] = downed_since[c.name] or now else downed_since[c.name] = nil end
        local readout = ''
        if downed then
            local s = math.floor(now - downed_since[c.name])
            readout = string.format('<color=#FF5A4D>DOWNED \\</color> %d:%02d', s // 60, s % 60)
        end
        -- status icons: the native card's status indicators (boarded the extraction plane, spectating, progress, ...),
        -- from the card's own list; the indicators under its status row if the list is empty
        local icons = {}
        local inds = card:get('statusIndicators')
        local found = {}
        for i = 1, inds and inds:len() or 0 do
            local si = inds:at(i)
            local go = si and si:get('gameObject')
            if si and (not go or go:active()) then found[#found + 1] = {si:get('indicator'), si:get('idString')} end
        end
        if #found == 0 then
            local sroot = card:get('statusListRoot')
            for _, ind in ipairs(sroot and sroot:components('GameHudStatusIndicator', false) or {}) do found[#found + 1] = {ind, '?'} end
        end
        for _, f in ipairs(found) do
            local img = f[1] and f[1]:get('icon')
            local spr = img and img:sprite()
            if not spr and f[1] then  -- (the boarded indicator draws its picture on another image inside it)
                for _, im in ipairs(f[1]:components('Image', false)) do
                    local sp = im:sprite()
                    if sp and not spr and not GENERIC_SPRITE[tostring(sp:name()):lower()] then spr = sp end
                end
            end
            if spr and #icons < 3 then icons[#icons + 1] = spr end
        end
        -- text and width: the panel grows to fit (for example while DOWNED is showing)
        -- [L]: the card's own leader marker (the game's own, on the same card as the name), else the team entry's
        local li = c.info:get('leaderIndicator')
        local isLeader = li and li:active()
        if isLeader == nil then isLeader = id.isLeader end
        local leader = isLeader and '<color=#FFBE08>[L]</color> ' or ''
        local you = ''
        local lv = level > 0 and ('<color=#FFBE08>' .. level .. '</color>  ') or ''
        local nameLine = you .. leader .. lv .. trunc(c.name, 16):upper()
        local subTitle = (sub ~= '') and subclass_name(sub) or ''
        local classLine = col[1] ~= '' and ('<color=' .. hex(col[2]) .. '>' .. col[1] .. '</color>' .. (subTitle ~= '' and ('  · ' .. trunc(subTitle, 18)) or '')) or ''
        local need = 12 + est(nameLine, 18) + 14 + #icons * 26 + (downed and (est(readout, 16) + 14) or 0) + 12
        local w = math.max(340, need)
        row_geometry(R, r, w)
        za.ui.set(R.bg, {visible = true})
        za.ui.set(R.stripe, {visible = true, color = col[2]})
        za.ui.set(R.name, {visible = true, text = nameLine, w = w - 24, color = downed and GREY or WHITE})
        za.ui.set(R.class, {visible = true, text = classLine})
        za.ui.set(R.readout, {visible = downed, text = readout})
        local ix = SQUAD_X + w - 12 - (downed and (est(readout, 16) + 14) or 0)
        for k = 1, 3 do
            if icons[k] then
                ix = ix - 26
                za.ui.set(R.status[k], {visible = true, sprite = icons[k], x = ix})
            else
                show(R.status[k], false)
            end
        end
        for p = 1, 3 do show(R.pips[p], false) end  -- (ability state is shown by the rings on the right only)
        za.ui.set(R.hpbg, {visible = true})
        za.ui.set(R.hp, {visible = true, amount = frac, color = downed and GREY or hp_colour(frac)})
        za.ui.set(R.sh, {visible = (shmax or 0) > 0 and (sh or 0) > 0, amount = (shmax or 0) > 0 and (sh or 0) / shmax or 0})
    end
    for r = #rows + 1, 4 do
        local R = E.squad[r]
        for _, id in ipairs({R.bg, R.stripe, R.name, R.readout, R.class, R.hpbg, R.hp, R.sh}) do show(id, false) end
        for p = 1, 3 do show(R.pips[p], false) end
        for k = 1, 3 do show(R.status[k], false) end
    end
end

-- ------------------------------------------------------------------------------------------ abilities --
-- A HUD ability slot -> its AbilityDefinitionSO, through the slot's cacheId (a WorldLoadDataId) indexing the mission's
-- ability definition list.
id_field = nil  -- (declared with the HUD state) the cacheId field that indexes abilityDefinitions, chosen per mission
local function choose_id_field(runner, displays)
    local defs = get(runner, 'worldController', 'loadDataBuilder', 'abilityDefinitions')
    if not defs or not displays or displays:len() < 2 then return nil end
    local first = displays:at(1) and displays:at(1):get('cacheId')
    if not first then return nil end
    if type(first:get('index')) == 'number' then id_field = 'index'; return id_field end   -- (WorldLoadDataId.index)
    for _, f in ipairs(first:fields()) do
        local vals, distinct, ok = {}, {}, true
        for i = 1, math.min(3, displays:len()) do
            local id = displays:at(i) and displays:at(i):get('cacheId')
            local v = id and id:get(f)
            if type(v) ~= 'number' or v < 0 or v >= defs:len() then ok = false end
            vals[#vals + 1] = tostring(v); distinct[tostring(v)] = true
        end
        local n = 0; for _ in pairs(distinct) do n = n + 1 end
        if ok and n == #vals and not id_field then id_field = f end
    end
    return id_field
end
local function ability_def(runner, display)
    local defs = get(runner, 'worldController', 'loadDataBuilder', 'abilityDefinitions')
    local id = display and display:get('cacheId')
    if not defs or not id or not id_field then return nil end
    local v = id:get(id_field)
    if type(v) ~= 'number' then return nil end
    local def = defs:at(v + 1)
    return def, id_field, v
end
local function update_abilities(runner, hud)
    local displays = hud:get('activatedAbilityDisplays')
    local cls = get(runner, 'lobby', 'missionSquad', 'players')
    cls = cls and cls:at(1) and cls:at(1):get('playerInfo')
    cls = cls and (cls:get('className') or ''):lower()
    if not id_field then choose_id_field(runner, displays) end
    local now = za.time()
    local enemies = #za.entities('Enemy') > 0
    -- the simulation's own ability slots and the world clock their timings are in: exact cooldown and active time left
    local ctrl = za.input.abilities and get(runner, 'characterInputManager', 'mainPlayer') or nil
    local sim = ctrl and za.input.abilities(ctrl) or nil
    local wt = sim and get(runner, 'worldController', '<GameWorldTime>k__BackingField') or nil
    if type(wt) ~= 'number' then sim = nil end
    for i = 1, 3 do
        local R = E.rings[i]
        local display = displays and displays:at(i)
        local gs = display and display:get('gearSlot')
        if not gs then
            for _, id in pairs(R) do show(id, false) end
        else
            local S = slot[i] or {state = 'ready'}
            slot[i] = S
            if not S.def then
                local def, _, idx = ability_def(runner, display)
                if def then
                    S.def = def
                    S.index = idx
                    S.name = title_of(def:get('title'), cls)
                    S.cooldown = def:get('cooldown') or 0
                    S.duration = def:get('abilityDuration') or 0
                end
            end
            local fillImg = gs:get('cooldownFill')
            local fill = fillImg and fillImg:fill() or 0
            local fillShown = fillImg and fillImg:active()
            local active = gs:get('isActiveBorder') and gs:get('isActiveBorder'):active() or false
            local amount = gs:get('amount') or 65535
            local state = active and 'active' or ((fillShown and fill > 0.002 and fill < 0.998) and 'cooldown' or 'ready')
            if amount == 0 then state = 'empty' end
            -- this slot in the simulation (matched by its ability's index): its state is the game's own
            local ss = nil
            if sim and S.index then for _, x in ipairs(sim) do if x.index == S.index then ss = x end end end
            if ss then
                if ss.state == 'Active' then state = 'active'
                elseif ss.state == 'InCooldown' then state = 'cooldown'
                elseif ss.state == 'OutOfUses' then state = 'empty'
                elseif ss.state == 'Ready' then state = 'ready' end
            end
            if state ~= S.state then
                if state == 'cooldown' then S.f0, S.t0, S.rate, S.shown = nil, nil, nil, nil end
                if state == 'active' then S.active_since = now end
                S.state, S.changed_at = state, now
            end
            local num, ringAmount, ringColor, stateText, showIcon = '', 1, WHITE, 'READY', true
            if state == 'active' then
                ringColor, stateText = AMBER, 'ACTIVE'
                if S.duration and S.duration > 0 and S.active_since then
                    local left = math.max(0, S.duration - (now - S.active_since))
                    if ss and ss.last_trigger > 0 then   -- exact, from the simulation
                        local el = ss.last_trigger + S.duration - wt
                        if el >= 0 and el <= S.duration + 0.5 then left = el end
                    end
                    ringAmount = left / S.duration
                    num = string.format('%d', math.ceil(left)); stateText = 'ACTIVE \\\\ ' .. num .. 's'; showIcon = false
                end
            elseif state == 'cooldown' then
                -- The native fill moves at a steady rate: its direction and speed are measured (readings 0.3 s or more
                -- apart, smoothed), so the seconds left are the distance to its end over that speed, whatever the
                -- real cooldown is (upgrades and buffs change it) and however far it got during the active phase.
                if not S.f0 then S.f0, S.t0 = fill, now end
                local dt = now - S.t0
                if dt >= 0.3 and math.abs(fill - S.f0) > 0.0005 then
                    local r = (fill - S.f0) / dt
                    S.rate = (S.rate and (S.rate > 0) == (r > 0)) and (S.rate * 0.6 + r * 0.4) or r
                    S.f0, S.t0 = fill, now
                end
                if S.rate then
                    local remaining = S.rate < 0 and fill or (1 - fill)
                    ringAmount = 1 - remaining
                    local secs = math.ceil(remaining / math.abs(S.rate))
                    if S.shown and secs > S.shown then secs = S.shown end  -- never counts up within a cycle
                    S.shown = secs
                    num = tostring(secs); stateText = secs .. 's'; showIcon = false
                else
                    ringAmount, stateText = 0, 'COOLDOWN'  -- (the first 0.3 s, while the speed is measured)
                end
                -- exact, from the simulation: the cooldown's start and length against the world clock
                if ss and ss.cooldown > 0 then
                    local el = ss.cooldown_start + ss.cooldown - wt
                    if el >= 0 and el <= ss.cooldown + 0.5 then
                        local secs = math.ceil(el)
                        ringAmount = 1 - el / ss.cooldown
                        num = tostring(secs); stateText = secs .. 's'; showIcon = false
                    end
                end
            elseif state == 'empty' then
                ringColor, stateText = GREY, '0 LEFT'
            end
            local sprite = gs:get('icon') and gs:get('icon'):sprite()
            za.ui.set(R.disc, {visible = true}); za.ui.set(R.track, {visible = true})
            za.ui.set(R.ring, {visible = true, amount = ringAmount, color = ringColor})
            za.ui.set(R.halo, {visible = state == 'active'})
            za.ui.set(R.icon, {visible = showIcon and sprite ~= nil, sprite = sprite or 'none', color = state == 'empty' and GREY or WHITE})
            za.ui.set(R.num, {visible = num ~= '', text = num, color = state == 'active' and AMBER or WHITE})
            za.ui.set(R.chip, {visible = true}); za.ui.set(R.key, {visible = true})
            local showName = OPT.ability_names == 'always'
                or (OPT.ability_names == 'on_change' and (now - (S.changed_at or 0) < 3 or not enemies))
            local stateCol = state == 'active' and '#FFBE08' or (state == 'empty' and '#9AA0A6' or '#F2F2EE')
            za.ui.set(R.label, {visible = showName and S.name ~= nil,
                                text = (S.name or '') .. '\n<size=16><color=' .. stateCol .. '>' .. stateText .. '</color></size>'})
        end
    end
end

-- ------------------------------------------------------------------------------------------ weapons --
-- Bottom centre: every weapon you carry. The active one is large in the middle (ammo above the gun, name below, a soft
-- white glow behind), the others smaller and faded to its sides in loadout order; switching slides and resizes them.
-- The game keeps one weapon panel (the active weapon) plus a tab pip per weapon, so the other weapons' names and icons
-- come from your loadout (the game's weapon catalogue), and each icon is also learned whenever that weapon is active.
-- Bottom right: turret / grenade slots with the game's own icons and counts.
local TWEEN = 0.28
-- loadout ({{name, icon}, ...} from your PlayerInfo.loadout), wcache (weapon index -> {name, sprite} seen while
-- active), ammo_max (weapon name -> most rounds seen loaded, for LOW): declared with the HUD state above

local function wname(s) return plain(s):gsub('%s*\\+%s*$', '') end

local function loaded_sprite(aref)  -- an Addressables sprite reference -> its Sprite, if the game has loaded it
    local op = aref and aref:get('m_Operation')
    local io = op and op:get('m_InternalOp')
    local res = io and (io:get('<Result>k__BackingField') or io:get('Result'))
    if type(res) == 'userdata' and res:class() == 'Sprite' then return res end
    return nil
end
local function def_icon(...)  -- the first loaded in-game (else menu) icon among these definitions
    for _, d in ipairs({...}) do
        if d then
            local s = loaded_sprite(d:get('ingameIcon')) or loaded_sprite(d:get('menuIcon'))
            if s then return s end
        end
    end
    -- not loaded by the game yet: load the in-game icon through the loader's own copy of the reference
    for _, d in ipairs({...}) do
        local ref = d and d:get('ingameIcon')
        local mine = ref and za.hud.load_sprite(ref)
        local s = mine and loaded_sprite(mine)
        if s then return s end
    end
    return nil
end
local function active_def_index(runner, display)  -- the active weapon's index in the mission's weapon definitions
    local defs = get(runner, 'worldController', 'loadDataBuilder', 'weaponDefinitions')
    local id = display and display:get('cacheId')
    if not defs or not id then return nil end
    for _, f in ipairs(id:fields()) do
        local v = id:get(f)
        if type(v) == 'number' and v >= 0 and v < defs:len() then return v end
    end
    return nil
end
local function number_in(v)  -- an enum, or a struct wrapping one (StringSerialisedEnum<T>), as a number
    if type(v) == 'number' then return v end
    if type(v) == 'userdata' then for _, f in ipairs(v:fields()) do local x = v:get(f); if type(x) == 'number' then return x end end end
    return nil
end
local function is_deployable(def)
    local dc = def and number_in(def:get('deployableCategory'))
    return dc ~= nil and dc ~= 0
end
local function build_loadout(runner, display, sel, n)
    local defs = get(runner, 'worldController', 'loadDataBuilder', 'weaponDefinitions')
    local id = display and display:get('cacheId')
    if not defs or not id then return nil end
    local idx = nil
    for _, f in ipairs(id:fields()) do
        local v = id:get(f)
        if type(v) == 'number' and v >= 0 and v < defs:len() then idx = v; break end
    end
    if not idx then return nil end
    local function clean(b)
        if b < 0 or b + n > defs:len() then return false end
        for j = b, b + n - 1 do if is_deployable(defs:at(j + 1)) then return false end end
        return true
    end
    local base = idx - (sel - 1)  -- the first weapon's index (0-based), by the tab pips
    if not clean(base) then
        for b = idx, idx - n + 1, -1 do if clean(b) then base = b; break end end
    end
    if base == loadout_base and loadout then return loadout end
    local out = {}
    for i = 1, n do
        local def = defs:at(base + i)
        if not def then return nil end
        local t = def:get('title')
        out[i] = {name = t and title_of(t) or nil, icon = def_icon(def), def = def}
    end
    loadout_base = base
    return out
end

-- a gear slot's key label: the text its own hotkey display shows (so a rebound key reads right); T / G by position only if
-- it has none. Read once a second.
local gear_key = {}
local function slot_key(gs, i, now)
    local c = gear_key[i]
    if c and now - c.at < 1 then return c.v end
    local v = nil
    local hk = gs:get('triggerHotkey')
    for _, t in ipairs(hk and hk:texts() or {}) do
        local x = plain(t)
        if x ~= '' then v = trunc(x:upper(), 5); break end
    end
    v = v or ({'T', 'G'})[i]
    gear_key[i] = {at = now, v = v}
    return v
end
local disagree_since = nil
local function update_weapon(runner, hud)
    local active = get(hud, 'weaponDisplay', 'info')
    local tabs = active and get(active, 'weaponTabDisplay', 'tabs')
    local n = tabs and tabs:len() or 0
    local sel = 1
    for i = 1, n do local s = tabs:at(i) and tabs:at(i):get('selected'); if s and s:active() then sel = i end end
    local cur = active_def_index(runner, hud:get('weaponDisplay'))
    -- the player's weapons as the simulation has them (the slot in hand, and each slot's weapon definition): exact, so the
    -- names and icons need no guessing from the tab pips or the active panel, which are not filled in yet at load-in
    local from_sim = false
    local ctrl = za.input.weapons and get(runner, 'characterInputManager', 'mainPlayer') or nil
    local act, list
    if ctrl then act, list = za.input.weapons(ctrl) end
    local defs = list and get(runner, 'worldController', 'loadDataBuilder', 'weaponDefinitions') or nil
    if act and list and defs and #list >= 1 and act < #list then
        from_sim = true
        local sig = table.concat(list, ',')
        if sig ~= loadout_sig then
            loadout_sig, loadout, wcache = sig, {}, {}
            for i, idx in ipairs(list) do
                local def = idx >= 0 and defs:at(idx + 1) or nil
                local t = def and def:get('title')
                loadout[i] = {name = t and title_of(t) or nil, icon = def and def_icon(def) or nil, def = def}
            end
        end
        -- icons that were still loading when the list was built: keep asking until they arrive
        for _, L in ipairs(loadout) do if not L.icon and L.def then L.icon = def_icon(L.def) end end
        n, sel = #list, act + 1
    elseif n > 1 then
        loadout = build_loadout(runner, hud:get('weaponDisplay'), sel, n) or loadout
        -- the active slot from the weapon actually in hand, not the tab pips (they lag behind at load-in)
        if loadout_base and cur and cur >= loadout_base and cur < loadout_base + n then sel = cur - loadout_base + 1 end
        -- icons that were still loading when the list was built: keep asking until they arrive
        for _, L in ipairs(loadout or {}) do if not L.icon and L.def then L.icon = def_icon(L.def) end end
    end
    n = math.max(n, 1)
    -- nothing drawn for the first 2.5 s of a mission, while the game is still filling in the weapon panel
    if active and not weapons_since then weapons_since = za.time() end
    if not weapons_since or za.time() - weapons_since < 2.5 then active = nil end
    -- nor during the mission's intro scene, while the weapons are not in hand yet: the loadout must be readable from the
    -- simulation and its weapon in hand must be the one the game's own panel names (otherwise the picture would jump about
    -- until the scene ends). If that never agrees (a weapon whose names differ), it is drawn anyway after 4 s.
    if active and za.input.weapons and ctrl then
        local ok = from_sim
        if ok then
            local nt = active:get('nameText')
            local panel = wname(nt and nt:text()):lower():gsub('[^%w]', '')
            local mine = loadout and loadout[sel] and loadout[sel].name
            ok = panel ~= '' and (not mine or tostring(mine):lower():gsub('[^%w]', '') == panel)
        end
        if ok then disagree_since = nil
        else
            disagree_since = disagree_since or za.time()
            if za.time() - disagree_since < 4 then active = nil end
        end
    end
    if not active then
        for _, id in ipairs({E.wglow, E.wtag, E.ammo, E.reloadbg, E.reload}) do show(id, false) end
        for _, S in ipairs(E.wslots) do show(S.icon, false); show(S.name, false) end
    else
        local nameText = active:get('nameText')
        local name = wname(nameText and nameText:text())
        local ammo = active:get('ammoTextAmount') or 0
        local mag = active:get('magAmmoTextAmount') or 65535
        local reloadImg = active:get('reloadFill')
        local reload = reloadImg and reloadImg:active() and (reloadImg:fill() or 0) or 0
        local reloading = reload > 0.001 and reload < 0.999
        ammo_max[name] = math.max(ammo_max[name] or 0, ammo)
        local mx = ammo_max[name]
        local Ldef = from_sim and loadout and loadout[sel] and loadout[sel].def or nil
        local cap = Ldef and Ldef:get('magazineCapacity')
        if type(cap) == 'number' and cap > 0 and ammo <= cap then mx = cap end   -- the weapon's real magazine size
        local low = (not reloading) and mx >= 4 and ammo < mx and ammo <= math.max(1, math.floor(mx * 0.25))
        local sprite = active:get('icon') and active:get('icon'):sprite()
        if not from_sim then wcache[sel] = {name = name, sprite = sprite} end
        local ammoText = mag ~= 65535 and (ammo .. '<size=55%> / ' .. mag .. '</size>') or tostring(ammo)
        za.ui.set(E.ammo, {visible = true, text = reloading and '0' or ammoText, color = low and AMBER or WHITE})
        za.ui.set(E.wtag, {visible = reloading or low, text = reloading and 'RELOADING' or 'LOW', color = AMBER})
        show(E.wglow, true)
        show(E.reloadbg, reloading); za.ui.set(E.reload, {visible = reloading, amount = reload})
        for i, S in ipairs(E.wslots) do
            if i > n then
                show(S.icon, false); show(S.name, false)
            else
                local k = i - sel
                local L = loadout and loadout[i] or nil
                local nm = (from_sim and L and L.name) or ((k == 0) and name or (L and L.name) or (wcache[i] and wcache[i].name) or ('WEAPON ' .. i))
                local spr
                if from_sim then spr = (L and L.icon) or ((k == 0) and sprite or nil)   -- the weapon definition's own icon
                else spr = (k == 0) and sprite or (wcache[i] and wcache[i].sprite) or (L and L.icon) end
                if k == 0 then
                    za.ui.set(S.icon, {visible = spr ~= nil, sprite = spr or 'none', x = 0, y = WY.icon, w = 168, h = 72,
                                       color = WHITE, tween = TWEEN})
                    za.ui.set(S.name, {visible = true, text = nm:upper(), x = 0, y = WY.name, w = 380, size = 17,
                                       color = WHITE, tween = TWEEN})
                else
                    local x = k * 200
                    za.ui.set(S.icon, {visible = spr ~= nil, sprite = spr or 'none', x = x, y = WY.icon + 14, w = 106, h = 46,
                                       color = {0.95, 0.95, 0.93, 0.45}, tween = TWEEN})
                    za.ui.set(S.name, {visible = true, text = trunc(nm:upper(), 20), x = x, y = WY.name + 8, w = 190, size = 13,
                                       color = {0.95, 0.95, 0.93, 0.55}, tween = TWEEN})
                end
            end
        end
    end
    -- gear: the game's icons and counts, bottom right
    local now_k = za.time()
    local deps = hud:get('deployableDisplays')
    for i = 1, 2 do
        local G = E.gear[i]
        local gs = deps and i <= deps:len() and deps:at(i) and deps:at(i):get('gearSlot')
        if gs then
            local spr = gs:get('icon') and gs:get('icon'):sprite()
            local cnt = gs:get('ammoTextAmount') or gs:get('amount') or 0
            local txt = cnt == 65535 and '∞' or tostring(cnt)
            za.ui.set(G.bg, {visible = true})
            za.ui.set(G.icon, {visible = spr ~= nil, sprite = spr or 'none', color = cnt == 0 and GREY or WHITE})
            za.ui.set(G.key, {visible = true, text = slot_key(gs, i, now_k)})
            za.ui.set(G.num, {visible = true, text = txt, color = cnt == 0 and GREY or WHITE})
        else
            for _, id in pairs(G) do show(id, false) end
        end
    end
end

-- ------------------------------------------------------------------------------------------ mission strip --
-- Concept A. Objective: the native objective rows' own text (a clock "m:ss" in it becomes TIME), with progress from
-- the native current objective. MISSION: time since the mission HUD appeared. ALIVE / KILLS: only while enemies are
-- alive (calm state). The strip is as wide as its segments (each sized to its text) and never wider than 1575 px;
-- segments that don't fit drop from the right.
local ORDER = {'obj', 'time', 'mission', 'alive', 'kills'}
local MAXW = {obj = 640}

local function clean(t) return (t:gsub('<[^>]->', ''):gsub('%s+', ' '):gsub('^ ', ''):gsub(' $', '')) end
local function seg_width(s, k)
    local plain = clean(s)
    local w = 44 + math.floor((utf8.len(plain) or #plain) * 10.5 + 0.5)
    return math.min(w, MAXW[k] or 400)
end
-- The game's banners as alert-row items: {head, value, frac}. A clock in the text ("00:30") becomes the value, as does
-- the part after a "\\" or a second line. Banners that never close by themselves (only fade) are shown for 8 s after
-- their text last changed; toasts for as long as they are up.
local function banner_items(hud, now)
    local out = {}
    for _, B in ipairs(BANNERS) do if not B.hide_only then
        local ref = hud:get(B.k)
        local parts = {}
        if ref and ref:active() then
            for _, t in ipairs(ref:texts() or {}) do
                local c = clean(t):gsub('%s*\\+%s*$', '')
                if c ~= '' then parts[#parts + 1] = c end
            end
        end
        local txt = table.concat(parts, ' | ')
        local seen = banner_seen[B.k]
        if txt ~= (seen and seen.text) then seen = {text = txt, since = now}; banner_seen[B.k] = seen end
        local fresh = B.k == 'ingameToastDisplay' or now - seen.since < 8
        if #parts > 0 and fresh then
            local head, value = parts[1], parts[2]
            local all = table.concat(parts, ' ')
            local clk = all:match('(%d+:%d%d)')
            if clk then
                head = clean((head:gsub(clk, '', 1)))
                value = clk:gsub('^0(%d:)', '%1')
            end
            if head == '' then head, value = value or '', nil end
            local item = {head = trunc(head:upper(), 40), value = value and trunc(value:upper(), 24) or nil}
            out[#out + 1] = item
        end
    end end
    return out
end

local function objective_text(hud)
    local o = hud:get('objectiveDisplayContainer')
    local root = o and o:get('objectiveDisplaysRoot')
    local texts = root and root:texts() or {}
    local parts, clock = {}, nil
    for _, t in ipairs(texts) do
        local c = clean(t)
        local m = c:match('(%d+:%d%d)')
        if m and not clock then clock = m; c = clean((c:gsub(m, '', 1))) end
        if c ~= '' and #parts < 2 then parts[#parts + 1] = c end
    end
    return parts, clock, #texts > 0
end

local function update_strip(runner, hud, in_hub)
    local T = E.strip
    if in_hub or not OPT.strip then
        for _, id in ipairs({T.bg, T.pbg, T.pfill}) do show(id, false) end
        for _, b in ipairs(T.brk) do show(b.id, false) end
        for _, id in pairs(T.segs) do show(id, false) end
        for _, id in ipairs(T.divs) do show(id, false) end
        local A = E.alert
        for _, id in ipairs({A.bg, A.rule, A.mark}) do show(id, false) end
        for i = 1, 3 do for _, id in ipairs({A.segs[i], A.hbg[i], A.hfill[i]}) do show(id, false) end end
        for _, id in ipairs(A.divs) do show(id, false) end
        return
    end
    local now = za.time()
    mission_start = mission_start or now
    local parts, clock, any = objective_text(hud)
    if any then obj_seen = true end
    local cur = runner:get('clientUpdateResult')
    local obj = cur and cur:get('cur_objective')
    local progress, target = obj and obj:get('progress'), obj and obj:get('target_for_difficulty')
    local frac = (type(progress) == 'number' and type(target) == 'number' and target > 1)
                 and math.max(0, math.min(1, progress / target)) or nil
    local alive = 0
    for _, e in ipairs(za.entities('Enemy')) do if not e.dead and e.hp > 0 then alive = alive + 1 end end
    local me = my_client(runner)
    local kills = me and me:get('total_kills')
    local el = math.floor(now - mission_start)
    local function V(v) return ' \\ <color=#FFBE08>' .. v .. '</color>' end
    local seg = {}
    if #parts > 0 then
        local head, tail = parts[1]:upper(), parts[2] and parts[2]:upper() or nil
        if frac and not (tail and tail:find('%d')) then tail = string.format('%d%%', math.floor(frac * 100 + 0.5)) end
        seg.obj = trunc(head, 40) .. (tail and V(trunc(tail, 14)) or '')
    end
    if clock then seg.time = 'TIME' .. V(clock) end
    seg.mission = 'MISSION' .. V(string.format('%d:%02d', el // 60, el % 60))
    if alive > 0 then  -- calm (no enemies alive): both hide, as in the design
        if za.setting('enemies') then seg.alive = 'ALIVE' .. V(alive) end
        if za.setting('kills') then seg.kills = 'KILLS' .. V(kills and tostring(kills) or '—') end
    end
    local G = E.strip_geom
    local x, d = G.x, 0
    for _, k in ipairs(ORDER) do
        local w = seg[k] and seg_width(seg[k], k)
        if w and x + w <= G.x + G.w then
            za.ui.set(T.segs[k], {visible = true, x = x + 22, w = w - 44, text = seg[k]})
            if k == 'obj' then
                za.ui.set(T.pbg, {visible = frac ~= nil, x = x + 22, w = w - 44})
                za.ui.set(T.pfill, {visible = frac ~= nil, x = x + 22, w = w - 44, amount = frac or 0})
            end
            x = x + w
            d = d + 1
            za.ui.set(T.divs[d], {visible = true, x = x})
        else
            show(T.segs[k], false)
            if k == 'obj' then show(T.pbg, false); show(T.pfill, false) end
        end
    end
    if d > 0 then show(T.divs[d], false) end  -- no divider after the last segment
    for i = d + 1, #T.divs do show(T.divs[i], false) end
    -- the alert row: the game's banners (when the top bar takes them over)
    local A = E.alert
    local items = za.setting('banners') ~= false and banner_items(hud, now) or {}
    local ax = G.x
    for i = 1, 3 do
        local it = items[i]
        if it then
            local txt = it.head .. (it.value and V(it.value) or '')
            local sw = math.min(seg_width(txt, 'alert') - 8, 520)
            za.ui.set(A.segs[i], {visible = true, x = ax + 18, w = sw - 36, text = txt})
            za.ui.set(A.hbg[i], {visible = it.frac ~= nil, x = ax + 18, w = sw - 36})
            za.ui.set(A.hfill[i], {visible = it.frac ~= nil, x = ax + 18, w = sw - 36, amount = it.frac or 0})
            ax = ax + sw
            if A.divs[i] then za.ui.set(A.divs[i], {visible = items[i + 1] ~= nil, x = ax}) end
        else
            for _, id in ipairs({A.segs[i], A.hbg[i], A.hfill[i]}) do show(id, false) end
            if A.divs[i] then show(A.divs[i], false) end
        end
    end
    local alert = #items > 0
    -- the frame follows the segments, around both rows
    local w = math.max(x - G.x, ax - G.x, 120)
    local H = G.h + (alert and ALERT_H or 0)
    za.ui.set(T.bg, {visible = true, w = w})
    za.ui.set(A.bg, {visible = alert, w = w})
    za.ui.set(A.rule, {visible = alert, w = w - 24})
    za.ui.set(A.mark, {visible = alert})
    for _, b in ipairs(T.brk) do za.ui.set(b.id, {visible = true, x = G.x + b.right * (w - b.w), y = G.y + b.bottom * (H - b.h)}) end
end

-- ------------------------------------------------------------------------------------------ frame --
za.on_frame(function()
    if not za.ui.ready() then return end
    local runner = za.static('GameRunner', '<Instance>k__BackingField')
    local hud = runner and runner:get('gameHud')
    local state = runner and runner:get('gameState')
    local mtype = runner and runner:get('<LastUsedMissionType>k__BackingField')
    local hub = runner and runner:is('IsInHub')
    if hub == nil then hub = mtype == MISSION_HUB end
    local hudUp = hud and hud:alive() and hud:active()
    local visible = hudUp and state == STATE_INGAME
    za.ui.visible(visible == true)
    if not visible then return end
    if not E or built_gen ~= za.ui.generation() then build(); built_gen = za.ui.generation() end
    local key = hud:addr()
    if key ~= hud_key then hud_key = key; on_new_hud(runner, hud) end
    if hub then mission_start = nil end
    za.ui.set(E.gs, {scale = za.setting('status_scale') or 1})
    za.ui.set(E.gw, {scale = za.setting('weapon_scale') or 1})
    za.ui.set(E.gg, {scale = za.setting('weapon_scale') or 1})
    hide_native(hud, hub)
    update_squad(runner, hud, hub)
    update_abilities(runner, hud)
    update_weapon(runner, hud)
    update_strip(runner, hud, hub)
end)

