/*
 * onnxruntime.dll 代理（仅用于本地样本分析）
 *
 * 为什么需要它：BeeEdit 的 C# 绑定不走导出符号，而是 OrtGetApiBase()->GetApi(v)
 * 取到 OrtApi 函数表后间接调用。直接 hook 导出无效；进程内注入又会触发样本的
 * 反调试自杀。因此在 DLL 层做代理：
 *   1) 把真实 DLL 改名 onnxruntime_orig.dll，本代理同名占位；
 *   2) 转发 OrtGetApiBase，返回一张"打了补丁"的 OrtApi 表；
 *   3) 表里 CreateSessionFromArray（index 8）换成我们的包装：
 *      先把 model_data 落盘，再调用真实实现。
 *   4) CreateSession（index 7）只记录路径，用于判断是否走"先解密到文件"的路径。
 *
 * 除 OrtGetApiBase 外的导出用 .def 转发给真实 DLL，行为完全不变。
 * 调试轨迹写到 %TEMP%\mbonx_proxy.log。
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct OrtApiBase OrtApiBase;
struct OrtApiBase {
    const void* (*GetApi)(uint32_t version);
    const char* (*GetVersionString)(void);
};

#define ORT_API_TABLE_ENTRIES 263 /* ORT 1.20 的 OrtApi 成员数 */
#define ORT_IDX_CREATE_SESSION 7
#define ORT_IDX_CREATE_FROM_ARRAY 8

static HMODULE g_real = NULL;
static const OrtApiBase* g_real_base = NULL;
static const void* (*g_real_get_api)(uint32_t) = NULL;
static void* g_real_create_from_array = NULL;
static void* g_real_create_session = NULL;
static const void* g_patched_api = NULL;
static char g_dump_dir[MAX_PATH] = {0};
static char g_log_path[MAX_PATH] = {0};
static DWORD g_last_len = 0;

static void trace(const char* fmt, ...) {
    if (!g_log_path[0]) return;
    FILE* f = NULL;
    if (fopen_s(&f, g_log_path, "a") != 0 || !f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void resolve_paths(HMODULE self) {
    char tmp[MAX_PATH] = {0};
    DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n > 0) _snprintf_s(g_log_path, MAX_PATH, _TRUNCATE, "%smbonx_proxy.log", tmp);

    DWORD m = GetEnvironmentVariableA("MBONX_DUMP_DIR", g_dump_dir, MAX_PATH);
    if (!(m > 0 && m < MAX_PATH)) {
        char p[MAX_PATH] = {0};
        GetModuleFileNameA(self, p, MAX_PATH);
        char* slash = strrchr(p, '\\');
        if (slash) *slash = 0;
        _snprintf_s(g_dump_dir, MAX_PATH, _TRUNCATE, "%s\\mbonx_dump", p);
        CreateDirectoryA(g_dump_dir, NULL);
    }
    trace("[proxy] init dump_dir=%s", g_dump_dir);
}

static void dump_model(const void* data, size_t len) {
    if (!data || len < 4096) return;
    if ((DWORD)len == g_last_len) return; /* 同一模型重复加载只留一份 */
    char path[MAX_PATH] = {0};
    _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s\\model_%zu.onnx", g_dump_dir, len);
    FILE* f = NULL;
    if (fopen_s(&f, path, "wb") != 0 || !f) {
        trace("[proxy] 写入失败 %s", path);
        return;
    }
    fwrite(data, 1, len, f);
    fclose(f);
    g_last_len = (DWORD)len;
    trace("[proxy] 已落盘 %s (%zu 字节)", path, len);
}

/* 与 OrtApi 中 CreateSessionFromArray 同签名（x64 只有一种调用约定） */
/* 注意：返回类型必须是 OrtStatus*（指针），写成 int 会在 x64 下截断返回值 */
static void* our_create_session_from_array(const void* env, const void* model_data,
                                           size_t model_data_length, const void* options,
                                           void** out) {
    trace("[proxy] CreateSessionFromArray len=%zu", model_data_length);
    dump_model(model_data, model_data_length);
    typedef void* (*fn_t)(const void*, const void*, size_t, const void*, void**);
    if (!g_real_create_from_array) return NULL;
    return ((fn_t)g_real_create_from_array)(env, model_data, model_data_length, options, out);
}

static void* our_create_session(const void* env, const wchar_t* path, const void* options,
                                void** out) {
    trace("[proxy] CreateSession(path) 被调用");
    typedef void* (*fn_t)(const void*, const wchar_t*, const void*, void**);
    if (!g_real_create_session) return NULL;
    return ((fn_t)g_real_create_session)(env, path, options, out);
}

static const void* our_get_api(uint32_t version) {
    if (!g_real_get_api) {
        trace("[proxy] our_get_api: 真实 GetApi 为空");
        return NULL;
    }
    const void* api = g_real_get_api(version);
    trace("[proxy] GetApi(%u) -> %p", version, api);
    if (!api) return NULL;

    /* 原地打补丁：不拷贝整张表。
       拷贝方案会把超出该版本真实长度的槽位填成 0，C# 绑定在
       Marshal.GetDelegateForFunctionPointer 时抛 ArgumentNullException。
       这里只改 CreateSession(7) / CreateSessionFromArray(8) 两项。 */
    if (api != g_patched_api) {
        void** t = (void**)api;
        DWORD old_prot = 0;
        const SIZE_T span = sizeof(void*) * (ORT_IDX_CREATE_FROM_ARRAY + 1);
        if (VirtualProtect(t, span, PAGE_READWRITE, &old_prot)) {
            if (!g_real_create_session) g_real_create_session = t[ORT_IDX_CREATE_SESSION];
            if (!g_real_create_from_array) g_real_create_from_array = t[ORT_IDX_CREATE_FROM_ARRAY];
            t[ORT_IDX_CREATE_SESSION] = (void*)our_create_session;
            t[ORT_IDX_CREATE_FROM_ARRAY] = (void*)our_create_session_from_array;
            VirtualProtect(t, span, old_prot, &old_prot);
            g_patched_api = api;
            trace("[proxy] 已原地替换 CreateSession/CreateSessionFromArray（真实指针 %p / %p）",
                  g_real_create_session, g_real_create_from_array);
        } else {
            trace("[proxy] VirtualProtect 失败，退化为直通");
        }
    }
    return api;
}

static OrtApiBase g_proxy_base = {our_get_api, NULL};

__declspec(dllexport) const OrtApiBase* OrtGetApiBase(void) {
    trace("[proxy] OrtGetApiBase 被调用");
    return &g_proxy_base;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    resolve_paths(self);

    char path[MAX_PATH] = {0};
    GetModuleFileNameA(self, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) {
        strcpy_s(slash + 1, MAX_PATH - (size_t)(slash + 1 - path), "onnxruntime_orig.dll");
    }
    g_real = LoadLibraryA(path);
    trace("[proxy] LoadLibrary(%s) = %p", path, g_real);
    if (g_real) {
        typedef const OrtApiBase* (*get_base_t)(void);
        get_base_t fn = (get_base_t)(void*)GetProcAddress(g_real, "OrtGetApiBase");
        if (fn) {
            g_real_base = fn();
            if (g_real_base) {
                g_real_get_api = g_real_base->GetApi;
                g_proxy_base.GetVersionString = g_real_base->GetVersionString;
            }
        }
        trace("[proxy] real GetApi=%p", g_real_get_api);
    }
    return TRUE;
}
