-- ZARModManager system script: embedded in the loader (not a mod). It draws on the loader's own canvas, above the game and
-- every mod: the notifications at start-up ("Loader Injected Successfully", then one per mod, or failures), the
-- "MODS" tab on the game's own Settings screen.
-- Returns the function the loader calls every frame tick (about 30 times a second).

local WHITE, AMBER, GREY, DIM = {0.95, 0.95, 0.93, 1}, {1, 0.745, 0.031, 1}, {0.66, 0.69, 0.72, 1}, {0.45, 0.48, 0.5, 1}
local DARK = {0.043, 0.051, 0.063, 1}
local KIND = {
    ok = {sprite = 'check', color = {0.36, 0.86, 0.46, 1}},
    warning = {sprite = 'warn', color = {1, 0.745, 0.031, 1}},
    error = {sprite = 'cross', color = {1, 0.36, 0.3, 1}},
}
local VK_LBUTTON, VK_ESCAPE = 0x01, 0x1B

local function text(spec)
    spec.outline = spec.outline or 0.15
    spec.color = spec.color or WHITE
    if spec.wrap == nil then spec.wrap = false end
    return za.ui.text(spec)
end
local function image(spec) spec.color = spec.color or WHITE; spec.sprite = spec.sprite or 'none'; return za.ui.image(spec) end
local function set(id, spec) if id then za.ui.set(id, spec) end end
local function lines_for(s, size, width)  -- a rough line count for wrapped text
    local n = utf8.len(s or '') or #(s or '')
    return math.max(1, math.ceil(n * size * 0.52 / width))
end

-- ------------------------------------------------------------------------------------------ notifications --
-- Top-right, stacked downwards. Each slides in, stays 5 s, slides out. Half-opacity background, a coloured symbol.
local TW, TOAST_SECS, SLIDE = 470, 7, 0.35
local queue, toasts, next_at = {}, {}, 0
local REL = {bg = 0, stripe = TW - 4, icon = TW - 54, title = 16, body = 16}  -- right-edge offsets inside a toast

local function toast_place(T, base, y, tween)
    for k, id in pairs(T.ids) do set(id, {x = base + REL[k], y = y + (T.dy[k] or 0), tween = tween}) end
end
local function toast_new(ev)
    local K = KIND[ev.kind] or KIND.ok
    local off = -(TW + 40)
    -- title on top, then 10 px, then the body (measured, so long descriptions fit)
    local T = {born = za.time(), y = nil, dy = {bg = 0, stripe = 0, icon = 16, title = 14, body = 50}}
    T.ids = {
        bg = image{anchor = 'tr', x = off, y = 0, w = TW, h = 70, color = {0, 0, 0, 0.5}},
        stripe = image{anchor = 'tr', x = off, y = 0, w = 4, h = 70, color = K.color},
        icon = image{anchor = 'tr', x = off, y = 0, w = 30, h = 30, sprite = K.sprite, color = K.color},
        title = text{anchor = 'tr', x = off, y = 0, w = TW - 86, h = 26, size = 19, align = 'left', valign = 'top', text = ev.title},
        body = text{anchor = 'tr', x = off, y = 0, w = TW - 86, h = 20, size = 15, align = 'left', valign = 'top',
                    text = ev.body, color = GREY, wrap = true},
    }
    local _, bh = za.ui.measure(T.ids.body, ev.body, TW - 86)
    if not bh or bh <= 0 then bh = lines_for(ev.body, 15, TW - 86) * 20 end
    if ev.body == '' then bh = 0 end
    T.h = math.max(70, 50 + bh + 16)
    set(T.ids.body, {h = bh + 4})
    set(T.ids.bg, {h = T.h}); set(T.ids.stripe, {h = T.h})
    return T
end
local function update_toasts()
    for _, ev in ipairs(za.sys.events()) do queue[#queue + 1] = ev end
    local now = za.time()
    if #queue > 0 and now >= next_at then
        local T = toast_new(table.remove(queue, 1))
        toasts[#toasts + 1] = T
        next_at = now + 0.3
    end
    local y = 25
    for i = #toasts, 1, -1 do
        local T = toasts[i]
        if now - T.born > TOAST_SECS + SLIDE + 0.1 then
            for _, id in pairs(T.ids) do za.ui.remove(id) end
            table.remove(toasts, i)
        end
    end
    for _, T in ipairs(toasts) do
        local age = now - T.born
        if age <= TOAST_SECS then
            if T.y == nil then T.y = y; toast_place(T, -(TW + 40), y, nil); toast_place(T, 25, y, SLIDE)  -- slide in
            elseif T.y ~= y then T.y = y; toast_place(T, 25, y, 0.25) end                                    -- close the gap
            y = y + T.h + 10
        elseif not T.leaving then
            T.leaving = true
            toast_place(T, -(TW + 40), T.y, SLIDE)                                                          -- slide out
        end
    end
end

-- ------------------------------------------------------------------------------------------ settings values --
local function fmt_slider(v) return string.format('%.2fx', v) end
local function snap(st, v)
    v = math.max(st.min, math.min(st.max, v))
    if st.step and st.step > 0 then v = st.min + math.floor((v - st.min) / st.step + 0.5) * st.step end
    return math.max(st.min, math.min(st.max, v))
end

-- ------------------------------------------------------------------------------------------ native settings tab --
-- A "MODS" tab on the game's own Settings screen: a copy of its Audio tab, a copy of its Gameplay page, and rows copied from
-- the game's own pages (a toggle, a slider, a keyboard binding, a section header), so everything looks and behaves like the
-- rest of the screen. The copies have no click actions of their own: this script reads their state each tick and saves a
-- changed setting at once.
local NT = nil            -- the tab built for the open Settings screen
local nt_error_logged = false
local BS = '\\\\'         -- the two backslashes the game puts after every label and heading

local function child(parent, name)
    if not parent then return nil end
    for _, c in ipairs(za.sys.nui.children(parent)) do
        if c:name() == name then return c end
    end
    return nil
end
local function path(root, ...)
    local c = root
    for _, n in ipairs({...}) do c = child(c, n) end
    return c
end

local function find(go, name, depth)   -- a descendant object by name
    for _, c in ipairs(za.sys.nui.children(go)) do
        if c:name() == name then return c end
        if depth > 1 then local f = find(c, name, depth - 1); if f then return f end end
    end
    return nil
end
local function find_comp(go, cls, depth)   -- a component of that class on the object or below it
    local c = za.sys.nui.component(go, cls)
    if c then return c end
    if depth > 0 then
        for _, k in ipairs(za.sys.nui.children(go)) do
            local f = find_comp(k, cls, depth - 1)
            if f then return f end
        end
    end
    return nil
end

local function settings_menu()   -- the game's Settings screen while it is open
    local r = za.static('GameRunner', '<Instance>k__BackingField')
    local ms = r and r:get('menuStack')
    local st = ms and ms:get('stack')
    local arr, size = st and st:get('_array'), st and st:get('_size')
    if type(size) ~= 'number' or not arr then return nil end
    for i = 1, math.min(size, 8) do
        local e = arr:at(i)
        local inst = e and e:get('instance')
        local ok, cls = pcall(function() return inst and inst:class() end)
        if ok and cls == 'MenuSettings' then return inst end
    end
    return nil
end

local function plain(s) return (tostring(s or ''):gsub('<[^>]->', '')) end
local function lines_of(s, width, max)   -- a text split at spaces into lines of at most `width` characters (the rest is cut)
    local out, cur = {}, ''
    for word in plain(s):gmatch('%S+') do
        if #cur > 0 and #cur + 1 + #word > width then
            out[#out + 1] = cur; cur = word
            if #out >= max then break end
        else cur = (#cur > 0 and (cur .. ' ') or '') .. word end
    end
    if #out < max and #cur > 0 then out[#out + 1] = cur end
    if #out == max and #plain(s) > #table.concat(out, ' ') + 2 then out[max] = out[max]:sub(1, math.max(1, width - 1)) .. '…' end
    return out
end

local function box_text(R)   -- what the value box of a key / choice / read-only row says
    local st = R.st
    local v = NT.vals[R.mod][st.id]
    if st.type == 'key' then
        if NT.capture == R then return 'PRESS A KEY' end
        return za.input.key_name(math.floor(v)):upper()
    end
    if st.type == 'choice' then return st.options[math.max(0, math.min(#st.options - 1, math.floor(v))) + 1]:upper() end
    return tostring(st.value):upper()
end

local function build_native(inst)
    local N = za.sys.nui
    local group, root = inst:get('headers'), inst:get('optionsRoot')
    local audio = child(group, 'header_audio')
    local gameplay = child(root, 'gameplay_options')
    local content = path(gameplay, 'Scroll View', 'Viewport', 'Content')
    local t_toggle, t_slider = child(content, 'weather toggle'), child(content, 'minimap_size_slider')
    local t_rebind = path(root, 'mnk_options', 'Scroll View', 'Viewport', 'Content', 'Rebind Move_Up')
    local t_dropdown = child(content, 'language_setting')
    local t_header = inst:get('optionsListHeaderPrefab')
    if not (group and root and audio and gameplay and content and t_toggle and t_slider and t_rebind and t_dropdown and t_header) then
        za.log('settings tab: a native piece was not found (' .. tostring(audio ~= nil) .. ' ' .. tostring(gameplay ~= nil) .. ' ' .. tostring(content ~= nil) ..
               ' ' .. tostring(t_toggle ~= nil) .. ' ' .. tostring(t_slider ~= nil) .. ' ' .. tostring(t_rebind ~= nil) .. ' ' .. tostring(t_header ~= nil) .. ')')
        return {failed = true}
    end
    local tab = N.clone(audio, group, 'tab', 'header_mods')
    local page = N.clone(gameplay, root, 'page', 'mods_options')
    local pcontent = path(page, 'Scroll View', 'Viewport', 'Content')
    if not (tab and page and pcontent) then za.log('settings tab: the copies could not be made'); return {failed = true} end
    for _, c in ipairs(N.children(pcontent)) do N.destroy(c) end
    N.text(tab, 'MODS')
    local tog = N.component(tab, 'Toggle')
    N.call(tog, 'SetIsOnWithoutNotify', false)
    N.call(tog, 'set_group', group)
    local receiver = N.component(page, 'PanelShowReceiver')
    N.call(receiver, 'ForceHide')
    local T = {tab = tab, tog = tog, page = page, receiver = receiver, group = group, rows = {}, vals = {}, shown = false, pending = {},
               mods = za.sys.mods(), refresh = 0}
    for _, m in ipairs(T.mods) do
        T.vals[m.index] = {}
        for _, st in ipairs(m.settings) do if st.type ~= 'info' and st.type ~= 'hidden' then T.vals[m.index][st.id] = st.value end end
    end
    local function header(txt)
        local h = N.clone(t_header, pcontent, 'row')
        N.text(h, txt)
        return h
    end
    local function paragraph(txt, color)   -- wrapped text in a row as tall as it needs to be
        local h = N.clone(t_header, pcontent, 'row')
        N.text(h, '<size=88%><color=' .. color .. '>' .. txt .. '</color></size>')
        local tmp = N.component(child(h, 'label'), 'NK_TextMeshProUGUI')
        N.call(tmp, 'set_textWrappingMode', 1)
        N.call(tmp, 'set_alignment', 257)
        N.place(h, nil, nil, nil, math.ceil(#plain(txt) / 72) * 28 + 22)
        return h
    end
    for i, m in ipairs(T.mods) do
        if i > 1 then header(' ') end   -- (a gap between one mod's rows and the next mod's heading)
        header(plain(m.name):upper() .. ' ' .. BS)
        local about = (m.version ~= '' and ('v' .. m.version .. '  -  ') or '') .. (m.description ~= '' and m.description or '')
        if about ~= '' then paragraph(plain(about):upper(), '#C8CDD0') end
        if m.status ~= 'ok' and m.message ~= '' then paragraph(plain(m.message):upper(), '#FF6B5C') end
        for _, st in ipairs(m.settings) do if st.type ~= 'hidden' then   -- (a hidden setting has no row)
            local R = {mod = m.index, st = st}
            if st.type == 'slider' then
                R.go = N.clone(t_slider, pcontent, 'row')
                R.comp = N.component(R.go, 'Slider')
                N.call(R.comp, 'set_wholeNumbers', false)
                N.call(R.comp, 'set_minValue', st.min)
                N.call(R.comp, 'set_maxValue', st.max)
                N.call(R.comp, 'SetValueWithoutNotify', st.value)
            elseif st.type == 'toggle' then
                R.go = N.clone(t_toggle, pcontent, 'row')
                R.comp = N.component(R.go, 'Toggle')
                N.call(R.comp, 'SetIsOnWithoutNotify', st.value ~= 0)
                N.text(R.go, st.label .. ' ' .. BS)
                R.label_done = true
                R.label_obj, R.label_base = R.go, st.label .. ' ' .. BS
            elseif st.type == 'choice' then   -- the game's own dropdown row (the language setting's)
                R.go = N.clone(t_dropdown, pcontent, 'row')
                local lab = child(R.go, 'label')
                if lab then N.text(lab, st.label .. ' ' .. BS) end
                R.label_done = true
                R.label_obj, R.label_base = lab, st.label .. ' ' .. BS
                R.comp = find_comp(R.go, 'TMP_Dropdown', 3)
                -- the game's dropdown shows pictures (its languages), so it has no text objects: give it a caption and an item
                -- label, copies of the row's own label text, and point the dropdown at them
                local dgo, item = child(R.go, 'LanguageDropdown'), find(R.go, 'Item', 6)
                local cap = dgo and lab and N.clone(lab, dgo, 'row', 'Caption')
                local ilab = item and lab and N.clone(lab, item, 'row', 'Item Label')
                for _, o in ipairs({cap, ilab}) do
                    N.fill(o, 16, 0, o == cap and 56 or 16, 0)
                    N.call(N.component(o, 'NK_TextMeshProUGUI'), 'set_alignment', 514)
                end
                if cap and ilab then
                    N.set_field(R.comp, 'm_CaptionText', N.component(cap, 'NK_TextMeshProUGUI'))
                    N.set_field(R.comp, 'm_ItemText', N.component(ilab, 'NK_TextMeshProUGUI'))
                end
                local opts = {}
                for k, o in ipairs(st.options) do opts[k] = o end
                N.dropdown(R.comp, opts, math.floor(st.value))
            else   -- a key or a read-only value: the game's own keyboard binding row, with one box
                R.go = N.clone(t_rebind, pcontent, 'row')
                local lab = child(R.go, 'label')
                if lab then N.text(lab, st.label .. ' ' .. BS) end
                R.label_done = true
                R.label_obj, R.label_base = lab, st.label .. ' ' .. BS
                local b1 = path(R.go, 'bindings', 'binding1')
                local b2 = path(R.go, 'bindings', 'binding2')
                local close = child(R.go, 'FrostedCloseButton')
                if b2 then N.active(b2, false) end
                if close then N.active(close, false) end
                R.btn = N.component(b1, 'ZAButton')
                R.box = child(b1, 'HotkeyDisplay')
                if b1 then   -- (wider than the game's key box, growing leftwards so the right edge stays put)
                    local x, y, w = N.rect(b1)
                    if x then
                        local want = st.type == 'key' and 440 or st.type == 'choice' and 560 or 700
                        N.place(b1, x - (want - w) / 2, y, want)
                    end
                end
                if st.type == 'info' and b1 then   -- a read-only value is plain text: no box, no hover or press look
                    for _, name in ipairs({'base', 'pressed', 'highlight'}) do
                        local part = child(b1, name)
                        if part then N.destroy(part) end
                    end
                end
            end
            T.rows[#T.rows + 1] = R
        end end
    end
    za.log('settings tab: MODS tab built with ' .. #T.rows .. ' settings of ' .. #T.mods .. ' mods')
    return T
end

local function save_mod(index)
    NT.pending[index] = nil
    za.sys.save(index, NT.vals[index])
end

local function clicked(R)   -- a press that was let go over the control (the copies have no click event of their own)
    -- the game's button keeps its state as flags: 1 = the pointer is over it, 2 = it is pressed
    local f = R.btn:get('currentFlags')
    if type(f) ~= 'number' then return false end
    local pressed, over = (math.floor(f) & 2) ~= 0, (math.floor(f) & 1) ~= 0
    local hit = R.was_pressed and not pressed and over
    R.was_pressed = pressed
    return hit
end

local LOCK_NOTE = '  <size=70%><color=#FFBE08>LOCKED DURING A MISSION</color></size>'
local function apply_lock(R, locked)   -- a setting marked lock = 'mission': greyed out and not usable while a mission is played
    local N = za.sys.nui
    R.locked = locked
    local control = R.comp or R.btn
    if control then N.call(control, 'set_interactable', not locked) end
    if R.btn and R.comp ~= R.btn then N.call(R.btn, 'set_interactable', not locked) end
    if R.label_obj then N.text(R.label_obj, R.label_base .. (locked and LOCK_NOTE or '')) end
    if R.st.type == 'slider' then R.text = nil end   -- (its text is made again below)
end

local function update_native()
    local inst = settings_menu()
    if not inst then
        if NT and not NT.failed then for index in pairs(NT.pending) do save_mod(index) end end   -- (a change not yet saved)
        NT = nil
        return
    end
    local addr = inst:addr()
    if not NT or NT.addr ~= addr then
        NT = {failed = true, addr = addr}   -- (stays so if building it fails: it is not tried again for this screen)
        local built = build_native(inst)
        if built and not built.failed then built.addr = addr; NT = built; za.sys.key_pressed() end
        return
    end
    if NT.failed then return end
    local N = za.sys.nui
    -- settings marked lock = 'mission' cannot be changed while a mission is played
    local rr = za.static('GameRunner', '<Instance>k__BackingField')
    local locked = rr ~= nil and rr:is('IsInMission') == true
    for _, R in ipairs(NT.rows) do
        if R.st.lock == 'mission' and (R.locked or false) ~= locked then apply_lock(R, locked) end
    end
    -- the tab is selected: show the page (the game hides its own when this tab's toggle takes over the group)
    local on = NT.tog:get('m_IsOn') == true
    if on ~= NT.shown then NT.shown = on; N.call(NT.receiver, 'Show', on) end
    -- live values of the read-only rows
    NT.refresh = NT.refresh + 1
    if NT.refresh % 10 == 0 then
        local fresh = za.sys.mods()
        for _, m in ipairs(fresh) do for _, st in ipairs(m.settings) do
            if st.type == 'info' then for _, R in ipairs(NT.rows) do if R.mod == m.index and R.st.id == st.id then R.st.value = st.value end end end
        end end
    end
    if NT.capture then
        local k = za.sys.key_pressed()
        if k == VK_ESCAPE then NT.capture = nil
        elseif k then NT.vals[NT.capture.mod][NT.capture.st.id] = k; NT.pending[NT.capture.mod] = za.time(); NT.capture = nil end
    end
    for _, R in ipairs(NT.rows) do
        local st, vals = R.st, NT.vals[R.mod]
        if st.type == 'toggle' then
            local v = R.comp:get('m_IsOn') == true and 1 or 0
            if v ~= vals[st.id] then vals[st.id] = v; NT.pending[R.mod] = za.time() - 10 end
        elseif st.type == 'slider' then
            local v = R.comp:get('m_Value')
            if type(v) == 'number' then
                v = snap(st, v)
                if math.abs(v - vals[st.id]) > 1e-6 then vals[st.id] = v; NT.pending[R.mod] = za.time() end
            end
            local txt = st.label .. ' ' .. BS .. '   <color=#FFBE08>' .. fmt_slider(vals[st.id]) .. '</color>' .. (R.locked and LOCK_NOTE or '')
            if txt ~= R.text then R.text = txt; N.text(R.go, txt) end
        elseif st.type == 'choice' then
            local v = R.comp and R.comp:get('m_Value')
            if type(v) == 'number' and v ~= vals[st.id] then vals[st.id] = v; NT.pending[R.mod] = za.time() - 10 end
        else
            if st.type == 'key' and R.btn and clicked(R) then NT.capture = R; za.sys.key_pressed() end
            local txt = box_text(R)
            if txt ~= R.text and R.box then R.text = txt; N.text(R.box, txt) end
        end
    end
    for index, t in pairs(NT.pending) do
        if za.time() - t > 0.4 then save_mod(index) end
    end
end

-- ------------------------------------------------------------------------------------------ pause state --
-- Whether one of the game's menus (the pause menu, Settings) is open on top of the game: the Auto-Shooter and the other
-- input features hold off while it is.
local function update_menu_state()
    local r = za.static('GameRunner', '<Instance>k__BackingField')
    local ms = r and r:get('menuStack')
    local st = ms and ms:get('stack')
    local size = st and st:get('_size')
    za.sys.menu_open(za.sys.in_game() and type(size) == 'number' and size > 1)
    za.sys.mission_active(r ~= nil and r:is('IsInMission') == true)
end

-- ------------------------------------------------------------------------------------------ frame --
return function()
    if not za.ui.ready() then return end
    update_toasts()
    local ok, err = pcall(update_menu_state)
    if not ok and not nt_error_logged then nt_error_logged = true; za.log('menu state error: ' .. tostring(err)) end
    ok, err = pcall(update_native)
    if not ok and not nt_error_logged then nt_error_logged = true; za.log('settings tab error: ' .. tostring(err)) end
end
