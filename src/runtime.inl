// ZARModManager runtime (step 2), included by loader.cpp: Lua mods on the game's main thread, and the Unity calls behind
// za.label. The game's state is only read; the only objects the loader creates and changes are its own labels.
//
// Main thread: UnityPlayer's main loop calls PeekMessageW every frame. Its import-table entry (a data pointer, not game
// code) is redirected to hook_peek, which runs frame_tick on the main thread at most every MIN_TICK_MS, then calls
// the real PeekMessageW.

#include "lua.hpp"

// True only while the frame tick runs on the game's main thread: Unity calls from Lua are allowed only then.
static bool g_inMainThreadTick = false;
static void register_ui_api(lua_State* L);  // ui.inl
static void set_canvas_visible(bool on);
static void ui_tick(double t);  // ui.inl: element tweens, every frame
static bool init_ui();
// system.inl
static void loader_tick(double t);
static void filter_message(MSG* msg);
static void register_mod_api(lua_State* L);
static void push_event(int kind, const std::string& title, const std::string& body);
static bool load_system_script();
static void fail_box(const char* reason);
static void push_object(lua_State* L, void* obj);  // ui.inl
static void remove_stray_labels(void* entityTr, void* keepGo);

// ------------------------------------------------------------------------------------------------ entities --
struct EntityInfo {
    uint32_t id; uint8_t type; bool dead; bool hasHp, hasShield; float hp[3], shield[3]; void* display;
};
struct EntityLayout {
    bool ok = false;
    void* runnerClass = nullptr; void* displayClass = nullptr;
    const FieldDesc *instance = nullptr, *worldController = nullptr;
    const FieldDesc *dead = nullptr, *type = nullptr, *health = nullptr, *shield = nullptr, *transform = nullptr;
    std::unordered_map<int64_t, std::string> typeNames;
};
static EntityLayout g_layout;

static bool init_layout() {
    EntityLayout& L = g_layout;
    L.runnerClass = find_game_class("GameRunner");
    L.displayClass = find_game_class("EntityDisplay");
    if (!L.runnerClass || !L.displayClass) { logf("runtime: GameRunner / EntityDisplay not found"); return false; }
    L.instance = find_field(L.runnerClass, "<Instance>k__BackingField", true);
    L.worldController = find_field(L.runnerClass, "worldController", false);
    L.dead = find_field(L.displayClass, "entityIsDead", false);
    L.type = find_field(L.displayClass, "<EntityType>k__BackingField", false);
    L.health = find_field(L.displayClass, "lastHealth", false);
    L.shield = find_field(L.displayClass, "lastShieldHealth", false);
    L.transform = find_field(L.displayClass, "transform", false);
    if (!L.instance || !L.worldController || !L.dead || !L.type || !L.health || !L.shield || !L.transform) {
        logf("runtime: an EntityDisplay / GameRunner field is missing (game updated?): entities disabled");
        return false;
    }
    L.typeNames = enum_names(L.type->klass);
    L.ok = true;
    return true;
}

// Every entity of entitiesById. False when there is no mission (or the layout no longer matches).
static bool scan_entities(std::vector<EntityInfo>& out) {
    out.clear();
    EntityLayout& L = g_layout;
    if (!L.ok) return false;
    void* runner = nullptr;
    if (!safe_static_get(L.instance->field, &runner) || !plausible(runner)) return false;
    void* wc = safe_ptr_at(runner, L.worldController->offset);
    void* wcClass = safe_object_class(wc);
    if (!valid_class(wcClass)) return false;
    const FieldDesc* dictF = find_field(wcClass, "entitiesById", false);
    if (!dictF) return false;
    void* dict = safe_ptr_at(wc, dictF->offset);
    void* dictClass = safe_object_class(dict);
    if (!valid_class(dictClass)) return false;
    const FieldDesc* entriesF = find_field_any(dictClass, {"_entries", "entries"});
    const FieldDesc* countF = find_field_any(dictClass, {"_count", "count"});
    if (!entriesF || !countF) return false;
    void* entries = safe_ptr_at(dict, entriesF->offset);
    void* entriesClass = safe_object_class(entries);
    int32_t count = 0;
    uint32_t len = 0;
    if (!valid_class(entriesClass) || !safe_copy(&count, (const uint8_t*)dict + countF->offset, 4) ||
        !safe_array_length(entries, &len))
        return false;
    void* entryClass = g_api.class_get_element_class(entriesClass);
    const FieldDesc* keyF = find_field(entryClass, "key", false);
    const FieldDesc* valueF = find_field(entryClass, "value", false);
    const FieldDesc* nextF = find_field(entryClass, "next", false);
    const FieldDesc* idF = keyF ? find_field(keyF->klass, "id", false) : nullptr;
    int esize = g_api.class_array_element_size(entryClass);
    if (!keyF || !valueF || !idF || esize <= 0) return false;

    const uint8_t* data = (const uint8_t*)entries + ARRAY_DATA;
    for (uint32_t i = 0; i < len && (int32_t)i < count; i++) {
        const uint8_t* e = data + (size_t)i * esize;
        if (nextF) { int32_t next = 0; safe_copy(&next, e + nextF->offset - OBJECT_HEADER, 4); if (next < -1) continue; }
        void* disp = safe_ptr_at(e, valueF->offset - OBJECT_HEADER);
        if (!plausible(disp) || safe_object_class(disp) != L.displayClass) continue;
        EntityInfo info{};
        safe_copy(&info.id, e + keyF->offset - OBJECT_HEADER + idF->offset - OBJECT_HEADER, 4);
        const uint8_t* d = (const uint8_t*)disp;
        uint8_t dead = 0;
        safe_copy(&dead, d + L.dead->offset, 1);
        safe_copy(&info.type, d + L.type->offset, 1);
        info.dead = dead != 0;
        info.hasHp = read_health(d, L.health, info.hp);
        info.hasShield = read_health(d, L.shield, info.shield);
        info.display = disp;
        out.push_back(info);
    }
    return true;
}

// The scan, guarded: a fault (the world being torn down during a scene change) means no data this tick.
static bool scan_entities_guarded(std::vector<EntityInfo>& out) {
    __try { return scan_entities(out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static const std::string& type_name_of(uint8_t t) {
    static const std::string unknown = "?";
    auto it = g_layout.typeNames.find(t);
    return it != g_layout.typeNames.end() ? it->second : unknown;
}

// --------------------------------------------------------------------------------------------- Unity bridge --
struct UnityBridge {
    bool ok = false, fontSearched = false;
    void* objectClass = nullptr; const FieldDesc* cachedPtr = nullptr;
    void *goClass = nullptr, *goCtor = nullptr, *goAddComponent = nullptr, *goSetActive = nullptr, *goGetTransform = nullptr;
    void *trGetPos = nullptr, *trSetPos = nullptr, *trGetRot = nullptr, *trSetRot = nullptr, *trGetUp = nullptr,
         *trGetForward = nullptr, *trSetParent = nullptr, *trSetLocalPos = nullptr, *trSetLocalScale = nullptr,
         *trGetLossyScale = nullptr;
    void *camGetMain = nullptr, *compGetTransform = nullptr, *objDestroy = nullptr, *objGetName = nullptr,
         *resFindAll = nullptr;
    void *tmpClass = nullptr, *tmpSetText = nullptr, *tmpSetFontSize = nullptr, *tmpSetAlignment = nullptr,
         *tmpSetColor = nullptr, *tmpSetFont = nullptr, *tmpSetOverlay = nullptr, *tmpSetOutlineWidth = nullptr;
    void *tmpType = nullptr, *fontAssetType = nullptr, *font = nullptr;  // GC handles (pointer-sized)
};
static UnityBridge U;
static int g_invokeErrors = 0;

static void* image_named(const char* name) {
    size_t n = 0;
    void** assemblies = g_api.domain_get_assemblies(g_api.domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        void* img = g_api.assembly_get_image(assemblies[i]);
        if (img && strcmp(g_api.image_get_name(img), name) == 0) return img;
    }
    return nullptr;
}
static void* method(void* klass, const char* name, int argc, bool required = true) {
    void* m = klass ? g_api.class_get_method_from_name(klass, name, argc) : nullptr;
    if (!m && required) logf("runtime: method %s(%d) not found", name, argc);
    return m;
}
static void* call(void* m, void* obj, std::initializer_list<void*> args, bool* ok = nullptr) {
    void* a[8] = {};
    int i = 0;
    for (void* x : args) a[i++] = x;
    void* exc = nullptr;
    void* r = g_api.runtime_invoke(m, obj, a, &exc);
    if (ok) *ok = exc == nullptr;
    if (exc) { g_invokeErrors++; return nullptr; }
    return r;
}
static void* target(void* handle) { return handle ? g_api.gchandle_get_target(handle) : nullptr; }
// A Unity object whose native side still exists (m_CachedPtr != 0).
static bool alive(void* obj) {
    if (!plausible(obj) || !U.cachedPtr) return false;
    void* native = safe_ptr_at(obj, U.cachedPtr->offset);
    return native != nullptr;
}
struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };
static bool unbox_into(void* boxed, void* out, size_t n) {
    if (!boxed) return false;
    return safe_copy(out, g_api.object_unbox(boxed), n);
}

static bool init_unity() {
    void* core = image_named("UnityEngine.CoreModule.dll");
    void* tmp = image_named("Unity.TextMeshPro.dll");
    if (!core || !tmp) { logf("runtime: UnityEngine.CoreModule / Unity.TextMeshPro not loaded"); return false; }
    U.objectClass = g_api.class_from_name(core, "UnityEngine", "Object");
    U.goClass = g_api.class_from_name(core, "UnityEngine", "GameObject");
    void* transform = g_api.class_from_name(core, "UnityEngine", "Transform");
    void* camera = g_api.class_from_name(core, "UnityEngine", "Camera");
    void* component = g_api.class_from_name(core, "UnityEngine", "Component");
    void* resources = g_api.class_from_name(core, "UnityEngine", "Resources");
    U.tmpClass = g_api.class_from_name(tmp, "TMPro", "TextMeshPro");
    void* tmpText = g_api.class_from_name(tmp, "TMPro", "TMP_Text");
    void* fontAsset = g_api.class_from_name(tmp, "TMPro", "TMP_FontAsset");
    if (!U.objectClass || !U.goClass || !transform || !camera || !component || !resources || !U.tmpClass || !tmpText || !fontAsset) {
        logf("runtime: a Unity / TextMeshPro class is missing: labels disabled");
        return false;
    }
    U.cachedPtr = find_field(U.objectClass, "m_CachedPtr", false);
    U.goCtor = method(U.goClass, ".ctor", 1);
    U.goAddComponent = method(U.goClass, "AddComponent", 1);
    U.goSetActive = method(U.goClass, "SetActive", 1);
    U.goGetTransform = method(U.goClass, "get_transform", 0);
    U.trGetPos = method(transform, "get_position", 0);
    U.trSetPos = method(transform, "set_position", 1);
    U.trGetRot = method(transform, "get_rotation", 0);
    U.trSetRot = method(transform, "set_rotation", 1);
    U.trGetUp = method(transform, "get_up", 0);
    U.trGetForward = method(transform, "get_forward", 0);
    U.trSetParent = method(transform, "SetParent", 2);
    U.trSetLocalPos = method(transform, "set_localPosition", 1);
    U.trSetLocalScale = method(transform, "set_localScale", 1);
    U.trGetLossyScale = method(transform, "get_lossyScale", 0);
    U.camGetMain = method(camera, "get_main", 0);
    U.compGetTransform = method(component, "get_transform", 0);
    U.objDestroy = method(U.objectClass, "Destroy", 1);
    U.objGetName = method(U.objectClass, "get_name", 0);
    U.tmpSetText = method(tmpText, "set_text", 1);
    U.tmpSetFontSize = method(tmpText, "set_fontSize", 1);
    U.tmpSetAlignment = method(tmpText, "set_alignment", 1);
    U.tmpSetColor = method(tmpText, "set_color", 1);
    U.tmpSetFont = method(tmpText, "set_font", 1);
    U.tmpSetOverlay = method(tmpText, "set_isOverlay", 1, false);          // optional: draw over the scene
    U.tmpSetOutlineWidth = method(tmpText, "set_outlineWidth", 1, false);  // optional: readability
    for (void* m : {U.goCtor, U.goAddComponent, U.goSetActive, U.goGetTransform, U.trGetPos, U.trSetPos, U.trGetRot,
                    U.trSetRot, U.trGetUp, U.trGetForward, U.camGetMain, U.compGetTransform, U.objDestroy,
                    U.objGetName, U.tmpSetText, U.tmpSetFontSize, U.tmpSetAlignment, U.tmpSetColor,
                    U.tmpSetFont, U.trSetParent, U.trSetLocalPos, U.trSetLocalScale, U.trGetLossyScale})
        if (!m) { logf("runtime: labels disabled (a required method is missing)"); return false; }
    if (!U.cachedPtr) { logf("runtime: UnityEngine.Object.m_CachedPtr missing: labels disabled"); return false; }
    U.tmpType = g_api.gchandle_new(g_api.type_get_object(g_api.class_get_type(U.tmpClass)), false);
    U.fontAssetType = g_api.gchandle_new(g_api.type_get_object(g_api.class_get_type(fontAsset)), false);
    U.ok = true;
    return true;
}

static std::string object_name(void* obj) {
    void* s = call(U.objGetName, obj, {});
    wchar_t w[128];
    if (!s || safe_string(s, w, 128) < 0) return "?";
    char u[256];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr);
    return u;
}

// TextMeshPro's own default font, read from memory (TMP_Settings.s_Instance.m_defaultFontAsset): no Unity call.
// (0.2.0 searched every loaded asset with Resources.FindObjectsOfTypeAll; that call faulted inside the game.)
// When it is not available, the label keeps the font TextMeshPro gives a new component by itself.
static void* g_tmpSettingsClass = nullptr;
static void find_font() {
    U.fontSearched = true;
    if (!g_tmpSettingsClass) g_tmpSettingsClass = g_api.class_from_name(image_named("Unity.TextMeshPro.dll"), "TMPro", "TMP_Settings");
    const FieldDesc* inst = find_field_any(g_tmpSettingsClass, {"s_Instance", "s_instance"}, true);
    const FieldDesc* def = find_field_any(g_tmpSettingsClass, {"m_defaultFontAsset"});
    void* settings = nullptr;
    if (!inst || !def || !safe_static_get(inst->field, &settings) || !plausible(settings)) {
        logf("runtime: TMP_Settings default font not readable: labels use TextMeshPro's own default");
        return;
    }
    void* font = safe_ptr_at(settings, def->offset);
    if (!alive(font)) { logf("runtime: TMP_Settings has no default font: labels use TextMeshPro's own default"); return; }
    U.font = g_api.gchandle_new(font, false);
    logf("runtime: label font %s (TextMeshPro default)", object_name(font).c_str());
}
// ---------------------------------------------------------------------------------------------------- labels --
struct LabelStyle { float height = 1.8f, size = 4.0f, outline = 0.25f; };
static LabelStyle g_style;
// A label is a child of its entity's transform: it follows the entity with no per-frame position work, and is
// destroyed with it. Its world size is size x the style's font size, whatever the entity's own scale.
struct Label {
    void *go = nullptr, *tr = nullptr, *tmp = nullptr;  // GC handles (pointer-sized)
    std::string text; float color[4] = {1, 1, 1, 1}; bool dirty = true;
    int round = -1; bool active = false; void* display = nullptr;
    void* parent = nullptr; float size = 1.0f, parentScale = 1.0f; double scaleAt = -1e9; bool layoutDirty = true;
};
static const size_t MAX_LABELS = 300;
static long long g_labelsCreated = 0, g_labelsDestroyed = 0;
static std::unordered_map<uint32_t, Label> g_labels;
static int g_round = 0;
static volatile bool g_labelsEnabled = true;

static void free_label(Label& l) {
    void* go = target(l.go);
    if (alive(go)) call(U.objDestroy, nullptr, {go});
    if (l.go) g_labelsDestroyed++;
    for (void* h : {l.go, l.tr, l.tmp}) if (h) g_api.gchandle_free(h);
    l.go = l.tr = l.tmp = nullptr;
    l.parent = nullptr;
}
static void set_active(Label& l, bool on) {
    if (l.active == on) return;
    void* go = target(l.go);
    if (alive(go)) { bool b = on; call(U.goSetActive, go, {&b}); }
    l.active = on;
}
static bool create_label(Label& l) {
    void* go = g_api.object_new(U.goClass);
    bool ok = false;
    call(U.goCtor, go, {g_api.string_new("ZA_HP_Label")}, &ok);
    if (!ok) return false;
    void* tmp = call(U.goAddComponent, go, {target(U.tmpType)});
    void* tr = call(U.goGetTransform, go, {});
    if (!tmp || !tr) { call(U.objDestroy, nullptr, {go}); return false; }
    if (U.font) call(U.tmpSetFont, tmp, {target(U.font)});
    float size = g_style.size; call(U.tmpSetFontSize, tmp, {&size});
    int align = 514; call(U.tmpSetAlignment, tmp, {&align});  // TextAlignmentOptions.Center
    if (U.tmpSetOutlineWidth) { float w = g_style.outline; call(U.tmpSetOutlineWidth, tmp, {&w}); }
    // after the outline: the outline makes a material instance, and overlay (no depth test, queue 4000) must be on it
    if (U.tmpSetOverlay) { bool b = true; call(U.tmpSetOverlay, tmp, {&b}); }
    l.go = g_api.gchandle_new(go, false);
    l.tr = g_api.gchandle_new(tr, false);
    l.tmp = g_api.gchandle_new(tmp, false);
    l.active = true; l.dirty = true; l.layoutDirty = true; l.parent = nullptr; l.scaleAt = -1e9;
    g_labelsCreated++;
    return true;
}

// ----------------------------------------------------------------------------------------------- Lua mods --
// A mod setting from its manifest (za.mod): 'toggle' (num 0 / 1), 'slider' (num in min..max by step), 'key' (num:
// a virtual-key code), 'info' (str: read-only text the mod updates)
struct ModSetting { std::string id, type, label, str, lock; double num = 0, min = 0, max = 1, step = 0; std::vector<std::string> options; };
struct LuaMod {
    std::string name;  // the mod's file name without .lua
    lua_State* L = nullptr; std::vector<int> onFrame, onTick; int errors = 0; bool disabled = false;
    // manifest (za.mod) and state for the loader's notifications and settings tab
    bool manifest = false; std::string title, description, version;
    std::vector<ModSetting> settings; int onSettings = LUA_NOREF;
    int status = 0; std::string error;  // 0 ok, 1 warning, 2 error
    double costSum = 0, costMax = 0; int costCalls = 0;  // time in its callbacks, for the stats line
};
static std::vector<LuaMod> g_mods;
static std::vector<EntityInfo> g_entities;
static std::unordered_map<uint32_t, void*> g_displayById;
static bool g_inMission = false;

static LuaMod* mod_of(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "za_mod_index");
    int i = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return (i >= 0 && i < (int)g_mods.size()) ? &g_mods[i] : nullptr;
}
static int za_log(lua_State* L) {
    LuaMod* m = mod_of(L);
    logf("[%s] %s", m ? m->name.c_str() : "loader", luaL_checkstring(L, 1));
    return 0;
}
static int za_on_frame(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (LuaMod* m = mod_of(L)) m->onFrame.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
// za.on_tick(fn): like za.on_frame, but also outside missions (title, menus, loading screens), every 100 ms
static int za_on_tick(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (LuaMod* m = mod_of(L)) m->onTick.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
static int za_in_mission(lua_State* L) { lua_pushboolean(L, g_inMission); return 1; }
// za.entities([type]) -> {{id, type, dead, hp, max_hp, reserve, shield, shield_max}, ...}  (read-only snapshot)
static int za_entities(lua_State* L) {
    const char* filter = luaL_optstring(L, 1, nullptr);
    lua_newtable(L);
    int n = 0;
    for (const EntityInfo& e : g_entities) {
        const std::string& t = type_name_of(e.type);
        if (filter && t != filter) continue;
        lua_newtable(L);
        lua_pushinteger(L, e.id); lua_setfield(L, -2, "id");
        lua_pushstring(L, t.c_str()); lua_setfield(L, -2, "type");
        lua_pushboolean(L, e.dead); lua_setfield(L, -2, "dead");
        lua_pushboolean(L, e.hasHp); lua_setfield(L, -2, "has_health");
        lua_pushnumber(L, e.hp[0]); lua_setfield(L, -2, "hp");
        lua_pushnumber(L, e.hp[1]); lua_setfield(L, -2, "max_hp");
        lua_pushnumber(L, e.hp[2]); lua_setfield(L, -2, "reserve");
        lua_pushnumber(L, e.shield[0]); lua_setfield(L, -2, "shield");
        lua_pushnumber(L, e.shield[1]); lua_setfield(L, -2, "shield_max");
        if (filter && strcmp(filter, "Player") == 0) { push_object(L, e.display); lua_setfield(L, -2, "ref"); }  // players: their display
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
// za.label(id, text [, r, g, b [, a]]): show (or update) this mod's floating label over entity id for this round.
static int za_label(lua_State* L) {
    uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
    size_t len = 0;
    const char* text = luaL_checklstring(L, 2, &len);
    auto d = g_displayById.find(id);
    if (d == g_displayById.end()) return 0;  // not an entity of this mission (any more)
    if (g_labels.size() >= MAX_LABELS && !g_labels.count(id)) return 0;  // cap: the oldest labels keep their slots
    Label& l = g_labels[id];
    l.display = d->second;
    l.round = g_round;
    std::string s(text, std::min<size_t>(len, 120));
    if (s != l.text) { l.text = s; l.dirty = true; }
    for (int i = 0; i < 4; i++) {
        float c = (float)luaL_optnumber(L, 3 + i, 1.0);
        if (c != l.color[i]) { l.color[i] = c; l.dirty = true; }
    }
    float size = (float)luaL_optnumber(L, 7, 1.0);  // za.label(id, text, r, g, b, a, size): size x the style's font size
    size = size < 0.2f ? 0.2f : size > 8.0f ? 8.0f : size;
    if (size != l.size) { l.size = size; l.layoutDirty = true; }
    return 0;
}
// za.style{height = metres above the entity (screen-up), size = font size, outline = 0..1}
static int za_style(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "height"); if (lua_isnumber(L, -1)) g_style.height = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "size"); if (lua_isnumber(L, -1)) g_style.size = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    lua_getfield(L, 1, "outline"); if (lua_isnumber(L, -1)) g_style.outline = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    return 0;
}

// Mods are Lua scripts in ZAMods: <name>.lua, each declaring itself with za.mod{...} (a .lua file without it is not a
// mod and is skipped). Each runs in its own Lua state with a restricted standard library (no io, os, package, debug;
// no loading other code). Mods from before 0.8 (<name>.zamod, the same Lua) are renamed to .lua on the way in.
static std::string read_file(const std::wstring& path) {
    std::string out;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}
static void migrate_zamod(const std::wstring& dir) {  // <name>.zamod -> <name>.lua (kept if a .lua of that name exists)
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.zamod").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring from = dir + L"\\" + fd.cFileName;
        std::wstring to = from.substr(0, from.size() - 6) + L".lua";
        if (MoveFileExW(from.c_str(), to.c_str(), 0)) logf("mods: renamed %ls to .lua", fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void load_mods() {
    migrate_zamod(g_modsDir);
    migrate_zamod(g_modsDir + L"\\disabled");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_modsDir + L"\\*.lua").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring file = fd.cFileName;
        std::wstring stem = file.substr(0, file.size() - 4);
        char name[260];
        WideCharToMultiByte(CP_UTF8, 0, stem.c_str(), -1, name, sizeof name, nullptr, nullptr);
        std::string src = read_file(g_modsDir + L"\\" + file);
        if (src.find("za.mod") == std::string::npos) { logf("mods: %s.lua skipped (no za.mod{...}: not a mod)", name); continue; }
        g_mods.push_back({name});
        LuaMod& m = g_mods.back();
        lua_State* L = luaL_newstate();
        m.L = L;
        luaL_requiref(L, "_G", luaopen_base, 1);
        luaL_requiref(L, "table", luaopen_table, 1);
        luaL_requiref(L, "string", luaopen_string, 1);
        luaL_requiref(L, "math", luaopen_math, 1);
        luaL_requiref(L, "utf8", luaopen_utf8, 1);
        lua_pop(L, 5);
        for (const char* f : {"dofile", "loadfile", "load", "require", "collectgarbage"}) {
            lua_pushnil(L); lua_setglobal(L, f);
        }
        lua_pushinteger(L, (lua_Integer)(g_mods.size() - 1)); lua_setfield(L, LUA_REGISTRYINDEX, "za_mod_index");
        lua_newtable(L);
        const luaL_Reg api[] = {{"log", za_log}, {"on_frame", za_on_frame}, {"on_tick", za_on_tick}, {"entities", za_entities},
                                {"label", za_label}, {"style", za_style}, {"in_mission", za_in_mission}, {nullptr, nullptr}};
        luaL_setfuncs(L, api, 0);
        lua_pushstring(L, kLoaderVersion); lua_setfield(L, -2, "version");
        register_ui_api(L);   // za.static, za.time, za.ui.*, za.hud.*, and the read-only ref type
        register_mod_api(L);  // za.mod, za.setting, za.set_info, za.on_settings, za.notify, za.input
        lua_setglobal(L, "za");
        std::string chunk = "@" + std::string(name) + ".lua";
        if (src.empty() || luaL_loadbuffer(L, src.data(), src.size(), chunk.c_str()) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            std::string err = src.empty() ? "the file is empty or unreadable" : lua_tostring(L, -1);
            logf("[%s] failed to load: %s", name, err.c_str());
            m.disabled = true; m.status = 2; m.error = err;
            continue;
        }
        if (!m.manifest) { m.status = 1; m.error = "No manifest (za.mod): no name, description or settings"; }
        logf("mod loaded: %s%s%s (%zu frame callbacks, %zu settings)", name, m.manifest ? " = " : "", m.title.c_str(),
             m.onFrame.size(), m.settings.size());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static double now_ms();
static void run_mods(bool ticks = false) {
    for (LuaMod& m : g_mods) {
        if (m.disabled) continue;
        for (int ref : (ticks ? m.onTick : m.onFrame)) {
            lua_rawgeti(m.L, LUA_REGISTRYINDEX, ref);
            double t0 = now_ms();
            int rc = lua_pcall(m.L, 0, 0, 0);
            double dt = now_ms() - t0;
            m.costSum += dt; m.costCalls++; if (dt > m.costMax) m.costMax = dt;
            if (rc != LUA_OK) {
                logf("[%s] error: %s", m.name.c_str(), lua_tostring(m.L, -1));
                lua_pop(m.L, 1);
                if (++m.errors >= 10) {
                    m.disabled = true; m.status = 2; m.error = "Stopped after 10 errors";
                    logf("[%s] disabled after 10 errors", m.name.c_str());
                    push_event(2, (m.title.empty() ? m.name : m.title) + " stopped", "It hit 10 script errors; see ZAMods\\loader.log.");
                }
            }
        }
    }
}

// --------------------------------------------------------------------------------------------- frame tick --
static const double LUA_EVERY_MS = 100, MIN_TICK_MS = 8;
static LARGE_INTEGER g_qpf;
static double g_lastTick = 0, g_lastLua = 0, g_pauseUntil = 0;
static int g_faults = 0;
static const int MAX_FAULTS = 25;
static int g_ticks = 0;

static double now_ms() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / g_qpf.QuadPart; }

static void clear_labels() { for (auto& [id, l] : g_labels) free_label(l); g_labels.clear(); }

static const char* volatile g_stage = "start";  // the step the frame tick is in, for fault reports
#define STAGE(s) (g_stage = (s))

// Frame timing for the stats line: the longest gap between ticks (the game's frame time), ticks more than 100 ms apart
// (hitches), and the longest time the loader itself took in one tick.
static double g_gapMax = 0, g_costMax = 0;
static int g_hitches = 0;

// za.perf(): the game's frame rate from Unity's own counters (Time.frameCount, Time.unscaledDeltaTime), sampled every
// tick and published every half second: frames per second, average frame time, and the longest frame in that window
// (the loader ticks at most every 8 ms, so above 125 FPS some frames are skipped: they are the short ones).
static void *g_timeFrameCount = nullptr, *g_timeDelta = nullptr;
static int g_perfInit = 0;                       // 0 not tried, 1 ready, -1 unavailable
static double g_perfT0 = 0, g_perfWorstAcc = 0;
static int32_t g_perfF0 = 0;
static double g_perfFps = 0, g_perfAvgMs = 0, g_perfWorstMs = 0;
static bool g_perfHave = false;
static void perf_sample(double t) {
    if (g_perfInit == 0) {
        g_perfInit = -1;
        void* time = g_api.class_from_name(image_named("UnityEngine.CoreModule.dll"), "UnityEngine", "Time");
        g_timeFrameCount = method(time, "get_frameCount", 0, false);
        g_timeDelta = method(time, "get_unscaledDeltaTime", 0, false);
        if (g_timeFrameCount && g_timeDelta) g_perfInit = 1; else logf("runtime: UnityEngine.Time not found: za.perf unavailable");
    }
    if (g_perfInit < 0) return;
    int32_t fc = 0;
    float dt = 0;
    if (!unbox_into(call(g_timeFrameCount, nullptr, {}), &fc, 4) || !unbox_into(call(g_timeDelta, nullptr, {}), &dt, 4)) return;
    g_perfWorstAcc = fmax(g_perfWorstAcc, dt * 1000.0);
    if (g_perfT0 == 0 || fc < g_perfF0 || t - g_perfT0 > 5000) { g_perfT0 = t; g_perfF0 = fc; g_perfWorstAcc = 0; return; }
    if (t - g_perfT0 >= 500 && fc > g_perfF0) {
        g_perfFps = (fc - g_perfF0) * 1000.0 / (t - g_perfT0);
        g_perfAvgMs = 1000.0 / g_perfFps;
        g_perfWorstMs = fmax(g_perfWorstAcc, g_perfAvgMs);
        g_perfHave = true;
        g_perfT0 = t; g_perfF0 = fc; g_perfWorstAcc = 0;
    }
}
static int za_perf(lua_State* L) {
    if (!g_perfHave) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, g_perfFps); lua_setfield(L, -2, "fps");
    lua_pushnumber(L, g_perfAvgMs); lua_setfield(L, -2, "frame_ms");
    lua_pushnumber(L, g_perfWorstMs); lua_setfield(L, -2, "worst_ms");
    return 1;
}

static void frame_tick_body() {
    double t = now_ms();
    if (t - g_lastTick < MIN_TICK_MS || t < g_pauseUntil) return;
    if (g_lastTick > 0 && t - g_lastTick < 5000) {
        g_gapMax = fmax(g_gapMax, t - g_lastTick);
        if (t - g_lastTick > 100) g_hitches++;
    }
    g_lastTick = t;
    if (++g_ticks == 1) logf("runtime: first frame tick on the main thread");
    STAGE("perf");
    perf_sample(t);
    static double lastStats = 0;
    if (t - lastStats >= 60000) {  // memory check: these numbers should stay flat over a long mission
        lastStats = t;
        size_t active = 0, luaKb = 0;
        for (auto& kv : g_labels) active += kv.second.active;
        for (LuaMod& m : g_mods) if (m.L) luaKb += (size_t)lua_gc(m.L, LUA_GCCOUNT, 0);
        logf("stats: %zu labels (%zu shown, cap %zu), %lld created / %lld destroyed this session, %zu entities, Lua %zu KB, %d faults; "
             "frames: longest %.0f ms, %d over 100 ms, loader's longest tick %.1f ms",
             g_labels.size(), active, MAX_LABELS, g_labelsCreated, g_labelsDestroyed, g_entities.size(), luaKb, g_faults,
             g_gapMax, g_hitches, g_costMax);
        g_gapMax = g_costMax = 0; g_hitches = 0;
        // each mod's share: average and longest time per call of its callbacks, over the same minute
        std::string per;
        for (LuaMod& m : g_mods) {
            if (!m.costCalls) continue;
            char b[160];
            snprintf(b, sizeof b, "%s%s %.2f / %.1f", per.empty() ? "" : ", ", m.name.c_str(), m.costSum / m.costCalls, m.costMax);
            per += b;
            m.costSum = m.costMax = 0; m.costCalls = 0;
        }
        if (!per.empty()) logf("stats: mods, ms per update (average / longest): %s", per.c_str());
    }
    STAGE("loader (input, system script)");
    loader_tick(t);
    STAGE("ui tweens");
    ui_tick(t);

    if (t - g_lastLua >= LUA_EVERY_MS) {
        g_lastLua = t;
        STAGE("scan_entities");
        bool mission = scan_entities_guarded(g_entities);
        if (mission != g_inMission) {
            g_inMission = mission;
            if (mission) logf("runtime: mission detected (%zu entities)", g_entities.size()); else logf("runtime: no mission");
            set_canvas_visible(mission);  // the mods' overlay shows in missions only
            clear_labels();
            if (U.font) { void* h = U.font; U.font = nullptr; g_api.gchandle_free(h); }  // cleared first: never freed twice
            U.fontSearched = false;
        }
        g_displayById.clear();
        for (const EntityInfo& e : g_entities) g_displayById[e.id] = e.display;
        STAGE("run_mods (Lua ticks)");
        run_mods(true);
        if (!mission) return;
        STAGE("find_font");
        if (U.ok && !U.fontSearched) find_font();
        g_round++;
        STAGE("run_mods (Lua)");
        run_mods();
        STAGE("hide unused labels");
        // Labels nobody refreshed this round: hidden (kept for reuse); gone entities: destroyed.
        for (auto it = g_labels.begin(); it != g_labels.end();) {
            Label& l = it->second;
            if (!g_displayById.count(it->first)) { free_label(l); it = g_labels.erase(it); continue; }
            if (l.round != g_round || !g_labelsEnabled) set_active(l, false);
            ++it;
        }
    }
    if (!g_inMission || !U.ok || !g_labelsEnabled) return;

    // Every tick: create / attach / face-the-camera the round's labels. The only per-frame allocation is the camera's
    // rotation (one boxed value); each label's position comes from being a child of its entity's transform.
    STAGE("camera");
    void* cam = call(U.camGetMain, nullptr, {});
    if (!alive(cam)) return;
    void* camTr = call(U.compGetTransform, cam, {});
    Quat rot{};
    if (!unbox_into(call(U.trGetRot, camTr, {}), &rot, sizeof rot)) return;
    for (auto& [id, l] : g_labels) {
        if (l.round != g_round) continue;
        STAGE("create_label");
        if (!alive(target(l.go)) && !create_label(l)) continue;
        STAGE("attach label");
        void* entityTr = safe_ptr_at(l.display, g_layout.transform->offset);
        if (!alive(entityTr)) { set_active(l, false); continue; }
        void* tr = target(l.tr);
        if (entityTr != l.parent) {  // first frame, or the game moved this entity to another (pooled) display
            remove_stray_labels(entityTr, target(l.go));
            bool keepWorld = false;
            call(U.trSetParent, tr, {entityTr, &keepWorld});
            l.parent = entityTr; l.layoutDirty = true; l.scaleAt = -1e9;
        }
        if (t - l.scaleAt >= 1000) {  // the entity's own scale, re-read once a second
            l.scaleAt = t;
            Vec3 s{};
            if (unbox_into(call(U.trGetLossyScale, entityTr, {}), &s, sizeof s) && s.x > 1e-4f &&
                fabsf(s.x - l.parentScale) > 0.01f * l.parentScale) {
                l.parentScale = s.x; l.layoutDirty = true;
            }
        }
        if (l.layoutDirty) {  // undo the entity's scale, then apply this label's own size and height
            l.layoutDirty = false;
            float k = l.size / l.parentScale;
            // height in world units above the entity, independent of label size (bigger labels sit a little higher)
            float h = g_style.height * (l.size > 1.0f ? l.size : 1.0f) / l.parentScale;
            Vec3 scale{k, k, k}, local{0.0f, h, 0.0f};
            call(U.trSetLocalScale, tr, {&scale});
            call(U.trSetLocalPos, tr, {&local});
        }
        call(U.trSetRot, tr, {&rot});
        STAGE("set label text");
        if (l.dirty) {  // text or colour changed
            l.dirty = false;
            call(U.tmpSetText, target(l.tmp), {g_api.string_new(l.text.c_str())});
            call(U.tmpSetColor, target(l.tmp), {l.color});
        }
        set_active(l, true);
    }
}

static volatile bool g_runtimeReady = false, g_runtimeFailed = false;
static DWORD g_mainThread = 0;

static DWORD g_faultCode = 0; static void* g_faultAt = nullptr; static ULONG_PTR g_faultData[2] = {};
static LONG fault_filter(EXCEPTION_POINTERS* ep) {
    g_faultCode = ep->ExceptionRecord->ExceptionCode;
    g_faultAt = ep->ExceptionRecord->ExceptionAddress;
    g_faultData[0] = ep->ExceptionRecord->NumberParameters > 0 ? ep->ExceptionRecord->ExceptionInformation[0] : 0;
    g_faultData[1] = ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
    return EXCEPTION_EXECUTE_HANDLER;
}
static void report_fault() {
    HMODULE mod = nullptr; char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)g_faultAt, &mod))
        GetModuleFileNameA(mod, name, MAX_PATH);
    const char* base = strrchr(name, '\\'); base = base ? base + 1 : name;
    logf("runtime: FAULT 0x%08lx in step '%s' at %s+0x%llx (%s address 0x%llx): pausing 2 s (fault %d of %d allowed)",
         g_faultCode, (const char*)g_stage, base, (unsigned long long)((uint8_t*)g_faultAt - (uint8_t*)mod),
         g_faultData[0] == 1 ? "writing" : g_faultData[0] == 8 ? "executing" : "reading", (unsigned long long)g_faultData[1], g_faults + 1, MAX_FAULTS);
}
static void release_label_handles() {
    for (auto& kv : g_labels)
        for (void* h : {kv.second.go, kv.second.tr, kv.second.tmp}) if (h) g_api.gchandle_free(h);
    g_labels.clear();
}
static void frame_tick() {  // no C++ objects here: a fault in the body disables the runtime instead of crashing
    double t0 = now_ms();
    __try { frame_tick_body(); g_costMax = fmax(g_costMax, now_ms() - t0); }
    __except (fault_filter(GetExceptionInformation())) {
        // Most faults happen while a scene loads or unloads: pause 2 s, forget the mission state, and try again.
        report_fault();
        g_pauseUntil = now_ms() + 2000;
        g_inMission = false;
        // Release every label's GC handles (0.2.1 dropped them: a leak). Their objects are children of entities, so
        // they go when the entities do; Unity is not called here, right after a fault.
        release_label_handles();
        if (++g_faults >= MAX_FAULTS) {
            g_runtimeFailed = true;
            logf("runtime: %d faults: disabled for this session", g_faults);
            fail_box("The loader hit repeated errors and switched itself off for this session (mods stopped).");
        }
    }
}

// ------------------------------------------------------------------------------------- PeekMessageW hook --
typedef BOOL(WINAPI* PeekMessageW_t)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMessageW_t g_realPeek = nullptr;
static bool g_inTick = false;

static BOOL WINAPI hook_peek(LPMSG msg, HWND hwnd, UINT a, UINT b, UINT remove) {
    if (g_runtimeReady && !g_runtimeFailed && !g_inTick && GetCurrentThreadId() == g_mainThread) {
        g_inTick = true;
        g_inMainThreadTick = true;
        frame_tick();
        g_inMainThreadTick = false;
        g_inTick = false;
    }
    BOOL got = g_realPeek(msg, hwnd, a, b, remove);
    if (got && (remove & PM_REMOVE) && msg) filter_message(msg);
    return got;
}

// Redirect one import-table entry of module (dll!function) to replacement. Data only: the module's code is unchanged.
static bool iat_hook(HMODULE module, const char* dll, const char* function, void* replacement, void** original) {
    auto base = (uint8_t*)module;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
        if (_stricmp((const char*)(base + imp->Name), dll) != 0) continue;
        auto names = (IMAGE_THUNK_DATA64*)(base + imp->OriginalFirstThunk);
        auto slots = (IMAGE_THUNK_DATA64*)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto byName = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (strcmp((const char*)byName->Name, function) != 0) continue;
            DWORD old = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) return false;
            *original = (void*)slots->u1.Function;
            slots->u1.Function = (ULONGLONG)replacement;
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

// Called on the loader thread once IL2CPP is ready.
static void start_runtime() {
    QueryPerformanceFrequency(&g_qpf);
    if (!init_layout()) { fail_box("This game version is not supported: the game's runner and entity data were not found."); return; }
    bool unity = init_unity();
    if (!unity) logf("runtime: labels unavailable; mods still get za.entities()");
    bool ui = unity && init_ui();
    load_mods();
    HMODULE player = GetModuleHandleW(L"UnityPlayer.dll");
    if (!player || !iat_hook(player, "USER32.dll", "PeekMessageW", (void*)hook_peek, (void**)&g_realPeek)) {
        fail_box("Could not attach to the game's main loop (UnityPlayer PeekMessageW): mods will not run.");
        return;
    }
    if (!ui || !load_system_script()) {
        fail_box(ui ? "The loader's own script failed to start: no notifications or MODS settings tab (mods still run)."
                    : "Unity UI was not found: mods that draw on screen, notifications and the MODS settings tab are unavailable.");
    }
    // notifications: the loader first, then one per mod
    int failed = 0;
    for (const LuaMod& m : g_mods) failed += m.status == 2;
    char body[160];
    snprintf(body, sizeof body, "ZARModManager %s · %zu mod%s loaded%s", kLoaderVersion, g_mods.size() - failed, g_mods.size() - failed == 1 ? "" : "s",
             failed ? " · some failed" : "");
    if (g_mods.empty()) push_event(1, "Loader Injected Successfully", "No mods found: put mods (.lua) in ZAMods (or import them with ZARModManager.exe).");
    else push_event(failed ? 1 : 0, "Loader Injected Successfully", body);
    if (!unity) push_event(1, "Loader: limited mode", "Unity text was not found: world labels are unavailable.");
    for (const LuaMod& m : g_mods) {
        std::string title = m.title.empty() ? m.name : m.title;
        if (!m.version.empty()) title += "  v" + m.version;
        if (m.status == 2) push_event(2, m.name + " failed to load", m.error.substr(0, 220));
        else if (m.status == 1) push_event(1, title, m.error);
        else push_event(0, title, m.description.empty() ? "Loaded." : m.description);
    }
    g_runtimeReady = true;
    logf("runtime: %zu mod(s) loaded; frame hook installed (main thread %lu). F7 toggles labels.", g_mods.size(), g_mainThread);
}
