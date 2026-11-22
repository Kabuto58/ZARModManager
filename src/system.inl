// ZARModManager system (step 3), included by loader.cpp after ui.inl: what the loader itself does in game, apart from any
// mod. Mod manifests and settings (za.mod / za.setting), keyboard and mouse input, the auto-fire input service,
// loader notifications, the pause-menu "Mod Settings" button, and the loader's own Lua script (system.lua, embedded
// in the DLL as a resource) that draws the notifications and the settings screen on the loader's canvas (layer 1).

// ---------------------------------------------------------------------------------------------- events --
// Notifications for the system script to show: kind 0 ok (green check), 1 warning (amber), 2 error (red cross)
struct LoaderEvent { int kind; std::string title, body; };
static std::vector<LoaderEvent> g_events;
static void push_event(int kind, const std::string& title, const std::string& body) {
    g_events.push_back({kind, title, body});
    logf("notify (%s): %s - %s", kind == 0 ? "ok" : kind == 1 ? "warning" : "error", title.c_str(), body.c_str());
}

// Failures that happen before (or instead of) the loader's own UI: a message box on its own thread, so the game keeps
// running behind it.
static DWORD WINAPI fail_box_thread(LPVOID p) {
    std::wstring* text = (std::wstring*)p;
    MessageBoxW(nullptr, text->c_str(), L"ZARModManager - Loader Failure", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    delete text;
    return 0;
}
static void fail_box(const char* reason) {
    logf("LOADER FAILURE: %s", reason);
    wchar_t w[1024];
    MultiByteToWideChar(CP_UTF8, 0, reason, -1, w, 1024);
    std::wstring* text = new std::wstring(L"Loader Failure\n\n");
    *text += w;
    *text += L"\n\nThe game keeps running without mods. Details: " + g_modsDir + L"\\loader.log";
    if (HANDLE t = CreateThread(nullptr, 0, fail_box_thread, text, 0, nullptr)) CloseHandle(t); else delete text;
}

// ------------------------------------------------------------------------------------------- settings --
static std::wstring settings_path(const LuaMod& m) {
    wchar_t w[260];
    MultiByteToWideChar(CP_UTF8, 0, m.name.c_str(), -1, w, 260);
    return g_modsDir + L"\\" + w + L".settings";
}
static void load_settings_file(LuaMod& m) {
    FILE* f = _wfopen(settings_path(m).c_str(), L"r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        std::string id = line, val = eq + 1;
        while (!val.empty() && (val.back() == '\n' || val.back() == '\r')) val.pop_back();
        for (ModSetting& s : m.settings)
            if (s.id == id && s.type != "info") s.num = atof(val.c_str());
    }
    fclose(f);
}
static bool save_settings_file(const LuaMod& m) {
    FILE* f = _wfopen(settings_path(m).c_str(), L"w");
    if (!f) return false;
    fprintf(f, "# %s settings (written by ZARModManager's Mod Settings screen)\n", m.title.c_str());
    for (const ModSetting& s : m.settings)
        if (s.type != "info") fprintf(f, "%s=%.6g\n", s.id.c_str(), s.num);
    fclose(f);
    return true;
}
static void push_setting_value(lua_State* L, const ModSetting& s) {
    if (s.type == "toggle") lua_pushboolean(L, s.num != 0);
    else if (s.type == "info") lua_pushstring(L, s.str.c_str());
    else if (s.type == "key") lua_pushinteger(L, (lua_Integer)s.num);
    else lua_pushnumber(L, s.num);
}
static ModSetting* find_setting(LuaMod* m, const char* id) {
    if (m) for (ModSetting& s : m->settings) if (s.id == id) return &s;
    return nullptr;
}

// za.mod{name, description, version, settings = {{id, type = 'toggle'|'slider'|'key'|'choice'|'info', label, default, min, max,
// step, lock = 'mission'}, ...}}: the mod's manifest, shown in loader notifications and on the MODS tab (a setting with
// lock = 'mission' cannot be changed while a mission is being played). Saved values (the
// .settings file next to the mod) replace the defaults straight away.
static std::string opt_field(lua_State* L, int idx, const char* k, const char* def = "") {
    lua_getfield(L, idx, k);
    std::string v = lua_isstring(L, -1) ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return v;
}
static double opt_number(lua_State* L, int idx, const char* k, double def) {
    lua_getfield(L, idx, k);
    double v = def;
    if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1) ? 1 : 0;
    else if (lua_isnumber(L, -1)) v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}
static int za_mod(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    LuaMod* m = mod_of(L);
    if (!m) return 0;
    m->manifest = true;
    m->title = opt_field(L, 1, "name", m->name.c_str());
    m->description = opt_field(L, 1, "description");
    m->version = opt_field(L, 1, "version");
    m->settings.clear();
    lua_getfield(L, 1, "settings");
    if (lua_istable(L, -1)) {
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n && i <= 32; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_istable(L, -1)) {
                int t = lua_gettop(L);
                ModSetting s;
                s.id = opt_field(L, t, "id");
                s.type = opt_field(L, t, "type", "toggle");
                s.label = opt_field(L, t, "label", s.id.c_str());
                s.min = opt_number(L, t, "min", 0);
                s.max = opt_number(L, t, "max", 1);
                s.step = opt_number(L, t, "step", 0);
                s.lock = opt_field(L, t, "lock");
                if (s.type == "choice") {  // options = {'text', ...}: the value is the chosen option's index, from 0
                    lua_getfield(L, t, "options");
                    if (lua_istable(L, -1)) {
                        int on = (int)lua_rawlen(L, -1);
                        for (int j = 1; j <= on && j <= 64; j++) {
                            lua_rawgeti(L, -1, j);
                            if (lua_isstring(L, -1)) s.options.push_back(lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);
                    if (s.options.empty()) s.options.push_back("-");
                    s.min = 0; s.max = (double)(s.options.size() - 1); s.step = 1;
                }
                if (s.type == "info") s.str = opt_field(L, t, "default", "—");
                else s.num = opt_number(L, t, "default", s.type == "slider" ? s.min : 0);
                if (!s.id.empty()) m->settings.push_back(s);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    load_settings_file(*m);
    return 0;
}
// za.setting(id) -> the saved value (toggle: boolean; slider: number; key: virtual-key code; info: text)
static int za_setting(lua_State* L) {
    ModSetting* s = find_setting(mod_of(L), luaL_checkstring(L, 1));
    if (!s) { lua_pushnil(L); return 1; }
    push_setting_value(L, *s);
    return 1;
}
// za.set_info(id, text): the text of a read-only ('info') setting, shown live on the Mod Settings screen
static int za_set_info(lua_State* L) {
    ModSetting* s = find_setting(mod_of(L), luaL_checkstring(L, 1));
    if (s && s->type == "info") s->str = luaL_checkstring(L, 2);
    return 0;
}
// za.set_setting(id, number [, save]): a mod changes one of its own settings (a 'choice', 'toggle', 'slider', 'key' or 'hidden' one).
// A 'hidden' setting has no row on the settings screen: it is a saved number only the mod itself reads and writes.
static int za_set_setting(lua_State* L) {
    ModSetting* s = find_setting(mod_of(L), luaL_checkstring(L, 1));
    if (!s || s->type == "info") { lua_pushboolean(L, false); return 1; }
    double v = luaL_checknumber(L, 2);
    if (s->type == "choice") v = std::max(0.0, std::min(s->max, std::floor(v)));
    s->num = v;
    if (lua_toboolean(L, 3)) { LuaMod* m = mod_of(L); if (m) save_settings_file(*m); }
    lua_pushboolean(L, true);
    return 1;
}
// za.on_settings(fn): called after the player saves changes on the Mod Settings screen
static int za_on_settings(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    LuaMod* m = mod_of(L);
    if (!m) return 0;
    if (m->onSettings != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, m->onSettings);
    lua_pushvalue(L, 1);
    m->onSettings = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}
// za.notify(kind, title, body): a loader notification ('ok' | 'warning' | 'error')
static int za_notify(lua_State* L) {
    std::string kind = luaL_checkstring(L, 1);
    LuaMod* m = mod_of(L);
    std::string title = luaL_checkstring(L, 2);
    if (m && !m->title.empty()) title = m->title + ": " + title;
    push_event(kind == "error" ? 2 : kind == "warning" ? 1 : 0, title, luaL_optstring(L, 3, ""));
    return 0;
}

// -------------------------------------------------------------------------------------------------- input --
// Sampled once per frame tick: key states (GetAsyncKeyState) and a press counter per key, so a script that runs
// every 100 ms still sees short taps. The game window's client area gives the mouse position.
static bool g_keyDown[256];
static uint32_t g_keyPresses[256];
static HWND g_gameWnd = nullptr;
static int g_wheel = 0;            // mouse wheel delta collected while the settings screen holds the input
static bool g_modal = false;       // the loader's settings screen is open: the game gets no keyboard / mouse input
static bool g_menuOpen = false;    // the game's pause menu is open (the system script found it)

static HWND game_window() {
    if (g_gameWnd && IsWindow(g_gameWnd)) return g_gameWnd;
    HWND w = nullptr;
    while ((w = FindWindowExW(nullptr, w, L"UnityWndClass", nullptr))) {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        if (pid == GetCurrentProcessId()) return g_gameWnd = w;
    }
    return nullptr;
}
static bool game_focused() {
    HWND w = game_window();
    return w && GetForegroundWindow() == w;
}
static void sample_input() {
    bool focus = game_focused();
    for (int vk = 1; vk < 256; vk++) {
        bool d = focus && (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (d && !g_keyDown[vk]) g_keyPresses[vk]++;
        g_keyDown[vk] = d;
    }
}
// The mouse in the loader canvas's design units (1080 high, width by aspect), from the top-left; false if outside
static bool mouse_design(float& x, float& y, float& w) {
    HWND wnd = game_window();
    RECT rc;
    POINT p;
    if (!wnd || !GetClientRect(wnd, &rc) || rc.bottom <= 0 || !GetCursorPos(&p) || !ScreenToClient(wnd, &p)) return false;
    float k = 1080.0f / (float)rc.bottom;
    x = p.x * k; y = p.y * k; w = rc.right * k;
    return p.x >= 0 && p.y >= 0 && p.x < rc.right && p.y < rc.bottom;
}
// ------------------------------------------------------------------------------------------ Pods bosses --
// In a Pods mission the boss nest releases the boss through the objective's own (native) logic, which allows exactly one; no
// spawner setting changes that. What can be changed is the game's data: entity definitions carry behaviours, among them
// SpawnEntity (spawn this entity, this many times, on this trigger), and the game builds its simulation data from them
// during the mission load. The nest's definition (Zombie_BossPod) is created during that build and passed to
// WorldLoadDataBuilder.CacheEntityDefinition just before its behaviours are cached; a hook there adds clones of the standard
// pod's SpawnEntity (which fire when the pod dies) with a boss from the mission's own boss pool as their entity. The nest's
// death then spawns the extra bosses next to the one the game releases. The template is taken from the standard pod in
// Objectives.Pods.PreDataBuild (a virtual method: its vtable slot is pointed at a hook that does this and runs the original).
// Only the host's game builds the mission, so it works alone and as the leader of a party. Nothing is changed when the
// count is 0 or 1, and if a hook cannot be placed (a different game version), the mission is left as the game made it.
typedef void (*PreBuildFn)(void*, void*, void*, void*);
typedef uintptr_t (*Args8Fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static int g_podsBosses = 0;               // bosses wanted from the nest (0, 1: the game's own)
static int g_podsHook = 0;                 // 0 not tried, 1 hooked, -1 unavailable
static PreBuildFn g_podsOrig = nullptr;
static uintptr_t g_cacheDefOrig = 0;
static bool g_cacheDefHooked = false;
static void* g_spawnTemplate = nullptr;    // the standard pod's SpawnEntity behaviour (kept alive by the pod's definition)
static bool g_bossPodInjected = false;     // once per mission

static std::vector<uint8_t*> calls_to(uintptr_t fn);
// padding runs in GameAssembly.dll for the 14-byte jumps of the hooks: each hook takes its own, one after another
static uint8_t* take_cave() {
    static std::vector<uint8_t*> caves;
    static size_t used = 0;
    if (caves.empty()) caves = calls_to(0);
    return used < caves.size() ? caves[used++] : nullptr;
}
static void* field_ptr(void* obj, const char* name) {
    void* k = plausible(obj) ? safe_object_class(obj) : nullptr;
    const FieldDesc* f = valid_class(k) ? find_field(k, name, false) : nullptr;
    return f ? safe_ptr_at(obj, f->offset) : nullptr;
}
static void* array_of(void* def, const char* listField) { return field_ptr(field_ptr(def, listField), "m_List"); }
static void* find_behaviour(void* arr, const char* cls) {
    uint32_t n = 0;
    if (!plausible(arr) || !safe_array_length(arr, &n)) return nullptr;
    for (uint32_t i = 0; i < n && i < 64; i++) {
        void* o = safe_ptr_at((const uint8_t*)arr + ARRAY_DATA, (size_t)i * 8);
        void* k = plausible(o) ? safe_object_class(o) : nullptr;
        if (valid_class(k) && strcmp(g_api.class_get_name(k), cls) == 0) return o;
    }
    return nullptr;
}
// swap a virtual method's slot in the class's vtable (the method's code address followed by its MethodInfo) for a hook
static bool hook_vslot(void* k, const char* name, int argc, uintptr_t hook, uintptr_t* orig) {
    void* m = k ? g_api.class_get_method_from_name(k, name, argc) : nullptr;
    uintptr_t fn = m ? *(uintptr_t*)m : 0;
    if (!fn) { logf("pods: %s not found: boss count unavailable", name); return false; }
    uintptr_t* slot = nullptr;
    int hits = 0;
    for (size_t off = 0; off < 0x3000; off += 8) {
        uintptr_t w[2];
        if (!safe_copy(w, (uint8_t*)k + off, sizeof w)) break;
        if (w[0] == fn && w[1] == (uintptr_t)m) { slot = (uintptr_t*)((uint8_t*)k + off); hits++; }
    }
    if (hits != 1) { logf("pods: %s has %d vtable slots (expected 1): boss count unavailable", name, hits); return false; }
    DWORD old;
    if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) { logf("pods: cannot write the vtable: boss count unavailable"); return false; }
    *orig = fn;
    *slot = hook;
    VirtualProtect(slot, 8, old, &old);
    return true;
}
static void pods_prebuild_hook(void* self, void* builder, void* rnd, void* mi) {
    g_bossPodInjected = false;   // a new mission: look again
    g_spawnTemplate = nullptr;
    if (g_podsBosses > 1 && plausible(self)) {
        void* def = field_ptr(field_ptr(self, "podsSpawner"), "entity");   // the standard pod
        g_spawnTemplate = find_behaviour(array_of(def, "untargetedBehaviours"), "SpawnEntity");
    }
    g_podsOrig(self, builder, rnd, mi);
}
// the boss nest's behaviour list, one longer per entity: a clone of the template that spawns that entity once
static bool inject_spawns(void* def, const std::vector<void*>& entities) {
    void* coll = field_ptr(def, "untargetedBehaviours");
    void* arr = field_ptr(coll, "m_List");
    uint32_t n = 0;
    if (!plausible(coll) || !plausible(arr) || !safe_array_length(arr, &n) || !g_spawnTemplate) return false;
    void* ak = safe_object_class(arr);
    void* ek = valid_class(ak) ? g_api.class_get_element_class(ak) : nullptr;
    void* tk = safe_object_class(g_spawnTemplate);
    if (!valid_class(ek) || !valid_class(tk) || entities.empty()) return false;
    const FieldDesc* fc = find_field(tk, "count", false);
    const FieldDesc* fe = find_field(tk, "entity", false);
    const FieldDesc* fi = find_field(tk, "instanceId", false);
    if (!fc || !fe || !fi || !fi->klass) return false;
    const FieldDesc* fv = find_field(fi->klass, "value", false);
    int size = g_api.class_instance_size(tk);
    if (!fv || size <= (int)OBJECT_HEADER) return false;
    void* na = g_api.array_new(ek, (uintptr_t)n + entities.size());
    if (!na) return false;
    if (n && !safe_copy((uint8_t*)na + ARRAY_DATA, (uint8_t*)arr + ARRAY_DATA, (size_t)n * 8)) return false;
    static uint32_t nextId = 900000;   // instance ids the game does not use
    for (size_t i = 0; i < entities.size(); i++) {
        void* clone = g_api.object_new(tk);
        if (!clone || !safe_copy((uint8_t*)clone + OBJECT_HEADER, (uint8_t*)g_spawnTemplate + OBJECT_HEADER, size - OBJECT_HEADER)) return false;
        uint32_t one = 1, id = nextId++;
        void* ent = entities[i];
        safe_copy((uint8_t*)clone + fc->offset, &one, 4);
        safe_copy((uint8_t*)clone + fe->offset, &ent, 8);
        safe_copy((uint8_t*)clone + fi->offset + fv->offset - OBJECT_HEADER, &id, 4);
        safe_copy((uint8_t*)na + ARRAY_DATA + ((size_t)n + i) * 8, &clone, 8);
    }
    void* ck = safe_object_class(coll);
    const FieldDesc* fl = valid_class(ck) ? find_field(ck, "m_List", false) : nullptr;
    return fl && safe_copy((uint8_t*)coll + fl->offset, &na, 8);
}
// the bosses this mission can have: the built (enhanced) entity of each of the builder's boss stage sets
static std::vector<void*> boss_candidates(void* builder) {
    std::vector<void*> out;
    void* list = field_ptr(builder, "bossStageSetDefinitions");
    void* items = field_ptr(list, "_items");
    void* lk = plausible(list) ? safe_object_class(list) : nullptr;
    const FieldDesc* fs = valid_class(lk) ? find_field(lk, "_size", false) : nullptr;
    int32_t size = 0;
    uint32_t cap = 0;
    if (!fs || !plausible(items) || !safe_array_length(items, &cap)) return out;
    safe_copy(&size, (uint8_t*)list + fs->offset, 4);
    for (int32_t i = 0; i < size && (uint32_t)i < cap && i < 32; i++) {
        void* set = safe_ptr_at((const uint8_t*)items + ARRAY_DATA, (size_t)i * 8);
        void* e = field_ptr(set, "enhancedBuiltEntity");
        if (plausible(e)) out.push_back(e);
    }
    return out;
}
static uintptr_t cache_def_hook(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f, uintptr_t g, uintptr_t h) {
    void* def = (void*)b;
    if (g_podsBosses > 1 && !g_bossPodInjected && g_spawnTemplate && plausible(def)) {
        void* k = safe_object_class(def);
        if (valid_class(k) && strcmp(g_api.class_get_name(k), "EntityDefinitionSO") == 0 &&
            object_name(def).find("Zombie_BossPod") != std::string::npos) {
            g_bossPodInjected = true;
            std::vector<void*> pool = boss_candidates((void*)a), picks;
            size_t extra = (size_t)g_podsBosses - 1;
            for (size_t i = 0, start = GetTickCount() % (pool.empty() ? 1 : pool.size()); i < extra && !pool.empty(); i++)
                picks.push_back(pool[(start + i) % pool.size()]);
            if (!picks.empty() && inject_spawns(def, picks)) logf("pods: the boss nest releases %zu extra boss(es)", picks.size());
            else logf("pods: could not add the extra bosses to the boss nest");
        }
    }
    return ((Args8Fn)g_cacheDefOrig)(a, b, c, d, e, f, g, h);
}
static bool cache_def_hook_install() {
    if (g_cacheDefHooked) return true;
    void* k = find_game_class("WorldLoadDataBuilder");
    void* m = k ? g_api.class_get_method_from_name(k, "CacheEntityDefinition", 2) : nullptr;
    uintptr_t fn = m ? *(uintptr_t*)m : 0;
    if (!fn) { logf("pods: WorldLoadDataBuilder.CacheEntityDefinition not found: boss count unavailable"); return false; }
    std::vector<uint8_t*> sites = calls_to(fn);
    uint8_t* cave = take_cave();
    if (sites.empty() || !cave) { logf("pods: CacheEntityDefinition has %zu call sites, cave %p: boss count unavailable", sites.size(), cave); return false; }
    uint8_t jump[14] = {0xFF, 0x25, 0, 0, 0, 0};
    void* hook = (void*)&cache_def_hook;
    memcpy(jump + 6, &hook, 8);
    DWORD old;
    if (!VirtualProtect(cave, sizeof jump, PAGE_EXECUTE_READWRITE, &old)) return false;
    g_cacheDefOrig = fn;
    memcpy(cave, jump, sizeof jump);
    VirtualProtect(cave, sizeof jump, old, &old);
    for (uint8_t* site : sites) {
        int32_t rel = (int32_t)((intptr_t)cave - (intptr_t)(site + 5));
        if (!VirtualProtect(site + 1, 4, PAGE_EXECUTE_READWRITE, &old)) continue;
        *(int32_t*)(site + 1) = rel;
        VirtualProtect(site + 1, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    FlushInstructionCache(GetCurrentProcess(), cave, sizeof jump);
    g_cacheDefHooked = true;
    return true;
}
static bool pods_hook() {
    if (g_podsHook) return g_podsHook > 0;
    g_podsHook = -1;
    void* k = nullptr;
    for (void* c : g_gameClasses)
        if (strcmp(g_api.class_get_name(c), "Pods") == 0 && strcmp(g_api.class_get_namespace(c), "Objectives") == 0) { k = c; break; }
    uintptr_t o = 0;
    if (!hook_vslot(k, "PreDataBuild", 2, (uintptr_t)&pods_prebuild_hook, &o)) return false;
    g_podsOrig = (PreBuildFn)o;
    if (!cache_def_hook_install()) return false;   // (the vtable hook stays and only runs the original)
    g_podsHook = 1;
    logf("pods: boss nest hooks in place");
    return true;
}
// za.pods_bosses(n) -> true if the hooks are in place: the boss nest of the next Pods mission loaded releases n bosses in all
// (the game's own and n - 1 more); 0 or 1 leaves it as the game made it
static int za_pods_bosses(lua_State* L) {
    lua_Integer n = luaL_checkinteger(L, 1);
    g_podsBosses = n < 0 ? 0 : n > 8 ? 8 : (int)n;
    lua_pushboolean(L, g_podsBosses <= 1 ? g_podsHook > 0 : pods_hook());
    return 1;
}

// ------------------------------------------------------------------------------------------- hidden names --
// za.hud.hide_names({name, ...}, mask) -> true if the hook is in place: while a list is set, every text the game puts into a
// TextMeshPro label (TMP_Text.text) has those names (whole words, any case) replaced by the mask before the label gets it,
// so a name never shows, wherever the game writes it and however often it rewrites it (a plain replacement afterwards is
// put back by the game). An empty or missing list switches it off. Labels written some other way, or before the list was
// set, are not covered: a mod replaces those itself (za.hud.find_texts / set_text).
static std::vector<std::wstring> g_hideNames;
static std::wstring g_hideMask;
static int g_textHooked = 0;   // vtable slots swapped
static std::wstring wide_of(const char* u) {
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, u, -1, &w[0], n);
    return w;
}
static std::wstring lower_of(std::wstring s) { for (wchar_t& c : s) c = (wchar_t)towlower(c); return s; }
static bool replace_whole_word(std::wstring& s, const std::wstring& name, const std::wstring& mask) {
    std::wstring ls = lower_of(s), ln = lower_of(name), lm = lower_of(mask);
    bool changed = false;
    size_t from = 0, pos;
    while ((pos = ls.find(ln, from)) != std::wstring::npos) {
        bool before = pos == 0 || !iswalnum(ls[pos - 1]);
        bool after = pos + ln.size() >= ls.size() || !iswalnum(ls[pos + ln.size()]);
        if (before && after) {
            s.replace(pos, ln.size(), mask);
            ls.replace(pos, ln.size(), lm);
            from = pos + mask.size();
            changed = true;
        } else from = pos + 1;
    }
    return changed;
}
static void* mask_text(void* str) {   // the string to give the label: the same one, or a copy with the names replaced
    if (g_hideNames.empty() || !str) return str;
    wchar_t w[1536];
    int n = safe_string(str, w, 1536);
    if (n <= 0 || n >= 1535) return str;
    std::wstring s(w, (size_t)n);
    bool changed = false;
    for (const std::wstring& nm : g_hideNames) changed |= replace_whole_word(s, nm, g_hideMask);
    if (!changed) return str;
    char u[4096];
    if (WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, u, sizeof u, nullptr, nullptr) <= 0) return str;
    void* ns = g_api.string_new(u);
    return ns ? ns : str;
}
static const int kTextVariants = 6;      // different setters (the plain one and each label class's override) that can be hooked
static uintptr_t g_textOrigs[kTextVariants];
template <int I> static void text_set_hook(void* self, void* str, void* mi) {
    ((void (*)(void*, void*, void*))g_textOrigs[I])(self, mask_text(str), mi);
}
static void* const g_textHooks[kTextVariants] = {(void*)&text_set_hook<0>, (void*)&text_set_hook<1>, (void*)&text_set_hook<2>,
                                                 (void*)&text_set_hook<3>, (void*)&text_set_hook<4>, (void*)&text_set_hook<5>};
// swap the text setter in the vtable of every label class (TMP_Text.text is virtual: the slot holds the method's code address
// followed by its MethodInfo). The game's own label class overrides it, so a class's setter is whichever it has: up to two
// different ones are hooked (the plain TextMeshPro one and the override).
static void install_text_hook() {
    if (g_textHooked) return;
    void* tmp = image_named("Unity.TextMeshPro.dll");
    void* base = tmp ? g_api.class_from_name(tmp, "TMPro", "TMP_Text") : nullptr;
    void* m = base ? g_api.class_get_method_from_name(base, "set_text", 1) : nullptr;
    uintptr_t fn = m ? *(uintptr_t*)m : 0;
    if (!fn) { logf("names: TMP_Text.set_text not found: hidden names unavailable"); return; }
    std::vector<void*> classes;
    size_t na = 0;
    void** asms = g_api.domain_get_assemblies(g_api.domain_get(), &na);
    for (size_t a = 0; a < na; a++) {
        void* img = g_api.assembly_get_image(asms[a]);
        size_t nc = img ? g_api.image_get_class_count(img) : 0;
        for (size_t i = 0; i < nc; i++) {
            void* k = g_api.image_get_class(img, i);
            for (void* c = k; c && valid_class(c); c = g_api.class_get_parent(c))
                if (strcmp(g_api.class_get_name(c), "TMP_Text") == 0) { classes.push_back(k); break; }
        }
    }
    for (void* k : classes) {
        void* mk = g_api.class_get_method_from_name(k, "set_text", 1);
        uintptr_t f = mk ? *(uintptr_t*)mk : 0;
        if (!f) continue;
        int which = -1;
        for (int v = 0; v < kTextVariants && which < 0; v++) if (g_textOrigs[v] == f || !g_textOrigs[v]) which = v;
        if (which < 0) { logf("names: %s has one setter too many: not hooked", g_api.class_get_name(k)); continue; }
        for (size_t off = 0; off < 0x3000; off += 8) {
            uintptr_t w[2];
            if (!safe_copy(w, (uint8_t*)k + off, sizeof w)) break;
            if (w[0] != f || w[1] != (uintptr_t)mk) continue;
            DWORD old;
            uint8_t* slot = (uint8_t*)k + off;
            if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) continue;
            g_textOrigs[which] = f;
            *(uintptr_t*)slot = (uintptr_t)g_textHooks[which];
            VirtualProtect(slot, 8, old, &old);
            g_textHooked++;
        }
    }
    logf("names: %d text setter slots hooked in %zu label classes", g_textHooked, classes.size());
}
static int za_hud_hide_names(lua_State* L) {
    std::vector<std::wstring> names;
    if (lua_istable(L, 1)) {
        int n = (int)lua_rawlen(L, 1);
        for (int i = 1; i <= n && i <= 4; i++) {
            lua_rawgeti(L, 1, i);
            if (lua_isstring(L, -1)) { std::wstring w = wide_of(lua_tostring(L, -1)); if (w.size() >= 3) names.push_back(w); }
            lua_pop(L, 1);
        }
    }
    g_hideMask = wide_of(luaL_optstring(L, 2, "Player"));
    g_hideNames = names;
    if (!names.empty() && g_inMainThreadTick) install_text_hook();
    lua_pushboolean(L, g_textHooked > 0);
    return 1;
}

// ------------------------------------------------------------------------------------------------- hover --
// za.hover(types, x, z [, radius]) -> id, type, distance | nil: the entity of one of those types (a table {Enemy = true, ...})
// nearest the world point (x, z) (the ground plane; y is up), within radius metres (1.2 by default; bigger and tougher ones
// count from further away). Read-only: it only reads entity positions.
// An entity's position, read through its Transform inside a structured-exception guard: a bad call is caught here (and logged
// once) instead of reaching the loader's fault handler, which pauses every mod. World position first, local position if that
// faults; once both have faulted, no more tries.
static int g_posMode = 0;   // 0 untried, 1 world position works, 2 local position works, -1 neither
static void* g_trGetLocalPos = nullptr;
static bool guarded_get(void* method, void* tr, Vec3* out) {
    __try {
        void* boxed = call(method, tr, {});
        return unbox_into(boxed, out, sizeof *out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static bool entity_position(void* tr, Vec3* out) {
    if (g_posMode == -1) return false;
    if (!g_trGetLocalPos) {
        void* tk = safe_object_class(tr);
        g_trGetLocalPos = valid_class(tk) ? g_api.class_get_method_from_name(tk, "get_localPosition", 0) : nullptr;
    }
    if (g_posMode == 0 || g_posMode == 1) {
        if (guarded_get(U.trGetPos, tr, out)) { if (g_posMode == 0) { g_posMode = 1; logf("hover: entity positions from Transform.get_position"); } return true; }
        if (g_posMode == 0) logf("hover: Transform.get_position faulted: trying get_localPosition");
        g_posMode = g_trGetLocalPos ? 2 : -1;
    }
    if (g_posMode == 2) {
        if (guarded_get(g_trGetLocalPos, tr, out)) return true;
        logf("hover: get_localPosition faulted too: entity positions unavailable");
        g_posMode = -1;
    }
    return false;
}
static int g_hoverStage = 0;
static uint32_t g_hoverId = 0;
static DWORD g_exCode = 0;
static uintptr_t g_exAddr = 0;
static int hover_filter(EXCEPTION_POINTERS* ep) {
    g_exCode = ep->ExceptionRecord->ExceptionCode;
    g_exAddr = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;
}
static int hover_impl(lua_State* L) {
    g_hoverStage = 1;
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!g_inMainThreadTick || !g_inMission || !U.ok) return 0;
    float wx = (float)luaL_checknumber(L, 2), wz = (float)luaL_checknumber(L, 3);
    float base = (float)luaL_optnumber(L, 4, 1.2);
    const EntityInfo* best = nullptr;
    float bestScore = 1e9f, bestDist = 0;
    for (const EntityInfo& e : g_entities) {
        if (e.dead || !e.hasHp || e.hp[1] <= 0 || e.hp[0] <= 0) continue;
        g_hoverId = e.id; g_hoverStage = 2;
        lua_getfield(L, 1, type_name_of(e.type).c_str());
        bool wanted = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (!wanted) continue;
        g_hoverStage = 3;
        void* tr = safe_ptr_at(e.display, g_layout.transform->offset);
        if (!alive(tr)) continue;
        g_hoverStage = 4;
        Vec3 p{};
        if (!entity_position(tr, &p)) { if (g_posMode == -1) break; continue; }
        float dx = p.x - wx, dz = p.z - wz;
        float d = sqrtf(dx * dx + dz * dz);
        float grow = 1.0f + 0.6f * log10f(e.hp[1] / 190.0f > 1.0f ? e.hp[1] / 190.0f : 1.0f);
        float r = base * (grow > 2.5f ? 2.5f : grow);
        if (d <= r && d / r < bestScore) { best = &e; bestScore = d / r; bestDist = d; }
    }
    g_hoverStage = 5;
    if (!best) return 0;
    lua_pushinteger(L, best->id);
    lua_pushstring(L, type_name_of(best->type).c_str());
    lua_pushnumber(L, bestDist);
    return 3;
}
static int za_hover(lua_State* L) {
    int top = lua_gettop(L);
    __try {
        return hover_impl(L);
    } __except (hover_filter(GetExceptionInformation())) {
        static int logged = 0;
        if (logged++ < 4) logf("hover: faulted (code %lx at %p) in stage %d, entity %u", g_exCode, (void*)g_exAddr, g_hoverStage, g_hoverId);
        lua_settop(L, top);
        return 0;
    }
}
// za.entity(id) -> {id, type, dead, has_health, hp, max_hp, shield, shield_max, ref} | nil: one entity of the mission (ref: its
// display, for ref:name() and the like)
static int entity_impl(lua_State* L) {
    g_hoverStage = 10;
    uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
    for (const EntityInfo& e : g_entities) {
        if (e.id != id) continue;
        lua_newtable(L);
        lua_pushinteger(L, e.id); lua_setfield(L, -2, "id");
        lua_pushstring(L, type_name_of(e.type).c_str()); lua_setfield(L, -2, "type");
        lua_pushboolean(L, e.dead); lua_setfield(L, -2, "dead");
        lua_pushboolean(L, e.hasHp); lua_setfield(L, -2, "has_health");
        lua_pushnumber(L, e.hp[0]); lua_setfield(L, -2, "hp");
        lua_pushnumber(L, e.hp[1]); lua_setfield(L, -2, "max_hp");
        lua_pushnumber(L, e.shield[0]); lua_setfield(L, -2, "shield");
        lua_pushnumber(L, e.shield[1]); lua_setfield(L, -2, "shield_max");
        g_hoverStage = 11;
        void* tr = safe_ptr_at(e.display, g_layout.transform->offset);
        g_hoverStage = 12;
        std::string nm = alive(tr) ? object_name(tr) : "?";   // the transform's name is the entity object's name
        g_hoverStage = 13;
        lua_pushstring(L, nm.c_str()); lua_setfield(L, -2, "name");
        return 1;
    }
    return 0;
}
static int za_entity(lua_State* L) {
    int top = lua_gettop(L);
    __try {
        return entity_impl(L);
    } __except (hover_filter(GetExceptionInformation())) {
        static int logged = 0;
        if (logged++ < 4) logf("entity: faulted (code %lx at %p) in stage %d", g_exCode, (void*)g_exAddr, g_hoverStage);
        lua_settop(L, top);
        return 0;
    }
}

static int za_input_down(lua_State* L) { int vk = (int)luaL_checkinteger(L, 1) & 255; lua_pushboolean(L, g_keyDown[vk]); return 1; }
static int za_input_presses(lua_State* L) { int vk = (int)luaL_checkinteger(L, 1) & 255; lua_pushinteger(L, g_keyPresses[vk]); return 1; }
static int za_input_focused(lua_State* L) { lua_pushboolean(L, game_focused()); return 1; }
// za.input.mouse() -> x, y (design units from the top-left of a 1920 x 1080 screen), inside the game window
static int za_input_mouse(lua_State* L) {
    float x = -1, y = -1, w = 1920;
    bool in = mouse_design(x, y, w);
    lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushboolean(L, in);
    return 3;
}
static int za_input_key_name(lua_State* L) {
    int vk = (int)luaL_checkinteger(L, 1) & 255;
    static const struct { int vk; const char* name; } named[] = {
        {VK_LBUTTON, "Mouse 1"}, {VK_RBUTTON, "Mouse 2"}, {VK_MBUTTON, "Mouse 3"}, {VK_XBUTTON1, "Mouse 4"}, {VK_XBUTTON2, "Mouse 5"},
        {VK_OEM_3, "` (Grave)"}, {VK_SPACE, "Space"}, {VK_TAB, "Tab"}, {VK_CAPITAL, "Caps Lock"}, {VK_LSHIFT, "Left Shift"},
        {VK_RSHIFT, "Right Shift"}, {VK_LCONTROL, "Left Ctrl"}, {VK_RCONTROL, "Right Ctrl"}, {VK_LMENU, "Left Alt"},
        {VK_RMENU, "Right Alt"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"}, {VK_END, "End"},
        {VK_PRIOR, "Page Up"}, {VK_NEXT, "Page Down"}};
    for (auto& n : named) if (n.vk == vk) { lua_pushstring(L, n.name); return 1; }
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t w[64] = {};
    char u[128] = {};
    if (sc && GetKeyNameTextW((LONG)(sc << 16), w, 64) > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr);
    else snprintf(u, sizeof u, "Key %d", vk);
    lua_pushstring(L, u);
    return 1;
}

// ----------------------------------------------------------------------------------------------- auto-fire --
// za.input.fire(mode): 'hold' keeps the left mouse button down, 'tap' clicks it repeatedly, 'off' lets go. The mod
// must keep asking (every frame callback): if it stops for 300 ms, or the game window loses focus, the pause menu or
// the settings screen opens, or the cursor leaves the window, the button is released. Clicks go where the cursor is.
static int g_fireMode = 0;         // 0 off, 1 hold, 2 tap
// The game's own trigger: CharacterInputController.SetWeaponTriggerState(CharacterTriggerState), called every frame
// tick (Unselected 0, Down 1, Held 2). No mouse events, no focus needed.
static void* g_fireCtrl = nullptr;  // GC handle of the controller
static void* g_fireSetTrigger = nullptr;
static int g_trigState = 0;
static double g_fireAsked = -1e9, g_fireFlip = 0, g_fireDwell = 45;   // (tap: ms the trigger is held down, and up)
static bool g_fireDown = false;
static void send_left(bool down) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof in);
    g_fireDown = down;
}
// While firing, the cursor is held inside the game window (ClipCursor), so in windowed mode it cannot drift out and
// send clicks elsewhere; it is let go as soon as firing stops. Clicks are never sent while another window is in front.
static bool g_clipped = false;
static void clip_to_game(bool on) {
    HWND w = game_window();
    RECT rc;
    POINT tl{0, 0}, br{0, 0};
    if (on && w && GetClientRect(w, &rc)) {
        br.x = rc.right; br.y = rc.bottom;
        ClientToScreen(w, &tl); ClientToScreen(w, &br);
        RECT clip{tl.x, tl.y, br.x, br.y};
        ClipCursor(&clip);  // re-applied every tick: Windows drops the clip on focus changes
        g_clipped = true;
    } else if (!on && g_clipped) {
        ClipCursor(nullptr);
        g_clipped = false;
    }
}
static void set_trigger(int state) {
    void* ctrl = g_fireCtrl ? target(g_fireCtrl) : nullptr;
    if (!ctrl || !g_fireSetTrigger) return;
    uint8_t s = (uint8_t)state;
    bool ok = false;
    call(g_fireSetTrigger, ctrl, {&s}, &ok);
    g_trigState = ok ? state : 0;
}
static void tick_fire(double t) {
    if (g_fireCtrl) {  // the game's trigger
        clip_to_game(false);
        bool can = g_fireMode != 0 && t - g_fireAsked < 300 && !g_modal && !g_menuOpen;
        if (!can) { if (g_trigState) set_trigger(0); return; }
        if (g_fireMode == 1) set_trigger(g_trigState == 0 ? 1 : 2);  // Down, then Held
        else if (t >= g_fireFlip) { set_trigger(g_trigState ? 0 : 1); g_fireFlip = t + g_fireDwell; }
        else if (g_trigState) set_trigger(1);  // keep the press asserted until the release
        return;
    }
    bool can = g_fireMode != 0 && t - g_fireAsked < 300 && game_focused() && !g_modal && !g_menuOpen;
    clip_to_game(can);
    if (!can) { if (g_fireDown) send_left(false); return; }
    if (g_fireMode == 1) { if (!g_fireDown) send_left(true); return; }
    if (t >= g_fireFlip) { send_left(!g_fireDown); g_fireFlip = t + 45; }  // tap: ~11 clicks a second
}
// za.input.fire(mode [, controller]): with the game's CharacterInputController, fire through its own trigger; without,
// through mouse clicks
static int za_input_fire(lua_State* L) {
    std::string mode = luaL_checkstring(L, 1);
    ZRef* ctrl = (ZRef*)luaL_testudata(L, 2, ZREF);
    void* want = (ctrl && !ctrl->isStruct) ? ctrl->p : nullptr;
    if (want && (!g_fireCtrl || target(g_fireCtrl) != want)) {
        void* m = g_api.class_get_method_from_name(ctrl->k, "SetWeaponTriggerState", 1);
        if (m) {
            if (g_trigState) set_trigger(0);
            if (g_fireCtrl) g_api.gchandle_free(g_fireCtrl);
            g_fireCtrl = g_api.gchandle_new(want, false);
            g_fireSetTrigger = m;
            g_trigState = 0;
            logf("input: firing through the game's own trigger (%s.SetWeaponTriggerState)", g_api.class_get_name(ctrl->k));
        }
    } else if (!want && g_fireCtrl && mode != "off") {
        if (g_trigState) set_trigger(0);
        g_api.gchandle_free(g_fireCtrl);
        g_fireCtrl = nullptr;
        logf("input: firing through mouse clicks");
    }
    if (lua_isnumber(L, 3)) g_fireDwell = std::max(1.0, std::min(200.0, lua_tonumber(L, 3)));
    g_fireMode = mode == "hold" ? 1 : mode == "tap" ? 2 : 0;
    g_fireAsked = now_ms();
    if (g_fireMode == 0 && g_fireDown) send_left(false);
    if (g_fireMode == 0 && g_trigState) set_trigger(0);
    return 0;
}

// za.input.ability(controller, slot) -> true if queued: presses ability slot 1..3 once, the way its key would.
// The game rebuilds its input record every frame in CharacterInputController.UpdateInput (each ability slot follows its
// key: no key held means "no ability slot"), then sends it. A press written earlier in the frame is erased before it is
// sent, so the press is written right after UpdateInput: its one call site (in the game's update, found by scanning
// GameAssembly.dll for calls to it) is pointed at za_ability_stub (forward.asm), which calls UpdateInput and then
// after_input_update(). The call reaches the stub through a 14-byte jump written into padding between two of the
// game's functions (a call's rel32 only reaches 2 GB; no memory is allocated for code).
// The press is Down, then Held for 120 ms; after that the game's own update sees no key held and releases it. The
// record carries one ability slot per frame, so presses go one at a time, and never over a slot the player is holding.
static void* g_abilityCtrl = nullptr;     // GC handle
static int g_abilityReady = 0;            // 0 not tried, 1 hooked, -1 unavailable
static size_t g_abilityIdxAt = 0, g_abilityStateAt = 0;  // controller offsets of current_ability_slot_idx / _button_state
static int g_abilityStep[3] = {};         // per slot: 0 idle, 1 queued, 2 holding
static double g_abilityUntil[3] = {};     // per slot: when the hold ends
static double g_abilityQueued[3] = {};    // per slot: when it was queued (presses go first come, first served)
static int g_abilityLast = -1;            // the slot last pressed, until its release is checked
static double g_abilityReleased = 0;      // when that press was released
extern "C" void za_ability_stub();
extern "C" uintptr_t za_update_input = 0;  // UpdateInput's code, called by the stub

// Called by the stub on the game's main thread, right after UpdateInput built the frame's input record.
static void after_input_update(void* ctrl) {
    if (!g_abilityCtrl || ctrl != target(g_abilityCtrl)) return;
    uint8_t* idx = (uint8_t*)ctrl + g_abilityIdxAt;
    uint8_t* state = (uint8_t*)ctrl + g_abilityStateAt;
    double t = now_ms();
    // A press the game did not take can leave its slot selected (as if still aiming it) after the release, and while
    // a slot is selected no other can be pressed. Shortly after our release, our slot still selected is cancelled the
    // way the game's own cancel does it (Cancelled, no slot). Only our own press: a slot the player uses is left alone.
    if (g_abilityLast >= 0 && t - g_abilityReleased >= 150) {
        if (*idx == (uint8_t)g_abilityLast) { *state = 4; *idx = 0xFF; }
        g_abilityLast = -1;
    }
    int busy = -1;  // the slot being held; else the queued slot that has waited longest
    for (int i = 0; i < 3; i++) if (g_abilityStep[i] == 2) busy = i;
    if (busy < 0)
        for (int i = 0; i < 3; i++)
            if (g_abilityStep[i] == 1 && (busy < 0 || g_abilityQueued[i] < g_abilityQueued[busy])) busy = i;
    if (busy < 0) return;
    if (g_modal || g_menuOpen) { g_abilityStep[busy] = 0; return; }
    if (*idx != 0xFF && *idx != busy) return;  // the player is using another slot: wait
    if (g_abilityStep[busy] == 1) {
        *idx = (uint8_t)busy; *state = 1;  // Down
        g_abilityStep[busy] = 2; g_abilityUntil[busy] = t + 120;
    } else if (t < g_abilityUntil[busy]) {
        *idx = (uint8_t)busy; *state = 2;  // Held
    } else {
        g_abilityStep[busy] = 0;  // released by the game's next update (no key held)
        g_abilityLast = busy; g_abilityReleased = t;
    }
}

extern "C" void za_after_input_update(void* ctrl) { after_input_update(ctrl); }

// Every call (E8 rel32) to `fn` in GameAssembly.dll's executable sections; with fn 0, instead the first run of 16
// int3 bytes (padding between functions, never executed: the game's functions are 16-byte aligned, so runs of up to
// 16 are common and longer ones rare).
static std::vector<uint8_t*> calls_to(uintptr_t fn) {
    std::vector<uint8_t*> out;
    uint8_t* base = (uint8_t*)GetModuleHandleW(L"GameAssembly.dll");
    if (!base) return out;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (int s = 0; s < nt->FileHeader.NumberOfSections; s++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* p = base + sec->VirtualAddress;
        size_t n = sec->Misc.VirtualSize;
        if (!fn) {
            for (size_t i = 0, run = 0; i < n; i++)
                if (p[i] != 0xCC) run = 0;
                else if (++run == 16) { out.push_back(p + i - 15 + 1); run = 0; if (out.size() >= 8) return out; }  // one int3 left on each side
            continue;
        }
        for (size_t i = 0; i + 5 <= n; i++)
            if (p[i] == 0xE8 && (uintptr_t)(p + i + 5 + *(int32_t*)(p + i + 1)) == fn) out.push_back(p + i);
    }
    return out;
}

static bool ability_hook(void* klass) {
    if (g_abilityReady) return g_abilityReady > 0;
    g_abilityReady = -1;
    // where the ability part of the input record lives
    size_t ci = 0;
    void* ciClass = nullptr;
    for (const FieldDesc& d : fields_of(klass, false))
        if (d.name == "characterInput") { ci = d.offset; ciClass = d.klass; }
    size_t idxAt = 0, stateAt = 0;
    if (ciClass)
        for (const FieldDesc& d : fields_of(ciClass, false)) {
            if (d.name == "current_ability_slot_idx") idxAt = ci + d.offset - OBJECT_HEADER;
            if (d.name == "current_ability_slot_button_state") stateAt = ci + d.offset - OBJECT_HEADER;
        }
    void* m = g_api.class_get_method_from_name(klass, "UpdateInput", 4);
    uintptr_t fn = m ? *(uintptr_t*)m : 0;  // MethodInfo.methodPointer
    if (!idxAt || !stateAt || !fn) { logf("input: ability input record or UpdateInput not found: ability triggering unavailable"); return false; }
    std::vector<uint8_t*> sites = calls_to(fn);
    if (sites.size() != 1) { logf("input: UpdateInput has %zu call sites (expected 1): ability triggering unavailable", sites.size()); return false; }
    uint8_t* site = sites[0];
    uint8_t* cave = take_cave();
    if (!cave) { logf("input: no padding in GameAssembly.dll for the jump: ability triggering unavailable"); return false; }
    uint8_t jump[14] = {0xFF, 0x25, 0, 0, 0, 0};  // jmp [rip+0]; dq za_ability_stub
    void* stub = (void*)&za_ability_stub;
    memcpy(jump + 6, &stub, 8);
    za_update_input = fn;
    int32_t rel = (int32_t)((intptr_t)cave - (intptr_t)(site + 5));
    DWORD old;
    if (!VirtualProtect(cave, sizeof jump, PAGE_EXECUTE_READWRITE, &old)) { logf("input: cannot write the jump: ability triggering unavailable"); return false; }
    memcpy(cave, jump, sizeof jump);
    VirtualProtect(cave, sizeof jump, old, &old);
    if (!VirtualProtect(site + 1, 4, PAGE_EXECUTE_READWRITE, &old)) { logf("input: cannot patch UpdateInput's call site"); return false; }
    *(int32_t*)(site + 1) = rel;  // main thread: the call site cannot be running now
    VirtualProtect(site + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), cave, sizeof jump);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_abilityIdxAt = idxAt; g_abilityStateAt = stateAt;
    g_abilityReady = 1;
    logf("input: abilities pressed after the game's input update (UpdateInput call site at GameAssembly+0x%zx)",
         (size_t)(site - (uint8_t*)GetModuleHandleW(L"GameAssembly.dll")));
    return true;
}
static void tick_abilities(double t) {  // queued presses are dropped while a menu is open, or after waiting 1 s
    for (int i = 0; i < 3; i++)
        if (g_abilityStep[i] == 1 && (g_modal || g_menuOpen || t - g_abilityQueued[i] > 1000)) g_abilityStep[i] = 0;
}
static int za_input_ability(lua_State* L) {
    ZRef* ctrl = (ZRef*)luaL_checkudata(L, 1, ZREF);
    int slot = (int)luaL_checkinteger(L, 2) - 1;
    if (slot < 0 || slot > 2 || ctrl->isStruct || !plausible(ctrl->p) || !ability_hook(ctrl->k)) { lua_pushboolean(L, false); return 1; }
    if (!g_abilityCtrl || target(g_abilityCtrl) != ctrl->p) {
        if (g_abilityCtrl) g_api.gchandle_free(g_abilityCtrl);
        g_abilityCtrl = g_api.gchandle_new(ctrl->p, false);
    }
    if (g_abilityStep[slot]) { lua_pushboolean(L, false); return 1; }
    g_abilityStep[slot] = 1;
    g_abilityQueued[slot] = now_ms();
    lua_pushboolean(L, true);
    return 1;
}

// za.input.ability_state(controller, slot) -> state, charges | nil: the game's own state of ability slot 1..3
// (NativeBuildInterop.AbilitySlotState: "Ready", "Active", "InCooldown", "Charging", "OutOfUses", "Disabled",
// "FailsTriggerConditions"), as the simulation has it. The controller's controlled entity (a native GameEntity) keeps
// its activated ability slots in activated_abilities_: a start pointer, an end pointer and a capacity, with the
// AbilitySlot structs back to back between start and end (the game's own accessors read it this way: activated_
// abilities(i) returns the start, the length is (end - start) / the struct's size). Offsets and sizes come from the
// game's own type data; anything unexpected (logged once) gives nil, and the caller falls back to reading the HUD.
static int g_slotReady = 0;            // 0 not tried, 1 usable, -1 unusable
static size_t g_ctrlEntityAt = 0, g_entSlotsAt = 0, g_slotSize = 0, g_slotStateAt = 0, g_slotCountAt = 0;
static std::unordered_map<int64_t, std::string> g_slotStates;
static void* full_class(const char* fullName) {
    for (void* k : g_gameClasses) if (class_full_name(k) == fullName) return k;
    return nullptr;
}
static bool slot_state_ready(void* ctrlClass) {
    if (g_slotReady) return g_slotReady > 0;
    g_slotReady = -1;
    void* ent = full_class("NativeBuildInterop.GameEntity");
    void* slot = full_class("NativeBuildInterop.AbilitySlot");
    void* stateEnum = full_class("NativeBuildInterop.AbilitySlotState");
    for (const FieldDesc& d : fields_of(ctrlClass, false)) if (d.name == "controlledEntity") g_ctrlEntityAt = d.offset;
    if (ent) for (const FieldDesc& d : fields_of(ent, false)) if (d.name == "activated_abilities_") g_entSlotsAt = d.offset - OBJECT_HEADER;
    if (slot) {
        g_slotSize = (size_t)g_api.class_instance_size(slot) - OBJECT_HEADER;
        for (const FieldDesc& d : fields_of(slot, false)) {
            if (d.name == "state") g_slotStateAt = d.offset - OBJECT_HEADER;
            if (d.name == "remaining_count") g_slotCountAt = d.offset - OBJECT_HEADER;
        }
    }
    g_slotStates = enum_names(stateEnum);
    bool ok = g_ctrlEntityAt && g_entSlotsAt && g_slotSize >= 16 && g_slotStateAt && g_slotCountAt && !g_slotStates.empty();
    logf("input: ability state from the simulation (GameEntity.activated_abilities_, %zu-byte slots): %s",
         g_slotSize, ok ? "usable" : "not usable");
    if (ok) g_slotReady = 1;
    return ok;
}
static int za_input_ability_state(lua_State* L) {
    ZRef* ctrl = (ZRef*)luaL_checkudata(L, 1, ZREF);
    int i = (int)luaL_checkinteger(L, 2) - 1;
    if (i < 0 || i > 2 || ctrl->isStruct || !plausible(ctrl->p) || !slot_state_ready(ctrl->k)) return 0;
    void* ent = safe_ptr_at(ctrl->p, g_ctrlEntityAt);
    if (!plausible(ent)) return 0;
    uintptr_t start = (uintptr_t)safe_ptr_at(ent, g_entSlotsAt), end = (uintptr_t)safe_ptr_at(ent, g_entSlotsAt + 8);
    if (start < 0x10000 || end <= start || end >= 0x00007FFFFFFFFFFFull || (end - start) / g_slotSize <= (size_t)i) return 0;
    const uint8_t* sl = (const uint8_t*)(start + (size_t)i * g_slotSize);
    uint8_t st = 0;
    uint16_t count = 0;
    if (!safe_copy(&st, sl + g_slotStateAt, 1) || !safe_copy(&count, sl + g_slotCountAt, 2)) return 0;
    auto it = g_slotStates.find(st);
    if (it == g_slotStates.end()) return 0;
    lua_pushstring(L, it->second.c_str());
    lua_pushinteger(L, count);
    return 2;
}

// za.input.abilities(controller) -> {{state, count, index, cooldown, cooldown_start, charge_start, last_trigger, replenish}, ...} | nil
// every activated ability slot of the controlled entity as the simulation has it: its state (as ability_state gives it), the uses left,
// the place of its ability in the mission's ability definitions (index, from 0), and its timings in the game's world time
// (GameRunner.worldController.<GameWorldTime>k__BackingField): the cooldown's length and when it began, when a charge began,
// when it was last triggered and its replenish time. The time left in a cooldown is cooldown_start + cooldown - world time.
static int g_aReady = 0;
static size_t g_aDataAt = 0, g_aCdAt = 0, g_aCdStartAt = 0, g_aChargeAt = 0, g_aLastAt = 0, g_aReplAt = 0;
static bool ability_times_ready(void* ctrlClass) {
    if (g_aReady) return g_aReady > 0;
    g_aReady = -1;
    if (!slot_state_ready(ctrlClass)) return false;
    void* slot = full_class("NativeBuildInterop.AbilitySlot");
    if (!slot) return false;
    for (const FieldDesc& d : fields_of(slot, false)) {
        if (d.name == "data_id") g_aDataAt = d.offset - OBJECT_HEADER;
        if (d.name == "cooldown_duration") g_aCdAt = d.offset - OBJECT_HEADER;
        if (d.name == "cooldown_start_time") g_aCdStartAt = d.offset - OBJECT_HEADER;
        if (d.name == "charge_start_time") g_aChargeAt = d.offset - OBJECT_HEADER;
        if (d.name == "last_trigger_time") g_aLastAt = d.offset - OBJECT_HEADER;
        if (d.name == "replenish_time") g_aReplAt = d.offset - OBJECT_HEADER;
    }
    bool ok = g_aDataAt || g_aCdAt;   // (data_id may sit at offset 0)
    ok = g_aCdAt && g_aCdStartAt && g_aChargeAt && g_aLastAt && g_aReplAt;
    logf("input: ability timings from the simulation: %s", ok ? "usable" : "not usable");
    if (ok) g_aReady = 1;
    return ok;
}
static int za_input_abilities(lua_State* L) {
    ZRef* ctrl = (ZRef*)luaL_checkudata(L, 1, ZREF);
    if (ctrl->isStruct || !plausible(ctrl->p) || !ability_times_ready(ctrl->k)) return 0;
    void* ent = safe_ptr_at(ctrl->p, g_ctrlEntityAt);
    if (!plausible(ent)) return 0;
    uintptr_t start = (uintptr_t)safe_ptr_at(ent, g_entSlotsAt), end = (uintptr_t)safe_ptr_at(ent, g_entSlotsAt + 8);
    if (start < 0x10000 || end <= start || end >= 0x00007FFFFFFFFFFFull) return 0;
    size_t n = (end - start) / g_slotSize;
    if (n == 0 || n > 8) return 0;
    lua_newtable(L);
    for (size_t i = 0; i < n; i++) {
        const uint8_t* sl = (const uint8_t*)(start + i * g_slotSize);
        uint8_t st = 0; uint16_t count = 0; uint32_t index = 0; float cd = 0, cds = 0, ch = 0, last = 0, repl = 0;
        if (!safe_copy(&st, sl + g_slotStateAt, 1) || !safe_copy(&count, sl + g_slotCountAt, 2)) continue;
        safe_copy(&index, sl + g_aDataAt, 4);
        safe_copy(&cd, sl + g_aCdAt, 4); safe_copy(&cds, sl + g_aCdStartAt, 4); safe_copy(&ch, sl + g_aChargeAt, 4);
        safe_copy(&last, sl + g_aLastAt, 4); safe_copy(&repl, sl + g_aReplAt, 4);
        lua_newtable(L);
        auto it = g_slotStates.find(st);
        lua_pushstring(L, it != g_slotStates.end() ? it->second.c_str() : "?"); lua_setfield(L, -2, "state");
        lua_pushinteger(L, count); lua_setfield(L, -2, "count");
        lua_pushinteger(L, index); lua_setfield(L, -2, "index");
        lua_pushnumber(L, cd); lua_setfield(L, -2, "cooldown");
        lua_pushnumber(L, cds); lua_setfield(L, -2, "cooldown_start");
        lua_pushnumber(L, ch); lua_setfield(L, -2, "charge_start");
        lua_pushnumber(L, last); lua_setfield(L, -2, "last_trigger");
        lua_pushnumber(L, repl); lua_setfield(L, -2, "replenish");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

// za.input.weapons(controller) -> active, {index, ...} | nil: the player's weapons as the simulation has them. The controlled
// entity (a native GameEntity) keeps them in weapons_, a native vector (start and end pointers) of WeaponState structs; the slot in hand is
// active_weapon (from 0), and each weapon's weapon_data_id.index is its place in the mission's weapon definitions
// (worldController.loadDataBuilder.weaponDefinitions, from 0), the same index the HUD's own weapon panel keeps in its
// cacheId. A list entry is -1 for a slot with no weapon. Offsets and sizes come from the game's own type data; anything
// unexpected gives nil.
static int g_wReady = 0;   // 0 not tried, 1 usable, -1 unusable
static size_t g_wArrAt = 0, g_wSize = 0, g_wCount = 0, g_wActiveAt = 0, g_wDataAt = 0, g_wIdAt = 0;
static bool g_wHaveData = false, g_wHaveId = false;
static bool weapons_ready(void* ctrlClass) {
    if (g_wReady) return g_wReady > 0;
    g_wReady = -1;
    void* ent = full_class("NativeBuildInterop.GameEntity");
    void* ws = full_class("NativeBuildInterop.WeaponState");
    for (const FieldDesc& d : fields_of(ctrlClass, false)) if (d.name == "controlledEntity") g_ctrlEntityAt = d.offset;
    if (ws) {
        g_wSize = (size_t)g_api.class_instance_size(ws) - OBJECT_HEADER;
        for (const FieldDesc& d : fields_of(ws, false)) {
            if (d.name == "weapon_data") { g_wDataAt = d.offset - OBJECT_HEADER; g_wHaveData = true; }
            if (d.name == "weapon_data_id") { g_wIdAt = d.offset - OBJECT_HEADER; g_wHaveId = true; }
        }
    }
    if (ent && g_wSize)
        for (const FieldDesc& d : fields_of(ent, false)) {
            if (d.name == "weapons_") g_wArrAt = d.offset - OBJECT_HEADER;   // (a native vector: start and end pointers)
            if (d.name == "active_weapon") g_wActiveAt = d.offset - OBJECT_HEADER;
        }
    bool ok = g_ctrlEntityAt && g_wSize >= 24 && g_wArrAt && g_wActiveAt && g_wHaveData && g_wHaveId;
    logf("input: weapons from the simulation (GameEntity.weapons_, %zu-byte slots): %s", g_wSize, ok ? "usable" : "not usable");
    if (ok) g_wReady = 1;
    return ok;
}
static int za_input_weapons(lua_State* L) {
    ZRef* ctrl = (ZRef*)luaL_checkudata(L, 1, ZREF);
    if (ctrl->isStruct || !plausible(ctrl->p) || !weapons_ready(ctrl->k)) return 0;
    void* ent = safe_ptr_at(ctrl->p, g_ctrlEntityAt);
    if (!plausible(ent)) return 0;
    uintptr_t start = (uintptr_t)safe_ptr_at(ent, g_wArrAt), end = (uintptr_t)safe_ptr_at(ent, g_wArrAt + 8);
    if (start < 0x10000 || end <= start || end >= 0x00007FFFFFFFFFFFull) return 0;
    size_t count = (end - start) / g_wSize;
    if (count < 1 || count > 16) return 0;
    int64_t idx[16];
    int last = -1;
    for (size_t i = 0; i < count; i++) {
        const uint8_t* w = (const uint8_t*)(start + i * g_wSize);
        uint64_t data = 0;
        uint32_t index = 0;
        idx[i] = -1;
        if (safe_copy(&data, w + g_wDataAt, 8) && data > 0x10000 && data < 0x00007FFFFFFFFFFFull && safe_copy(&index, w + g_wIdAt, 4)) { idx[i] = index; last = (int)i; }
    }
    uint8_t active = 0;
    if (last < 0 || !safe_copy(&active, (const uint8_t*)ent + g_wActiveAt, 1)) return 0;
    lua_pushinteger(L, active);
    lua_newtable(L);
    for (int i = 0; i <= last; i++) { lua_pushinteger(L, idx[i]); lua_rawseti(L, -2, i + 1); }
    return 2;
}

static void register_input_api(lua_State* L, bool system) {  // za.input on the za table at the top of the stack
    lua_newtable(L);
    const luaL_Reg in[] = {{"down", za_input_down}, {"presses", za_input_presses}, {"focused", za_input_focused}, {"mouse", za_input_mouse},
                           {"key_name", za_input_key_name}, {nullptr, nullptr}};
    luaL_setfuncs(L, in, 0);
    if (!system) {
        lua_pushcfunction(L, za_input_fire); lua_setfield(L, -2, "fire");
        lua_pushcfunction(L, za_input_ability); lua_setfield(L, -2, "ability");
        lua_pushcfunction(L, za_input_ability_state); lua_setfield(L, -2, "ability_state");
        lua_pushcfunction(L, za_input_weapons); lua_setfield(L, -2, "weapons");
        lua_pushcfunction(L, za_input_abilities); lua_setfield(L, -2, "abilities");
    }
    lua_setfield(L, -2, "input");
}
// za.enum(className, value) -> name | nil: the name of a value of one of the game's enums (full name, for example
// "NativeBuildInterop.NetworkConnectionState"); enum fields read through ref:get come as numbers
static std::unordered_map<std::string, std::unordered_map<int64_t, std::string>> g_enumCache;
static int za_enum(lua_State* L) {
    std::string cls = luaL_checkstring(L, 1);
    int64_t v = (int64_t)luaL_checkinteger(L, 2);
    auto it = g_enumCache.find(cls);
    if (it == g_enumCache.end()) it = g_enumCache.emplace(cls, enum_names(full_class(cls.c_str()))).first;
    auto n = it->second.find((int64_t)(uint8_t)v);  // (enum_names keys by the low byte)
    if (n == it->second.end()) { lua_pushnil(L); return 1; }
    lua_pushstring(L, n->second.c_str());
    return 1;
}
// za.missions: the Operations mission list, for Mission Re-roller. The hub's mission select menu (MenuMissionSelect)
// builds that list when a lobby you host is created, from the seed stored in GameRunner.lastMissionSelectRNGSeed
// (GameRunner.GetMissionSelectRNGSeed keeps it until the game resets it, which is why leaving a mission re-rolls).
// Only these, on purpose (no general way for a mod to call game methods):
//   za.missions.seed() -> the stored seed | nil (none stored: the next list draws one)
//   za.missions.set_seed(n): store the seed for the next list
//   za.missions.preview() -> List<MissionInfoInstance> | nil, why: the list the menu builds with the stored seed (the
//     menu's own GenerateOperationsMissionInfoList, on the menu object: the same count, profile and levels as the game)
//   za.missions.menu() -> the mission select menu while it is showing | nil
//   za.missions.reroll(n) -> true, applied | false, why: on the mission select screen, a new list for the open lobby:
//     the seed is stored, the menu builds the list, and the lobby gets it through Lobby.SetMissionList, as the game
//     itself does when the lobby asks for one; applied is false when the lobby did not take it yet (it will use the
//     stored seed when it asks). The caller checks the lobby is the player's own (alone, or leading the party).
static const int32_t kNoSeed = 0x7FFFFFFF;  // GameRunner's "no seed stored"
static struct {
    int ready = 0;  // 0 not tried, 1 usable, -1 unusable
    const FieldDesc* instance = nullptr;
    size_t seedAt = 0, listAt = 0;
    void* menuClass = nullptr;
    void* menuType = nullptr;  // GC handle of its System.Type
    void* generate = nullptr;  // MenuMissionSelect.GenerateOperationsMissionInfoList(out bool, bool)
    void* setList = nullptr;   // Lobby.SetMissionList(Lobby, List<MissionInfoInstance>, bool, uint)
    const FieldDesc* lobby = nullptr;  // GameRunner.lobby
} MS;
static bool missions_ready() {
    if (MS.ready) return MS.ready > 0;
    MS.ready = -1;
    void* runner = find_game_class("GameRunner");
    MS.menuClass = find_game_class("MenuMissionSelect");
    MS.instance = runner ? find_field(runner, "<Instance>k__BackingField", true) : nullptr;
    const FieldDesc* seed = runner ? find_field(runner, "lastMissionSelectRNGSeed", false) : nullptr;
    const FieldDesc* list = MS.menuClass ? find_field(MS.menuClass, "missionInfoInstanceList", false) : nullptr;
    MS.generate = MS.menuClass ? g_api.class_get_method_from_name(MS.menuClass, "GenerateOperationsMissionInfoList", 2) : nullptr;
    if (MS.menuClass) MS.menuType = type_handle(MS.menuClass);
    if (void* lobby = find_game_class("Lobby")) MS.setList = g_api.class_get_method_from_name(lobby, "SetMissionList", 4);
    MS.lobby = runner ? find_field(runner, "lobby", false) : nullptr;
    bool ok = MS.instance && seed && list && MS.generate && MS.menuType && UI.findObjectsAll;
    if (ok) { MS.seedAt = seed->offset; MS.listAt = list->offset; MS.ready = 1; }
    logf("missions: Operations list access %s", ok ? "ready" : "unavailable (the game's mission select code was not found)");
    return ok;
}
static void* game_runner() {
    void* v = nullptr;
    return MS.instance && safe_static_get(MS.instance->field, &v) && plausible(v) ? v : nullptr;
}
static int za_missions_seed(lua_State* L) {
    void* r = g_inMainThreadTick && missions_ready() ? game_runner() : nullptr;
    int32_t s = kNoSeed;
    if (!r || !safe_copy(&s, (const uint8_t*)r + MS.seedAt, 4) || s == kNoSeed) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, s);
    return 1;
}
static int za_missions_set_seed(lua_State* L) {
    int32_t s = (int32_t)luaL_checkinteger(L, 1);
    void* r = g_inMainThreadTick && missions_ready() ? game_runner() : nullptr;
    if (!r || s == kNoSeed) { lua_pushboolean(L, false); return 1; }
    *(int32_t*)((uint8_t*)r + MS.seedAt) = s;
    lua_pushboolean(L, true);
    return 1;
}
// The menu object: it exists once the game has shown mission select (inactive between visits); showingOnly: only while
// it is on screen
static void* mission_menu(bool showingOnly) {
    int include = 1, sortNone = 0;
    void* arr = call(UI.findObjectsAll, nullptr, {target(MS.menuType), &include, &sortNone});
    uint32_t n = 0;
    if (!arr || !safe_array_length(arr, &n)) return nullptr;
    for (uint32_t i = 0; i < n; i++) {
        void* m = safe_ptr_at((const uint8_t*)arr + ARRAY_DATA, (size_t)i * 8);
        if (!alive(m)) continue;
        if (!showingOnly) return m;
        bool on = false;
        void* go = get_game_object(m);
        if (go && unbox_into(call(UI.goActiveInHierarchy, go, {}), &on, 1) && on) return m;
    }
    return nullptr;
}
// The menu's own list builder with the stored seed -> the list (in the menu), or nullptr
static void* build_mission_list(void* menu, bool* generatedNew, const char** why) {
    bool keepSeed = true;
    void* args[2] = {generatedNew, &keepSeed};
    void* exc = nullptr;
    g_api.runtime_invoke(MS.generate, menu, args, &exc);
    void* list = exc ? nullptr : safe_ptr_at(menu, MS.listAt);
    if (!plausible(list)) { *why = exc ? "the game's list builder failed" : "no list"; return nullptr; }
    return list;
}
static int za_missions_preview(lua_State* L) {
    if (!g_inMainThreadTick || !missions_ready()) { lua_pushnil(L); lua_pushstring(L, "unavailable"); return 2; }
    void* menu = mission_menu(false);
    if (!menu) { lua_pushnil(L); lua_pushstring(L, "open mission select once"); return 2; }
    bool generatedNew = false;
    const char* why = nullptr;
    void* list = build_mission_list(menu, &generatedNew, &why);
    if (!list) { lua_pushnil(L); lua_pushstring(L, why); return 2; }
    push_object(L, list);
    return 1;
}
static int za_missions_menu(lua_State* L) {
    void* menu = g_inMainThreadTick && missions_ready() ? mission_menu(true) : nullptr;
    if (menu) push_object(L, menu); else lua_pushnil(L);
    return 1;
}
static int za_missions_reroll(lua_State* L) {
    int32_t seed = (int32_t)luaL_checkinteger(L, 1);
    auto fail = [L](const char* why) { lua_pushboolean(L, false); lua_pushstring(L, why); return 2; };
    if (!g_inMainThreadTick || !missions_ready() || !MS.setList || !MS.lobby) return fail("unavailable");
    void* runner = game_runner();
    void* menu = mission_menu(true);
    void* lobby = runner ? safe_ptr_at(runner, MS.lobby->offset) : nullptr;
    if (!runner || !menu) return fail("not on the mission select screen");
    if (!plausible(lobby)) return fail("no lobby");
    if (seed == kNoSeed) return fail("bad seed");
    *(int32_t*)((uint8_t*)runner + MS.seedAt) = seed;
    bool generatedNew = false;
    const char* why = nullptr;
    void* list = build_mission_list(menu, &generatedNew, &why);
    if (!list) return fail(why);
    uint32_t useed = (uint32_t)seed;
    void* args[4] = {lobby, list, &generatedNew, &useed};
    void* exc = nullptr;
    g_api.runtime_invoke(MS.setList, nullptr, args, &exc);
    // (a lobby that has not asked for its list yet may refuse one: the stored seed is what the game uses when it asks)
    lua_pushboolean(L, true);
    lua_pushboolean(L, exc == nullptr);
    return 2;
}
static void register_missions_api(lua_State* L) {  // za.missions on the za table at the top of the stack
    lua_newtable(L);
    const luaL_Reg fns[] = {{"seed", za_missions_seed}, {"set_seed", za_missions_set_seed}, {"preview", za_missions_preview},
                            {"menu", za_missions_menu}, {"reroll", za_missions_reroll}, {nullptr, nullptr}};
    luaL_setfuncs(L, fns, 0);
    lua_setfield(L, -2, "missions");
}
static void register_mod_api(lua_State* L) {  // the per-mod parts of za (the za table is at the top of the stack)
    const luaL_Reg api[] = {{"mod", za_mod}, {"setting", za_setting}, {"set_info", za_set_info}, {"set_setting", za_set_setting}, {"on_settings", za_on_settings},
                            {"notify", za_notify}, {"pods_bosses", za_pods_bosses}, {"hover", za_hover}, {"entity", za_entity}, {"enum", za_enum}, {nullptr, nullptr}};
    luaL_setfuncs(L, api, 0);
    register_input_api(L, false);
    register_missions_api(L);
}

// ------------------------------------------------------------------------------------- copying game UI --
static bool class_name_has(void* obj, std::initializer_list<const char*> parts) {
    void* k = safe_object_class(obj);
    if (!valid_class(k)) return false;
    std::string n = g_api.class_get_name(k);
    for (const char* p : parts) if (n.find(p) != std::string::npos) return true;
    return false;
}
static bool go_has_component(void* go, std::initializer_list<const char*> parts) {
    void* arr = go ? call(UI.goGetComponents, go, {target(UI.componentType)}) : nullptr;
    uint32_t n = 0;
    if (!arr || !safe_array_length(arr, &n)) return false;
    for (uint32_t i = 0; i < n && i < 32; i++)
        if (class_name_has(safe_ptr_at(arr, ARRAY_DATA + 8 * i), parts)) return true;
    return false;
}
// Replaces every click event on a copied button: UnityEvent fields get a fresh, empty event of the same type; delegate
// fields are cleared. The copy then keeps the native hover / press visuals but no click action of the original.
static bool derives_from(void* k, const char* name) {
    for (void* c = k; c && valid_class(c); c = g_api.class_get_parent(c))
        if (strcmp(g_api.class_get_name(c), name) == 0) return true;
    return false;
}
static int neutralise_events(void* comp, std::string& log) {
    void* k = safe_object_class(comp);
    int n = 0;
    for (const FieldDesc& d : fields_of(k, false)) {
        if (!d.reference || !d.klass) continue;
        void* cur = safe_ptr_at(comp, d.offset);
        if (derives_from(d.klass, "UnityEventBase")) {
            void* fresh = g_api.object_new(d.klass);
            void* ctor = g_api.class_get_method_from_name(d.klass, ".ctor", 0);
            bool ok = false;
            if (fresh && ctor) call(ctor, fresh, {}, &ok);
            if (!ok) continue;
            safe_copy((uint8_t*)comp + d.offset, &fresh, sizeof fresh);
            log += d.name + "(event) "; n++;
        } else if (cur && derives_from(d.klass, "MulticastDelegate")) {
            void* null = nullptr;
            safe_copy((uint8_t*)comp + d.offset, &null, sizeof null);
            log += d.name + "(delegate) "; n++;
        }
    }
    return n;
}
// ---------------------------------------------------------------------------------------- system script --
static lua_State* g_sys = nullptr;
static int g_sysFrame = LUA_NOREF, g_sysErrors = 0;
static HMODULE g_selfModule = nullptr;

static int sys_mods(lua_State* L) {
    lua_newtable(L);
    for (size_t i = 0; i < g_mods.size(); i++) {
        const LuaMod& m = g_mods[i];
        lua_newtable(L);
        lua_pushinteger(L, (lua_Integer)i + 1); lua_setfield(L, -2, "index");
        lua_pushstring(L, (m.title.empty() ? m.name : m.title).c_str()); lua_setfield(L, -2, "name");
        lua_pushstring(L, m.description.c_str()); lua_setfield(L, -2, "description");
        lua_pushstring(L, m.version.c_str()); lua_setfield(L, -2, "version");
        lua_pushstring(L, m.status == 2 ? "error" : m.status == 1 ? "warning" : "ok"); lua_setfield(L, -2, "status");
        lua_pushstring(L, m.error.c_str()); lua_setfield(L, -2, "message");
        lua_newtable(L);
        int n = 0;
        for (const ModSetting& s : m.settings) {
            lua_newtable(L);
            lua_pushstring(L, s.id.c_str()); lua_setfield(L, -2, "id");
            lua_pushstring(L, s.type.c_str()); lua_setfield(L, -2, "type");
            lua_pushstring(L, s.label.c_str()); lua_setfield(L, -2, "label");
            lua_pushnumber(L, s.min); lua_setfield(L, -2, "min");
            lua_pushnumber(L, s.max); lua_setfield(L, -2, "max");
            lua_pushnumber(L, s.step); lua_setfield(L, -2, "step");
            if (!s.lock.empty()) { lua_pushstring(L, s.lock.c_str()); lua_setfield(L, -2, "lock"); }
            if (s.type == "info") lua_pushstring(L, s.str.c_str()); else lua_pushnumber(L, s.num);
            lua_setfield(L, -2, "value");
            if (!s.options.empty()) {
                lua_newtable(L);
                for (size_t j = 0; j < s.options.size(); j++) { lua_pushstring(L, s.options[j].c_str()); lua_rawseti(L, -2, (lua_Integer)j + 1); }
                lua_setfield(L, -2, "options");
            }
            lua_rawseti(L, -2, ++n);
        }
        lua_setfield(L, -2, "settings");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}
// za.sys.mission_active(on): the system script says whether a mission is being played (not the hub): settings marked
// lock = 'mission' are then refused by za.sys.save, whatever asks
static bool g_missionActive = false;
static int sys_mission_active(lua_State* L) { g_missionActive = lua_toboolean(L, 1) != 0; return 0; }
// za.sys.save(index, {id = number, ...}) -> ok: apply, write the .settings file, tell the mod (za.on_settings)
static int sys_save(lua_State* L) {
    int i = (int)luaL_checkinteger(L, 1) - 1;
    luaL_checktype(L, 2, LUA_TTABLE);
    if (i < 0 || i >= (int)g_mods.size()) { lua_pushboolean(L, false); return 1; }
    LuaMod& m = g_mods[i];
    for (ModSetting& s : m.settings) {
        if (s.type == "info") continue;
        lua_getfield(L, 2, s.id.c_str());
        if (lua_isnumber(L, -1)) {
            if (s.lock == "mission" && g_missionActive && lua_tonumber(L, -1) != s.num)   // (a locked setting stays as it is mid-mission)
                logf("loader: %s: '%s' cannot be changed during a mission", m.name.c_str(), s.id.c_str());
            else s.num = lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
    }
    bool ok = save_settings_file(m);
    if (!m.disabled && m.L && m.onSettings != LUA_NOREF) {
        lua_rawgeti(m.L, LUA_REGISTRYINDEX, m.onSettings);
        if (lua_pcall(m.L, 0, 0, 0) != LUA_OK) { logf("[%s] on_settings error: %s", m.name.c_str(), lua_tostring(m.L, -1)); lua_pop(m.L, 1); }
    }
    logf("loader: settings saved for %s", m.name.c_str());
    lua_pushboolean(L, ok);
    return 1;
}
static int sys_events(lua_State* L) {
    lua_newtable(L);
    int n = 0;
    for (const LoaderEvent& e : g_events) {
        lua_newtable(L);
        lua_pushstring(L, e.kind == 2 ? "error" : e.kind == 1 ? "warning" : "ok"); lua_setfield(L, -2, "kind");
        lua_pushstring(L, e.title.c_str()); lua_setfield(L, -2, "title");
        lua_pushstring(L, e.body.c_str()); lua_setfield(L, -2, "body");
        lua_rawseti(L, -2, ++n);
    }
    g_events.clear();
    return 1;
}
static int sys_menu_open(lua_State* L) { g_menuOpen = lua_toboolean(L, 1) != 0; return 0; }
static int sys_in_game(lua_State* L) { lua_pushboolean(L, g_inMission); return 1; }
// za.sys.key_pressed() -> the first key (virtual-key code) with a new press since the last call, for key binding
static uint32_t g_keySeen[256];
static int sys_key_pressed(lua_State* L) {
    int found = 0;
    for (int vk = 1; vk < 256; vk++) {
        if (g_keyPresses[vk] != g_keySeen[vk]) {
            g_keySeen[vk] = g_keyPresses[vk];
            if (!found && vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) found = vk;
        }
    }
    if (found) lua_pushinteger(L, found); else lua_pushnil(L);
    return 1;
}

// ------------------------------------------------------------------------------------- native settings tab --
// za.sys.nui.*: the loader's own tools for adding a "MODS" tab to the game's Settings screen out of copies of the game's
// own pieces (a tab, a page, rows), so it looks and behaves like the rest of it. Loader-only (a mod cannot call these).
//   children(ref) -> {GameObject, ...}         the direct children of an object
//   clone(src, parent [, mode [, name]])       a copy of an object under another; mode 'page' keeps the show / hide animation of
//       a settings page, 'row' / 'tab' (default 'row') switch the animations and localisation off and show the copy at full size
//   destroy(ref), active(ref, bool), text(ref, string)   remove / show / relabel (every text under the object)
//   component(ref, className) -> the first component of that exact class on the object, or nil
//   call(ref, method [, arg]) -> true | false  a short fixed list of the game's own setters (see allowed below)
static int nui_children(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    lua_newtable(L);
    if (!g_inMainThreadTick || r->isStruct) return 1;
    void* tr = get_transform(r->p);
    int count = 0;
    if (!alive(tr) || !unbox_into(call(UI.trChildCount, tr, {}), &count, 4)) return 1;
    int n = 0;
    for (int i = 0; i < count && i < 96; i++) {
        void* c = call(UI.trGetChild, tr, {&i});
        void* go = alive(c) ? get_game_object(c) : nullptr;
        if (!go) continue;
        push_object(L, go);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
static int nui_clone(lua_State* L) {
    ZRef* src = check_ref(L, 1);
    ZRef* par = check_ref(L, 2);
    const char* mode = luaL_optstring(L, 3, "row");
    const char* name = luaL_optstring(L, 4, nullptr);
    bool page = strcmp(mode, "page") == 0;
    if (!g_inMainThreadTick || src->isStruct || par->isStruct || !UI.objInstantiate || !UI.compInChildren || !UI.behSetEnabled) { lua_pushnil(L); return 1; }
    void* original = get_game_object(src->p);
    void* ptr = get_transform(par->p);
    void* clone = alive(original) && alive(ptr) ? call(UI.objInstantiate, nullptr, {original, ptr}) : nullptr;
    if (!alive(clone)) { lua_pushnil(L); return 1; }
    if (name && UI.objSetName) call(UI.objSetName, clone, {g_api.string_new(name)});
    void* ctr = call(U.goGetTransform, clone, {});
    bool all = true, off = false;
    void* behs = call(UI.compInChildren, ctr, {target(UI.behaviourType), &all});
    uint32_t n = 0;
    std::string events;
    if (behs && safe_array_length(behs, &n))
        for (uint32_t i = 0; i < n && i < 300; i++) {
            void* b = safe_ptr_at(behs, ARRAY_DATA + 8 * i);
            if (!alive(b)) continue;
            if (class_name_has(b, {"Button", "Selectable", "Toggle", "Slider", "Dropdown"})) neutralise_events(b, events);
            bool dropScript = class_name_has(b, {"Dropdown"}) && strcmp(g_api.class_get_name(safe_object_class(b)), "TMP_Dropdown") != 0;
            if (dropScript || class_name_has(b, {"Locali", "Hotkey", "Analytic", "Tooltip", "Rebind"}) || (!page && class_name_has(b, {"Tween"})))
                call(UI.behSetEnabled, b, {&off});
        }
    if (!page && UI.canvasGroupType && UI.cgSetAlpha) {   // (a copy of something hidden would stay faded out and shrunk)
        void* groups = call(UI.compInChildren, ctr, {target(UI.canvasGroupType), &all});
        if (groups && safe_array_length(groups, &n))
            for (uint32_t i = 0; i < n && i < 300; i++) {
                void* g = safe_ptr_at(groups, ARRAY_DATA + 8 * i);
                if (!alive(g)) continue;
                float one = 1;
                call(UI.cgSetAlpha, g, {&one});
                Vec3 s{1, 1, 1};
                if (void* t = get_transform(g)) call(U.trSetLocalScale, t, {&s});
            }
    }
    int last = 0x7fffffff;
    if (UI.trSetSiblingIndex) call(UI.trSetSiblingIndex, ctr, {&last});
    logf("nui: cloned %s as %s (%s): events cleared %s", object_name(original).c_str(), object_name(clone).c_str(), mode, events.empty() ? "none" : events.c_str());
    push_object(L, clone);
    return 1;
}
static int nui_destroy(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    if (g_inMainThreadTick && !r->isStruct) if (void* go = get_game_object(r->p)) call(U.objDestroy, nullptr, {go});
    return 0;
}
static int nui_active(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    bool on = lua_toboolean(L, 2) != 0;
    if (g_inMainThreadTick && !r->isStruct) if (void* go = get_game_object(r->p)) call(U.goSetActive, go, {&on});
    return 0;
}
static int nui_text(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* s = luaL_checkstring(L, 2);
    if (!g_inMainThreadTick || r->isStruct || !UI.compInChildren || !UI.tmpTextType) return 0;
    bool all = true;
    void* tr = get_transform(r->p);
    void* arr = tr ? call(UI.compInChildren, tr, {target(UI.tmpTextType), &all}) : nullptr;
    uint32_t n = 0;
    if (arr && safe_array_length(arr, &n))
        for (uint32_t i = 0; i < n && i < 16; i++) {
            void* t = safe_ptr_at(arr, ARRAY_DATA + 8 * i);
            if (alive(t)) call(U.tmpSetText, t, {g_api.string_new(s)});
        }
    return 0;
}
static int nui_component(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* cls = luaL_checkstring(L, 2);
    if (!g_inMainThreadTick || r->isStruct) { lua_pushnil(L); return 1; }
    void* go = get_game_object(r->p);
    void* arr = go ? call(UI.goGetComponents, go, {target(UI.componentType)}) : nullptr;
    uint32_t n = 0;
    if (arr && safe_array_length(arr, &n))
        for (uint32_t i = 0; i < n && i < 32; i++) {
            void* c = safe_ptr_at(arr, ARRAY_DATA + 8 * i);
            void* k = safe_object_class(c);
            if (alive(c) && valid_class(k) && strcmp(g_api.class_get_name(k), cls) == 0) { push_object(L, c); return 1; }
        }
    lua_pushnil(L);
    return 1;
}
static int nui_call(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* name = luaL_checkstring(L, 2);
    static const char* allowed[] = {"Show", "ForceHide", "set_group", "SetIsOnWithoutNotify", "SetValueWithoutNotify", "set_minValue",
                                    "set_maxValue", "set_wholeNumbers", "set_alignment", "set_textWrappingMode", "set_fontSize", "set_interactable", nullptr};
    bool permitted = false;
    for (int i = 0; allowed[i]; i++) if (strcmp(name, allowed[i]) == 0) permitted = true;
    int argc = lua_gettop(L) >= 3 ? 1 : 0;
    void* m = permitted && g_inMainThreadTick && !r->isStruct && alive(r->p) ? g_api.class_get_method_from_name(r->k, name, argc) : nullptr;
    if (!m) { lua_pushboolean(L, false); return 1; }
    bool b = false, ok = false;
    int iv = 0;
    float fv = 0;
    void* argp = nullptr;
    if (argc) {
        int t = g_api.type_get_type(g_api.method_get_param(m, 0));
        if (t == 2) { b = lua_toboolean(L, 3) != 0; argp = &b; }
        else if (t == 12) { fv = (float)luaL_checknumber(L, 3); argp = &fv; }
        else if (t == 8 || t == 17) { iv = (int)luaL_checkinteger(L, 3); argp = &iv; }
        else if (t == 18) { ZRef* o = check_ref(L, 3); argp = o->p; }
        else { lua_pushboolean(L, false); return 1; }
    }
    if (argc) call(m, r->p, {argp}, &ok); else call(m, r->p, {}, &ok);
    lua_pushboolean(L, ok);
    return 1;
}
// rect(ref) -> x, y, w, h: a UI object's anchored position and size; place(ref, x, y, w, h): set them (the size as the
// size delta, right for an object that is not stretched by its anchors)
static int nui_rect(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    Vec2 pos{};
    Rect4 rect{};
    void* tr = g_inMainThreadTick && !r->isStruct ? get_transform(r->p) : nullptr;
    if (!alive(tr) || !UI.rtGetPos || !UI.rtGetRect || !unbox_into(call(UI.rtGetPos, tr, {}), &pos, sizeof pos) ||
        !unbox_into(call(UI.rtGetRect, tr, {}), &rect, sizeof rect)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, pos.x); lua_pushnumber(L, pos.y); lua_pushnumber(L, rect.w); lua_pushnumber(L, rect.h);
    return 4;
}
static int nui_place(lua_State* L) {   // place(ref, x, y, w, h): each part optional (nil keeps it)
    ZRef* r = check_ref(L, 1);
    void* tr = g_inMainThreadTick && !r->isStruct ? get_transform(r->p) : nullptr;
    if (!alive(tr)) return 0;
    if (!lua_isnoneornil(L, 2) && !lua_isnoneornil(L, 3)) {
        Vec2 pos{(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3)};
        call(UI.rtSetPos, tr, {&pos});
    }
    // the size is the size delta (an object stretched by its anchors in one direction keeps that direction's size)
    // (w is the wanted width of the object as it is drawn, whatever its anchors make of the size delta; h is the size delta's height)
    static void* getDelta = g_api.class_get_method_from_name(UI.rectTrClass, "get_sizeDelta", 0);
    Vec2 size{};
    Rect4 rect{};
    if ((!lua_isnoneornil(L, 4) || !lua_isnoneornil(L, 5)) && getDelta && unbox_into(call(getDelta, tr, {}), &size, sizeof size) &&
        UI.rtGetRect && unbox_into(call(UI.rtGetRect, tr, {}), &rect, sizeof rect)) {
        if (!lua_isnoneornil(L, 4)) size.x += (float)luaL_checknumber(L, 4) - rect.w;
        if (!lua_isnoneornil(L, 5)) size.y = (float)luaL_checknumber(L, 5);
        call(UI.rtSetSize, tr, {&size});
    }
    return 0;
}
// dropdown(ref, {text, ...}, selected) -> true | false: fill a copy of the game's dropdown (the TMP_Dropdown component) with
// these options and select one, without raising its changed event
static int nui_dropdown(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    int sel = (int)luaL_optinteger(L, 3, 0);
    if (!g_inMainThreadTick || r->isStruct || !alive(r->p)) { lua_pushboolean(L, false); return 1; }
    void* getOpt = g_api.class_get_method_from_name(r->k, "get_options", 0);
    void* list = getOpt ? call(getOpt, r->p, {}) : nullptr;
    void* lk = alive(list) ? safe_object_class(list) : nullptr;
    void* clear = lk ? g_api.class_get_method_from_name(lk, "Clear", 0) : nullptr;
    void* add = lk ? g_api.class_get_method_from_name(lk, "Add", 1) : nullptr;
    void* optClass = add ? g_api.class_from_type(g_api.method_get_param(add, 0)) : nullptr;
    void* ctor = optClass ? method_exact(optClass, ".ctor", {"System.String"}) : nullptr;
    if (!clear || !add || !optClass || !ctor) { lua_pushboolean(L, false); return 1; }
    call(clear, list, {});
    int n = (int)lua_rawlen(L, 2);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 2, i);
        const char* s = lua_tostring(L, -1);
        void* od = g_api.object_new(optClass);
        bool ok = false;
        if (od && s) call(ctor, od, {g_api.string_new(s)}, &ok);
        if (ok) call(add, list, {od});
        lua_pop(L, 1);
    }
    if (sel < 0) sel = 0;
    if (void* setv = g_api.class_get_method_from_name(r->k, "SetValueWithoutNotify", 1)) call(setv, r->p, {&sel});
    if (void* refresh = g_api.class_get_method_from_name(r->k, "RefreshShownValue", 0)) call(refresh, r->p, {});
    lua_pushboolean(L, true);
    return 1;
}
// fill(ref, left, top, right, bottom): stretch an object over its parent with these margins
static int nui_fill(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    void* tr = g_inMainThreadTick && !r->isStruct ? get_transform(r->p) : nullptr;
    if (!alive(tr)) return 0;
    float l = (float)luaL_checknumber(L, 2), t = (float)luaL_checknumber(L, 3), rt = (float)luaL_checknumber(L, 4), b = (float)luaL_checknumber(L, 5);
    Vec2 amin{0, 0}, amax{1, 1}, omin{l, b}, omax{-rt, -t};
    call(UI.rtSetAnchorMin, tr, {&amin});
    call(UI.rtSetAnchorMax, tr, {&amax});
    if (void* m = g_api.class_get_method_from_name(UI.rectTrClass, "set_offsetMin", 1)) call(m, tr, {&omin});
    if (void* m = g_api.class_get_method_from_name(UI.rectTrClass, "set_offsetMax", 1)) call(m, tr, {&omax});
    return 0;
}
// set_field(ref, name, valueRef): point one of the game's dropdown fields (its caption / item text) at a text object
static int nui_set_field(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* name = luaL_checkstring(L, 2);
    ZRef* v = check_ref(L, 3);
    bool permitted = strcmp(name, "m_CaptionText") == 0 || strcmp(name, "m_ItemText") == 0;
    const FieldDesc* f = permitted && g_inMainThreadTick && !r->isStruct && !v->isStruct && alive(r->p) && alive(v->p) ? find_field(r->k, name, false) : nullptr;
    void* p = v->p;
    lua_pushboolean(L, f && safe_copy((uint8_t*)r->p + f->offset, &p, sizeof p));
    return 1;
}
static void register_nui(lua_State* L) {   // the nui table, on the sys table at the top of the stack
    lua_newtable(L);
    const luaL_Reg fns[] = {{"children", nui_children}, {"clone", nui_clone}, {"destroy", nui_destroy}, {"active", nui_active},
                            {"text", nui_text}, {"component", nui_component}, {"call", nui_call}, {"rect", nui_rect}, {"place", nui_place}, {"dropdown", nui_dropdown}, {"fill", nui_fill}, {"set_field", nui_set_field}, {nullptr, nullptr}};
    luaL_setfuncs(L, fns, 0);
    lua_setfield(L, -2, "nui");
}

static bool load_system_script() {
    HRSRC res = FindResourceW(g_selfModule, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    HGLOBAL data = res ? LoadResource(g_selfModule, res) : nullptr;
    const char* src = data ? (const char*)LockResource(data) : nullptr;
    DWORD size = res ? SizeofResource(g_selfModule, res) : 0;
    if (!src || !size) { logf("loader: the system script resource is missing"); return false; }
    lua_State* L = luaL_newstate();
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, "table", luaopen_table, 1);
    luaL_requiref(L, "string", luaopen_string, 1);
    luaL_requiref(L, "math", luaopen_math, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1);
    lua_pop(L, 5);
    for (const char* f : {"dofile", "loadfile", "load", "require"}) { lua_pushnil(L); lua_setglobal(L, f); }
    lua_pushinteger(L, -1); lua_setfield(L, LUA_REGISTRYINDEX, "za_mod_index");
    lua_pushinteger(L, 1); lua_setfield(L, LUA_REGISTRYINDEX, "za_ui_layer");  // za.ui draws on the loader canvas
    lua_newtable(L);
    lua_pushcfunction(L, za_log); lua_setfield(L, -2, "log");
    lua_pushstring(L, kLoaderVersion); lua_setfield(L, -2, "version");
    register_ui_api(L);
    register_input_api(L, true);
    lua_newtable(L);
    const luaL_Reg sys[] = {{"mods", sys_mods}, {"save", sys_save}, {"events", sys_events}, {"menu_open", sys_menu_open},
                            {"in_game", sys_in_game}, {"key_pressed", sys_key_pressed}, {"mission_active", sys_mission_active}, {nullptr, nullptr}};
    luaL_setfuncs(L, sys, 0);
    register_nui(L);
    lua_setfield(L, -2, "sys");
    lua_setglobal(L, "za");
    if (luaL_loadbuffer(L, src, size, "=system.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
        logf("loader: system script failed: %s", lua_tostring(L, -1));
        lua_close(L);
        return false;
    }
    if (!lua_isfunction(L, -1)) { logf("loader: the system script returned no frame function"); lua_close(L); return false; }
    g_sysFrame = luaL_ref(L, LUA_REGISTRYINDEX);
    g_sys = L;
    return true;
}
static double g_lastSys = 0, g_sysPauseUntil = 0;
static int g_sysFaults = 0;
// The system script's own fault guard (a pause-menu object can die mid-call while a scene unloads): a fault here only
// pauses the system script for 2 s, instead of going through the runtime's fault handling (which resets every mod).
static int sys_pcall_guarded(lua_State* L) {
    __try { return lua_pcall(L, 0, 0, 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void run_system(double t) {
    if (!g_sys || g_sysFrame == LUA_NOREF || t - g_lastSys < 30 || t < g_sysPauseUntil) return;
    g_lastSys = t;
    lua_rawgeti(g_sys, LUA_REGISTRYINDEX, g_sysFrame);
    int r = sys_pcall_guarded(g_sys);
    if (r == -1) {
        lua_settop(g_sys, 0);
        g_sysPauseUntil = t + 2000;
        logf("loader: the system script hit a fault (a menu object went away mid-call): paused 2 s (%d)", ++g_sysFaults);
        if (g_sysFaults >= 20) { logf("loader: system script stopped after 20 faults"); g_sysFrame = LUA_NOREF; g_modal = false; }
    } else if (r != LUA_OK) {
        logf("loader: system script error: %s", lua_tostring(g_sys, -1));
        lua_pop(g_sys, 1);
        if (++g_sysErrors >= 10) { logf("loader: system script stopped after 10 errors"); g_sysFrame = LUA_NOREF; g_modal = false; }
    }
}
// Every frame tick (from frame_tick_body): input, auto-fire, the system script
static void loader_tick(double t) {
    sample_input();
    tick_fire(t);
    tick_abilities(t);
    run_system(t);
}
// The message filter (hook_peek): while the settings screen is open, keyboard and mouse messages never reach the game
static void filter_message(MSG* msg) {
    if (!g_modal) return;
    UINT m = msg->message;
    if (m == WM_MOUSEWHEEL) g_wheel += GET_WHEEL_DELTA_WPARAM(msg->wParam);
    if (m == WM_INPUT) DefWindowProcW(msg->hwnd, m, msg->wParam, msg->lParam);  // lets Windows release the raw input
    if ((m >= WM_KEYFIRST && m <= WM_KEYLAST) || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || m == WM_INPUT) msg->message = WM_NULL;
}
