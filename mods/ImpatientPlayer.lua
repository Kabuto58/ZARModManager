-- Impatient Player (a ZARModManager mod): fewer waiting screens.
-- At the end of a mission it moves through the XP and strongbox screens by itself (behind a cover), reports what you got
-- as cards in the middle of the screen, and asks Collect / Scrap / Sell for each weapon or armour piece on its own buttons,
-- pressing the game's own review buttons for you.

za.mod{
    name = 'Impatient Player',
    version = '1.2.4',
    description = 'Fewer waiting screens: skips the end-of-mission screens, shows your rewards as notifications and lets you Collect, Scrap or Sell new gear from them.',
    settings = {
        {id = 'skip_end', type = 'toggle', label = 'Skip the end-of-mission screens', default = true},
    },
}

local LOOT_CURRENCY, LOOT_MOD = 1, 3   -- Loot.type
local WHITE, GREY = {0.95, 0.95, 0.93, 1}, {0.6, 0.63, 0.65, 1}
local PANEL = {0.043, 0.051, 0.063, 0.86}
local RARITY = {   -- item quality name -> colour of the notification
    common    = {0.62, 0.64, 0.66, 1},
    uncommon  = {0.36, 0.82, 0.45, 1},
    rare      = {0.25, 0.60, 1.00, 1},
    epic      = {0.70, 0.42, 1.00, 1},
    legendary = {1.00, 0.78, 0.15, 1},
}
local function runner() return za.static('GameRunner', '<Instance>k__BackingField') end
local function norm(s) return (tostring(s or ''):lower():gsub('[^%w]', '')) end
local function thousands(n) return (tostring(math.floor(n)):reverse():gsub('(%d%d%d)', '%1,'):reverse():gsub('^,', '')) end

-- ------------------------------------------------------------------------------------------ reward cards --
-- What the mission gave you is shown as a row of large cards, centred on the screen under "Collecting rewards...": the XP,
-- each strongbox item, and (after a choice) what a sale or scrap paid. A weapon or armour piece's card carries Collect / Scrap
-- / Sell buttons and stays until one is chosen; the others go a few seconds after the end screens are done. The cards (and
-- their pictures and texts) shrink to fit when there are many.
local GAP, SLOTS, HOLD, MIN_SHOW = 12, 12, 5, 6
local CARD_W, CARD_H = 280, 440            -- the card the layout numbers are drawn for (scaled by w / CARD_W)
local MAX_W, MIN_W = 196, 120              -- a card's width: 70% of that, less if there are many
local CARD_BG = {0.085, 0.095, 0.11, 1}
local end_done_at = nil                    -- when the end screens were done (the cards go a few seconds after)
local ROW_W = 1800                             -- the width the row may use
local BUTTONS = {{id = 'collect', label = 'COLLECT', native = 'buttonCollect'},
                 {id = 'scrap', label = 'SCRAP', native = 'buttonDismantle'},
                 {id = 'sell', label = 'SELL', native = 'buttonSell'}}
local slots, queue, built_gen = nil, {}, nil
local cover, cover_on = nil, false
local function build()
    slots = {}
    cover_on = false
    cover = {}
    cover.bg = za.ui.image{anchor = 'tl', x = 0, y = 0, w = 1920, h = 1080, sprite = 'none', color = {0.03, 0.035, 0.04, 1}, visible = false}
    cover.text = za.ui.text{anchor = 'c', x = 0, y = -250, w = 900, h = 44, size = 32, align = 'center', color = GREY, outline = 0, text = 'Collecting rewards...', visible = false}
    for i = 1, SLOTS do
        local g = za.ui.group{anchor = 'c', x = 0, y = 700, w = CARD_W, h = CARD_H, visible = false}
        local S = {g = g, active = false, born = 0, lay = '', w = CARD_W, h = CARD_H, cx = 0, cy = 0, btn = {}, rect = {}}
        S.border = za.ui.image{parent = g, anchor = 'tl', x = -2, y = -2, w = CARD_W + 4, h = CARD_H + 4, sprite = 'none', color = {0.95, 0.95, 0.93, 0.16}}
        S.bg = za.ui.image{parent = g, anchor = 'tl', x = 0, y = 0, w = CARD_W, h = CARD_H, sprite = 'none', color = CARD_BG}
        S.stripe = za.ui.image{parent = g, anchor = 'tl', x = 0, y = 0, w = CARD_W, h = 5, sprite = 'none', color = WHITE}
        S.icon = za.ui.image{parent = g, anchor = 'c', x = 0, y = -129, w = 130, h = 130, sprite = 'none', color = {1, 1, 1, 1}, visible = false}
        S.glyph = za.ui.text{parent = g, anchor = 'c', x = 0, y = -129, w = 130, h = 80, size = 60, align = 'center', color = {0.95, 0.95, 0.93, 0.9}, outline = 0, text = '', visible = false}
        S.title = za.ui.text{parent = g, anchor = 'tl', x = 8, y = 170, w = CARD_W - 16, h = 64, size = 26, align = 'center', color = WHITE, outline = 0.2, text = ''}
        S.body = za.ui.text{parent = g, anchor = 'tl', x = 8, y = 236, w = CARD_W - 16, h = 26, size = 18, align = 'center', color = GREY, outline = 0.2, text = ''}
        for b = 1, #BUTTONS do
            S.btn[b] = {
                bg = za.ui.image{parent = g, anchor = 'tl', x = 18, y = 280, w = CARD_W - 36, h = 40, sprite = 'none', color = {0.95, 0.95, 0.93, 0.1}, visible = false},
                text = za.ui.text{parent = g, anchor = 'tl', x = 18, y = 288, w = CARD_W - 36, h = 24, size = 19, align = 'center', color = WHITE, outline = 0, text = BUTTONS[b].label, visible = false},
                state = -1,
            }
        end
        slots[i] = S
    end
end
-- aref: the item's picture; gear: {name = normalised name} for a weapon / armour piece that is asked about
local function notify(title, body, color, aref, gear, sprite, glyph) queue[#queue + 1] = {title = title, body = body, color = color or WHITE, aref = aref, gear = gear, sprite = sprite, glyph = glyph} end
local function loaded_sprite(aref)  -- an Addressables sprite reference -> its Sprite, once it is loaded
    local op = aref and aref:get('m_Operation')
    local io = op and op:get('m_InternalOp')
    local res = io and (io:get('<Result>k__BackingField') or io:get('Result'))
    if type(res) == 'userdata' and res:class() == 'Sprite' then return res end
    return nil
end
local function item_sprite(aref)   -- the game's own copy if it has loaded it, else ours (loading; asked again until it is there)
    return loaded_sprite(aref) or loaded_sprite(za.hud.load_sprite(aref))
end
local function dismiss(S, now)
    if S.active and not S.leaving then
        S.leaving, S.left_at = true, now
        za.ui.set(S.g, {y = 700, tween = 0.3})
    end
end
-- a title that fits the card: as is, or on two lines (split at the space nearest the middle), in a smaller size if need be
local function fit_title(raw, wpx, size)
    local per = size * 0.52   -- about this many pixels a character
    if #raw * per <= wpx then return raw, size end
    local mid, best = #raw / 2, nil
    for i = 1, #raw do
        if raw:sub(i, i) == ' ' and (not best or math.abs(i - mid) < math.abs(best - mid)) then best = i end
    end
    if best then
        local a, b = raw:sub(1, best - 1), raw:sub(best + 1)
        local longest = math.max(#a, #b)
        return a .. '\n' .. b, math.max(11, math.min(size, math.floor(wpx / (longest * 0.52))))
    end
    return raw, math.max(11, math.floor(wpx / (#raw * 0.52)))
end
-- a card's parts for its size: w wide, k the scale against full size
local function apply_geometry(S, w, k)
    local h = CARD_H * k
    S.w, S.h = w, h
    local dy = S.gear and 0 or 80 * k   -- (a card without buttons has its content lower, about the middle)
    local isz, ty = 130 * k, 26 * k + 130 * k + 14 * k + dy
    za.ui.set(S.g, {w = w, h = h})
    za.ui.set(S.border, {w = w + 4, h = h + 4})
    za.ui.set(S.bg, {w = w, h = h})
    za.ui.set(S.stripe, {w = w, h = 5})
    local icy = 26 * k + dy + isz / 2 - h / 2   -- the picture's centre, from the card's centre
    za.ui.set(S.icon, {x = 0, y = icy, w = w - 16 * k, h = isz})
    za.ui.set(S.glyph, {x = 0, y = icy - 6 * k, w = isz, h = 80 * k, size = math.floor(math.max(20, 60 * k))})
    local txt, size = fit_title(S.raw_title or '', w - 12, math.floor(math.max(12, 26 * k)))
    za.ui.set(S.title, {x = 8, y = ty, w = w - 16, h = 64 * k, size = size, text = txt})
    za.ui.set(S.body, {x = 8, y = ty + 66 * k, w = w - 16, h = 26 * k, size = math.floor(math.max(10, 18 * k))})
    local bw, bh = w - 36 * k, 40 * k
    local by0 = ty + 66 * k + 44 * k
    for b = 1, #BUTTONS do
        local by = by0 + (b - 1) * (bh + 10 * k)
        S.rect[b] = {x = 18 * k, y = by, w = bw, h = bh}
        za.ui.set(S.btn[b].bg, {x = 18 * k, y = by, w = bw, h = bh})
        za.ui.set(S.btn[b].text, {x = 18 * k, y = by + (bh - 24 * k) / 2, w = bw, h = 24 * k, size = math.floor(math.max(11, 19 * k))})
    end
end
local function update_cards(now)
    -- free slots take the next waiting card
    for _, S in ipairs(slots) do
        if not S.active and #queue > 0 then
            local n = table.remove(queue, 1)
            S.active, S.born, S.leaving, S.aref, S.has_icon, S.gear, S.acted, S.pending, S.action = true, now, false, n.aref, false, n.gear, false, nil, nil
            S.lay, S.fresh = '', true
            za.ui.set(S.stripe, {color = n.color})
            za.ui.set(S.icon, {sprite = 'none', visible = false})
            if n.sprite then S.has_icon = true; za.ui.set(S.icon, {sprite = n.sprite, visible = true}) end
            S.glyph_text = n.glyph
            za.ui.set(S.glyph, {text = n.glyph or '', visible = n.glyph ~= nil and not n.aref and not n.sprite})   -- (a card with no picture to wait for shows its glyph)
            S.raw_title = n.title
            za.ui.set(S.title, {text = n.title, color = n.color})
            za.ui.set(S.body, {text = n.body})
            for b = 1, #BUTTONS do
                S.btn[b].state = -1
                za.ui.set(S.btn[b].bg, {visible = n.gear ~= nil}); za.ui.set(S.btn[b].text, {visible = n.gear ~= nil})
            end
            za.ui.set(S.g, {visible = true})
        end
    end
    -- pictures arrive once their sprite has loaded
    for _, S in ipairs(slots) do
        if S.active and S.aref and not S.has_icon then
            local sp = item_sprite(S.aref)
            if sp then S.has_icon = true; za.ui.set(S.icon, {sprite = sp, visible = true}); za.ui.set(S.glyph, {visible = false})
            elseif now - S.born > 1.5 then S.aref = nil; if S.glyph_text then za.ui.set(S.glyph, {visible = true}) end end
        end
    end
    -- a card that is not asking anything goes a few seconds after the end screens are done, once no card is asking
    local asking = false
    for _, S in ipairs(slots) do if S.active and S.gear and not S.leaving then asking = true end end
    for _, S in ipairs(slots) do
        if S.active and not S.leaving and not S.gear then
            if end_done_at and not asking and now - end_done_at >= HOLD and now - S.born >= MIN_SHOW then dismiss(S, now) end
        end
        if S.active and S.leaving and now - S.left_at >= 0.4 then S.active = false; za.ui.set(S.g, {visible = false}) end
    end
    -- the live ones in a row about the centre of the screen
    local live = {}
    for _, S in ipairs(slots) do if S.active and not S.leaving then live[#live + 1] = S end end
    table.sort(live, function(a, b) return a.born < b.born end)
    local n = #live
    local w = n > 0 and math.max(MIN_W, math.min(MAX_W, math.floor((ROW_W - (n - 1) * GAP) / n))) or MAX_W
    local k = w / CARD_W
    local tallest = 0
    for i, S in ipairs(live) do
        local cx = (i - (n + 1) / 2) * (w + GAP)
        local key = n .. '|' .. cx .. '|' .. w
        if S.lay ~= key then
            apply_geometry(S, w, k)
            S.cx, S.cy = cx, 0
            if S.fresh then za.ui.set(S.g, {x = cx, y = 80}); S.fresh = false end   -- (rises into place)
            za.ui.set(S.g, {x = cx, y = 0, tween = 0.3})
            S.lay = key
        end
        tallest = math.max(tallest, S.h)
    end
    if tallest > 0 then za.ui.set(cover.text, {y = -(tallest / 2) - 60}) end
end

-- ------------------------------------------------------------------------------------------ reading the game --
-- the entries of the menu stack: {class of the menu instance, the entry, its start-up data}
local function stack_entries()
    local out = {}
    local r = runner()
    local ms = r and r:get('menuStack')
    local st = ms and ms:get('stack')
    local arr = st and st:get('_array')
    local size = st and st:get('_size')
    if type(size) ~= 'number' or not arr then return out end
    for i = 1, math.min(size, 8) do
        local e = arr:at(i)
        local inst = e and e:get('instance')
        local ok, cls = pcall(function() return inst and inst:class() end)
        out[#out + 1] = {class = ok and cls or '?', entry = e, params = e and e:get('initParams')}
    end
    return out
end

local function quality_of(q)   -- -> display name, key into RARITY
    local n = type(q) == 'number' and za.enum('ItemQuality', q) or nil
    if not n then return 'Quality ' .. tostring(q), nil end
    if n:lower() == 'last' then n = 'Legendary' end   -- (the enum's last member shares the top rarity's value)
    return n:sub(1, 1):upper() .. n:sub(2):lower(), n:lower()
end
local function loc_of(ref, field)
    local key = ref and ref:get(field)
    if type(key) ~= 'string' or key == '' then return nil end
    local s = za.loc(key)
    if s then s = s:gsub('%[/?[%w=#]+%]', ''):gsub('<[^>]->', '') end   -- (the game styles some texts with [r]..[/r] and <color>..)
    return (s and s ~= '') and s or nil
end

-- one strongbox item -> title, body, colour, picture, gear (nil title if it holds nothing we can name)
local rewards_inst = nil   -- the rewards screen (MenuRewards) the cards are made from
local function mod_icon(mod)   -- the reward display's icon for the mod's type
    local rd = rewards_inst and rewards_inst:get('rewardModsDisplayPrefab')
    local d = rd and rd:get('modIconsType')
    local keys, vals = d and d:get('keys'), d and d:get('values')
    local mt = mod and mod:get('type')
    if keys and vals and type(mt) == 'number' then
        for i = 1, keys:len() do if keys:at(i) == mt then return vals:at(i) end end
    end
    -- an armour mod (type 5) has no entry there: its icon is its element's, the first damage type its bonuses are for
    if mt == 5 then
        local bonuses = mod:get('bonusStats')
        local dt = nil
        for i = 1, bonuses and bonuses:len() or 0 do
            local b = bonuses:at(i)
            if b and b:get('useSpecificDamageType') == true then dt = b:get('specificDamageType'); break end
        end
        local dd = rd and rd:get('modIconsDamageType')
        local dk, dv = dd and dd:get('keys'), dd and dd:get('values')
        for i = 1, dt and dk and dv and dk:len() or 0 do
            if dk:at(i) == dt then return dv:at(i) end
        end
    end
    return nil
end
local function describe_loot(l)
    local t = l:get('type')
    if t == LOOT_MOD then
        local mod = l:get('mod')
        local qn, qk = quality_of(l:get('quality'))
        return loc_of(mod, 'title') or 'Mod', qn .. ' mod', RARITY[qk] or WHITE, mod_icon(mod) or (mod and mod:get('icon')), nil, 'MOD'
    elseif t == LOOT_CURRENCY then
        local c = l:get('currency')
        return '+' .. tostring(l:get('amount')) .. ' ' .. (loc_of(c, 'title') or 'currency'), 'Strongbox reward', WHITE, c and c:get('defaultIconRef')
    end
    local gear = l:get('gear')
    if type(gear) == 'userdata' then
        local ok, cls = pcall(function() return gear:class() end)
        cls = ok and cls or ''
        local qn, qk = quality_of(l:get('quality'))
        local maker = loc_of(gear:get('manufacturer'), 'title')
        local own = loc_of(gear, 'title') or 'Gear'
        local name = own
        if maker and not own:lower():find(maker:lower(), 1, true) then name = maker .. ' ' .. own end
        local kind = cls:find('Armour') and 'armour' or cls:find('Weapon') and 'weapon' or 'gear'
        return name, qn .. ' ' .. kind, RARITY[qk] or WHITE, gear:get('menuIcon') or gear:get('icon'), {name = norm(own)}
    end
    return nil
end

-- ------------------------------------------------------------------------------------------ the gear review --
-- The game asks Collect / Dismantle / Sell for each new weapon or armour piece, one at a time, on its review screen
-- (MenuRewards.equipmentReviewDisplay). The mod keeps that screen covered and presses its buttons for the choice made on
-- the notification of the item it is showing.
local cur = nil          -- the review as of the last look: {inst, erd, state, name}
local stuck = false      -- a choice that did not go through (a popup, a full inventory): the cover comes off
local function actionable(S)
    return cur ~= nil and cur.state ~= 0 and cur.name ~= '' and S.gear ~= nil and S.gear.name ~= '' and cur.name:find(S.gear.name, 1, true) ~= nil
end
local function do_action(S, b)
    local btn = cur.erd:get(b.native)
    if type(btn) == 'userdata' and btn:click() then
        S.acted, S.acted_at, S.action = true, za.time(), b.id
    end
end
-- what the sell / dismantle popup pays: its list of currency displays (the game's UI), each with the amount it shows and the
-- currency it is for (currentCurrency: its name and icon). The amounts count up when the popup opens, so this is read a
-- moment after it is up.
local function popup_rewards(pop)
    local out = {}
    local list = pop:get('currencies')
    for i = 1, list and math.min(list:len(), 4) or 0 do
        local it = list:at(i)
        if it then
            local amount
            for _, t in ipairs(it:texts()) do
                local digits = tostring(t):gsub('[^%d]', '')
                if digits ~= '' then amount = tonumber(digits); break end
            end
            local c = it:get('currentCurrency')
            local name = type(c) == 'userdata' and loc_of(c, 'title') or nil
            local aref = type(c) == 'userdata' and c:get('defaultIconRef') or nil
            if amount and amount > 0 then
                out[#out + 1] = {title = '+' .. thousands(amount) .. (name and (' ' .. name) or ''), aref = type(aref) == 'userdata' and aref or nil}
            end
        end
    end
    return out
end
local last_presses = 0
local function handle_buttons(now)
    local mx, my, inside = za.input.mouse()
    local p = za.input.presses(1)   -- the left mouse button
    local clicked = p ~= last_presses
    last_presses = p
    for _, S in ipairs(slots) do
        if S.active and S.gear and not S.leaving and S.lay ~= '' then
            local ready = actionable(S)
            -- a choice made before the game is ready (its box is still opening) waits and goes through as soon as it can
            if S.pending and not S.acted and ready then
                for _, b in ipairs(BUTTONS) do if b.id == S.pending then do_action(S, b) end end
            end
            local open = not S.acted and not S.pending
            local left, top = 960 + S.cx - S.w / 2, 540 + S.cy - S.h / 2
            for i, b in ipairs(BUTTONS) do
                local r = S.rect[i]
                local over = r and inside and mx >= left + r.x and mx < left + r.x + r.w and my >= top + r.y and my < top + r.y + r.h
                local chosen = (S.acted and S.action == b.id) or (S.pending == b.id and not S.acted)
                local st = chosen and 3 or open and (over and 2 or 1) or 0
                if S.btn[i].state ~= st then
                    S.btn[i].state = st
                    za.ui.set(S.btn[i].bg, {color = st == 3 and {1, 0.745, 0.031, 0.9} or st == 2 and {0.95, 0.95, 0.93, 0.3} or st == 1 and {0.95, 0.95, 0.93, 0.16} or {0.95, 0.95, 0.93, 0.05}})
                    za.ui.set(S.btn[i].text, {color = st == 3 and {0.1, 0.1, 0.1, 1} or st == 0 and GREY or WHITE})
                end
                if over and clicked and open then
                    if ready then do_action(S, b) else S.pending = b.id end
                end
            end
        end
    end
    -- a Scrap or Sell asks to be confirmed on the game's popup: confirm it too (nothing happens while it is not up)
    if cur then
        for _, S in ipairs(slots) do
            if S.active and S.acted and S.action ~= 'collect' and not S.leaving then
                local pop = cur.inst:get('sellGearPopup')
                if type(pop) == 'userdata' and pop:is('get_IsVisible') == true then
                    S.popup_seen = S.popup_seen or now
                    -- the amounts count up as the popup opens: read them a moment later, just before confirming
                    if now - S.popup_seen >= 0.7 and (S.confirm_at or 0) + 1 < now then
                        local got = popup_rewards(pop)
                        if pop:confirm() then
                            S.confirm_at = now
                            if not S.reported then
                                S.reported = true
                                for _, r in ipairs(got) do notify(r.title, S.action == 'sell' and 'Sold for' or 'Scrapped for', WHITE, r.aref) end
                            end
                        end
                    end
                else
                    S.popup_seen = nil
                end
            end
        end
    end
end

local shown, xp_shown = false, false
-- the buttons to press, in order, as paths from the menu: its own, then the strongbox opening's (Skip, then Collect)
local SKIP_BUTTONS = {{'continueButton'}, {'collectButton'}, {'strongboxDisplay', 'skipButton'}, {'strongboxDisplay', 'collectButton'}}
local cover_until, was_end = 0, false
local function set_cover(on)
    if on ~= cover_on then cover_on = on; za.ui.set(cover.bg, {visible = on}); za.ui.set(cover.text, {visible = on}) end
end
local next_at, next_press = 0, 0
za.on_frame(function()
    if not za.ui.ready() then return end
    if not slots or built_gen ~= za.ui.generation() then build(); built_gen = za.ui.generation(); queue = {} end
    local now = za.time()
    update_cards(now)
    handle_buttons(now)
    if now < next_at then return end
    next_at = now + 0.1   -- (looks ten times a second so the cover goes up the moment an end screen does)
    local entries = stack_entries()
    local skipping = za.setting('skip_end') ~= false
    local menu
    for _, e in ipairs(entries) do
        if e.class == 'MenuRewards' then menu = e end
        if e.class == 'MenuGameEnd' and not xp_shown then   -- the XP screen: the amount is in its result data
            local inst = e.entry:get('instance')
            local rd = inst and inst:get('resultData')
            local xp = rd and rd:get('totalXpGained')
            if type(xp) == 'number' and xp > 0 then
                xp_shown = true
                notify('+' .. thousands(xp) .. ' XP', loc_of(rd, 'levelNameLoc') or 'Mission complete', WHITE, nil, nil, nil, 'XP')
            end
        end
    end
    if #entries == 0 then xp_shown = false end

    -- the gear review, as it stands
    cur = nil
    if menu then
        local inst = menu.entry:get('instance')
        local erd = inst and inst:get('equipmentReviewDisplay')
        local name = erd and erd:get('itemNameText')
        local ok, txt = pcall(function() return name and name:text() end)
        cur = {inst = inst, erd = erd, state = inst and inst:get('equipmentReviewState') or 0, name = norm(ok and txt or '')}
        if not erd then cur = nil end
    end
    -- choices made: the review has moved on to something else (or ended) -> the notification is done; one that has not
    -- gone through in 3 s (a popup, a full inventory) needs you on the game's own screen
    stuck = false
    for _, S in ipairs(slots) do
        if S.active and S.gear and S.acted and not S.leaving then
            if not actionable(S) then dismiss(S, now)
            elseif now - S.acted_at > 3 then
                stuck = true
                za.ui.set(S.body, {text = 'Needs you on the game screen'})
            end
        end
    end

    -- skipping the end screens: press the game's own button on each, once what it holds has been reported
    local top = entries[#entries]
    if top and skipping then
        local inst = top.entry:get('instance')
        if top.class == 'MenuGameEnd' then   -- the game's own switches for going straight past the XP / stats screens
            local rd = inst:get('resultData')
            if rd then rd:set_flag('shouldSkipOverview', true) end
            inst:set_flag('shouldSkipStats', true)
        end
        local reviewing = top.class == 'MenuRewards' and cur ~= nil and cur.state ~= 0   -- (the gear review is yours to answer)
        if ((top.class == 'MenuGameEnd' and xp_shown) or (top.class == 'MenuRewards' and shown)) and not reviewing and now >= next_press then
            next_press = now + 0.4   -- (the buttons are pressed at the old pace)
            for _, path in ipairs(SKIP_BUTTONS) do
                local b = inst
                for _, f in ipairs(path) do b = type(b) == 'userdata' and b:get(f) or nil end
                if type(b) == 'userdata' and b:click() then
                    cover_until = now + 2.5   -- (things are still moving: keep the screens covered)
                    break
                end
            end
        end
    end
    -- the end screens stay covered while the mod is moving through them or has gear to ask about; if it stops for 2.5 s,
    -- or a choice did not go through, the cover comes off
    local on_end = top and (top.class == 'MenuGameEnd' or top.class == 'MenuRewards') and skipping
    if on_end and not was_end then cover_until = now + 2.5 end   -- (a fresh end screen)
    if on_end then end_done_at = nil elseif was_end then end_done_at = now end
    if not skipping then end_done_at = end_done_at or 0 end
    was_end = on_end or false
    local asking, cards_up = false, false
    for _, S in ipairs(slots) do
        if S.active and not S.leaving then cards_up = true; if S.gear then asking = true end end
    end
    if on_end and asking and not stuck then cover_until = now + 2.5 end
    -- the backdrop stays for as long as a card is on screen (the results of a Scrap or Sell arrive after the end screens)
    set_cover((on_end and now < cover_until) or (skipping and cards_up and not stuck) or false)

    if not menu then
        shown = false
        for _, S in ipairs(slots) do if S.active and S.gear then dismiss(S, now) end end   -- (answered some other way)
        return
    end
    if shown then return end
    shown = true
    rewards_inst = menu.entry:get('instance')
    local rw = menu.params and menu.params:get('rewards')
    for i = 1, rw and rw:len() or 0 do
        local l = rw:at(i)
        if l and l:get('type') == LOOT_CURRENCY then
            local c = l:get('currency')
            local amount = l:get('amount')
            if type(amount) == 'number' and amount > 0 then
                notify('+' .. thousands(amount) .. ' ' .. (loc_of(c, 'title') or 'credits'), 'Mission reward', WHITE, c and c:get('defaultIconRef'))
            end
        end
    end
    local boxes = menu.params and menu.params:get('strongboxes')
    for i = 1, boxes and boxes:len() or 0 do
        local loot = boxes:at(i):get('loot')
        for j = 1, loot and loot:len() or 0 do
            local l = loot:at(j)
            local title, body, color, aref, gear, glyph = describe_loot(l)
            if title then notify(title, body, color, type(aref) == 'userdata' and aref or nil, skipping and gear or nil, nil, glyph)
            end
        end
    end
end)
