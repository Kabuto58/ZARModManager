// ZARModManager UI layer (0.4.0), included by loader.cpp after runtime.inl.
//
//  1. za.static / ref:get / ref:at / ...: READ-ONLY reflection over the game's objects for Lua mods. Every read is
//     guarded; a getter call is limited to a fixed list of property getters (text, fill amount, active, sprite, ...).
//  2. za.ui.*: the loader's own screen canvas (1920 x 1080 reference, scaled), with text and image elements, generated
//     sprites (disc, rings, map frame, scrim), radial / horizontal fills, and the game's own sprites reused.
//  3. za.hud.*: layout changes to native HUD containers: scale (hide), circular map mask, hierarchy dump.
// Nothing here changes game state: the only objects created or modified are on-screen UI.

// ------------------------------------------------------------------------------------- Unity UI bindings --
struct UiBridge {
    bool ok = false;
    void *canvasClass = nullptr, *scalerClass = nullptr, *imageClass = nullptr, *maskClass = nullptr, *tmpUguiClass = nullptr,
         *texClass = nullptr, *spriteClass = nullptr, *rectTrClass = nullptr, *componentClass = nullptr;
    void *canvasType = nullptr, *scalerType = nullptr, *imageType = nullptr, *maskType = nullptr, *tmpUguiType = nullptr,
         *componentType = nullptr;  // GC handles of Type objects
    void *cvSetRenderMode = nullptr, *cvSetSortingOrder = nullptr;
    void *scSetMode = nullptr, *scSetRef = nullptr, *scSetMatch = nullptr;
    void *rtSetAnchorMin = nullptr, *rtSetAnchorMax = nullptr, *rtSetPivot = nullptr, *rtSetPos = nullptr, *rtSetSize = nullptr;
    void *trGetParent = nullptr, *trChildCount = nullptr, *trGetChild = nullptr, *trGetLocalScale = nullptr, *trSetAsLast = nullptr;
    void *imgSetSprite = nullptr, *imgGetSprite = nullptr, *imgSetType = nullptr, *imgSetFillMethod = nullptr,
         *imgSetFillOrigin = nullptr, *imgSetFillClockwise = nullptr, *imgSetFillAmount = nullptr, *imgGetFillAmount = nullptr,
         *imgSetPreserveAspect = nullptr;
    void *gfxSetColor = nullptr, *gfxSetRaycast = nullptr;
    void *maskSetShow = nullptr;
    void *tmpGetText = nullptr, *tmpGetFont = nullptr, *tmpSetOutlineColor = nullptr;
    void *texCtor = nullptr, *texLoadRaw = nullptr, *texApply = nullptr, *texSetFilter = nullptr, *texSetWrap = nullptr;
    void *spriteCreate = nullptr;
    void *objDontDestroy = nullptr;
    void *compGetGameObject = nullptr, *goActiveInHierarchy = nullptr, *goGetComponents = nullptr, *behGetEnabled = nullptr;
    void *compInChildren = nullptr, *tmpTextType = nullptr;  // optional: ref:texts()
    void *rectTrType = nullptr, *behaviourType = nullptr, *canvasClassType = nullptr;   // GC handles of Type objects
    void *behSetEnabled = nullptr, *trGetSiblingIndex = nullptr, *trSetSiblingIndex = nullptr, *objInstantiate = nullptr,
         *objSetName = nullptr, *findObjectsByType = nullptr, *compInParent = nullptr, *cvGetRoot = nullptr,
         *cvGetRenderMode = nullptr, *cvGetCamera = nullptr, *rtContains = nullptr, *rtGetRect = nullptr,
         *rtGetPos = nullptr, *tmpSetWrap = nullptr, *tmpPreferred = nullptr, *raycasterType = nullptr, *cgGetAlpha = nullptr,
         *cgSetAlpha = nullptr, *canvasGroupType = nullptr;
    void *trTransformPoint = nullptr, *trInverseTransformPoint = nullptr;  // optional: za.hud.rect
    void *graphicType = nullptr;                                            // optional: za.hud.content_rect
    void *rtGetAnchorMin = nullptr, *rtGetAnchorMax = nullptr;                // optional: za.hud.nudge_anchor
    void *findObjectsAll = nullptr;                                         // optional: FindObjectsByType, inactive too
};
static UiBridge UI;

// A method by name and exact parameter type names (for overloaded methods with the same parameter count).
static void* method_exact(void* klass, const char* name, std::initializer_list<const char*> params) {
    for (void* c = klass; c; c = g_api.class_get_parent(c)) {
        void* iter = nullptr;
        while (void* m = g_api.class_get_methods(c, &iter)) {
            if (strcmp(g_api.method_get_name(m), name) != 0 || g_api.method_get_param_count(m) != params.size()) continue;
            bool match = true; uint32_t i = 0;
            for (const char* p : params)
                if (type_name(g_api.method_get_param(m, i++)) != p) { match = false; break; }
            if (match) return m;
        }
    }
    logf("ui: method %s with %zu exact parameters not found", name, params.size());
    return nullptr;
}

static void* type_handle(void* klass) {
    return klass ? g_api.gchandle_new(g_api.type_get_object(g_api.class_get_type(klass)), false) : nullptr;
}

static bool init_ui() {
    if (!U.ok) return false;
    void* core = image_named("UnityEngine.CoreModule.dll");
    void* uiModule = image_named("UnityEngine.UIModule.dll");
    void* ugui = image_named("UnityEngine.UI.dll");
    void* tmp = image_named("Unity.TextMeshPro.dll");
    if (!core || !uiModule || !ugui || !tmp) { logf("ui: UnityEngine.UIModule / UnityEngine.UI not loaded: za.ui disabled"); return false; }
    UI.canvasClass = g_api.class_from_name(uiModule, "UnityEngine", "Canvas");
    UI.scalerClass = g_api.class_from_name(ugui, "UnityEngine.UI", "CanvasScaler");
    UI.imageClass = g_api.class_from_name(ugui, "UnityEngine.UI", "Image");
    UI.maskClass = g_api.class_from_name(ugui, "UnityEngine.UI", "Mask");
    void* graphic = g_api.class_from_name(ugui, "UnityEngine.UI", "Graphic");
    void* behaviour = g_api.class_from_name(core, "UnityEngine", "Behaviour");
    UI.tmpUguiClass = g_api.class_from_name(tmp, "TMPro", "TextMeshProUGUI");
    void* tmpText = g_api.class_from_name(tmp, "TMPro", "TMP_Text");
    UI.texClass = g_api.class_from_name(core, "UnityEngine", "Texture2D");
    void* texture = g_api.class_from_name(core, "UnityEngine", "Texture");
    UI.spriteClass = g_api.class_from_name(core, "UnityEngine", "Sprite");
    UI.rectTrClass = g_api.class_from_name(core, "UnityEngine", "RectTransform");
    void* transform = g_api.class_from_name(core, "UnityEngine", "Transform");
    UI.componentClass = g_api.class_from_name(core, "UnityEngine", "Component");
    if (!UI.canvasClass || !UI.scalerClass || !UI.imageClass || !UI.maskClass || !graphic || !behaviour || !UI.tmpUguiClass ||
        !tmpText || !UI.texClass || !texture || !UI.spriteClass || !UI.rectTrClass || !transform || !UI.componentClass) {
        logf("ui: a Unity UI class is missing: za.ui disabled");
        return false;
    }
    UI.cvSetRenderMode = method(UI.canvasClass, "set_renderMode", 1);
    UI.cvSetSortingOrder = method(UI.canvasClass, "set_sortingOrder", 1);
    UI.scSetMode = method(UI.scalerClass, "set_uiScaleMode", 1);
    UI.scSetRef = method(UI.scalerClass, "set_referenceResolution", 1);
    UI.scSetMatch = method(UI.scalerClass, "set_matchWidthOrHeight", 1);
    UI.rtSetAnchorMin = method(UI.rectTrClass, "set_anchorMin", 1);
    UI.rtSetAnchorMax = method(UI.rectTrClass, "set_anchorMax", 1);
    UI.rtSetPivot = method(UI.rectTrClass, "set_pivot", 1);
    UI.rtSetPos = method(UI.rectTrClass, "set_anchoredPosition", 1);
    UI.rtSetSize = method(UI.rectTrClass, "set_sizeDelta", 1);
    UI.trGetParent = method(transform, "get_parent", 0);
    UI.trChildCount = method(transform, "get_childCount", 0);
    UI.trGetChild = method(transform, "GetChild", 1);
    UI.trGetLocalScale = method(transform, "get_localScale", 0);
    UI.trSetAsLast = method(transform, "SetAsLastSibling", 0);
    UI.imgSetSprite = method(UI.imageClass, "set_sprite", 1);
    UI.imgGetSprite = method(UI.imageClass, "get_sprite", 0);
    UI.imgSetType = method(UI.imageClass, "set_type", 1);
    UI.imgSetFillMethod = method(UI.imageClass, "set_fillMethod", 1);
    UI.imgSetFillOrigin = method(UI.imageClass, "set_fillOrigin", 1);
    UI.imgSetFillClockwise = method(UI.imageClass, "set_fillClockwise", 1);
    UI.imgSetFillAmount = method(UI.imageClass, "set_fillAmount", 1);
    UI.imgGetFillAmount = method(UI.imageClass, "get_fillAmount", 0);
    UI.imgSetPreserveAspect = method(UI.imageClass, "set_preserveAspect", 1);
    UI.gfxSetColor = method(graphic, "set_color", 1);
    UI.gfxSetRaycast = method(graphic, "set_raycastTarget", 1);
    UI.maskSetShow = method(UI.maskClass, "set_showMaskGraphic", 1);
    UI.tmpGetText = method(tmpText, "get_text", 0);
    UI.tmpGetFont = method(tmpText, "get_font", 0);
    UI.tmpSetOutlineColor = method(tmpText, "set_outlineColor", 1, false);
    UI.texCtor = method_exact(UI.texClass, ".ctor", {"System.Int32", "System.Int32", "UnityEngine.TextureFormat", "System.Boolean"});
    UI.texLoadRaw = method_exact(UI.texClass, "LoadRawTextureData", {"System.IntPtr", "System.Int32"});
    UI.texApply = method(UI.texClass, "Apply", 0);
    UI.texSetFilter = method(texture, "set_filterMode", 1);
    UI.texSetWrap = method(texture, "set_wrapMode", 1);
    UI.spriteCreate = method_exact(UI.spriteClass, "Create", {"UnityEngine.Texture2D", "UnityEngine.Rect", "UnityEngine.Vector2"});
    UI.objDontDestroy = method(U.objectClass, "DontDestroyOnLoad", 1);
    UI.compGetGameObject = method(UI.componentClass, "get_gameObject", 0);
    UI.goActiveInHierarchy = method(U.goClass, "get_activeInHierarchy", 0);
    UI.goGetComponents = method_exact(U.goClass, "GetComponents", {"System.Type"});
    UI.behGetEnabled = method(behaviour, "get_enabled", 0);
    UI.compInChildren = method_exact(UI.componentClass, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    UI.tmpTextType = type_handle(tmpText);
    // loader (pause-menu button, settings screen) and groups: optional, each checked where it is used
    UI.rectTrType = type_handle(UI.rectTrClass);
    UI.behaviourType = type_handle(behaviour);
    UI.canvasClassType = type_handle(UI.canvasClass);
    UI.behSetEnabled = method(behaviour, "set_enabled", 1, false);
    UI.trGetSiblingIndex = method(transform, "GetSiblingIndex", 0, false);
    UI.trSetSiblingIndex = method(transform, "SetSiblingIndex", 1, false);
    UI.objInstantiate = method_exact(U.objectClass, "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform"});
    UI.objSetName = method(U.objectClass, "set_name", 1, false);
    UI.findObjectsByType = method_exact(U.objectClass, "FindObjectsByType", {"System.Type", "UnityEngine.FindObjectsSortMode"});
    UI.findObjectsAll = method_exact(U.objectClass, "FindObjectsByType",
                                     {"System.Type", "UnityEngine.FindObjectsInactive", "UnityEngine.FindObjectsSortMode"});
    UI.compInParent = method_exact(UI.componentClass, "GetComponentInParent", {"System.Type"});
    UI.cvGetRoot = method(UI.canvasClass, "get_rootCanvas", 0, false);
    UI.cvGetRenderMode = method(UI.canvasClass, "get_renderMode", 0, false);
    UI.cvGetCamera = method(UI.canvasClass, "get_worldCamera", 0, false);
    if (void* rtu = g_api.class_from_name(uiModule, "UnityEngine", "RectTransformUtility"))
        UI.rtContains = method_exact(rtu, "RectangleContainsScreenPoint", {"UnityEngine.RectTransform", "UnityEngine.Vector2", "UnityEngine.Camera"});
    UI.rtGetRect = method(UI.rectTrClass, "get_rect", 0, false);
    UI.trTransformPoint = method_exact(transform, "TransformPoint", {"UnityEngine.Vector3"});
    UI.trInverseTransformPoint = method_exact(transform, "InverseTransformPoint", {"UnityEngine.Vector3"});
    UI.rtGetPos = method(UI.rectTrClass, "get_anchoredPosition", 0, false);
    UI.rtGetAnchorMin = method(UI.rectTrClass, "get_anchorMin", 0, false);
    UI.rtGetAnchorMax = method(UI.rectTrClass, "get_anchorMax", 0, false);
    UI.tmpSetWrap = method(tmpText, "set_textWrappingMode", 1, false);
    UI.tmpPreferred = method_exact(tmpText, "GetPreferredValues", {"System.String", "System.Single", "System.Single"});
    if (void* gr = g_api.class_from_name(ugui, "UnityEngine.UI", "GraphicRaycaster")) UI.raycasterType = type_handle(gr);
    if (graphic) UI.graphicType = type_handle(graphic);
    if (void* cg = g_api.class_from_name(uiModule, "UnityEngine", "CanvasGroup")) {
        UI.canvasGroupType = type_handle(cg);
        UI.cgGetAlpha = method(cg, "get_alpha", 0, false);
        UI.cgSetAlpha = method(cg, "set_alpha", 1, false);
    }
    for (void* m : {UI.cvSetRenderMode, UI.cvSetSortingOrder, UI.scSetMode, UI.scSetRef, UI.scSetMatch, UI.rtSetAnchorMin,
                    UI.rtSetAnchorMax, UI.rtSetPivot, UI.rtSetPos, UI.rtSetSize, UI.trGetParent, UI.trChildCount, UI.trGetChild,
                    UI.trGetLocalScale, UI.trSetAsLast, UI.imgSetSprite, UI.imgGetSprite, UI.imgSetType, UI.imgSetFillMethod,
                    UI.imgSetFillOrigin, UI.imgSetFillClockwise, UI.imgSetFillAmount, UI.imgGetFillAmount, UI.imgSetPreserveAspect,
                    UI.gfxSetColor, UI.gfxSetRaycast, UI.maskSetShow, UI.tmpGetText, UI.tmpGetFont, UI.texCtor, UI.texLoadRaw,
                    UI.texApply, UI.texSetFilter, UI.texSetWrap, UI.spriteCreate, UI.objDontDestroy, UI.compGetGameObject,
                    UI.goActiveInHierarchy, UI.goGetComponents, UI.behGetEnabled})
        if (!m) { logf("ui: a Unity UI method is missing: za.ui disabled"); return false; }
    UI.canvasType = type_handle(UI.canvasClass);
    UI.scalerType = type_handle(UI.scalerClass);
    UI.imageType = type_handle(UI.imageClass);
    UI.maskType = type_handle(UI.maskClass);
    UI.tmpUguiType = type_handle(UI.tmpUguiClass);
    UI.componentType = type_handle(UI.componentClass);
    UI.ok = true;
    logf("ui: za.ui ready (canvas, text, image, masks)");
    return true;
}

struct Vec2 { float x, y; };
struct Rect4 { float x, y, w, h; };
struct Color4 { float r, g, b, a; };

static void* g_transformClass = nullptr;
static void* get_transform(void* obj) {  // a Component or GameObject -> its Transform (or itself if it is one)
    if (!alive(obj)) return nullptr;
    if (!g_transformClass) g_transformClass = g_api.class_from_name(image_named("UnityEngine.CoreModule.dll"), "UnityEngine", "Transform");
    void* k = safe_object_class(obj);
    for (void* c = k; c; c = g_api.class_get_parent(c))
        if (c == g_transformClass) return obj;
    if (k == U.goClass) return call(U.goGetTransform, obj, {});
    return call(U.compGetTransform, obj, {});
}
static void* get_game_object(void* obj) {
    if (!alive(obj)) return nullptr;
    if (safe_object_class(obj) == U.goClass) return obj;
    return call(UI.compGetGameObject, obj, {});
}

// Labels whose handles were dropped (after a fault) stay on their entity; entity displays are pooled and reused, so such
// a stale label would show on another enemy. Before a label attaches to an entity, any other label there is destroyed.
static void remove_stray_labels(void* entityTr, void* keepGo) {
    int n = 0;
    if (!UI.trChildCount || !unbox_into(call(UI.trChildCount, entityTr, {}), &n, 4)) return;
    for (int i = n - 1; i >= 0; i--) {
        void* child = call(UI.trGetChild, entityTr, {&i});
        void* go = alive(child) ? get_game_object(child) : nullptr;
        if (go && go != keepGo && object_name(go) == "ZA_HP_Label") call(U.objDestroy, nullptr, {go});
    }
}

// --------------------------------------------------------------------------------------- generated sprites --
// White RGBA sprites (tinted by the Image colour), built once: disc, ring (4 px at 72 px), ring_thin, mapring (a thin
// ring with four bracket arcs at 45 degrees), scrim (soft radial fade).
static std::unordered_map<std::string, void*> g_sprites;  // name -> GC handle of the Sprite

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static void* make_sprite(const char* name, int n, float (*alphaAt)(float x, float y, float r)) {
    std::vector<uint8_t> px((size_t)n * n * 4);
    float r = n * 0.5f;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float a = clamp01(alphaAt(x + 0.5f - r, y + 0.5f - r, r));
            uint8_t* p = &px[((size_t)y * n + x) * 4];
            p[0] = p[1] = p[2] = 255; p[3] = (uint8_t)(a * 255.0f + 0.5f);
        }
    void* tex = g_api.object_new(UI.texClass);
    int w = n, h = n, fmt = 4 /* RGBA32 */; bool mip = false;
    bool ok = false;
    call(UI.texCtor, tex, {&w, &h, &fmt, &mip}, &ok);
    if (!ok) { logf("ui: Texture2D(%d) failed for sprite %s", n, name); return nullptr; }
    int filter = 1 /* Bilinear */, wrap = 1 /* Clamp */;
    call(UI.texSetFilter, tex, {&filter});
    call(UI.texSetWrap, tex, {&wrap});
    void* data = px.data(); int size = (int)px.size();
    call(UI.texLoadRaw, tex, {&data, &size}, &ok);
    if (!ok) { logf("ui: LoadRawTextureData failed for sprite %s", name); return nullptr; }
    call(UI.texApply, tex, {});
    Rect4 rect{0, 0, (float)n, (float)n};
    Vec2 pivot{0.5f, 0.5f};
    void* sprite = call(UI.spriteCreate, nullptr, {tex, &rect, &pivot}, &ok);
    if (!ok || !sprite) { logf("ui: Sprite.Create failed for %s", name); return nullptr; }
    void* h2 = g_api.gchandle_new(sprite, false);
    g_api.gchandle_new(tex, false);  // keep the texture alive with the sprite
    g_sprites[name] = h2;
    return h2;
}
static float a_disc(float x, float y, float r) { return r - 1.0f - sqrtf(x * x + y * y) + 0.5f; }
static float a_ring(float x, float y, float r, float t) {
    float d = sqrtf(x * x + y * y);
    return std::min(r - 1.0f - d + 0.5f, d - (r - 1.0f - t) + 0.5f);
}
static float a_ring4(float x, float y, float r) { return a_ring(x, y, r, r * 2.0f * 4.0f / 72.0f); }
static float a_ring2(float x, float y, float r) { return a_ring(x, y, r, r * 2.0f * 2.0f / 72.0f); }
static float a_mapring(float x, float y, float r) {
    float thin = a_ring(x, y, r - 3.0f, 1.5f) * 0.55f;
    float ang = atan2f(y, x) * 57.29578f;  // -180..180
    float best = 999;
    for (float c : {45.0f, 135.0f, -45.0f, -135.0f}) { float d = fabsf(ang - c); best = std::min(best, d); }
    float arc = best < 17.0f ? a_ring(x, y, r, 3.5f) : 0.0f;
    return std::max(thin, arc);
}
static float a_white(float, float, float) { return 1.0f; }
static float a_scrim(float x, float y, float r) { float d = sqrtf(x * x + y * y) / r; return 0.5f * clamp01(1.0f - d) * clamp01(1.0f - d) * 2.0f; }

static float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = clamp01((wx * vx + wy * vy) / (vx * vx + vy * vy));
    float dx = wx - vx * t, dy = wy - vy * t;
    return sqrtf(dx * dx + dy * dy);
}
// symbols (y up): a check mark, a cross, and a warning triangle with the "!" cut out
static float a_check(float x, float y, float r) {
    float t = r * 0.12f;
    float d = std::min(seg_dist(x, y, -0.55f * r, 0.02f * r, -0.15f * r, -0.4f * r), seg_dist(x, y, -0.15f * r, -0.4f * r, 0.6f * r, 0.45f * r));
    return t - d + 0.5f;
}
static float a_cross(float x, float y, float r) {
    float t = r * 0.12f, k = 0.5f * r;
    float d = std::min(seg_dist(x, y, -k, -k, k, k), seg_dist(x, y, -k, k, k, -k));
    return t - d + 0.5f;
}
static float edge_in(float x, float y, float x1, float y1, float x2, float y2) {  // signed distance to an edge, + inside (ccw)
    float ex = x2 - x1, ey = y2 - y1, len = sqrtf(ex * ex + ey * ey);
    return (ex * (y - y1) - ey * (x - x1)) / len;
}
static float a_warn(float x, float y, float r) {
    // the triangle (0, .85r) (-.9r, -.72r) (.9r, -.72r), counter-clockwise, softened at the edges
    float ax = 0, ay = 0.85f * r, bx = -0.9f * r, by = -0.72f * r, cx = 0.9f * r, cy = -0.72f * r;
    float inside = std::min({edge_in(x, y, ax, ay, bx, by), edge_in(x, y, bx, by, cx, cy), edge_in(x, y, cx, cy, ax, ay)});
    float tri = clamp01(inside + 0.5f);
    float bar = clamp01(0.075f * r - seg_dist(x, y, 0, 0.42f * r, 0, -0.18f * r) + 0.5f);
    float dot = clamp01(0.09f * r - sqrtf(x * x + (y + 0.45f * r) * (y + 0.45f * r)) + 0.5f);
    return tri * (1.0f - std::max(bar, dot));
}

static void* sprite_named(const char* name) {
    auto it = g_sprites.find(name);
    if (it != g_sprites.end()) return target(it->second);
    void* h = nullptr;
    if (!strcmp(name, "disc")) h = make_sprite(name, 256, a_disc);
    else if (!strcmp(name, "ring")) h = make_sprite(name, 256, a_ring4);
    else if (!strcmp(name, "ring_thin")) h = make_sprite(name, 256, a_ring2);
    else if (!strcmp(name, "mapring")) h = make_sprite(name, 512, a_mapring);
    else if (!strcmp(name, "scrim")) h = make_sprite(name, 256, a_scrim);
    else if (!strcmp(name, "check")) h = make_sprite(name, 128, a_check);
    else if (!strcmp(name, "white")) h = make_sprite(name, 8, a_white);
    else if (!strcmp(name, "cross")) h = make_sprite(name, 128, a_cross);
    else if (!strcmp(name, "warn")) h = make_sprite(name, 128, a_warn);
    return h ? target(h) : nullptr;
}

// ------------------------------------------------------------------------------------------- the canvas --
static void* g_canvasGo = nullptr;  // GC handle
static void* g_canvasTr = nullptr;  // GC handle
static int g_uiGeneration = 0;
static void* g_hudFont = nullptr;   // GC handle: the game's own HUD font, taken from a native HUD text

// Layer 0: the mods' overlay (shown in missions only). Layer 1: the loader's own (notifications, the settings
// screen), always shown and above everything.
static void* g_sysCanvasGo = nullptr;  // GC handle
static void* g_sysCanvasTr = nullptr;  // GC handle
static bool make_canvas(const char* name, int order, void*& goH, void*& trH) {
    void* go = g_api.object_new(U.goClass);
    bool ok = false;
    call(U.goCtor, go, {g_api.string_new(name)}, &ok);
    if (!ok) return false;
    void* canvas = call(U.goAddComponent, go, {target(UI.canvasType)});
    void* scaler = call(U.goAddComponent, go, {target(UI.scalerType)});
    if (!canvas || !scaler) { call(U.objDestroy, nullptr, {go}); logf("ui: canvas creation failed"); return false; }
    int mode = 0 /* ScreenSpaceOverlay */;
    call(UI.cvSetRenderMode, canvas, {&mode});
    call(UI.cvSetSortingOrder, canvas, {&order});
    int scaleMode = 1 /* ScaleWithScreenSize */; Vec2 ref{1920, 1080}; float match = 1.0f /* height */;
    call(UI.scSetMode, scaler, {&scaleMode});
    call(UI.scSetRef, scaler, {&ref});
    call(UI.scSetMatch, scaler, {&match});
    call(UI.objDontDestroy, nullptr, {go});
    if (goH) { g_api.gchandle_free(goH); g_api.gchandle_free(trH); }
    goH = g_api.gchandle_new(go, false);
    trH = g_api.gchandle_new(call(U.goGetTransform, go, {}), false);
    g_uiGeneration++;
    logf("ui: canvas %s created (generation %d)", name, g_uiGeneration);
    return true;
}
static bool ensure_canvas(int layer = 0) {
    if (!UI.ok) return false;
    void*& go = layer ? g_sysCanvasGo : g_canvasGo;
    void*& tr = layer ? g_sysCanvasTr : g_canvasTr;
    if (go && alive(target(go))) return true;
    if (!layer) return make_canvas("ZARModManager_HUD", 100, go, tr);
    if (!make_canvas("ZARModManager_System", 32000, go, tr)) return false;
    if (UI.raycasterType) call(U.goAddComponent, target(go), {target(UI.raycasterType)});
    return true;
}
static void set_canvas_visible(bool on) {
    void* go = g_canvasGo ? target(g_canvasGo) : nullptr;
    if (alive(go)) { bool b = on; call(U.goSetActive, go, {&b}); }
}

// ------------------------------------------------------------------------------------------- elements --
enum UiKind { UI_TEXT = 1, UI_IMAGE = 2, UI_GROUP = 3 };
struct UiElem {
    int kind = 0; void *go = nullptr, *tr = nullptr, *gfx = nullptr;  // GC handles
    int generation = 0;
    int layer = 0, parent = 0;  // canvas layer; parent group id (0: the canvas)
    int mod = -1;               // the mod that made it (its top-level elements live in that mod's own container)
    float scale = -1;
    // last applied values (only changes are sent to Unity)
    std::string text, anchor = "tl", sprite, align = "left", fill;
    float x = -1e9f, y = -1e9f, w = -1e9f, h = -1e9f, size = -1, amount = -1, outline = -1;
    float color[4] = {-1, -1, -1, -1};
    int visible = -1; void* spriteRef = nullptr; bool clockwise = true;
    // tween (spec.tween = seconds): x / y / w / h / size glide from what is shown to the new values, eased, per frame
    bool tweening = false; double t0 = 0, dur = 0;
    float from[5] = {}, to[5] = {};
};
static std::unordered_map<int, UiElem> g_ui;
static int g_uiNextId = 1;

// Each mod's own full-screen container on the overlay canvas: its top-level elements go in it, and za.ui.visible shows
// or hides only that container, so one mod hiding its HUD (on menu screens, say) leaves the others' elements alone.
struct ModRoot { void *go = nullptr, *tr = nullptr; int generation = 0; bool visible = true; };
static std::unordered_map<int, ModRoot> g_modRoots;
static void* mod_root(int mod) {
    ModRoot& r = g_modRoots[mod];
    if (r.go && r.generation == g_uiGeneration && alive(target(r.go))) return target(r.tr);
    if (!ensure_canvas(0)) return nullptr;
    void* go = g_api.object_new(U.goClass);
    bool ok = false;
    call(U.goCtor, go, {g_api.string_new("ZA_Mod")}, &ok);
    void* rt = ok ? call(U.goAddComponent, go, {target(UI.rectTrType)}) : nullptr;
    void* tr = ok ? call(U.goGetTransform, go, {}) : nullptr;
    if (!rt || !tr) return nullptr;
    bool keep = false;
    call(U.trSetParent, tr, {target(g_canvasTr), &keep});
    Vec2 zero{0, 0}, one{1, 1};
    call(UI.rtSetAnchorMin, tr, {&zero});
    call(UI.rtSetAnchorMax, tr, {&one});
    call(UI.rtSetPos, tr, {&zero});
    call(UI.rtSetSize, tr, {&zero});
    if (r.go) { g_api.gchandle_free(r.go); g_api.gchandle_free(r.tr); }
    r.go = g_api.gchandle_new(go, false);
    r.tr = g_api.gchandle_new(tr, false);
    r.generation = g_uiGeneration;
    if (!r.visible) { bool b = false; call(U.goSetActive, go, {&b}); }
    return tr;
}

static bool create_elem(UiElem& e) {
    if (!ensure_canvas(e.layer)) return false;
    void* go = g_api.object_new(U.goClass);
    bool ok = false;
    call(U.goCtor, go, {g_api.string_new(e.kind == UI_TEXT ? "ZA_Text" : e.kind == UI_GROUP ? "ZA_Group" : "ZA_Image")}, &ok);
    if (!ok) return false;
    void* type = e.kind == UI_TEXT ? UI.tmpUguiType : e.kind == UI_GROUP ? UI.rectTrType : UI.imageType;
    void* gfx = call(U.goAddComponent, go, {target(type)});
    void* tr = call(U.goGetTransform, go, {});
    if (!gfx || !tr) { call(U.objDestroy, nullptr, {go}); return false; }
    if (e.kind == UI_GROUP) gfx = nullptr;
    void* parentTr = target(e.layer ? g_sysCanvasTr : g_canvasTr);
    if (!e.layer && e.mod >= 0) if (void* mr = mod_root(e.mod)) parentTr = mr;
    if (e.parent) {
        auto it = g_ui.find(e.parent);
        if (it != g_ui.end() && it->second.kind == UI_GROUP && alive(target(it->second.tr))) parentTr = target(it->second.tr);
    }
    bool keep = false;
    call(U.trSetParent, tr, {parentTr, &keep});
    bool ray = false;
    if (gfx) call(UI.gfxSetRaycast, gfx, {&ray});
    if (e.kind == UI_TEXT) {
        if (!g_hudFont) {  // the game's HUD font, from the native weapon name text
            if (U.font) g_hudFont = g_api.gchandle_new(target(U.font), false);
        }
        if (g_hudFont) call(U.tmpSetFont, gfx, {target(g_hudFont)});
    }
    e.go = g_api.gchandle_new(go, false);
    e.tr = g_api.gchandle_new(tr, false);
    e.gfx = gfx ? g_api.gchandle_new(gfx, false) : nullptr;
    e.scale = -1;
    e.generation = g_uiGeneration;
    // force every property to be sent
    e.x = e.y = e.w = e.h = -1e9f; e.size = e.amount = e.outline = -1; e.visible = -1; e.text.assign("\x01");
    for (float& c : e.color) c = -1;
    e.anchor.assign("\x01"); e.sprite.assign("\x01"); e.align.assign("\x01"); e.fill.assign("\x01"); e.spriteRef = nullptr;
    return true;
}
static void free_elem(UiElem& e) {
    void* go = target(e.go);
    if (alive(go)) call(U.objDestroy, nullptr, {go});
    for (void* h : {e.go, e.tr, e.gfx}) if (h) g_api.gchandle_free(h);
    e.go = e.tr = e.gfx = nullptr;
}

static void anchor_vectors(const std::string& a, Vec2& mn, Vec2& pv, float& sx, float& sy) {
    // design coordinates: x from the anchor's horizontal edge inwards, y from its vertical edge inwards (y down)
    float ax = 0, ay = 1; sx = 1; sy = -1;
    if (a == "tl") { ax = 0; ay = 1; sx = 1; sy = -1; }
    else if (a == "tr") { ax = 1; ay = 1; sx = -1; sy = -1; }
    else if (a == "bl") { ax = 0; ay = 0; sx = 1; sy = 1; }
    else if (a == "br") { ax = 1; ay = 0; sx = -1; sy = 1; }
    else if (a == "t") { ax = 0.5f; ay = 1; sx = 1; sy = -1; }
    else if (a == "b") { ax = 0.5f; ay = 0; sx = 1; sy = 1; }
    else if (a == "l") { ax = 0; ay = 0.5f; sx = 1; sy = -1; }
    else if (a == "r") { ax = 1; ay = 0.5f; sx = -1; sy = -1; }
    else if (a == "c") { ax = 0.5f; ay = 0.5f; sx = 1; sy = -1; }
    mn = {ax, ay}; pv = {ax, ay};
}

// Apply a Lua spec table (at index idx) to an element; only changed values are sent to Unity.
static float opt_num(lua_State* L, int idx, const char* k, float def, bool* has = nullptr) {
    lua_getfield(L, idx, k);
    bool h = lua_isnumber(L, -1);
    float v = h ? (float)lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    if (has) *has = h;
    return v;
}
static bool opt_str(lua_State* L, int idx, const char* k, std::string& out) {
    lua_getfield(L, idx, k);
    bool h = lua_type(L, -1) == LUA_TSTRING;
    if (h) { size_t n = 0; const char* s = lua_tolstring(L, -1, &n); out.assign(s, std::min<size_t>(n, 2000)); }
    lua_pop(L, 1);
    return h;
}
struct ZRef { void* p; void* k; int isStruct; };
static const char* ZREF = "za.ref";

static void set_layout(UiElem& e, float x, float y, float w, float h, float size) {
    void* tr = target(e.tr); void* gfx = target(e.gfx);
    if (x != e.x || y != e.y) {
        Vec2 mn, pv; float sx, sy;
        anchor_vectors(e.anchor, mn, pv, sx, sy);
        Vec2 pos{x * sx, y * sy};
        call(UI.rtSetPos, tr, {&pos});
        e.x = x; e.y = y;
    }
    if (w != e.w || h != e.h) { Vec2 sz{w, h}; call(UI.rtSetSize, tr, {&sz}); e.w = w; e.h = h; }
    if (e.kind == UI_TEXT && size > 0 && size != e.size) { call(U.tmpSetFontSize, gfx, {&size}); e.size = size; }
}
static double g_uiNow = 0;
static void ui_tick(double t) {
    g_uiNow = t;
    for (auto& kv : g_ui) {
        UiElem& e = kv.second;
        if (!e.tweening) continue;
        if (!alive(target(e.go))) { e.tweening = false; continue; }
        double k = e.dur > 0 ? (t - e.t0) / e.dur : 1.0;
        if (k >= 1.0) { k = 1.0; e.tweening = false; }
        float q = (float)(1.0 - (1.0 - k) * (1.0 - k) * (1.0 - k));  // ease-out cubic
        float v[5];
        for (int i = 0; i < 5; i++) v[i] = e.from[i] + (e.to[i] - e.from[i]) * q;
        set_layout(e, v[0], v[1], v[2], v[3], v[4]);
    }
}

static void apply_spec(lua_State* L, int idx, UiElem& e) {
    void* tr = target(e.tr); void* gfx = target(e.gfx); void* go = target(e.go);
    if (!alive(go)) return;
    // layout
    std::string anchor = e.anchor;
    opt_str(L, idx, "anchor", anchor);
    bool hx, hy, hw, hh, ht, hsz;
    float x = opt_num(L, idx, "x", e.tweening ? e.to[0] : e.x, &hx), y = opt_num(L, idx, "y", e.tweening ? e.to[1] : e.y, &hy);
    float w = opt_num(L, idx, "w", e.tweening ? e.to[2] : e.w, &hw), h = opt_num(L, idx, "h", e.tweening ? e.to[3] : e.h, &hh);
    float tween = opt_num(L, idx, "tween", 0, &ht);
    if (ht && tween > 0 && anchor == e.anchor && e.x > -1e8f && e.w > -1e8f) {
        float tsize = opt_num(L, idx, "size", e.tweening ? e.to[4] : e.size, &hsz);
        float goal[5] = {x, y, w, h, e.kind == UI_TEXT ? tsize : -1};
        bool same = e.tweening ? memcmp(goal, e.to, sizeof goal) == 0
                               : (x == e.x && y == e.y && w == e.w && h == e.h && (e.kind != UI_TEXT || tsize == e.size));
        if (!same) {
            float cur[5] = {e.x, e.y, e.w, e.h, e.kind == UI_TEXT ? e.size : -1};
            memcpy(e.from, cur, sizeof cur); memcpy(e.to, goal, sizeof goal);
            e.t0 = g_uiNow; e.dur = tween * 1000.0; e.tweening = true;
        }
        x = e.x; y = e.y; w = e.w; h = e.h;  // the tween moves them from here
    } else if (e.tweening && (hx || hy || hw || hh)) {
        e.tweening = false;  // an untweened move replaces a running tween
    }
    if (anchor != e.anchor || x != e.x || y != e.y) {
        Vec2 mn, pv; float sx, sy;
        anchor_vectors(anchor, mn, pv, sx, sy);
        if (anchor != e.anchor) {
            call(UI.rtSetAnchorMin, tr, {&mn}); call(UI.rtSetAnchorMax, tr, {&mn}); call(UI.rtSetPivot, tr, {&pv});
        }
        Vec2 pos{(x < -1e8f ? 0 : x) * sx, (y < -1e8f ? 0 : y) * sy};
        call(UI.rtSetPos, tr, {&pos});
        e.anchor = anchor; e.x = x; e.y = y;
    }
    if (w != e.w || h != e.h) {
        Vec2 size{w < -1e8f ? 100 : w, h < -1e8f ? 30 : h};
        call(UI.rtSetSize, tr, {&size});
        e.w = w; e.h = h;
    }
    lua_getfield(L, idx, "raycast");
    if (lua_isboolean(L, -1) && gfx) { bool ray = lua_toboolean(L, -1) != 0; call(UI.gfxSetRaycast, gfx, {&ray}); }
    lua_pop(L, 1);
    bool hsc; float sc = opt_num(L, idx, "scale", e.scale, &hsc);
    if (hsc && sc != e.scale) { Vec3 v{sc, sc, sc}; call(U.trSetLocalScale, tr, {&v}); e.scale = sc; }
    if (e.kind == UI_GROUP) {
        lua_getfield(L, idx, "visible");
        if (lua_isboolean(L, -1)) {
            int v = lua_toboolean(L, -1) ? 1 : 0;
            if (v != e.visible) { bool b = v != 0; call(U.goSetActive, go, {&b}); e.visible = v; }
        }
        lua_pop(L, 1);
        return;
    }
    // colour
    lua_getfield(L, idx, "color");
    if (lua_istable(L, -1)) {
        float c[4];
        for (int i = 0; i < 4; i++) { lua_rawgeti(L, -1, i + 1); c[i] = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : 1.0f; lua_pop(L, 1); }
        if (memcmp(c, e.color, sizeof c) != 0) {
            Color4 col{c[0], c[1], c[2], c[3]};
            call(UI.gfxSetColor, gfx, {&col});
            memcpy(e.color, c, sizeof c);
        }
    }
    lua_pop(L, 1);
    if (e.kind == UI_TEXT) {
        std::string text = e.text;
        if (opt_str(L, idx, "text", text) && text != e.text) {
            call(U.tmpSetText, gfx, {g_api.string_new(text.c_str())});
            e.text = text;
        }
        lua_getfield(L, idx, "wrap");
        if (lua_isboolean(L, -1) && UI.tmpSetWrap) { int m = lua_toboolean(L, -1) ? 1 /* Normal */ : 0 /* NoWrap */; call(UI.tmpSetWrap, gfx, {&m}); }
        lua_pop(L, 1);
        bool hs; float size = opt_num(L, idx, "size", e.size, &hs);
        if (hs && size != e.size && !e.tweening) { call(U.tmpSetFontSize, gfx, {&size}); e.size = size; }
        // align ('left' | 'center' | 'right') and valign ('middle' default | 'top' | 'bottom'), kept together in e.align
        std::string h = e.align.substr(0, e.align.find('|')), v = e.align.find('|') == std::string::npos ? "middle" : e.align.substr(e.align.find('|') + 1);
        bool ha = opt_str(L, idx, "align", h), hv = opt_str(L, idx, "valign", v);
        std::string align = h + "|" + v;
        if ((ha || hv) && align != e.align) {
            int a = (h == "right" ? 4 : h == "center" ? 2 : 1) + (v == "top" ? 256 : v == "bottom" ? 1024 : 512);  // TextAlignmentOptions
            call(U.tmpSetAlignment, gfx, {&a});
            e.align = align;
        }
        bool ho; float outline = opt_num(L, idx, "outline", e.outline, &ho);
        if (ho && outline != e.outline && U.tmpSetOutlineWidth) {
            call(U.tmpSetOutlineWidth, gfx, {&outline});
            if (UI.tmpSetOutlineColor) { uint8_t black[4] = {0, 0, 0, 255}; call(UI.tmpSetOutlineColor, gfx, {black}); }
            e.outline = outline;
        }
    } else {
        // sprite: a generated name, or a game Sprite reference
        lua_getfield(L, idx, "sprite");
        if (lua_type(L, -1) == LUA_TSTRING) {
            std::string s = lua_tostring(L, -1);
            if (s != e.sprite) {
                void* sp = s == "none" ? nullptr : sprite_named(s.c_str());
                call(UI.imgSetSprite, gfx, {sp});
                e.sprite = s; e.spriteRef = nullptr;
            }
        } else if (ZRef* r = (ZRef*)luaL_testudata(L, -1, ZREF)) {
            if (r->p != e.spriteRef && alive(r->p)) {
                call(UI.imgSetSprite, gfx, {r->p});
                bool keepAspect = true; call(UI.imgSetPreserveAspect, gfx, {&keepAspect});
                e.spriteRef = r->p; e.sprite.clear();
            }
        }
        lua_pop(L, 1);
        std::string fill = e.fill;
        if (opt_str(L, idx, "fill", fill) && fill != e.fill) {
            int type = fill == "none" ? 0 /* Simple */ : 3 /* Filled */;
            call(UI.imgSetType, gfx, {&type});
            if (fill == "radial") { int m = 4, o = 2; call(UI.imgSetFillMethod, gfx, {&m}); call(UI.imgSetFillOrigin, gfx, {&o}); }
            else if (fill == "horizontal") { int m = 0, o = 0; call(UI.imgSetFillMethod, gfx, {&m}); call(UI.imgSetFillOrigin, gfx, {&o}); }
            e.fill = fill;
        }
        lua_getfield(L, idx, "clockwise");
        if (lua_isboolean(L, -1)) {
            bool cw = lua_toboolean(L, -1) != 0;
            if (cw != e.clockwise) { call(UI.imgSetFillClockwise, gfx, {&cw}); e.clockwise = cw; }
        }
        lua_pop(L, 1);
        // Unity draws an Image without a sprite as a plain rectangle and ignores its fill: filled images get a white one
        if (e.fill != "none" && e.fill != "\x01" && !e.spriteRef && (e.sprite == "none" || e.sprite.empty() || e.sprite == "\x01")) {
            if (void* white = sprite_named("white")) { call(UI.imgSetSprite, gfx, {white}); e.sprite = "white"; }
        }
        bool ha; float amount = opt_num(L, idx, "amount", e.amount, &ha);
        if (ha && amount != e.amount) { amount = clamp01(amount); call(UI.imgSetFillAmount, gfx, {&amount}); e.amount = amount; }
    }
    lua_getfield(L, idx, "visible");
    if (lua_isboolean(L, -1)) {
        int v = lua_toboolean(L, -1) ? 1 : 0;
        if (v != e.visible) { bool b = v != 0; call(U.goSetActive, go, {&b}); e.visible = v; }
    }
    lua_pop(L, 1);
}

static UiElem* elem_for_set(lua_State* L, int id) {
    auto it = g_ui.find(id);
    if (it == g_ui.end()) return nullptr;
    UiElem& e = it->second;
    if (e.generation != g_uiGeneration || !alive(target(e.go))) {  // canvas recreated, or destroyed: rebuild
        free_elem(e);
        if (!create_elem(e)) return nullptr;
    }
    return &e;
}

// za.ui.text{...} / za.ui.image{...} -> id; za.ui.set(id, {...}); za.ui.remove(id)
static int ui_layer_of(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "za_ui_layer");
    int layer = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return layer;
}
static int za_ui_new(lua_State* L, int kind) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (!UI.ok || !g_inMainThreadTick) { lua_pushnil(L); return 1; }
    UiElem e; e.kind = kind;
    e.layer = ui_layer_of(L);
    lua_getfield(L, LUA_REGISTRYINDEX, "za_mod_index"); e.mod = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1; lua_pop(L, 1);
    lua_getfield(L, 1, "parent"); e.parent = (int)lua_tointeger(L, -1); lua_pop(L, 1);
    if (!create_elem(e)) { lua_pushnil(L); return 1; }
    int id = g_uiNextId++;
    g_ui[id] = e;
    apply_spec(L, 1, g_ui[id]);
    lua_pushinteger(L, id);
    return 1;
}
static int za_ui_text(lua_State* L) { return za_ui_new(L, UI_TEXT); }
static int za_ui_image(lua_State* L) { return za_ui_new(L, UI_IMAGE); }
// za.ui.group{anchor, x, y, w, h, scale, parent} -> id: an empty container; elements with parent = id are laid out
// inside its rectangle (anchors relative to it) and scale with it
static int za_ui_group(lua_State* L) { return za_ui_new(L, UI_GROUP); }
static int za_ui_set(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (!g_inMainThreadTick) return 0;
    if (UiElem* e = elem_for_set(L, id)) apply_spec(L, 2, *e);
    return 0;
}
static int za_ui_remove(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    auto it = g_ui.find(id);
    if (it != g_ui.end() && g_inMainThreadTick) { free_elem(it->second); g_ui.erase(it); }
    return 0;
}
// za.ui.use_font(textRef): use the font of a native HUD text (for example the weapon name) for every za.ui text
static int za_ui_use_font(lua_State* L) {
    ZRef* r = (ZRef*)luaL_checkudata(L, 1, ZREF);
    if (!g_inMainThreadTick || r->isStruct || !alive(r->p)) return 0;
    void* font = call(UI.tmpGetFont, r->p, {});
    if (!alive(font)) return 0;
    if (g_hudFont) g_api.gchandle_free(g_hudFont);
    g_hudFont = g_api.gchandle_new(font, false);
    if (U.font) g_api.gchandle_free(U.font);  // world labels (HP numbers) use the HUD font too
    U.font = g_api.gchandle_new(font, false);
    for (auto& kv : g_labels) if (alive(target(kv.second.tmp))) call(U.tmpSetFont, target(kv.second.tmp), {font});
    for (auto& kv : g_ui)  // re-font existing texts
        if (kv.second.kind == UI_TEXT && alive(target(kv.second.gfx))) call(U.tmpSetFont, target(kv.second.gfx), {font});
    logf("ui: HUD font %s", object_name(font).c_str());
    return 0;
}
// za.ui.measure(textId, text, width) -> w, h: the size that text needs in that element (its font and size) at that width
static int za_ui_measure(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    const char* str = luaL_checkstring(L, 2);
    float w = (float)luaL_checknumber(L, 3), h = 0;
    auto it = g_ui.find(id);
    Vec2 out{0, 0};
    if (g_inMainThreadTick && UI.tmpPreferred && it != g_ui.end() && it->second.kind == UI_TEXT && alive(target(it->second.gfx)))
        unbox_into(call(UI.tmpPreferred, target(it->second.gfx), {g_api.string_new(str), &w, &h}), &out, sizeof out);
    lua_pushnumber(L, out.x); lua_pushnumber(L, out.y);
    return 2;
}
static int za_ui_generation(lua_State* L) { lua_pushinteger(L, g_uiGeneration); return 1; }
static int za_ui_ready(lua_State* L) { lua_pushboolean(L, UI.ok && g_inMainThreadTick && ensure_canvas(ui_layer_of(L))); return 1; }

// ---------------------------------------------------------------------------- read-only reflection (za.ref) --
static void push_ref(lua_State* L, void* p, void* k, bool isStruct) {
    if (!p || !k) { lua_pushnil(L); return; }
    ZRef* r = (ZRef*)lua_newuserdatauv(L, sizeof(ZRef), 0);
    r->p = p; r->k = k; r->isStruct = isStruct;
    luaL_setmetatable(L, ZREF);
}
static ZRef* check_ref(lua_State* L, int i) { return (ZRef*)luaL_checkudata(L, i, ZREF); }
static bool is_array_class(void* k) {
    const char* n = g_api.class_get_name(k);
    size_t l = n ? strlen(n) : 0;
    return l > 2 && n[l - 2] == '[' && n[l - 1] == ']';
}
static void push_string(lua_State* L, void* s);
static void push_object(lua_State* L, void* obj) {  // a string arrives in Lua as a string, anything else as a ref
    if (!plausible(obj)) { lua_pushnil(L); return; }
    void* k = safe_object_class(obj);
    if (!valid_class(k)) { lua_pushnil(L); return; }
    if (strcmp(g_api.class_get_name(k), "String") == 0 && strcmp(g_api.class_get_namespace(k), "System") == 0) {
        push_string(L, obj);
        return;
    }
    push_ref(L, obj, k, false);
}
static void push_string(lua_State* L, void* s) {
    wchar_t w[1024];
    if (!plausible(s) || safe_string(s, w, 1024) < 0) { lua_pushnil(L); return; }
    char u[3072];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr);
    lua_pushstring(L, u);
}
static int enum_kind(void* klass) {
    for (const FieldDesc& d : fields_of(klass, false)) if (d.name == "value__") return d.kind;
    return T_I4;
}
static void push_value(lua_State* L, int kind, void* klass, bool valuetype, bool isenum, const uint8_t* addr) {
    if (valuetype && klass && !isenum) { push_ref(L, (void*)addr, klass, true); return; }
    if (isenum) kind = enum_kind(klass);
    uint8_t b[8] = {};
    switch (kind) {
        case T_BOOLEAN: safe_copy(b, addr, 1); lua_pushboolean(L, b[0] != 0); return;
        case T_I1: safe_copy(b, addr, 1); lua_pushinteger(L, (int8_t)b[0]); return;
        case T_U1: safe_copy(b, addr, 1); lua_pushinteger(L, b[0]); return;
        case T_I2: safe_copy(b, addr, 2); lua_pushinteger(L, *(int16_t*)b); return;
        case T_U2: case T_CHAR: safe_copy(b, addr, 2); lua_pushinteger(L, *(uint16_t*)b); return;
        case T_I4: safe_copy(b, addr, 4); lua_pushinteger(L, *(int32_t*)b); return;
        case T_U4: safe_copy(b, addr, 4); lua_pushinteger(L, *(uint32_t*)b); return;
        case T_I8: case T_U8: safe_copy(b, addr, 8); lua_pushinteger(L, *(int64_t*)b); return;
        case T_R4: safe_copy(b, addr, 4); lua_pushnumber(L, *(float*)b); return;
        case T_R8: safe_copy(b, addr, 8); lua_pushnumber(L, *(double*)b); return;
        case T_STRING: push_string(L, safe_ptr_at(addr, 0)); return;
        case T_CLASS: case T_OBJECT: case T_SZARRAY: case T_ARRAY: case T_GENERICINST: push_object(L, safe_ptr_at(addr, 0)); return;
        default: lua_pushnil(L); return;
    }
}
// ref:get(name): a field (instance first, then static) as a number / boolean / string / ref / nil
static int ref_get(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* name = luaL_checkstring(L, 2);
    if (const FieldDesc* d = find_field(r->k, name, false)) {
        const uint8_t* addr = (const uint8_t*)r->p + d->offset - (r->isStruct ? OBJECT_HEADER : 0);
        if (d->kind == T_PTR) {  // a native pointer: byte* -> a C string; T* -> a native struct array (ref:at)
            void* pv = safe_ptr_at(addr, 0);
            void* el = d->klass ? g_api.class_get_element_class(d->klass) : nullptr;
            if (!pv || !el) { lua_pushnil(L); return 1; }
            if (strcmp(g_api.class_get_name(el), "Byte") == 0) {
                char buf[257] = {};
                for (int i = 0; i < 256 && safe_copy(&buf[i], (const uint8_t*)pv + i, 1) && buf[i]; i++) {}
                lua_pushstring(L, buf);
                return 1;
            }
            if (g_api.class_is_valuetype(el)) { push_ref(L, pv, el, false); ((ZRef*)lua_touserdata(L, -1))->isStruct = 2; return 1; }
            lua_pushnil(L);
            return 1;
        }
        bool vt = d->valuetype && !(d->kind == T_GENERICINST && !d->valuetype);
        push_value(L, d->kind, d->klass, vt, d->isenum, addr);
        return 1;
    }
    if (const FieldDesc* d = find_field(r->k, name, true)) {
        void* v = nullptr;
        if (d->reference && safe_static_get(d->field, &v)) { push_object(L, v); return 1; }
    }
    lua_pushnil(L);
    return 1;
}
static const FieldDesc* list_field(void* k, const char* a, const char* b) { return find_field_any(k, {a, b}); }
static int ref_len(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    if (!r->isStruct && is_array_class(r->k)) {
        uint32_t n = 0;
        lua_pushinteger(L, safe_array_length(r->p, &n) ? n : 0);
        return 1;
    }
    if (const FieldDesc* d = list_field(r->k, "_size", "_count")) {
        int32_t n = 0;
        safe_copy(&n, (const uint8_t*)r->p + d->offset - (r->isStruct ? OBJECT_HEADER : 0), 4);
        lua_pushinteger(L, n);
        return 1;
    }
    lua_pushinteger(L, 0);
    return 1;
}
// ref:at(i), 1-based: an array element, a List<T> element, or a Dictionary entry (a struct with key / value / next)
static int ref_at(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    lua_Integer i = luaL_checkinteger(L, 2);
    if (r->isStruct == 2) {  // a native T* array: element i (the caller bounds it with the native count field)
        int32_t size = g_api.class_instance_size(r->k) - (int32_t)OBJECT_HEADER;
        if (i < 1 || size <= 0) { lua_pushnil(L); return 1; }
        push_ref(L, (uint8_t*)r->p + (size_t)(i - 1) * size, r->k, true);
        ((ZRef*)lua_touserdata(L, -1))->isStruct = 2;
        return 1;
    }
    void* arr = r->p; void* ak = r->k;
    if (r->isStruct || !is_array_class(r->k)) {  // a List<T> (_items) or a Dictionary (_entries)
        const FieldDesc* items = list_field(r->k, "_items", "_entries");
        const FieldDesc* size = list_field(r->k, "_size", "_count");
        if (!items) { lua_pushnil(L); return 1; }
        int32_t n = 0;
        if (size) safe_copy(&n, (const uint8_t*)r->p + size->offset, 4);
        if (i < 1 || i > n) { lua_pushnil(L); return 1; }
        arr = safe_ptr_at(r->p, items->offset);
        ak = safe_object_class(arr);
        if (!valid_class(ak)) { lua_pushnil(L); return 1; }
    }
    uint32_t len = 0;
    if (i < 1 || !safe_array_length(arr, &len) || (uint32_t)i > len) { lua_pushnil(L); return 1; }
    void* elem = g_api.class_get_element_class(ak);
    const uint8_t* data = (const uint8_t*)arr + ARRAY_DATA;
    if (elem && g_api.class_is_valuetype(elem)) {
        int esize = g_api.class_array_element_size(elem);
        if (g_api.class_is_enum(elem)) { push_value(L, enum_kind(elem), elem, false, false, data + (i - 1) * esize); return 1; }
        push_ref(L, (void*)(data + (i - 1) * esize), elem, true);
    } else {
        push_object(L, safe_ptr_at(data, (size_t)(i - 1) * 8));
    }
    return 1;
}
static int ref_class(lua_State* L) { ZRef* r = check_ref(L, 1); lua_pushstring(L, g_api.class_get_name(r->k)); return 1; }
static int ref_addr(lua_State* L) { ZRef* r = check_ref(L, 1); lua_pushinteger(L, (lua_Integer)(uintptr_t)r->p); return 1; }
static int ref_alive(lua_State* L) { ZRef* r = check_ref(L, 1); lua_pushboolean(L, !r->isStruct && alive(r->p)); return 1; }

// Read-only Unity getters (main thread only)
static int ref_text(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    if (!g_inMainThreadTick || r->isStruct || !alive(r->p)) { lua_pushnil(L); return 1; }
    push_string(L, call(UI.tmpGetText, r->p, {}));
    return 1;
}
static int ref_fill(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    float f = 0;
    if (!g_inMainThreadTick || r->isStruct || !alive(r->p) || !unbox_into(call(UI.imgGetFillAmount, r->p, {}), &f, 4)) {
        lua_pushnil(L); return 1;
    }
    lua_pushnumber(L, f);
    return 1;
}
static int ref_active(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    void* go = (g_inMainThreadTick && !r->isStruct) ? get_game_object(r->p) : nullptr;
    bool a = false;
    if (!go || !unbox_into(call(UI.goActiveInHierarchy, go, {}), &a, 1)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, a);
    return 1;
}
static int ref_enabled(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    bool a = false;
    if (!g_inMainThreadTick || r->isStruct || !alive(r->p) || !unbox_into(call(UI.behGetEnabled, r->p, {}), &a, 1)) {
        lua_pushnil(L); return 1;
    }
    lua_pushboolean(L, a);
    return 1;
}
static int ref_sprite(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    if (!g_inMainThreadTick || r->isStruct || !alive(r->p)) { lua_pushnil(L); return 1; }
    push_object(L, call(UI.imgGetSprite, r->p, {}));
    return 1;
}
// ref:fields() -> {name, ...} (instance fields, for discovery)
static int ref_fields(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    lua_newtable(L);
    int n = 0;
    for (const FieldDesc& d : fields_of(r->k, false)) { lua_pushstring(L, d.name.c_str()); lua_rawseti(L, -2, ++n); }
    return 1;
}
// ref:parent() -> the Transform's parent;  ref:name() -> the Unity object's name
static int ref_parent(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    void* tr = (g_inMainThreadTick && !r->isStruct) ? get_transform(r->p) : nullptr;
    push_object(L, tr ? call(UI.trGetParent, tr, {}) : nullptr);
    return 1;
}
static int ref_name(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    if (!g_inMainThreadTick || r->isStruct || !alive(r->p)) { lua_pushnil(L); return 1; }
    lua_pushstring(L, object_name(r->p).c_str());
    return 1;
}
// ref:is(name) -> boolean | nil: a parameterless boolean query method of the object, limited to names starting with
// "Is" / "Has" so it stays read-only (for example GameRunner.IsInHub)
static int ref_is(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* name = luaL_checkstring(L, 2);
    bool prefixed = strncmp(name, "Is", 2) == 0 || strncmp(name, "Has", 3) == 0 || strncmp(name, "get_Is", 6) == 0 ||
                    strncmp(name, "get_Has", 7) == 0;
    if (!prefixed || !g_inMainThreadTick || r->isStruct || !alive(r->p)) { lua_pushnil(L); return 1; }
    void* m = g_api.class_get_method_from_name(r->k, name, 0);
    bool b = false, ok = false;
    void* res = m ? call(m, r->p, {}, &ok) : nullptr;
    if (!m || !ok || !unbox_into(res, &b, 1)) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, b);
    return 1;
}
// ref:set_flag(name, bool) -> true | false: set one of the game's own "skip" switches (a boolean field of a game object).
// Only the names listed here are accepted; any other field name is refused.
static int ref_set_flag(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* name = luaL_checkstring(L, 2);
    uint8_t b = lua_toboolean(L, 3) ? 1 : 0;
    static const char* allowed[] = {"shouldSkipOverview", "shouldSkipStats", nullptr};
    bool permitted = false;
    for (int i = 0; allowed[i]; i++) if (strcmp(name, allowed[i]) == 0) permitted = true;
    const FieldDesc* f = (permitted && g_inMainThreadTick && !r->isStruct && alive(r->p)) ? find_field(r->k, name, false) : nullptr;
    lua_pushboolean(L, f && safe_copy((uint8_t*)r->p + f->offset, &b, 1));
    return 1;
}
// ref:confirm() -> true | false: the game's own confirmation of its sell / dismantle popup (SellGearPopup.OnConfirmPressed,
// what its Confirm button calls), only while the popup is showing. Nothing else can be called this way.
static int ref_confirm(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    bool ok = false, vis = false;
    if (g_inMainThreadTick && !r->isStruct && alive(r->p) && strcmp(g_api.class_get_name(r->k), "SellGearPopup") == 0) {
        void* isv = g_api.class_get_method_from_name(r->k, "get_IsVisible", 0);
        void* conf = g_api.class_get_method_from_name(r->k, "OnConfirmPressed", 0);
        if (isv && conf && unbox_into(call(isv, r->p, {}, &ok), &vis, 1) && ok && vis) {
            ok = false;
            call(conf, r->p, {}, &ok);
            lua_pushboolean(L, ok);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}
// ref:click() -> true | false: raise a UnityEngine.UI.Button's onClick event, as a click on it would. Only for a Button that
// is active and interactable: nothing else can be clicked this way (the same effect as the player pressing it).
static bool class_is(void* k, const char* name) {
    for (void* c = k; valid_class(c); c = g_api.class_get_parent(c))
        if (strcmp(g_api.class_get_name(c), name) == 0) return true;
    return false;
}
static int ref_click(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    bool ok = false, flag = false;
    void* go = (g_inMainThreadTick && !r->isStruct && alive(r->p) && class_is(r->k, "Button")) ? get_game_object(r->p) : nullptr;
    if (go && unbox_into(call(UI.goActiveInHierarchy, go, {}), &flag, 1) && flag) {
        void* inter = g_api.class_get_method_from_name(r->k, "get_interactable", 0);
        flag = false;
        if (inter && unbox_into(call(inter, r->p, {}, &ok), &flag, 1) && ok && flag) {
            void* on = g_api.class_get_method_from_name(r->k, "get_onClick", 0);
            ok = false;
            void* ev = on ? call(on, r->p, {}, &ok) : nullptr;
            void* ek = (ok && plausible(ev)) ? safe_object_class(ev) : nullptr;
            void* inv = valid_class(ek) ? g_api.class_get_method_from_name(ek, "Invoke", 0) : nullptr;
            if (inv) { ok = false; call(inv, ev, {}, &ok); lua_pushboolean(L, ok); return 1; }
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}
// ref:texts() -> {string, ...}: the text of every active TextMeshPro under this object, in hierarchy order
static int ref_texts(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    lua_newtable(L);
    if (!g_inMainThreadTick || r->isStruct || !UI.compInChildren || !UI.tmpTextType) return 1;
    void* tr = get_transform(r->p);
    bool includeInactive = false;
    void* arr = tr ? call(UI.compInChildren, tr, {target(UI.tmpTextType), &includeInactive}) : nullptr;
    uint32_t len = 0;
    if (!arr || !safe_array_length(arr, &len)) return 1;
    int n = 0;
    for (uint32_t i = 0; i < len && i < 64; i++) {
        void* t = safe_ptr_at((const uint8_t*)arr + ARRAY_DATA, (size_t)i * 8);
        if (!alive(t)) continue;
        push_string(L, call(UI.tmpGetText, t, {}));
        if (lua_isnil(L, -1)) { lua_pop(L, 1); continue; }
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
// ref:components(className [, includeInactive]) -> {ref, ...}: the game's components of that class under this object
// (GetComponentsInChildren), in hierarchy order, at most 32; also "Image" (UnityEngine.UI.Image)
static std::unordered_map<std::string, void*> g_typeHandles;  // class name -> GC handle of its System.Type
static int ref_components(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* cls = luaL_checkstring(L, 2);
    bool includeInactive = lua_toboolean(L, 3);
    lua_newtable(L);
    if (!g_inMainThreadTick || r->isStruct || !UI.compInChildren) return 1;
    void*& th = g_typeHandles[cls];
    if (!th) {
        void* k = strcmp(cls, "Image") == 0 ? UI.imageClass : find_game_class(cls);
        if (!k) return 1;
        th = type_handle(k);
    }
    void* tr = get_transform(r->p);
    void* arr = tr ? call(UI.compInChildren, tr, {target(th), &includeInactive}) : nullptr;
    uint32_t len = 0;
    if (!arr || !safe_array_length(arr, &len)) return 1;
    int n = 0;
    for (uint32_t i = 0; i < len && n < 32; i++) {
        void* c = safe_ptr_at((const uint8_t*)arr + ARRAY_DATA, (size_t)i * 8);
        if (!alive(c)) continue;
        push_object(L, c);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
static int ref_tostring(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    lua_pushfstring(L, "za.ref<%s%s @%p>", g_api.class_get_name(r->k), r->isStruct ? " struct" : "", r->p);
    return 1;
}

// za.static(className, field) -> ref | nil   (a static field of one of the game's own classes)
static int za_static(lua_State* L) {
    const char* cls = luaL_checkstring(L, 1);
    const char* field = luaL_checkstring(L, 2);
    void* k = find_game_class(cls);
    const FieldDesc* d = k ? find_field(k, field, true) : nullptr;
    void* v = nullptr;
    if (!d || !d->reference || !safe_static_get(d->field, &v)) { lua_pushnil(L); return 1; }
    push_object(L, v);
    return 1;
}
static int za_time(lua_State* L) { lua_pushnumber(L, now_ms() / 1000.0); return 1; }

// za.loc(key) -> the game's own localised text for key (NinjaKiwi.Localization), or nil. The string tables are found
// once by walking LocalizationManager's statics for Dictionary<string, string> instances (read-only).
static std::unordered_map<std::string, std::string> g_loc;
static double g_locBuiltAt = -1e9;
static std::string read_utf8(void* s) {
    wchar_t w[1024];
    if (!plausible(s) || safe_string(s, w, 1024) < 0) return std::string();
    char u[3072];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr);
    return u;
}
static void loc_collect(void* obj, int depth, std::unordered_set<void*>& seen) {
    if (!plausible(obj) || depth > 3 || !seen.insert(obj).second || g_loc.size() > 300000) return;
    void* k = safe_object_class(obj);
    if (!valid_class(k)) return;
    const char* cn = g_api.class_get_name(k);
    if (strncmp(cn, "Dictionary`2", 12) == 0) {
        const FieldDesc* entries = find_field_any(k, {"_entries", "entries"});
        const FieldDesc* count = find_field_any(k, {"_count", "count"});
        void* arr = entries ? safe_ptr_at(obj, entries->offset) : nullptr;
        void* ak = safe_object_class(arr);
        int32_t n = 0; uint32_t len = 0;
        if (!count || !valid_class(ak) || !safe_copy(&n, (const uint8_t*)obj + count->offset, 4) || !safe_array_length(arr, &len)) return;
        void* ek = g_api.class_get_element_class(ak);
        const FieldDesc* kf = find_field(ek, "key", false);
        const FieldDesc* vf = find_field(ek, "value", false);
        int es = g_api.class_array_element_size(ek);
        if (!kf || !vf || kf->kind != T_STRING || vf->kind != T_STRING || es <= 0) return;
        const uint8_t* data = (const uint8_t*)arr + ARRAY_DATA;
        for (uint32_t i = 0; i < len && (int32_t)i < n; i++) {
            const uint8_t* e = data + (size_t)i * es;
            std::string key = read_utf8(safe_ptr_at(e, kf->offset - OBJECT_HEADER));
            if (!key.empty() && !g_loc.count(key)) g_loc[key] = read_utf8(safe_ptr_at(e, vf->offset - OBJECT_HEADER));
        }
        return;
    }
    for (const FieldDesc& d : fields_of(k, false))
        if (d.reference && d.kind != T_STRING) loc_collect(safe_ptr_at(obj, d.offset), depth + 1, seen);
}
static int za_loc(lua_State* L) {
    std::string key = luaL_checkstring(L, 1);
    if (g_loc.empty() && now_ms() - g_locBuiltAt > 10000) {  // build (or retry every 10 s until the tables load)
        g_locBuiltAt = now_ms();
        void* lm = find_game_class("LocalizationManager");
        std::unordered_set<void*> seen;
        if (lm) for (const FieldDesc& d : fields_of(lm, true)) {
            void* v = nullptr;
            if (d.reference && safe_static_get(d.field, &v)) loc_collect(v, 0, seen);
        }
        logf("loc: %zu localised strings found", g_loc.size());
    }
    auto it = g_loc.find(key);
    if (it == g_loc.end()) { lua_pushnil(L); return 1; }
    lua_pushstring(L, it->second.c_str());
    return 1;
}
// za.ui.visible(bool): show or hide all of this mod's overlay elements (its own container; other mods' are untouched)
static int za_ui_visible(lua_State* L) {
    if (!g_inMainThreadTick || ui_layer_of(L) != 0) return 0;
    lua_getfield(L, LUA_REGISTRYINDEX, "za_mod_index");
    int mod = lua_isinteger(L, -1) ? (int)lua_tointeger(L, -1) : -1;
    lua_pop(L, 1);
    bool on = lua_toboolean(L, 1) != 0;
    if (mod < 0) { set_canvas_visible(on); return 0; }
    ModRoot& r = g_modRoots[mod];
    if (r.visible == on && r.go && r.generation == g_uiGeneration) return 0;
    r.visible = on;
    if (void* tr = mod_root(mod)) {
        void* go = target(r.go);
        if (alive(go)) { bool b = on; call(U.goSetActive, go, {&b}); }
        (void)tr;
    }
    return 0;
}

// --------------------------------------------------------------------------------- native HUD layout --
// za.hud.scale(ref, s): scale a native HUD container (0 hides it; 1 restores). Visual only.
// za.hud.skip_dialogue(hud) -> true while a dialogue is playing: presses the game's own "skip" for the active
// dialogue (GameHud.activeDialogue.skipRequested), so pages finish at once and their reveal sounds stop
static int za_hud_skip_dialogue(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    bool skipping = false;
    if (g_inMainThreadTick && !r->isStruct) {
        const FieldDesc* a = find_field(r->k, "activeDialogue", false);
        void* d = a ? safe_ptr_at(r->p, a->offset) : nullptr;
        void* dk = plausible(d) ? safe_object_class(d) : nullptr;
        const FieldDesc* f = valid_class(dk) ? find_field(dk, "skipRequested", false) : nullptr;
        if (f) { uint8_t one = 1; skipping = safe_copy((uint8_t*)d + f->offset, &one, 1); }
    }
    lua_pushboolean(L, skipping);
    return 1;
}
// za.hud.silence(componentRef, field, ...) -> n: makes the component's Wwise sound events (AK.Wwise.Event fields) post
// nothing, by clearing each event's reference to its sound (Event.Post on an event without one is a no-op in Wwise).
// Sound only: what the component does is unchanged. Returns how many events were silenced (0 when already silent).
// Every silenced event, so it can be given its sound back (za.hud.unsilence): event -> {GC handles of the event and of
// its original sound reference, the field name it was silenced under}
struct Silenced { void* event; void* sound; size_t offset; std::string field; };
static std::vector<Silenced> g_silenced;
static int za_hud_silence(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    int n = 0, top = lua_gettop(L);
    if (!g_inMainThreadTick || r->isStruct || !alive(r->p)) { lua_pushinteger(L, 0); return 1; }
    for (int i = 2; i <= top; i++) {
        const FieldDesc* f = find_field(r->k, luaL_checkstring(L, i), false);
        void* ev = f ? safe_ptr_at(r->p, f->offset) : nullptr;
        void* ek = plausible(ev) ? safe_object_class(ev) : nullptr;
        if (!valid_class(ek)) continue;
        const FieldDesc* ref = nullptr;
        for (const FieldDesc& d : fields_of(ek, false))
            if (d.name.find("ObjectReference") != std::string::npos && d.reference) { ref = &d; break; }
        if (!ref) {
            std::string names;
            for (const FieldDesc& d : fields_of(ek, false)) { names += d.name; names += ' '; }
            logf("hud: silence: no sound reference in %s (fields: %s)", g_api.class_get_name(ek), names.c_str());
            continue;
        }
        void* cur = safe_ptr_at(ev, ref->offset);
        if (!cur) continue;
        void* null = nullptr;
        if (safe_copy((uint8_t*)ev + ref->offset, &null, sizeof null)) {
            g_silenced.push_back({g_api.gchandle_new(ev, false), g_api.gchandle_new(cur, false), ref->offset, lua_tostring(L, i)});
            n++;
        }
    }
    lua_pushinteger(L, n);
    return 1;
}
// za.hud.unsilence(field) -> n: gives back the sound of every event silenced under that field name (for example
// 'voiceOverEvent'), as long as nothing else has set one since
static int za_hud_unsilence(lua_State* L) {
    std::string field = luaL_checkstring(L, 1);
    int n = 0;
    for (auto it = g_silenced.begin(); it != g_silenced.end();) {
        if (it->field != field) { ++it; continue; }
        void* ev = target(it->event), *snd = target(it->sound);
        if (plausible(ev) && snd && !safe_ptr_at(ev, it->offset) && safe_copy((uint8_t*)ev + it->offset, &snd, sizeof snd)) n++;
        g_api.gchandle_free(it->event);
        g_api.gchandle_free(it->sound);
        it = g_silenced.erase(it);
    }
    lua_pushinteger(L, n);
    return 1;
}
// za.hud.nudge(ref, dx, dy): move a native HUD element by (dx, dy) from where it was when first nudged (its own canvas
// units; y up). Repeated calls keep the same offset; za.hud.nudge_reset() forgets the remembered positions (new HUD).
static std::unordered_map<void*, Vec2> g_nudged;  // RectTransform -> its original anchored position
static bool derives_from_name(void* k, const char* name) {
    for (void* c = k; c && valid_class(c); c = g_api.class_get_parent(c))
        if (strcmp(g_api.class_get_name(c), name) == 0) return true;
    return false;
}
static int za_hud_nudge(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    float dx = (float)luaL_checknumber(L, 2), dy = (float)luaL_checknumber(L, 3);
    if (!g_inMainThreadTick || r->isStruct || !UI.rtGetPos) { lua_pushboolean(L, false); return 1; }
    void* tr = get_transform(r->p);
    if (!alive(tr) || !derives_from_name(safe_object_class(tr), "RectTransform")) { lua_pushboolean(L, false); return 1; }
    auto it = g_nudged.find(tr);
    if (it == g_nudged.end()) {
        Vec2 p{};
        if (!unbox_into(call(UI.rtGetPos, tr, {}), &p, sizeof p)) { lua_pushboolean(L, false); return 1; }
        it = g_nudged.emplace(tr, p).first;
        logf("hud: nudging %s by (%.0f, %.0f) from (%.0f, %.0f)", object_name(get_game_object(tr)).c_str(), dx, dy, p.x, p.y);
    }
    Vec2 want{it->second.x + dx, it->second.y + dy}, cur{};
    if (!unbox_into(call(UI.rtGetPos, tr, {}), &cur, sizeof cur) || fabsf(cur.x - want.x) > 0.01f || fabsf(cur.y - want.y) > 0.01f)
        call(UI.rtSetPos, tr, {&want});
    lua_pushboolean(L, true);
    return 1;
}
// za.hud.nudge_anchor(ref, dy): move a native HUD element down by dy (a fraction of its parent's height) by moving its
// anchors, from where they were when first moved. For elements the game animates (an Animator sets their position
// every frame, so za.hud.nudge is undone): animations move the position, which then applies from the moved anchors.
struct AnchorPair { Vec2 min, max; };
static std::unordered_map<void*, AnchorPair> g_anchored;  // RectTransform -> its original anchors
static int za_hud_nudge_anchor(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    float dy = (float)luaL_checknumber(L, 2);
    if (!g_inMainThreadTick || r->isStruct || !UI.rtGetAnchorMin || !UI.rtGetAnchorMax) { lua_pushboolean(L, false); return 1; }
    void* tr = get_transform(r->p);
    if (!alive(tr) || !derives_from_name(safe_object_class(tr), "RectTransform")) { lua_pushboolean(L, false); return 1; }
    auto it = g_anchored.find(tr);
    if (it == g_anchored.end()) {
        AnchorPair a{};
        if (!unbox_into(call(UI.rtGetAnchorMin, tr, {}), &a.min, sizeof a.min) ||
            !unbox_into(call(UI.rtGetAnchorMax, tr, {}), &a.max, sizeof a.max)) { lua_pushboolean(L, false); return 1; }
        it = g_anchored.emplace(tr, a).first;
        logf("hud: moving %s by its anchors (%.3f of its parent's height)", object_name(get_game_object(tr)).c_str(), dy);
    }
    Vec2 mn{it->second.min.x, it->second.min.y - dy}, mx{it->second.max.x, it->second.max.y - dy}, cur{};
    if (!unbox_into(call(UI.rtGetAnchorMin, tr, {}), &cur, sizeof cur) || fabsf(cur.y - mn.y) > 1e-4f) {
        call(UI.rtSetAnchorMin, tr, {&mn});
        call(UI.rtSetAnchorMax, tr, {&mx});
    }
    lua_pushboolean(L, true);
    return 1;
}
static int za_hud_nudge_reset(lua_State*) { g_nudged.clear(); g_anchored.clear(); return 0; }

// za.hud.rect(ref) -> left, top, right, bottom | nil: where a native HUD element's RectTransform is, as fractions of
// its root canvas (0, 0 top-left; 1, 1 bottom-right), whether or not it is showing. For keeping clear of the game's
// own banners and toasts.
struct RectF { float x, y, w, h; };
// A RectTransform's corners in its root canvas, as fractions (0, 0 top-left; 1, 1 bottom-right): l, t, r, b. The root
// canvas's RectTransform spans the screen; the topmost transform above it may not (the game keeps its UI under an
// object scaled to 0, which would turn every measurement into infinity).
static void* root_of(void* tr) {
    void* canvas = UI.compInParent && UI.canvasClassType ? call(UI.compInParent, tr, {target(UI.canvasClassType)}) : nullptr;
    void* root = alive(canvas) && UI.cvGetRoot ? call(UI.cvGetRoot, canvas, {}) : canvas;
    void* rt = alive(root) ? get_transform(root) : nullptr;
    return alive(rt) ? rt : tr;
}
static bool rect_in_root(void* tr, void* root, float out[4]) {
    RectF rc{}, rr{};
    if (root == tr || !unbox_into(call(UI.rtGetRect, tr, {}), &rc, sizeof rc) ||
        !unbox_into(call(UI.rtGetRect, root, {}), &rr, sizeof rr) || rr.w <= 0 || rr.h <= 0) return false;
    float fx[2], fy[2];
    for (int k = 0; k < 2; k++) {
        Vec3 local{rc.x + k * rc.w, rc.y + k * rc.h, 0}, world{}, inRoot{};
        if (!unbox_into(call(UI.trTransformPoint, tr, {&local}), &world, sizeof world) ||
            !unbox_into(call(UI.trInverseTransformPoint, root, {&world}), &inRoot, sizeof inRoot)) return false;
        fx[k] = (inRoot.x - rr.x) / rr.w;
        fy[k] = 1 - (inRoot.y - rr.y) / rr.h;
    }
    if (!std::isfinite(fx[0]) || !std::isfinite(fx[1]) || !std::isfinite(fy[0]) || !std::isfinite(fy[1])) return false;
    out[0] = fminf(fx[0], fx[1]); out[1] = fminf(fy[0], fy[1]); out[2] = fmaxf(fx[0], fx[1]); out[3] = fmaxf(fy[0], fy[1]);
    return true;
}
static bool rect_ready() { return g_inMainThreadTick && UI.rtGetRect && UI.trTransformPoint && UI.trInverseTransformPoint; }
static int za_hud_rect(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    if (!rect_ready() || r->isStruct) return 0;
    void* tr = get_transform(r->p);
    if (!alive(tr) || !derives_from_name(safe_object_class(tr), "RectTransform")) return 0;
    float q[4];
    if (!rect_in_root(tr, root_of(tr), q)) return 0;
    for (float v : q) lua_pushnumber(L, v);
    return 4;
}
// za.hud.content_rect(ref) -> left, top, right, bottom | nil: what a native element actually shows: the bounds of its
// active text and images (UnityEngine.UI.Graphic, at most 64), leaving out any one bigger than half the screen (a
// backdrop) and empty containers. Same fractions as za.hud.rect.
static int za_hud_content_rect(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    if (!rect_ready() || r->isStruct || !UI.graphicType || !UI.compInChildren) return 0;
    void* tr = get_transform(r->p);
    if (!alive(tr)) return 0;
    void* root = root_of(tr);
    bool inactive = false;
    void* arr = call(UI.compInChildren, tr, {target(UI.graphicType), &inactive});
    uint32_t n = 0;
    if (!arr || !safe_array_length(arr, &n)) return 0;
    float u[4] = {2, 2, -1, -1};
    int used = 0;
    for (uint32_t i = 0; i < n && i < 64; i++) {
        void* g = safe_ptr_at((const uint8_t*)arr + ARRAY_DATA, (size_t)i * 8);
        void* gt = alive(g) ? get_transform(g) : nullptr;
        float q[4];
        if (!gt || !rect_in_root(gt, root, q)) continue;
        if (q[3] - q[1] > 0.5f || q[2] - q[0] > 0.9f || q[3] - q[1] < 0.001f) continue;
        u[0] = fminf(u[0], q[0]); u[1] = fminf(u[1], q[1]); u[2] = fmaxf(u[2], q[2]); u[3] = fmaxf(u[3], q[3]);
        used++;
    }
    if (!used) return 0;
    for (float v : u) lua_pushnumber(L, v);
    return 4;
}

// za.hud.load_sprite(assetRef) -> a ref to the loader's own copy of an Addressables sprite reference, loading. The game's
// reference is never touched (an AssetReference can only be loaded once, and the game loads its own when it needs it):
// the copy has the same asset GUID / sub-object, starts unloaded, and is loaded once through AssetReferenceT<Sprite>
// .LoadAssetAsync(). Read the sprite from the copy (m_Operation's result) when the load completes. Copies are kept
// for the session (one small icon each).
static std::unordered_map<void*, void*> g_spriteRefs;  // the game's AssetReference -> GC handle of our loading copy
static void* load_method(void* k) {  // the non-generic LoadAssetAsync() of AssetReferenceT<T>, up the class chain
    static bool (*isGeneric)(void*) = (bool (*)(void*))GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"), "il2cpp_method_is_generic");
    for (void* c = k; c && valid_class(c); c = g_api.class_get_parent(c)) {
        void* iter = nullptr;
        while (void* m = g_api.class_get_methods(c, &iter))
            if (strcmp(g_api.method_get_name(m), "LoadAssetAsync") == 0 && g_api.method_get_param_count(m) == 0 && (!isGeneric || !isGeneric(m)))
                return m;
    }
    return nullptr;
}
static std::string guid_of(void* aref, void* k) {  // an AssetReference's m_AssetGUID
    const FieldDesc* g = find_field(k, "m_AssetGUID", false);
    wchar_t w[128];
    void* s = g ? safe_ptr_at(aref, g->offset) : nullptr;
    if (!plausible(s) || safe_string(s, w, 128) < 0) return "";
    char u[384];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr);
    return u;
}
static int za_hud_load_sprite(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    if (!g_inMainThreadTick || r->isStruct || !plausible(r->p)) { lua_pushnil(L); return 1; }
    auto it = g_spriteRefs.find(r->p);
    if (it != g_spriteRefs.end()) {
        // the same address can be a different reference in a later mission: reuse the copy only for the same asset
        void* copy = it->second ? target(it->second) : nullptr;
        if (!it->second || (copy && guid_of(copy, r->k) == guid_of(r->p, r->k))) { push_object(L, copy); return 1; }
        g_spriteRefs.erase(it);
    }
    void* k = r->k;
    void* m = load_method(k);
    const FieldDesc* op = find_field(k, "m_Operation", false);
    int size = g_api.class_instance_size(k);
    if (!m || !op || size <= (int)OBJECT_HEADER || size > 4096) {
        logf("hud: load_sprite: %s has no usable LoadAssetAsync / m_Operation", g_api.class_get_name(k));
        g_spriteRefs[r->p] = nullptr;
        lua_pushnil(L);
        return 1;
    }
    void* copy = g_api.object_new(k);
    if (!copy || !safe_copy((uint8_t*)copy + OBJECT_HEADER, (uint8_t*)r->p + OBJECT_HEADER, size - OBJECT_HEADER)) { lua_pushnil(L); return 1; }
    // the copy starts unloaded: clear its operation handle
    int opSize = op->klass ? g_api.class_instance_size(op->klass) - (int)OBJECT_HEADER : 0;
    if (opSize > 0 && opSize <= 64) { uint8_t zero[64] = {}; safe_copy((uint8_t*)copy + op->offset, zero, opSize); }
    void* h = g_api.gchandle_new(copy, false);
    g_spriteRefs[r->p] = h;
    bool ok = false;
    call(m, copy, {}, &ok);
    if (!ok) logf("hud: load_sprite: LoadAssetAsync failed for a %s", g_api.class_get_name(k));
    push_object(L, copy);
    return 1;
}
static int za_hud_scale(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    float s = (float)luaL_checknumber(L, 2);
    if (!g_inMainThreadTick || r->isStruct) return 0;
    void* tr = get_transform(r->p);
    if (!alive(tr)) return 0;
    Vec3 cur{};
    if (unbox_into(call(UI.trGetLocalScale, tr, {}), &cur, sizeof cur) && fabsf(cur.x - s) < 1e-4f) return 0;
    Vec3 v{s, s, s};
    call(U.trSetLocalScale, tr, {&v});
    return 0;
}
// za.hud.dump(ref, depth): log the transform hierarchy under a native container (names and component classes)
static void dump_tree(void* tr, int depth, int maxDepth, int& budget) {
    if (!alive(tr) || budget <= 0) return;
    budget--;
    void* go = call(UI.compGetGameObject, tr, {});
    std::string comps;
    void* arr = go ? call(UI.goGetComponents, go, {target(UI.componentType)}) : nullptr;
    uint32_t n = 0;
    if (arr && safe_array_length(arr, &n))
        for (uint32_t i = 0; i < n && i < 8; i++) {
            void* c = safe_ptr_at(arr, ARRAY_DATA + 8 * i);
            void* ck = safe_object_class(c);
            if (valid_class(ck)) { comps += g_api.class_get_name(ck); comps += ' '; }
        }
    bool on = false;
    if (go) unbox_into(call(UI.goActiveInHierarchy, go, {}), &on, 1);
    logf("hud tree %*s%s%s  [%s]", depth * 2, "", object_name(go ? go : tr).c_str(), on ? "" : " (hidden)", comps.c_str());
    if (depth >= maxDepth) return;
    int count = 0;
    unbox_into(call(UI.trChildCount, tr, {}), &count, 4);
    for (int i = 0; i < count && i < 24; i++) dump_tree(call(UI.trGetChild, tr, {&i}), depth + 1, maxDepth, budget);
}
// za.hud.find_texts(needle [, includeInactive]) -> {ref, ...}: the TextMeshPro texts on screen (or in the world) whose text
// contains `needle`, ignoring case and rich-text tags, at most 64. za.hud.set_text(ref, text): set the text of a TextMeshPro
// ref (nothing else). For a mod that hides or replaces a word in the game's own labels (a name, say).
static std::string hud_plain_lower(void* str) {
    wchar_t w[512];
    if (!plausible(str) || safe_string(str, w, 512) < 0) return "";
    char u[1536];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr);
    std::string out;
    bool tag = false;
    for (const char* c = u; *c; c++) {
        if (*c == '<') tag = true;
        else if (*c == '>') tag = false;
        else if (!tag) out += (char)tolower((unsigned char)*c);
    }
    return out;
}
static int za_hud_find_texts(lua_State* L) {
    std::string needle = luaL_checkstring(L, 1);
    bool inactive = lua_toboolean(L, 2) != 0;
    lua_newtable(L);
    if (needle.empty() || !g_inMainThreadTick || !UI.tmpTextType) return 1;
    for (char& c : needle) c = (char)tolower((unsigned char)c);
    void* arr = nullptr;
    int sortNone = 0, include = 1;
    if (inactive && UI.findObjectsAll) arr = call(UI.findObjectsAll, nullptr, {target(UI.tmpTextType), &include, &sortNone});
    else if (UI.findObjectsByType) arr = call(UI.findObjectsByType, nullptr, {target(UI.tmpTextType), &sortNone});
    uint32_t len = 0;
    if (!arr || !safe_array_length(arr, &len)) return 1;
    int n = 0;
    for (uint32_t i = 0; i < len && i < 8000 && n < 64; i++) {
        void* t = safe_ptr_at((const uint8_t*)arr + ARRAY_DATA, (size_t)i * 8);
        if (!alive(t)) continue;
        if (hud_plain_lower(call(UI.tmpGetText, t, {})).find(needle) == std::string::npos) continue;
        push_object(L, t);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
static bool hud_is_tmp_text(void* obj) {
    for (void* c = safe_object_class(obj); c && valid_class(c); c = g_api.class_get_parent(c))
        if (strcmp(g_api.class_get_name(c), "TMP_Text") == 0) return true;
    return false;
}
static int za_hud_set_text(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    const char* s = luaL_checkstring(L, 2);
    bool ok = false;
    if (g_inMainThreadTick && !r->isStruct && alive(r->p) && U.tmpSetText && hud_is_tmp_text(r->p)) {
        call(U.tmpSetText, r->p, {g_api.string_new(s)}, &ok);
    }
    lua_pushboolean(L, ok);
    return 1;
}
static int za_hud_hide_names(lua_State* L);   // system.inl
static int za_hud_dump(lua_State* L) {
    ZRef* r = check_ref(L, 1);
    int depth = (int)luaL_optinteger(L, 2, 3);
    if (!g_inMainThreadTick || r->isStruct) return 0;
    int budget = 200;
    dump_tree(get_transform(r->p), 0, depth, budget);
    return 0;
}
// za.hud.circle_mask(containerRef, contentRef [, ringColor]) -> true | nil
// Adds a circular mask over containerRef's rect (a new child of it, stretched to fill) and moves contentRef under it,
// so the content is clipped to a circle. Visual only; idempotent (a second call does nothing).
static std::unordered_set<void*> g_masked;
static int za_hud_circle_mask(lua_State* L) {
    ZRef* box = check_ref(L, 1);
    ZRef* content = check_ref(L, 2);
    if (!g_inMainThreadTick || box->isStruct || content->isStruct) { lua_pushnil(L); return 1; }
    void* boxTr = get_transform(box->p);
    void* contentTr = get_transform(content->p);
    if (!alive(boxTr) || !alive(contentTr)) { lua_pushnil(L); return 1; }
    if (boxTr == contentTr) { logf("hud: circle mask skipped: the content is the container itself"); lua_pushnil(L); return 1; }
    if (g_masked.count(contentTr)) { lua_pushboolean(L, 1); return 1; }
    void* go = g_api.object_new(U.goClass);
    bool ok = false;
    call(U.goCtor, go, {g_api.string_new("ZA_CircleMask")}, &ok);
    void* img = ok ? call(U.goAddComponent, go, {target(UI.imageType)}) : nullptr;
    void* mask = img ? call(U.goAddComponent, go, {target(UI.maskType)}) : nullptr;
    void* tr = call(U.goGetTransform, go, {});
    if (!img || !mask || !tr) { logf("hud: circle mask creation failed"); if (ok) call(U.objDestroy, nullptr, {go}); lua_pushnil(L); return 1; }
    call(UI.imgSetSprite, img, {sprite_named("disc")});
    bool show = false, ray = false;
    call(UI.maskSetShow, mask, {&show});
    call(UI.gfxSetRaycast, img, {&ray});
    bool keep = false;
    call(U.trSetParent, tr, {boxTr, &keep});
    Vec2 zero{0, 0}, one{1, 1}, half{0.5f, 0.5f};
    call(UI.rtSetAnchorMin, tr, {&zero}); call(UI.rtSetAnchorMax, tr, {&one}); call(UI.rtSetPivot, tr, {&half});
    call(UI.rtSetPos, tr, {&zero}); call(UI.rtSetSize, tr, {&zero});
    bool keepWorld = true;
    call(U.trSetParent, contentTr, {tr, &keepWorld});
    g_masked.insert(contentTr);
    g_api.gchandle_new(go, false);
    logf("hud: circular mask applied (%s under %s)", object_name(get_game_object(content->p)).c_str(),
         object_name(get_game_object(box->p)).c_str());
    lua_pushboolean(L, 1);
    return 1;
}

static void register_ui_api(lua_State* L) {
    // the ref metatable
    luaL_newmetatable(L, ZREF);
    const luaL_Reg methods[] = {{"get", ref_get}, {"len", ref_len}, {"at", ref_at}, {"class", ref_class},
                                {"addr", ref_addr}, {"alive", ref_alive}, {"text", ref_text}, {"fill", ref_fill},
                                {"active", ref_active}, {"enabled", ref_enabled}, {"sprite", ref_sprite}, {"fields", ref_fields},
                                {"parent", ref_parent}, {"name", ref_name}, {"is", ref_is}, {"click", ref_click}, {"confirm", ref_confirm}, {"set_flag", ref_set_flag}, {"texts", ref_texts}, {"components", ref_components},
                                {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, methods, 0);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, ref_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);
    // za.static, za.time, za.ui.*, za.hud.* on the za table (at the top of the stack)
    lua_pushcfunction(L, za_static); lua_setfield(L, -2, "static");
    lua_pushcfunction(L, za_time); lua_setfield(L, -2, "time");
    lua_pushcfunction(L, za_perf); lua_setfield(L, -2, "perf");
    lua_pushcfunction(L, za_loc); lua_setfield(L, -2, "loc");
    lua_newtable(L);
    const luaL_Reg ui[] = {{"text", za_ui_text}, {"image", za_ui_image}, {"group", za_ui_group}, {"measure", za_ui_measure}, {"set", za_ui_set}, {"remove", za_ui_remove},
                           {"generation", za_ui_generation}, {"ready", za_ui_ready}, {"use_font", za_ui_use_font}, {"visible", za_ui_visible}, {nullptr, nullptr}};
    luaL_setfuncs(L, ui, 0);
    lua_setfield(L, -2, "ui");
    lua_newtable(L);
    const luaL_Reg hud[] = {{"scale", za_hud_scale}, {"dump", za_hud_dump}, {"find_texts", za_hud_find_texts}, {"hide_names", za_hud_hide_names}, {"set_text", za_hud_set_text}, {"circle_mask", za_hud_circle_mask}, {"skip_dialogue", za_hud_skip_dialogue}, {"nudge", za_hud_nudge}, {"nudge_reset", za_hud_nudge_reset}, {"load_sprite", za_hud_load_sprite}, {"load_asset", za_hud_load_sprite}, {"silence", za_hud_silence}, {"unsilence", za_hud_unsilence}, {"rect", za_hud_rect}, {"content_rect", za_hud_content_rect}, {"nudge_anchor", za_hud_nudge_anchor}, {nullptr, nullptr}};
    luaL_setfuncs(L, hud, 0);
    lua_setfield(L, -2, "hud");
}
