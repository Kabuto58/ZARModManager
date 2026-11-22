-- Mission Re-roller (a ZARModManager mod): re-rolls the Operations missions by itself until one of the three offered
-- matches what you asked for in Settings > MODS (mission type, drops, difficulty). The panel is read-only.
--
-- How the game picks the missions: the mission select menu builds the Operations list from a stored seed
-- (GameRunner.lastMissionSelectRNGSeed), with your mission history weighed in, and gives it to the lobby
-- (Lobby.SetMissionList); the mission cards come from the lobby. A re-roll does the same with a new seed (za.missions):
-- the menu's own list builder, then the lobby gets the new list, or, if the lobby has not asked for one yet, uses the
-- stored seed when it does. Only in your own lobby (alone, or leading the party): in someone else's, the list is theirs.
--
-- Where: on every page of mission select, from the moment it opens (F at Missions and Operations); once a multiplayer
-- vote has started it goes away (the squad is choosing by then).

local MODES = {   -- LevelMode.title of each Operations mission type, and the name shown for it
    {key = 'ModeEradication_Title',      name = 'Eradication'},
    {key = 'ModeEradicationTimed_Title', name = 'Eradication Surge'},
    {key = 'ModeCacheRecovery_Title',    name = 'Cache Recovery'},
    {key = 'ModeCloseBreach_Title',      name = 'Close Breach'},
    {key = 'ModePods_Title',             name = 'Pods'},
    {key = 'ModeDroneRecon_Title',       name = 'Drone Recon'},
    {key = 'ModeUplink_Title',           name = 'Uplink'},
    {key = 'ModeRescue_Title',           name = 'Rescue (Save the Merc)'},
}
local TYPE_OPTIONS = {'No preference'}
for _, m in ipairs(MODES) do TYPE_OPTIONS[#TYPE_OPTIONS + 1] = m.name end

za.mod{
    name = 'Mission Re-roller',
    version = '2.1.2',
    description = 'Re-rolls the Operations missions for you until one matches your preferences (mission type, drops, difficulty), set in Settings > MODS. Works in your own lobby (alone, or leading the party).',
    settings = {
        {id = 'mission_type', type = 'choice', label = 'Mission Type', options = TYPE_OPTIONS, default = 0},
        {id = 'drops', type = 'choice', label = 'Preferred Drops', options = {'No preference', '1 crate', '2 crates'}, default = 0},
        {id = 'difficulty', type = 'choice', label = 'Preferred Difficulty', options = {'No preference', 'Normal', 'Tough'}, default = 0},
    },
}

local WHITE, GREY, AMBER = {0.95, 0.95, 0.93, 1}, {0.6, 0.63, 0.65, 1}, {1, 0.745, 0.031, 1}
local PANEL = {0.043, 0.051, 0.063, 0.82}
local OPERATIONS = 1        -- NativeBuildInterop.MissionType of Operations (the Operations button sets it)
local W, ROWS = 560, 4
local TICK = 0.25           -- the mod looks at the lobby this often (seconds), and every ROLL_GAP while it is rolling
local ROLL_GAP = 0          -- no wait between two re-rolls: one per frame (the squad countdown only gives about 5 seconds)
local MAX_ROLLS = 300       -- gives up after this many (nothing offered matches, or the combination is rare)
local DIFFICULTY = {[1] = 'Easy', [2] = 'Medium', [3] = 'Hard'}   -- LevelDifficulty
local TOUGH = 1             -- LevelModifier.Tough (0 is none)
local function num(v) return type(v) == 'number' and v or nil end
local function title_case(s) return ((s or ''):gsub('_', ' '):lower():gsub("(%a)([%w']*)", function(a, b) return a:upper() .. b end)) end

-- A mission entry (MissionInfoInstance) -> "MISSION · Map · Difficulty · Tough"
local function describe(mi)
    local info = mi and mi:get('missionInfo')
    if not info then return '?' end
    local mode = info:get('mode')
    local mkey = type(mode) == 'userdata' and mode:get('title') or nil
    local mission = type(mkey) == 'string' and za.loc(mkey) or nil
    local tkey = info:get('titleLocId')
    local map = type(tkey) == 'string' and za.loc(tkey) or nil
    if not map or map == '' then map = info:get('levelId') end
    local d = mi:get('selectedDifficulty')
    local dv = type(d) == 'userdata' and (d:get('hasValue') and num(d:get('value'))) or num(d)
    local diff = dv and DIFFICULTY[dv]
    local parts = {(mission and mission ~= '' and mission or map or '?'):upper()}
    if mission and mission ~= '' and map then parts[#parts + 1] = title_case(map) end
    if diff then parts[#parts + 1] = diff end
    if num(mi:get('selectedModifier')) == TOUGH then parts[#parts + 1] = '<color=#FF4A4A>Tough</color>' end
    return parts[1] .. (#parts > 1 and ('  <color=#9AA1A6>' .. table.concat(parts, '  ·  ', 2) .. '</color>') or '')
end

-- ------------------------------------------------------------------------------------------ matching --
-- The crates a mission drops (the "x1" on its card), from its reward table for its difficulty: each crate entry counts for
-- its chance (an entry that is always given counts 1; one given 40% of the time counts 0.4; on a Tough mission an entry
-- marked to give its maximum for Tough is always given), and the card shows the total rounded to the nearest whole crate.
-- (Read off the cards: 1 given + 0.4 chance = x1, 0.5 chance alone = x1, 1 given + 1 more = x2.)
local STRONGBOX = 4   -- RangedLootValue.Category of a strongbox
local function crates_of(mi)
    local info = mi:get('missionInfo')
    local mode = info and info:get('mode')
    local rw = (mode and mode:get('rewards')) or (info and info:get('rewards'))
    local d = mi:get('selectedDifficulty')
    local dv = type(d) == 'userdata' and (d:get('hasValue') and num(d:get('value'))) or num(d)
    local modifier = num(mi:get('selectedModifier')) or 0
    if not rw or not dv then return nil end
    local name = rw:get('difficultyDependentLootTable') and (dv == 1 and 'lootTableEasy' or dv == 2 and 'lootTableMedium' or 'lootTableHard') or 'lootTable'
    local tb = rw:get(name)
    local groups = tb and tb:get('groups')
    if not groups then return nil end
    local total = 0
    for gi = 1, groups:len() do
        local els = groups:at(gi):get('elements')
        for ei = 1, els and els:len() or 0 do
            local e = els:at(ei)
            local chance = num(e:get('chance')) or 1
            local give_max = num(e:get('giveMaxRewardForModifiers')) or 0
            if modifier ~= 0 and (math.floor(give_max) & math.floor(modifier)) ~= 0 then chance = 1 end   -- (Tough: given for certain)
            local loot = e:get('loot')
            for li = 1, loot and loot:len() or 0 do
                local l = loot:at(li)
                if l:get('category') == STRONGBOX then
                    local r = l:get('amountRange')
                    local top = r and (num(r:get('m_Y')) or num(r:get('y')))
                    total = total + chance * math.max(1, top or 1)
                end
            end
        end
    end
    return math.floor(total + 0.5)
end

local function matches(mi, want)
    local info = mi and mi:get('missionInfo')
    if not info then return false end
    if want.type > 0 then
        local mode = info:get('mode')
        if (type(mode) == 'userdata' and mode:get('title') or nil) ~= MODES[want.type].key then return false end
    end
    if want.difficulty > 0 then
        local tough = num(mi:get('selectedModifier')) == TOUGH
        if tough ~= (want.difficulty == 2) then return false end
    end
    if want.drops > 0 then
        local c = crates_of(mi)
        if c ~= want.drops then return false end
    end
    return true
end
local function choice(id, count)
    local v = za.setting(id)
    return type(v) == 'number' and math.max(0, math.min(count - 1, math.floor(v))) or 0
end
local function wanted()
    return {type = choice('mission_type', #TYPE_OPTIONS), drops = choice('drops', 3), difficulty = choice('difficulty', 3)}
end

-- ------------------------------------------------------------------------------------------ panel --
local E, built_gen, drawn = nil, nil, nil
local function build()
    -- bottom centre, above the lobby's player row; it grows upwards with the mission rows (see draw)
    local g = za.ui.group{anchor = 'b', x = 0, y = 150, w = W, h = 60}
    local function text(spec) spec.parent = g; spec.outline = spec.outline or 0.2; spec.color = spec.color or WHITE; return za.ui.text(spec) end
    local function image(spec) spec.parent = g; spec.sprite = spec.sprite or 'none'; return za.ui.image(spec) end
    E = {g = g, rows = {}}
    E.bg = image{anchor = 'tl', x = 0, y = 0, w = W, h = 60, color = PANEL}
    E.stripe = image{anchor = 'tl', x = 0, y = 0, w = 3, h = 60, color = AMBER}
    E.head = text{anchor = 'tl', x = 16, y = 9, w = W - 32, h = 22, size = 17, align = 'left', text = ''}
    for i = 1, ROWS do E.rows[i] = text{anchor = 'tl', x = 16, y = 38 + (i - 1) * 26, w = W - 32, h = 22, size = 15, align = 'left', text = '', visible = false} end
    E.note = text{anchor = 'tl', x = 16, y = 38, w = W - 32, h = 18, size = 13, align = 'left', text = '', color = GREY}
    drawn = nil
end
local function draw(head, lines, note)
    local sig = head .. '\1' .. table.concat(lines, '\2') .. '\1' .. note   -- (only touches the UI when something changed)
    if sig == drawn then return end
    drawn = sig
    za.ui.set(E.head, {text = head})
    for i = 1, ROWS do za.ui.set(E.rows[i], {visible = lines[i] ~= nil, text = lines[i] or ''}) end
    local y = 38 + #lines * 26 + (#lines > 0 and 4 or 0)
    za.ui.set(E.note, {y = y, text = note})
    za.ui.set(E.g, {h = y + 28})
    za.ui.set(E.bg, {h = y + 28})
    za.ui.set(E.stripe, {h = y + 28})
end

-- ------------------------------------------------------------------------------------------ state --
local NOT_READ = -1   -- shown_seed before the list has been read on this visit (a seed is never negative)
local rolls, state = 0, 'idle'        -- state: idle (nothing asked for), rolling, found, gave_up
local shown_seed, lines, matched = NOT_READ, {}, false
local own_state, own_since = nil, 0   -- whether the lobby is yours, held steady (see below)
local next_tick, last_roll = 0, 0
-- The game rebuilds the lobby's mission list when something refreshes the lobby (a squad member back in the hub, the leader
-- back at Choose Missions): the list the lobby holds is then no longer the one found. `force` makes the next pass roll again
-- and hand the lobby a list, even if the stored seed already matches.
local force = true
local sig_seen, sig_since, sig_acted = nil, 0, nil
local function reset() rolls, state, shown_seed, lines, matched, force = 0, 'idle', NOT_READ, {}, false, true; sig_seen, sig_acted = nil, nil end
local function lobby_sig(lobby, runner)   -- what changes when the game refreshes the lobby: the lobby itself, and its squad and party rosters
    local function hash(t) local h = t and t:get('playersHash'); local n = t and t:get('players'); return tostring(h) .. '/' .. tostring(n and n:len()) end
    local ok, a = pcall(function() return lobby:addr() end)
    return tostring(ok and a or '?') .. '|' .. hash(lobby:get('missionSquad')) .. '|' .. hash(runner:get('party'))
end
za.on_settings(reset)   -- a changed preference starts the search again
local function read_list(list, want)
    local out, hit = {}, false
    for i = 1, list and math.min(list:len(), ROWS) or 0 do
        local mi = list:at(i)
        out[#out + 1] = describe(mi)
        if matches(mi, want) then hit = true end
    end
    return out, hit
end

za.on_frame(function()
    if not za.ui.ready() or not za.missions then return end
    local now = za.time()
    if now < next_tick then return end
    next_tick = now + (state == 'rolling' and ROLL_GAP or TICK)
    if not E or built_gen ~= za.ui.generation() then build(); built_gen = za.ui.generation() end
    local menu = za.missions.menu()   -- the mission select menu, on any of its pages
    local runner = za.static('GameRunner', '<Instance>k__BackingField')
    local lobby = runner and runner:get('lobby')
    local online = lobby and lobby:is('get_IsOnlineLobby') == true
    local cards = menu and menu:get('missionCards')
    local voting = online and cards and cards:len() > 0   -- a multiplayer vote has started: too late to re-roll
    local show = menu ~= nil and not voting
    za.ui.set(E.g, {visible = show == true})
    -- leaving the menu forgets the list: a finished mission changes the seed (the game clears it) and the history
    if not menu then reset(); own_state = nil end
    if not show then return end

    -- your own lobby: offline, or you lead its squad (matchmade, in Fill Squad) or your party: your own entry in that
    -- team's player list (isLocalPlayer) has its player info marked isLeader. Either team saying so counts (the squad
    -- can still be the last mission's for a moment).
    local function leads(t)
        local list = t and t:get('players')
        for i = 1, list and math.min(list:len(), 8) or 0 do
            local tp = list:at(i)
            if tp and tp:get('isLocalPlayer') == true then
                local pi = tp:get('playerInfo')
                return pi ~= nil and pi:get('isLeader') == true
            end
        end
        return false
    end
    local squad, party = lobby and lobby:get('missionSquad'), runner and runner:get('party')
    local members = party and party:get('players')
    local partySize = members and members:len() or 0
    -- no lobby yet (the first page): yours unless you are in someone else's party (its leader decides the list)
    local raw = (not lobby and (partySize <= 1 or leads(party))) or
                (lobby ~= nil and (not online or leads(squad) or leads(party)))
    -- the leader flag blinks while the game re-syncs the squad (a player joining): the answer only changes once it
    -- has held for 3 s, so a blip neither stops the search nor lets a real change of leader go unnoticed for long
    if own_state == nil then own_state, own_since = raw, now end
    if raw == own_state then own_since = now elseif now - own_since >= 3 then own_state, own_since = raw, now end
    local own = own_state
    local params = menu:get('localMatchMakingParams')
    local storyLobby = lobby ~= nil and params ~= nil and params:get('missionType') ~= OPERATIONS
    if not own or storyLobby then
        draw('Unable to roll mission', {}, not own and 'Only the lobby leader can re-roll' or 'Only Operations missions can be re-rolled')
        return
    end

    local want = wanted()
    local asked = want.type > 0 or want.drops > 0 or want.difficulty > 0
    -- the missions the list holds (or will hold, from the stored seed), read again whenever the seed changes
    if shown_seed == NOT_READ or za.missions.seed() ~= shown_seed then
        if not za.missions.seed() then za.missions.set_seed(math.random(1, 0x7FFFFFFE)) end
        shown_seed = za.missions.seed()
        lines, matched = read_list(za.missions.preview(), want)
    end
    if #lines == 0 then draw('Unable to roll mission', {}, 'No missions to read yet'); return end

    -- a refreshed lobby (see `force`): once the roster has settled for a second, roll again
    if lobby and asked and online then
        local sg = lobby_sig(lobby, runner)
        if sg ~= sig_seen then sig_seen, sig_since = sg, now end
        if sig_acted == nil then sig_acted = sg end
        if sg ~= sig_acted and now - sig_since >= 1 then
            sig_acted = sg
            if state == 'found' or state == 'gave_up' then rolls, state, force, shown_seed = 0, 'rolling', true, NOT_READ end
        end
    end
    if not asked then state = 'idle'
    elseif matched and not force then state = 'found'
    elseif state ~= 'gave_up' then state = 'rolling' end

    if state == 'rolling' then
        if rolls >= MAX_ROLLS then state = 'gave_up'
        elseif shown_seed ~= NOT_READ and now - last_roll >= ROLL_GAP then
            last_roll = now
            local seed
            repeat seed = math.random(1, 0x7FFFFFFE) until seed ~= za.missions.seed()
            -- with a lobby open, it gets the new list; before one exists, the stored seed is what it will be built from
            local ok
            if lobby then ok = za.missions.reroll(seed) else ok = za.missions.set_seed(seed) end
            if ok then rolls = rolls + 1; shown_seed = NOT_READ; force = false
            else draw('Unable to roll mission', {}, 'The game would not take the new list'); return end
        end
    end

    local plural = rolls == 1 and ' roll' or ' rolls'
    if state == 'rolling' then draw('Rolling... (' .. rolls .. plural .. ' so far)', {}, '')
    elseif state == 'found' then draw('Match found' .. (rolls > 0 and (' after ' .. rolls .. plural) or ''), lines, 'The Operations this lobby will offer')
    elseif state == 'gave_up' then draw('No match after ' .. rolls .. ' rolls', lines, 'Loosen a preference in Settings > MODS')
    else draw('Set a preference in Settings > MODS', lines, 'The Operations this lobby will offer') end
end)
