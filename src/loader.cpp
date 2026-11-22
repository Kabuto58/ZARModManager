// ZARModManager runtime (formerly ZARModManager) - step 1: inspect only.
//
// A version.dll stand-in that UnityPlayer.dll loads from the game folder. Every version.dll call is forwarded to the
// real System32 DLL (forward.asm). Inside zombieassault.exe only, one background thread:
//   1. waits for GameAssembly.dll and the IL2CPP runtime (the game's own il2cpp_* API, resolved at run time),
//   2. writes ZAMods\classes.txt (every class of the game's own assemblies) and ZAMods\schema.txt (the fields and
//      methods of its health / enemy / zombie / entity classes),
//   3. on F8 while the game window has focus, writes ZAMods\snapshot_N.txt: a read-only walk of the live objects
//      reachable from the game's static fields, with the values of every health / enemy / entity object it meets.
// Nothing is written into the game: every access to game memory is a guarded read (SEH), and no game code is patched.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <share.h>
#include <cmath>

#include "version.h"  // ZAR_VERSION: the VERSION file, shared with the app (build.cmd)
static const char* kLoaderVersion = ZAR_VERSION;

// ------------------------------------------------------------------------------------------- version.dll proxy --
extern "C" void* za_version_orig[17] = {};
static const char* kVersionExports[17] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
    "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"};

// --------------------------------------------------------------------------------------------------- logging --
static FILE* g_log = nullptr;
static std::wstring g_modsDir;

static void logf(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}

static FILE* open_out(const wchar_t* name) {
    std::wstring p = g_modsDir + L"\\" + name;
    FILE* f = nullptr; _wfopen_s(&f, p.c_str(), L"w");
    return f;
}

// ------------------------------------------------------------------------------------------------ IL2CPP API --
struct Il2CppApi {
    void* (*get_corlib)();
    void* (*domain_get)();
    void* (*thread_attach)(void*);
    void** (*domain_get_assemblies)(void*, size_t*);
    void* (*assembly_get_image)(void*);
    const char* (*image_get_name)(void*);
    size_t (*image_get_class_count)(void*);
    void* (*image_get_class)(void*, size_t);
    const char* (*class_get_name)(void*);
    const char* (*class_get_namespace)(void*);
    void* (*class_get_parent)(void*);
    void* (*class_get_fields)(void*, void**);
    void* (*class_get_methods)(void*, void**);
    void* (*class_get_image)(void*);
    bool (*class_is_valuetype)(void*);
    bool (*class_is_enum)(void*);
    bool (*class_is_generic)(void*);
    void* (*class_get_element_class)(void*);
    int (*class_array_element_size)(void*);
    int32_t (*class_instance_size)(void*);
    void* (*class_from_type)(void*);
    const char* (*field_get_name)(void*);
    void* (*field_get_type)(void*);
    size_t (*field_get_offset)(void*);
    int (*field_get_flags)(void*);
    void (*field_static_get_value)(void*, void*);
    char* (*type_get_name)(void*);
    int (*type_get_type)(void*);
    const char* (*method_get_name)(void*);
    uint32_t (*method_get_param_count)(void*);
    uint32_t (*array_length)(void*);
    int32_t (*string_length)(void*);
    uint16_t* (*string_chars)(void*);
    void (*free_)(void*);
    void* (*class_from_name)(void*, const char*, const char*);
    void* (*class_get_method_from_name)(void*, const char*, int);
    void* (*runtime_invoke)(void*, void*, void**, void**);
    void* (*object_new)(void*);
    void* (*array_new)(void*, uintptr_t);
    void* (*object_unbox)(void*);
    void* (*string_new)(const char*);
    void* (*type_get_object)(void*);
    void* (*class_get_type)(void*);
    // GC handles are pointer-sized in this IL2CPP (Il2CppGCHandle); 0.2.0 truncated them to 32 bits and every use faulted.
    void* (*gchandle_new)(void*, bool);
    void* (*gchandle_get_target)(void*);
    void (*gchandle_free)(void*);
    void* (*method_get_param)(void*, uint32_t);
};
static Il2CppApi g_api = {};

static bool resolve_api(HMODULE ga) {
    bool ok = true;
#define R(field, name)                                                         \
    g_api.field = (decltype(g_api.field))GetProcAddress(ga, name);             \
    if (!g_api.field) { logf("missing IL2CPP export %s", name); ok = false; }
    R(get_corlib, "il2cpp_get_corlib");
    R(domain_get, "il2cpp_domain_get");
    R(thread_attach, "il2cpp_thread_attach");
    R(domain_get_assemblies, "il2cpp_domain_get_assemblies");
    R(assembly_get_image, "il2cpp_assembly_get_image");
    R(image_get_name, "il2cpp_image_get_name");
    R(image_get_class_count, "il2cpp_image_get_class_count");
    R(image_get_class, "il2cpp_image_get_class");
    R(class_get_name, "il2cpp_class_get_name");
    R(class_get_namespace, "il2cpp_class_get_namespace");
    R(class_get_parent, "il2cpp_class_get_parent");
    R(class_get_fields, "il2cpp_class_get_fields");
    R(class_get_methods, "il2cpp_class_get_methods");
    R(class_get_image, "il2cpp_class_get_image");
    R(class_is_valuetype, "il2cpp_class_is_valuetype");
    R(class_is_enum, "il2cpp_class_is_enum");
    R(class_is_generic, "il2cpp_class_is_generic");
    R(class_get_element_class, "il2cpp_class_get_element_class");
    R(class_array_element_size, "il2cpp_class_array_element_size");
    R(class_instance_size, "il2cpp_class_instance_size");
    R(class_from_type, "il2cpp_class_from_type");
    R(field_get_name, "il2cpp_field_get_name");
    R(field_get_type, "il2cpp_field_get_type");
    R(field_get_offset, "il2cpp_field_get_offset");
    R(field_get_flags, "il2cpp_field_get_flags");
    R(field_static_get_value, "il2cpp_field_static_get_value");
    R(type_get_name, "il2cpp_type_get_name");
    R(type_get_type, "il2cpp_type_get_type");
    R(method_get_name, "il2cpp_method_get_name");
    R(method_get_param_count, "il2cpp_method_get_param_count");
    R(array_length, "il2cpp_array_length");
    R(string_length, "il2cpp_string_length");
    R(string_chars, "il2cpp_string_chars");
    R(free_, "il2cpp_free");
    R(class_from_name, "il2cpp_class_from_name");
    R(class_get_method_from_name, "il2cpp_class_get_method_from_name");
    R(runtime_invoke, "il2cpp_runtime_invoke");
    R(object_new, "il2cpp_object_new");
    R(array_new, "il2cpp_array_new");
    R(object_unbox, "il2cpp_object_unbox");
    R(string_new, "il2cpp_string_new");
    R(type_get_object, "il2cpp_type_get_object");
    R(class_get_type, "il2cpp_class_get_type");
    R(gchandle_new, "il2cpp_gchandle_new");
    R(gchandle_get_target, "il2cpp_gchandle_get_target");
    R(gchandle_free, "il2cpp_gchandle_free");
    R(method_get_param, "il2cpp_method_get_param");
#undef R
    return ok;
}

// ----------------------------------------------------------- guarded access (no C++ objects in these functions) --
static bool plausible(const void* p) {
    uintptr_t v = (uintptr_t)p;
    return v >= 0x10000 && v < 0x00007FFFFFFFFFFFull && (v & 7) == 0;
}
static bool safe_copy(void* dst, const void* src, size_t n) {
    __try { memcpy(dst, src, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void* safe_ptr_at(const void* base, size_t off) {
    void* v = nullptr;
    return safe_copy(&v, (const uint8_t*)base + off, sizeof(v)) ? v : nullptr;
}
static void* safe_class_image(void* k) {
    __try { return g_api.class_get_image(k); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static bool safe_static_get(void* field, void** out) {
    __try { *out = nullptr; g_api.field_static_get_value(field, out); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = nullptr; return false; }
}
static bool safe_array_length(void* arr, uint32_t* out) {
    __try { *out = g_api.array_length(arr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static int safe_string(void* s, wchar_t* buf, int cap) {  // copies at most cap-1 chars; -1 on failure
    __try {
        int32_t n = g_api.string_length(s);
        if (n < 0 || n > 100000) return -1;
        uint16_t* c = g_api.string_chars(s);
        int m = n < cap - 1 ? n : cap - 1;
        for (int i = 0; i < m; i++) buf[i] = (wchar_t)c[i];
        buf[m] = 0;
        return m;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void* safe_object_class(void* obj) {
    if (!plausible(obj)) return nullptr;
    return safe_ptr_at(obj, 0);  // Il2CppObject.klass
}

// ---------------------------------------------------------------------------------------------- reflection --
enum : int {  // Il2CppTypeEnum (ECMA-335 element types)
    T_BOOLEAN = 0x02, T_CHAR = 0x03, T_I1 = 0x04, T_U1 = 0x05, T_I2 = 0x06, T_U2 = 0x07, T_I4 = 0x08, T_U4 = 0x09,
    T_I8 = 0x0a, T_U8 = 0x0b, T_R4 = 0x0c, T_R8 = 0x0d, T_STRING = 0x0e, T_PTR = 0x0f, T_VALUETYPE = 0x11,
    T_CLASS = 0x12, T_VAR = 0x13, T_ARRAY = 0x14, T_GENERICINST = 0x15, T_I = 0x18, T_U = 0x19, T_OBJECT = 0x1c,
    T_SZARRAY = 0x1d, T_MVAR = 0x1e,
};
static const int FIELD_STATIC = 0x10, FIELD_LITERAL = 0x40;
static const size_t OBJECT_HEADER = 16;  // Il2CppObject: klass + monitor
static const size_t ARRAY_DATA = 32;     // Il2CppArray: header + bounds + max_length

struct FieldDesc {
    void* field; std::string name; void* type; int kind; size_t offset; int flags; void* klass;
    bool reference; bool valuetype; bool isenum;
};

static std::unordered_set<void*> g_images, g_gameImages;
static std::unordered_map<void*, std::vector<FieldDesc>> g_instanceFields, g_staticFields;

static std::string type_name(void* type) {
    char* n = g_api.type_get_name(type);
    std::string s = n ? n : "?";
    if (n) g_api.free_(n);
    return s;
}
static std::string class_full_name(void* k) {
    const char* ns = g_api.class_get_namespace(k);
    const char* n = g_api.class_get_name(k);
    std::string s = (ns && *ns) ? std::string(ns) + "." : std::string();
    return s + (n ? n : "?");
}
static bool valid_class(void* k) {
    if (!plausible(k)) return false;
    void* img = safe_class_image(k);
    return img && g_images.count(img);
}
static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
// Class-name keywords for schema.txt and the F8 snapshot: ZAMods\inspect.txt, one per line ('#' comments), created
// with these defaults when missing.
static std::vector<std::string> g_keywords;
static const char* kDefaultKeywords[] = {"health", "enemy", "zombie", "entity", "boss", "mob", "hud", "objective",
                                        "timer", "gamestate", "mission", "elapsed", "clock", "dialogue"};
static void load_keywords() {
    std::wstring path = g_modsDir + L"\\inspect.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) {
        if (_wfopen_s(&f, path.c_str(), L"w") == 0 && f) {
            fputs("# Class-name keywords (case-insensitive) for schema.txt and the F8 snapshot. One per line.\n", f);
            for (const char* k : kDefaultKeywords) fprintf(f, "%s\n", k);
            fclose(f);
        }
        for (const char* k : kDefaultKeywords) g_keywords.push_back(k);
        return;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (!s.empty() && s[0] != '#') g_keywords.push_back(lower(s));
    }
    fclose(f);
}
static bool interesting(const std::string& name) {
    std::string l = lower(name);
    for (const std::string& w : g_keywords)
        if (l.find(w) != std::string::npos) return true;
    return false;
}

static FieldDesc describe_field(void* f) {
    FieldDesc d{};
    d.field = f;
    const char* n = g_api.field_get_name(f);
    d.name = n ? n : "?";
    d.type = g_api.field_get_type(f);
    d.kind = g_api.type_get_type(d.type);
    d.offset = g_api.field_get_offset(f);
    d.flags = g_api.field_get_flags(f);
    d.klass = g_api.class_from_type(d.type);
    switch (d.kind) {
        case T_CLASS: case T_OBJECT: case T_STRING: case T_SZARRAY: case T_ARRAY: d.reference = true; break;
        case T_GENERICINST: d.reference = !(d.klass && g_api.class_is_valuetype(d.klass)); d.valuetype = !d.reference; break;
        case T_VALUETYPE: d.valuetype = true; break;
        default: break;
    }
    if (d.valuetype && d.klass) d.isenum = g_api.class_is_enum(d.klass);
    return d;
}

// Fields declared on k and every parent (instance fields), or declared on k only (static fields).
static const std::vector<FieldDesc>& fields_of(void* k, bool statics) {
    auto& cache = statics ? g_staticFields : g_instanceFields;
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    std::vector<FieldDesc> out;
    for (void* c = k; c; c = statics ? nullptr : g_api.class_get_parent(c)) {
        void* iter = nullptr;
        while (void* f = g_api.class_get_fields(c, &iter)) {
            FieldDesc d = describe_field(f);
            bool isStatic = (d.flags & FIELD_STATIC) != 0;
            if (isStatic != statics || (d.flags & FIELD_LITERAL)) continue;
            if (statics && d.offset == (size_t)-1) continue;  // thread-static
            out.push_back(d);
        }
    }
    return cache.emplace(k, std::move(out)).first->second;
}

static std::string format_value(int kind, void* klass, const uint8_t* addr) {
    uint8_t b[8] = {};
    char s[160];
    int size = 8;
    switch (kind) {
        case T_BOOLEAN: case T_I1: case T_U1: size = 1; break;
        case T_CHAR: case T_I2: case T_U2: size = 2; break;
        case T_I4: case T_U4: case T_R4: size = 4; break;
        default: break;
    }
    if (kind == T_VALUETYPE && klass && g_api.class_is_enum(klass)) {  // an enum: read its underlying value__ type
        kind = T_I4;
        for (const FieldDesc& d : fields_of(klass, false))
            if (d.name == "value__") { kind = d.kind; break; }
        switch (kind) {
            case T_BOOLEAN: case T_I1: case T_U1: size = 1; break;
            case T_CHAR: case T_I2: case T_U2: size = 2; break;
            case T_I8: case T_U8: size = 8; break;
            default: size = 4; break;
        }
    }
    if (!safe_copy(b, addr, size)) return "<unreadable>";
    switch (kind) {
        case T_BOOLEAN: return b[0] ? "true" : "false";
        case T_I1: snprintf(s, sizeof s, "%d", (int8_t)b[0]); break;
        case T_U1: snprintf(s, sizeof s, "%u", b[0]); break;
        case T_CHAR: case T_U2: snprintf(s, sizeof s, "%u", *(uint16_t*)b); break;
        case T_I2: snprintf(s, sizeof s, "%d", *(int16_t*)b); break;
        case T_I4: snprintf(s, sizeof s, "%d", *(int32_t*)b); break;
        case T_U4: snprintf(s, sizeof s, "%u", *(uint32_t*)b); break;
        case T_I8: snprintf(s, sizeof s, "%lld", (long long)*(int64_t*)b); break;
        case T_U8: snprintf(s, sizeof s, "%llu", (unsigned long long)*(uint64_t*)b); break;
        case T_R4: snprintf(s, sizeof s, "%g", *(float*)b); break;
        case T_R8: snprintf(s, sizeof s, "%g", *(double*)b); break;
        case T_PTR: case T_I: case T_U: snprintf(s, sizeof s, "0x%llx", (unsigned long long)*(uint64_t*)b); break;
        case T_STRING: {
            void* str = *(void**)b;
            if (!str) return "null";
            wchar_t w[81];
            if (!plausible(str) || safe_string(str, w, 81) < 0) return "<bad string>";
            char u[256];
            WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr);
            return std::string("\"") + u + "\"";
        }
        default: {
            void* p = *(void**)b;
            if (!p) return "null";
            void* k = safe_object_class(p);
            if (!valid_class(k)) return "<ref>";
            snprintf(s, sizeof s, "-> %s @%p", class_full_name(k).c_str(), p);
        }
    }
    return s;
}

// The values of an object (or of a struct at base, with struct field offsets corrected by -header).
static void dump_values(FILE* f, void* klass, const uint8_t* base, bool isStruct, const char* indent, int depth) {
    int n = 0;
    for (const FieldDesc& d : fields_of(klass, false)) {
        if (++n > 80) { fprintf(f, "%s...\n", indent); break; }
        const uint8_t* addr = base + d.offset - (isStruct ? OBJECT_HEADER : 0);
        if (d.valuetype && !d.isenum && d.klass) {
            fprintf(f, "%s%s : %s\n", indent, d.name.c_str(), type_name(d.type).c_str());
            if (depth < 2) {  // two levels: Nullable<HealthState> -> HealthState -> current / max
                std::string deeper = std::string(indent) + "    ";
                dump_values(f, d.klass, addr, true, deeper.c_str(), depth + 1);
            }
            continue;
        }
        fprintf(f, "%s%s : %s = %s\n", indent, d.name.c_str(), type_name(d.type).c_str(),
                format_value(d.kind, d.klass, addr).c_str());
    }
}

// ------------------------------------------------------------------------------------------- startup dumps --
static std::vector<void*> g_gameClasses;

static void collect_images() {
    g_images.clear(); g_gameImages.clear(); g_gameClasses.clear();
    size_t n = 0;
    void** assemblies = g_api.domain_get_assemblies(g_api.domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        void* img = g_api.assembly_get_image(assemblies[i]);
        if (!img) continue;
        g_images.insert(img);
        std::string name = g_api.image_get_name(img);
        if (name.rfind("NinjaKiwi", 0) == 0 || name.rfind("Assembly-CSharp", 0) == 0) {
            g_gameImages.insert(img);
            size_t c = g_api.image_get_class_count(img);
            for (size_t j = 0; j < c; j++)
                if (void* k = g_api.image_get_class(img, j)) g_gameClasses.push_back(k);
        }
    }
}

static void dump_schema() {
    FILE* cls = open_out(L"classes.txt");
    FILE* sch = open_out(L"schema.txt");
    if (!cls || !sch) { logf("cannot write classes.txt / schema.txt"); if (cls) fclose(cls); if (sch) fclose(sch); return; }
    int interestingCount = 0;
    for (void* k : g_gameClasses) {
        std::string full = class_full_name(k);
        fprintf(cls, "%s | %s\n", g_api.image_get_name(g_api.class_get_image(k)), full.c_str());
        if (!interesting(g_api.class_get_name(k))) continue;
        interestingCount++;
        void* parent = g_api.class_get_parent(k);
        fprintf(sch, "\n== %s : %s  (%s, instance size %d)\n", full.c_str(),
                parent ? class_full_name(parent).c_str() : "-", g_api.class_is_valuetype(k) ? "struct" : "class",
                g_api.class_instance_size(k));
        void* iter = nullptr;
        while (void* f = g_api.class_get_fields(k, &iter)) {
            FieldDesc d = describe_field(f);
            fprintf(sch, "   %s%s %s  @0x%zx\n", (d.flags & FIELD_STATIC) ? "static " : "",
                    type_name(d.type).c_str(), d.name.c_str(), d.offset);
        }
        iter = nullptr;
        std::string methods;
        while (void* m = g_api.class_get_methods(k, &iter)) {
            char buf[160];
            snprintf(buf, sizeof buf, "%s(%u) ", g_api.method_get_name(m), g_api.method_get_param_count(m));
            methods += buf;
        }
        if (!methods.empty()) fprintf(sch, "   methods: %s\n", methods.c_str());
    }
    fclose(cls); fclose(sch);
    logf("wrote classes.txt (%zu classes) and schema.txt (%d classes matching inspect.txt)",
         g_gameClasses.size(), interestingCount);
}

// ---------------------------------------------------------------------------------------------- F8 snapshot --
static int g_snapshotNo = 0;

// With each snapshot: ZAMods\code_addresses.txt, where the methods of the game's mission and lobby classes are in
// GameAssembly.dll (and their parameter types), for reading their machine code from the DLL on disk. A method's code
// is only known once the game has used its class ("not loaded yet" until then).
static void write_code_addresses() {
    FILE* f = open_out(L"code_addresses.txt");
    if (!f) return;
    uintptr_t base = (uintptr_t)GetModuleHandleW(L"GameAssembly.dll");
    int classes = 0;
    for (void* k : g_gameClasses) {
        std::string l = lower(g_api.class_get_name(k));
        if (l.find("mission") == std::string::npos && l.find("lobby") == std::string::npos) continue;
        classes++;
        fprintf(f, "== %s\n", class_full_name(k).c_str());
        void* iter = nullptr;
        while (void* m = g_api.class_get_methods(k, &iter)) {
            uintptr_t p = *(uintptr_t*)m;  // MethodInfo.methodPointer
            std::string params;
            uint32_t n = g_api.method_get_param_count(m);
            for (uint32_t i = 0; i < n; i++) params += (i ? ", " : "") + type_name(g_api.method_get_param(m, i));
            if (p) fprintf(f, "   rva 0x%zx  %s(%s)\n", (size_t)(p - base), g_api.method_get_name(m), params.c_str());
            else fprintf(f, "   (not loaded yet)  %s(%s)\n", g_api.method_get_name(m), params.c_str());
        }
    }
    fclose(f);
    logf("wrote code_addresses.txt (%d mission / lobby classes)", classes);
}

static void snapshot() {
    collect_images();
    wchar_t name[64]; swprintf_s(name, L"snapshot_%d.txt", ++g_snapshotNo);
    FILE* f = open_out(name);
    if (!f) { logf("cannot write snapshot"); return; }
    DWORD t0 = GetTickCount();

    struct Node { void* obj; int depth; std::string path; };
    std::deque<Node> queue;
    std::unordered_set<void*> visited;
    std::unordered_map<std::string, int> counts;   // interesting class -> instances seen
    std::unordered_map<std::string, int> recorded; // interesting class -> instances written
    int roots = 0;
    const int MAX_DEPTH = 7, MAX_OBJECTS = 80000, MAX_ELEMENTS = 512, MAX_RECORDS_PER_CLASS = 20;

    // Roots: every non-null static reference field of the game's own classes.
    for (void* k : g_gameClasses) {
        if (g_api.class_is_generic(k)) continue;
        for (const FieldDesc& d : fields_of(k, true)) {
            if (!d.reference || d.kind == T_STRING) continue;
            void* v = nullptr;
            if (!safe_static_get(d.field, &v) || !plausible(v)) continue;
            queue.push_back({v, 0, class_full_name(k) + "::" + d.name});
            roots++;
        }
    }

    fprintf(f, "ZARModManager %s snapshot %d\n\n", kLoaderVersion, g_snapshotNo);
    while (!queue.empty() && (int)visited.size() < MAX_OBJECTS) {
        Node n = std::move(queue.front()); queue.pop_front();
        if (!visited.insert(n.obj).second) continue;
        void* k = safe_object_class(n.obj);
        if (!valid_class(k)) continue;
        const char* cname = g_api.class_get_name(k);
        std::string full = class_full_name(k);
        bool isArray = cname && strstr(cname, "[]") != nullptr;
        bool inGame = g_gameImages.count(g_api.class_get_image(k)) != 0;

        if (interesting(cname ? cname : "") && !isArray) {
            int c = ++counts[full];
            if (c <= MAX_RECORDS_PER_CLASS) {
                recorded[full]++;
                fprintf(f, "== %s @%p\n   path: %s\n", full.c_str(), n.obj, n.path.c_str());
                dump_values(f, k, (const uint8_t*)n.obj, false, "   ", 0);
                fputc('\n', f);
            }
        }
        if (n.depth >= MAX_DEPTH) continue;
        std::string ns = g_api.class_get_namespace(k);
        bool expand = isArray || inGame || ns.rfind("System.Collections", 0) == 0;
        if (!expand) continue;

        if (isArray) {
            uint32_t len = 0;
            if (!safe_array_length(n.obj, &len)) continue;
            void* elem = g_api.class_get_element_class(k);
            if (!elem) continue;
            const uint8_t* data = (const uint8_t*)n.obj + ARRAY_DATA;
            uint32_t m = std::min<uint32_t>(len, MAX_ELEMENTS);
            if (!g_api.class_is_valuetype(elem)) {
                for (uint32_t i = 0; i < m; i++) {
                    void* child = safe_ptr_at(data, i * 8);
                    if (plausible(child) && !visited.count(child))
                        queue.push_back({child, n.depth + 1, n.path + "[" + std::to_string(i) + "]"});
                }
            } else {
                int esize = g_api.class_array_element_size(elem);
                if (esize <= 0) continue;
                bool structInteresting = interesting(g_api.class_get_name(elem));
                for (uint32_t i = 0; i < m; i++) {
                    const uint8_t* e = data + (size_t)i * esize;
                    if (structInteresting && ++counts[class_full_name(elem)] <= MAX_RECORDS_PER_CLASS) {
                        fprintf(f, "== struct %s  path: %s[%u]\n", class_full_name(elem).c_str(), n.path.c_str(), i);
                        dump_values(f, elem, e, true, "   ", 0);
                        fputc('\n', f);
                    }
                    for (const FieldDesc& d : fields_of(elem, false)) {
                        if (!d.reference) continue;
                        void* child = safe_ptr_at(e, d.offset - OBJECT_HEADER);
                        if (plausible(child) && !visited.count(child))
                            queue.push_back({child, n.depth + 1, n.path + "[" + std::to_string(i) + "]." + d.name});
                    }
                }
            }
            continue;
        }
        for (const FieldDesc& d : fields_of(k, false)) {
            if (!d.reference || d.kind == T_STRING) continue;
            void* child = safe_ptr_at(n.obj, d.offset);
            if (plausible(child) && !visited.count(child))
                queue.push_back({child, n.depth + 1, n.path + "." + d.name});
        }
    }

    fprintf(f, "\n== SUMMARY: %d static roots, %zu objects visited, %lu ms\n", roots, visited.size(),
            GetTickCount() - t0);
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) { return a.second > b.second; });
    for (auto& [cls, c] : sorted) fprintf(f, "   %6d  %s\n", c, cls.c_str());
    fclose(f);
    logf("snapshot %d: %d roots, %zu objects, %zu health / enemy / entity classes seen (%lu ms)", g_snapshotNo, roots,
         visited.size(), counts.size(), GetTickCount() - t0);
    write_code_addresses();
}

// ----------------------------------------------------------------------------------------- F9 entity report --
// GameRunner.<Instance>k__BackingField .worldController .entitiesById (Dictionary<GameEntityId, EntityDisplay>): every
// entity of the mission, each with the display layer's copy of its native HealthState (lastHealth / lastShieldHealth).
// Every class, field and offset is found by name through the IL2CPP API; a missing one stops the report (fail closed).
static int g_reportNo = 0;

static const FieldDesc* find_field(void* k, const char* name, bool statics) {
    if (!k) return nullptr;
    for (const FieldDesc& d : fields_of(k, statics))
        if (d.name == name) return &d;
    return nullptr;
}
static const FieldDesc* find_field_any(void* k, std::initializer_list<const char*> names, bool statics = false) {
    for (const char* n : names)
        if (const FieldDesc* d = find_field(k, n, statics)) return d;
    return nullptr;
}
static void* find_game_class(const char* name) {
    for (void* k : g_gameClasses)
        if (strcmp(g_api.class_get_name(k), name) == 0) return k;
    return nullptr;
}
// Enum value -> name, from the enum's literal fields.
static std::unordered_map<int64_t, std::string> enum_names(void* klass) {
    std::unordered_map<int64_t, std::string> out;
    if (!klass) return out;
    void* iter = nullptr;
    while (void* f = g_api.class_get_fields(klass, &iter)) {
        if (!(g_api.field_get_flags(f) & FIELD_LITERAL)) continue;
        void* raw = nullptr;  // 8 bytes: large enough for any enum underlying type
        if (!safe_static_get(f, &raw)) continue;
        out[(int64_t)(uint8_t)(uintptr_t)raw] = g_api.field_get_name(f);  // GameEntityType is a byte enum
    }
    return out;
}
// A Nullable<HealthState> field of obj -> {current, max, reserve}. False when it has no value.
static bool read_health(const uint8_t* obj, const FieldDesc* nf, float out[3]) {
    if (!nf || !nf->klass) return false;
    const FieldDesc* has = find_field_any(nf->klass, {"hasValue", "has_value"});
    const FieldDesc* val = find_field_any(nf->klass, {"value"});
    if (!has || !val || !val->klass) return false;
    const uint8_t* nb = obj + nf->offset;
    uint8_t hv = 0;
    if (!safe_copy(&hv, nb + has->offset - OBJECT_HEADER, 1) || !hv) return false;
    const uint8_t* hb = nb + val->offset - OBJECT_HEADER;
    const char* names[3] = {"current", "max", "reserve_health"};
    for (int i = 0; i < 3; i++) {
        const FieldDesc* f = find_field(val->klass, names[i], false);
        if (!f || !safe_copy(&out[i], hb + f->offset - OBJECT_HEADER, 4)) return false;
    }
    return true;
}

static void entity_report() {
    collect_images();
    auto fail = [](const char* what) { logf("entity report stopped: %s not found (game updated?)", what); };
    void* runnerClass = find_game_class("GameRunner");
    if (!runnerClass) return fail("class GameRunner");
    const FieldDesc* inst = find_field(runnerClass, "<Instance>k__BackingField", true);
    if (!inst) return fail("GameRunner.Instance");
    void* runner = nullptr;
    if (!safe_static_get(inst->field, &runner) || !plausible(runner)) return fail("a live GameRunner (in a mission?)");
    const FieldDesc* wcF = find_field(runnerClass, "worldController", false);
    if (!wcF) return fail("GameRunner.worldController");
    void* wc = safe_ptr_at(runner, wcF->offset);
    void* wcClass = safe_object_class(wc);
    if (!valid_class(wcClass)) return fail("a live worldController");
    const FieldDesc* dictF = find_field(wcClass, "entitiesById", false);
    if (!dictF) return fail("worldController.entitiesById");
    void* dict = safe_ptr_at(wc, dictF->offset);
    void* dictClass = safe_object_class(dict);
    if (!valid_class(dictClass)) return fail("a live entitiesById dictionary");
    const FieldDesc* entriesF = find_field_any(dictClass, {"_entries", "entries"});
    const FieldDesc* countF = find_field_any(dictClass, {"_count", "count"});
    if (!entriesF || !countF) return fail("Dictionary._entries / _count");
    void* entries = safe_ptr_at(dict, entriesF->offset);
    int32_t count = 0;
    safe_copy(&count, (const uint8_t*)dict + countF->offset, 4);
    uint32_t len = 0;
    void* entriesClass = safe_object_class(entries);
    if (!valid_class(entriesClass) || !safe_array_length(entries, &len)) return fail("the dictionary's entries array");
    void* entryClass = g_api.class_get_element_class(entriesClass);
    const FieldDesc* valueF = find_field(entryClass, "value", false);
    const FieldDesc* nextF = find_field(entryClass, "next", false);
    int esize = g_api.class_array_element_size(entryClass);
    if (!valueF || esize <= 0) return fail("the dictionary entry layout");

    void* displayClass = find_game_class("EntityDisplay");
    const FieldDesc* deadF = find_field(displayClass, "entityIsDead", false);
    const FieldDesc* typeF = find_field(displayClass, "<EntityType>k__BackingField", false);
    const FieldDesc* hpF = find_field(displayClass, "lastHealth", false);
    const FieldDesc* shF = find_field(displayClass, "lastShieldHealth", false);
    if (!deadF || !typeF || !hpF || !shF) return fail("EntityDisplay.entityIsDead / EntityType / lastHealth / lastShieldHealth");
    auto typeNames = enum_names(typeF->klass);

    wchar_t name[64]; swprintf_s(name, L"entities_%d.txt", ++g_reportNo);
    FILE* f = open_out(name);
    if (!f) { logf("cannot write entity report"); return; }
    fprintf(f, "ZARModManager %s entity report %d: %d entities in entitiesById\n\n", kLoaderVersion, g_reportNo, count);
    fprintf(f, "%-6s %-18s %-5s %12s %12s %10s %12s\n", "#", "type", "dead", "HP", "max HP", "reserve", "shield");
    std::unordered_map<std::string, int> byType;
    int withHealth = 0;
    const uint8_t* data = (const uint8_t*)entries + ARRAY_DATA;
    for (uint32_t i = 0; i < len && (int32_t)i < count; i++) {
        const uint8_t* e = data + (size_t)i * esize;
        if (nextF) { int32_t next = 0; safe_copy(&next, e + nextF->offset - OBJECT_HEADER, 4); if (next < -1) continue; }
        void* disp = safe_ptr_at(e, valueF->offset - OBJECT_HEADER);
        if (!plausible(disp) || safe_object_class(disp) != displayClass) continue;
        const uint8_t* d = (const uint8_t*)disp;
        uint8_t dead = 0, type = 0;
        safe_copy(&dead, d + deadF->offset, 1);
        safe_copy(&type, d + typeF->offset, 1);
        auto tn = typeNames.find(type);
        std::string tname = tn != typeNames.end() ? tn->second : std::to_string(type);
        byType[tname]++;
        float hp[3] = {}, sh[3] = {};
        bool hasHp = read_health(d, hpF, hp), hasSh = read_health(d, shF, sh);
        if (!hasHp && tname != "Enemy" && tname != "Player") continue;  // list every enemy / player, others only with HP
        if (hasHp) withHealth++;
        char hpS[32] = "-", maxS[32] = "-", resS[32] = "-", shS[48] = "-";
        if (hasHp) { snprintf(hpS, 32, "%.1f", hp[0]); snprintf(maxS, 32, "%.1f", hp[1]); snprintf(resS, 32, "%.1f", hp[2]); }
        if (hasSh) snprintf(shS, 48, "%.1f/%.1f", sh[0], sh[1]);
        fprintf(f, "%-6u %-18s %-5s %12s %12s %10s %12s\n", i, tname.c_str(), dead ? "yes" : "no", hpS, maxS, resS, shS);
    }
    fprintf(f, "\nby type:");
    for (auto& [t, c] : byType) fprintf(f, "  %s %d", t.c_str(), c);
    fputc('\n', f);
    fclose(f);
    logf("entity report %d: %d entities, %d with a health state", g_reportNo, count, withHealth);
}

#include "runtime.inl"
#include "ui.inl"
#include "system.inl"

// -------------------------------------------------------------------------------------------------- thread --
static bool game_has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

static DWORD WINAPI main_thread(LPVOID) {
    HMODULE ga = nullptr;
    for (int i = 0; i < 1200 && !(ga = GetModuleHandleW(L"GameAssembly.dll")); i++) Sleep(100);
    if (!ga) { fail_box("GameAssembly.dll never loaded (2 minutes): the game did not start as expected."); return 0; }
    load_keywords();
    logf("inspect keywords: %zu (ZAMods\\inspect.txt)", g_keywords.size());
    if (!resolve_api(ga)) { fail_box("The game's IL2CPP API is incomplete (a game update?): nothing was changed."); return 0; }
    logf("GameAssembly.dll found; waiting for the IL2CPP runtime");

    void* domain = nullptr;
    for (int i = 0; i < 1200; i++) {  // up to 5 minutes
        if (g_api.get_corlib()) { domain = g_api.domain_get(); break; }
        Sleep(250);
    }
    if (!domain) { fail_box("The game's IL2CPP runtime never initialised (5 minutes)."); return 0; }
    g_api.thread_attach(domain);

    bool gameAssembly = false;
    for (int i = 0; i < 240 && !gameAssembly; i++) {  // up to 60 s for the game's own assembly
        collect_images();
        for (void* img : g_gameImages)
            if (strcmp(g_api.image_get_name(img), "NinjaKiwi.ZombieAssault.Runtime.dll") == 0) gameAssembly = true;
        if (!gameAssembly) Sleep(250);
    }
    logf("IL2CPP ready: %zu assemblies, %zu game assemblies, %zu game classes%s", g_images.size(),
         g_gameImages.size(), g_gameClasses.size(), gameAssembly ? "" : " (NinjaKiwi.ZombieAssault.Runtime.dll NOT found)");
    dump_schema();
    start_runtime();
    logf("ready: in a mission (game window focused) F7 toggles labels, F8 writes a snapshot, F9 the entity HP report");

    bool wasF7 = false, wasF8 = false, wasF9 = false;
    for (;;) {
        bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if (f7 && !wasF7 && game_has_focus()) { g_labelsEnabled = !g_labelsEnabled; logf("labels %s", g_labelsEnabled ? "on" : "off"); }
        wasF7 = f7;
        bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0, f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (f8 && !wasF8 && game_has_focus()) snapshot();
        if (f9 && !wasF9 && game_has_focus()) entity_report();
        wasF8 = f8; wasF9 = f9;
        Sleep(50);
    }
}

// ------------------------------------------------------------------------------------------------- DllMain --
BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    g_selfModule = self;
    g_mainThread = GetCurrentThreadId();  // UnityPlayer's imports load on the game's main thread

    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    wcscat_s(sys, L"\\version.dll");
    if (HMODULE real = LoadLibraryW(sys))
        for (int i = 0; i < 17; i++) za_version_orig[i] = (void*)GetProcAddress(real, kVersionExports[i]);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash || _wcsicmp(slash + 1, L"zombieassault.exe") != 0) return TRUE;  // only inspect inside the game
    *slash = 0;
    g_modsDir = std::wstring(exe) + L"\\ZAMods";
    CreateDirectoryW(g_modsDir.c_str(), nullptr);
    g_log = _wfsopen((g_modsDir + L"\\loader.log").c_str(), L"w", _SH_DENYNO);  // readable while the game runs
    logf("ZARModManager %s runtime loaded into zombieassault.exe (main thread %lu)", kLoaderVersion, g_mainThread);

    if (HANDLE t = CreateThread(nullptr, 0, main_thread, nullptr, 0, nullptr)) CloseHandle(t);
    return TRUE;
}
