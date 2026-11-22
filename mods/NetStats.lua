-- Net Stats (a ZARModManager mod): your frame rate and connection, live. Compact: FPS, frame time and ping. Details
-- (a hotkey): frame time spikes, network jitter, snapshot rate and size, the world's load (entities, enemies,
-- projectiles, events per update) and who is hosting, with a 30-second graph for the ones that move. Read-only: frame
-- times come from Unity's own counters (za.perf), everything else from the game's per-frame network summary
-- (GameRunner.clientUpdateResult), the same data its own connection indicator uses. It only measures this PC: other
-- players' frame rates are not sent by the game.

za.mod{
    name = 'Net Stats',
    version = '1.2.0',
    description = 'Live frame rate and connection stats: FPS, frame time, ping, plus jitter, snapshot and world load details on a hotkey. Refreshes less often on PCs that are struggling.',
    settings = {
        {id = 'show', type = 'toggle', label = 'Show the Net Stats panel', default = true},
        {id = 'key', type = 'key', label = 'Details key', default = 0x75},
        {id = 'corner', type = 'slider', label = 'Position (1 top right, 2 top left, 3 bottom left, 4 bottom right)', min = 1, max = 4, step = 1, default = 1},
        {id = 'size', type = 'slider', label = 'Panel Size (scaling factor)', min = 0.5, max = 2, step = 0.05, default = 1},
        {id = 'graphs', type = 'toggle', label = 'Graphs (last 30 s)', default = true},
        {id = 'log_drops', type = 'toggle', label = 'Log FPS drops', default = false},
        {id = 'drop_fps', type = 'slider', label = 'FPS drop threshold', min = 10, max = 120, step = 5, default = 30},
    },
}

local STATE_INGAME = 3
local W, HEAD, ROW = 380, 32, 22
local SPARK_X, SPARK_W, SPARK_N, SPARK_H = 232, 138, 30, 16
-- where the panel sits per corner, clear of the Minimalistic Mod HUD (strip, menu buttons, minimap, squad, gear)
local CORNERS = {
    {anchor = 'tr', x = 95, y = 90},    -- top right: under the strip's end, left of the menu buttons
    {anchor = 'tl', x = 25, y = 240},   -- top left: under the minimap
    {anchor = 'bl', x = 25, y = 340},   -- bottom left: above the squad panel
    {anchor = 'br', x = 25, y = 220},   -- bottom right: above turret / grenade
}
local WHITE, GREY, PANEL = {0.95, 0.95, 0.93, 1}, {0.6, 0.63, 0.65, 1}, {0.043, 0.051, 0.063, 0.72}
local GOOD, WARN, BAD = {0.45, 1, 0.45, 1}, {1, 0.745, 0.031, 1}, {1, 0.35, 0.3, 1}

local function hex(c) return string.format('#%02X%02X%02X', math.floor(c[1] * 255), math.floor(c[2] * 255), math.floor(c[3] * 255)) end
local function grade(v, good, ok, higherIsBetter)
    if v == nil then return GREY end
    if higherIsBetter then return v >= good and GOOD or v >= ok and WARN or BAD end
    return v <= good and GOOD or v <= ok and WARN or BAD
end
local function col(text, c) return '<color=' .. hex(c) .. '>' .. text .. '</color>' end

-- Detail rows: label, and the graph's metric (nil: no graph) with its colour grading
local ROWS = {
    {key = 'fps', label = 'FPS', graph = true},
    {key = 'frame', label = 'FRAME TIME', graph = true},
    {key = 'ping', label = 'PING', graph = true},
    {key = 'offset', label = 'JITTER'},
    {key = 'link', label = 'CONNECTION'},
    {key = 'snap', label = 'SNAPSHOTS'},
    {key = 'world', label = 'ENTITIES'},
    {key = 'proj', label = 'PROJECTILES', graph = true},
    {key = 'events', label = 'EVENTS', graph = true},
    {key = 'host', label = 'HOST'},
}

-- ------------------------------------------------------------------------------------------ elements --
local E, built_gen, built_corner = nil, nil, nil
local function remove_all()
    if not E then return end
    for _, id in ipairs(E.all) do za.ui.remove(id) end
    E = nil
end
local function build(corner)
    remove_all()
    local C = CORNERS[corner] or CORNERS[1]
    local all = {}
    local function add(id) all[#all + 1] = id; return id end
    local g = add(za.ui.group{anchor = C.anchor, x = C.x, y = C.y, w = W, h = HEAD + #ROWS * ROW + 12})
    local function text(spec) spec.parent = g; spec.outline = spec.outline or 0.2; spec.color = spec.color or WHITE; return add(za.ui.text(spec)) end
    local function image(spec) spec.parent = g; spec.sprite = spec.sprite or 'none'; return add(za.ui.image(spec)) end
    E = {g = g, all = all, rows = {}}
    E.head_bg = image{anchor = 'tl', x = 0, y = 0, w = W, h = HEAD, color = PANEL}
    E.head_stripe = image{anchor = 'tl', x = 0, y = 0, w = 4, h = HEAD, color = GREY}
    E.head_left = text{anchor = 'tl', x = 12, y = 6, w = 230, h = 22, size = 17, align = 'left', text = ''}
    E.head_right = text{anchor = 'tl', x = W - 152, y = 6, w = 120, h = 22, size = 17, align = 'right', text = ''}
    E.dot = image{anchor = 'tl', x = W - 24, y = 11, w = 10, h = 10, sprite = 'disc', color = GREY}
    E.body_bg = image{anchor = 'tl', x = 0, y = HEAD + 2, w = W, h = #ROWS * ROW + 10, color = PANEL, visible = false}
    for i, R in ipairs(ROWS) do
        local y = HEAD + 6 + (i - 1) * ROW
        local r = {
            label = text{anchor = 'tl', x = 12, y = y, w = 110, h = 18, size = 13, align = 'left', text = R.label, color = GREY, visible = false},
            value = text{anchor = 'tl', x = 120, y = y, w = (R.graph and SPARK_X - 126 or W - 132), h = 18, size = 14, align = 'left', text = '', visible = false},
            bars = {}, y = y,
        }
        if R.graph then
            local bw = SPARK_W / SPARK_N
            for b = 1, SPARK_N do
                r.bars[b] = image{anchor = 'tl', x = SPARK_X + (b - 1) * bw, y = y + SPARK_H - 1, w = math.max(1, bw - 1), h = 1, color = GREY, visible = false}
            end
        end
        E.rows[R.key] = r
    end
end

-- ------------------------------------------------------------------------------------------ sampling --
-- Every frame callback (10 a second) reads the current numbers; once a second each graph gets a point (the worst of
-- that second for frame time, projectiles and events; the latest for FPS and ping).
local hist = {fps = {}, frame = {}, ping = {}, proj = {}, events = {}}
local acc = {}                -- this second's accumulators
local sec_start, snap_id0, snap_rate = nil, nil, nil
local last_drop_log = -100

local function push(h, v)
    h[#h + 1] = v
    if #h > SPARK_N then table.remove(h, 1) end
end
local function num(v) return type(v) == 'number' and v or nil end

-- The client's latency report (NativeBuildInterop.SLatencySampleReport: average_ms, lowest_ms, highest_ms,
-- percentile_95_ms)
local function latency_report(cur)
    local r = cur:get('client_latency_report')
    if not r or type(r) ~= 'userdata' then return nil end
    return {average_ms = r:get('average_ms'), highest_ms = r:get('highest_ms'), percentile_95_ms = r:get('percentile_95_ms')}
end

-- The ping the game's own connection indicator shows (GameHud.networkHealthIndicator, or the host's when hosting):
-- the number in its latency text, which the game keeps up to date whether or not the text is visible
local function game_ping(runner)
    local hud = runner:get('gameHud')
    if not hud then return nil end
    for _, k in ipairs({'networkHealthIndicator', 'hostNetworkHealthIndicator'}) do
        local ind = hud:get(k)
        local t = ind and ind:get('latency_text')
        local ms = t and tonumber((t:text() or ''):match('(%d+)'))
        if ms and ind:active() then return ms end
    end
    return nil
end

local function read(runner)
    local s = {}
    local p = za.perf()
    if p then s.fps, s.frame_ms, s.worst_ms = p.fps, p.frame_ms, p.worst_ms end
    local cur = runner:get('clientUpdateResult')
    if not cur then return s end
    -- ping: the number the game's own connection indicator shows (its signal bars are graded from it); else the
    -- client's latency report (average_ms, lowest_ms, highest_ms, percentile_95_ms); host_latency_rtt_ms only as a last
    -- resort (in the hub it reads far above the game's own figure)
    s.ping = game_ping(runner)
    local lr = latency_report(cur)
    if lr then
        if not s.ping then s.ping = num(lr.average_ms) end
        s.ping_p95 = num(lr.percentile_95_ms) or num(lr.highest_ms)
        -- jitter: how far the slower round trips run above the average one (the game's own measurement), so a steady
        -- connection reads near 0 whatever its ping, and however often the server sends updates
        local avg = num(lr.average_ms)
        if avg and s.ping_p95 and (avg > 0 or s.ping_p95 > 0) then s.jitter = math.max(0, s.ping_p95 - avg) end
    end
    if not s.ping or s.ping == 0 then s.ping = num(cur:get('host_latency_rtt_ms')) or s.ping end
    local st = num(cur:get('client_connection_status'))
    s.link = st and (za.enum('NativeBuildInterop.NetworkConnectionState', st) or tostring(st))
    local er = num(cur:get('client_network_error_reason'))
    if er and er ~= 0 then s.error = za.enum('NativeBuildInterop.SimulationNetworkErrorReason', er) or tostring(er) end
    s.snap_id = num(cur:get('current_snapshot_id'))
    s.snap_avg = num(cur:get('avg_snapshot_in_history_size'))
    s.snap_max = num(cur:get('largest_snapshot_size'))
    s.entities, s.enemies = num(cur:get('entity_count')), num(cur:get('enemy_entity_count'))
    s.proj = num(cur:get('projectile_entity_mapping_count'))
    s.events = num(cur:get('event_count'))
    -- players, and who hosts. Whether you host is the game's own answer (GameRunner.IsServerHost: the game server runs
    -- on this PC; else HasServer); the client list's is_mission_host flag only names the other player who does, or
    -- decides when the game gives no answer
    local clients, n, myId = cur:get('clients'), num(cur:get('client_count')) or 0, cur:get('client_id')
    s.players = n
    local hosting = runner:is('get_IsServerHost')
    if hosting == nil then hosting = runner:is('get_HasServer') end
    local flagged = nil
    for i = 1, clients and math.min(n, 16) or 0 do
        local c = clients:at(i)
        if c and c:get('is_mission_host') == true then
            if c:get('id') == myId then flagged = flagged or 'you' else flagged = tostring(c:get('display_name') or '?') end
        end
    end
    if hosting == true then s.host = 'you'
    elseif hosting == false then s.host = flagged ~= 'you' and flagged or nil
    else s.host = flagged end
    return s
end

local function sample(s, now)
    acc.frame = math.max(acc.frame or 0, s.worst_ms or 0)
    acc.proj = math.max(acc.proj or 0, s.proj or 0)
    acc.events = math.max(acc.events or 0, s.events or 0)
    if not sec_start then sec_start, snap_id0 = now, s.snap_id; return end
    if now - sec_start < 1 then return end
    if s.snap_id and snap_id0 and s.snap_id >= snap_id0 then snap_rate = (s.snap_id - snap_id0) / (now - sec_start) end
    push(hist.fps, s.fps or 0)
    push(hist.frame, acc.frame)
    push(hist.ping, s.ping or 0)
    push(hist.proj, acc.proj)
    push(hist.events, acc.events)
    sec_start, snap_id0, acc = now, s.snap_id, {}
end

-- ------------------------------------------------------------------------------------------ drawing --
local GRAPH = {  -- per graph: its scale (a fixed one, or at least this and grown to the highest point) and grading
    fps = {min_top = 120, grade = function(v) return grade(v, 55, 30, true) end},
    frame = {fixed = 100, grade = function(v) return grade(v, 33, 66) end},
    ping = {fixed = 250, grade = function(v) return grade(v, 80, 150) end},
    proj = {min_top = 10, grade = function() return GREY end},
    events = {min_top = 10, grade = function() return GREY end},
}
local function draw_graph(key)
    local r, h, G = E.rows[key], hist[key], GRAPH[key]
    local top = G.fixed
    if not top then top = G.min_top; for _, v in ipairs(h) do top = math.max(top, v) end end
    for b = 1, SPARK_N do
        local v = h[b - SPARK_N + #h]   -- right-aligned: the newest point on the right
        if v then
            local bh = math.max(1, math.floor(math.min(1, v / top) * SPARK_H + 0.5))
            za.ui.set(r.bars[b], {visible = true, h = bh, y = r.y + SPARK_H - bh, color = G.grade(v)})
        else
            za.ui.set(r.bars[b], {visible = false})
        end
    end
end
local function hide_graphs()
    for _, R in ipairs(ROWS) do
        if R.graph then for b = 1, SPARK_N do za.ui.set(E.rows[R.key].bars[b], {visible = false}) end end
    end
end

local function fmt(v, f) return v and string.format(f, v) or '—' end

local function draw_head(s)
    local fpsC, pingC = grade(s.fps, 55, 30, true), grade(s.ping, 80, 150)
    za.ui.set(E.head_left, {text = col(fmt(s.fps, '%.0f'), fpsC) .. ' <size=13>FPS</size>   ' .. fmt(s.frame_ms, '%.1f') .. ' <size=13>ms</size>'})
    za.ui.set(E.head_right, {text = (s.host == 'you' and '<size=13>HOST</size> ' or '<size=13>PING</size> ') ..
                             (s.host == 'you' and '—' or col(fmt(s.ping, '%.0f'), pingC) .. ' <size=13>ms</size>')})
    za.ui.set(E.dot, {color = s.error and BAD or (s.host == 'you' and GOOD) or pingC})
    za.ui.set(E.head_stripe, {color = fpsC})
end

local function draw_rows(s)
    local fpsC, pingC = grade(s.fps, 55, 30, true), grade(s.ping, 80, 150)
    local linkC = s.error and BAD or (s.link and s.link:lower():find('connect') and not s.link:lower():find('dis') and GOOD) or (s.link and WARN) or GREY
    local V = {
        fps = col(fmt(s.fps, '%.0f'), fpsC),
        frame = fmt(s.frame_ms, '%.1f') .. ' / ' .. col(fmt(s.worst_ms, '%.0f') .. ' ms', grade(s.worst_ms, 33, 66)),
        ping = s.host == 'you' and 'hosting' or (col(fmt(s.ping, '%.0f') .. ' ms', pingC) ..
               (s.ping_p95 and string.format('   <color=#9AA1A6>p95</color> %.0f', s.ping_p95) or '')),
        offset = s.jitter and col(string.format('%.0f ms', s.jitter), grade(s.jitter, 15, 40)) or '—',
        link = s.error and col(s.error, BAD) or col(s.link or '—', linkC),
        snap = fmt(snap_rate, '%.0f/s') .. '   ' .. fmt(s.snap_avg and s.snap_avg / 1024, '%.1f KB') ..
               (s.snap_max and string.format(' (max %.1f)', s.snap_max / 1024) or ''),
        world = fmt(s.entities, '%d') .. '   <color=#9AA1A6>enemies</color> ' .. fmt(s.enemies, '%d'),
        proj = fmt(s.proj, '%d'),
        events = fmt(s.events, '%d') .. ' <color=#9AA1A6>/ update</color>',
        host = (s.host or '—') .. (s.players and s.players > 0 and string.format('   <color=#9AA1A6>%d player%s</color>', s.players, s.players == 1 and '' or 's') or ''),
    }
    for _, R in ipairs(ROWS) do za.ui.set(E.rows[R.key].value, {text = V[R.key] or '—'}) end
end

local function show_details(on)
    za.ui.set(E.body_bg, {visible = on})
    for _, R in ipairs(ROWS) do
        local r = E.rows[R.key]
        za.ui.set(r.label, {visible = on})
        za.ui.set(r.value, {visible = on})
    end
    if not on then hide_graphs() end
end

-- The light reading taken every callback, for the per-second peaks and the graphs: frame times, projectiles, events
-- and the snapshot counter (a handful of field reads).
local function light(runner)
    local l = {}
    local p = za.perf()
    if p then l.fps, l.worst_ms = p.fps, p.worst_ms end
    local cur = runner:get('clientUpdateResult')
    if cur then
        l.proj, l.events, l.snap_id = num(cur:get('projectile_entity_mapping_count')), num(cur:get('event_count')), num(cur:get('current_snapshot_id'))
    end
    return l
end

-- ------------------------------------------------------------------------------------------ frame --
-- Adaptive refresh. The runtime calls each mod at most 10 times a second; here each call only takes the light reading
-- above. The panel is redrawn (the full reading) twice a second and the graphs once a second, and the detail rows and
-- graphs are not touched while the details are closed. On a PC that is struggling (under 40 FPS) or if this mod's own
-- work averages over 0.5 ms a call, the panel is redrawn once a second and the graphs every 2 seconds.
local detail, key, seen = false, nil, 0
local shown_detail, shown_graphs, group_state = nil, nil, nil
local next_draw, next_graph, cost, last_ping = 0, 0, 0, nil
za.on_frame(function()
    local k = za.setting('key') or 0x75
    if k ~= key then key = k; seen = za.input.presses(k) end
    local p = za.input.presses(key)
    if p ~= seen then seen = p; detail = not detail end

    if not za.ui.ready() then return end
    local t0 = za.time()
    local runner = za.static('GameRunner', '<Instance>k__BackingField')
    local inGame = runner and runner:get('gameState') == STATE_INGAME
    local corner = math.floor(za.setting('corner') or 1)
    if inGame and (not E or built_gen ~= za.ui.generation() or built_corner ~= corner) then
        build(corner); built_gen, built_corner = za.ui.generation(), corner
        shown_detail, shown_graphs, group_state, next_draw, next_graph = nil, nil, nil, 0, 0
    end
    if not E then return end
    local size = za.setting('size') or 1
    local shown = za.setting('show') ~= false   -- the panel can be switched off entirely in Settings, MODS tab
    local gs = tostring(inGame == true) .. tostring(shown) .. size
    if gs ~= group_state then group_state = gs; za.ui.set(E.g, {visible = inGame == true and shown, scale = size}) end
    if not inGame then return end
    if not shown and not za.setting('log_drops') then return end   -- (nothing to draw, nothing to read: only the FPS drop log still needs the numbers)

    local now = t0
    local l = light(runner)
    l.ping = last_ping
    sample(l, now)

    local slow = (l.fps and l.fps < 40) or cost > 0.5
    local graphs = detail and za.setting('graphs') ~= false
    if detail ~= shown_detail then show_details(detail); shown_detail, next_draw, next_graph = detail, 0, 0 end
    if graphs ~= shown_graphs then if not graphs then hide_graphs() end; shown_graphs, next_graph = graphs, 0 end

    if now >= next_draw then
        next_draw = now + (slow and 1 or 0.5)
        local s = read(runner)
        last_ping = s.ping
        draw_head(s)
        if detail then draw_rows(s) end
        if za.setting('log_drops') and s.fps and s.fps < (za.setting('drop_fps') or 30) and now - last_drop_log >= 5 then
            last_drop_log = now
            za.log(string.format('FPS drop: %.0f FPS (worst frame %.0f ms), ping %s, entities %s (enemies %s), projectiles %s, events %s, snapshot %s KB',
                s.fps, s.worst_ms or 0, fmt(s.ping, '%.0f ms'), fmt(s.entities, '%d'), fmt(s.enemies, '%d'), fmt(s.proj, '%d'),
                fmt(s.events, '%d'), fmt(s.snap_avg and s.snap_avg / 1024, '%.1f')))
        end
    end
    if graphs and now >= next_graph then
        next_graph = now + (slow and 2 or 1)
        for _, R in ipairs(ROWS) do if R.graph then draw_graph(R.key) end end
    end
    cost = cost * 0.9 + (za.time() - now) * 1000 * 0.1   -- this mod's own work per call, in ms (smoothed)
end)
