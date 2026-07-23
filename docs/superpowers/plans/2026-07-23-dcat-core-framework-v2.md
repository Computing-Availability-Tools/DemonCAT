# dcat 核心框架 v2 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。
>
> **本计划取代** `2026-07-21-dcat-core-framework.md`（旧计划基于已废弃的 SQL-like 语法与 5 步预检，与当前 SPEC/DESIGN 不一致）。

**目标：** 实现 DemonCAT（dcat）核心框架——9 个核心模块 + types + injectors 留位 + CMakeLists + cJSON vendoring + 极简测试框架，可编译、可跑单元测试；以 3 条示例故障（rCPU_overload / rNET_delay / rPROC_exit）打通 inject/clean/query/list 端到端，为后续 38 条故障目录打好基础。

**架构：** 分层：main 编排 → cli 解析（argv 子命令式 `--key=value`）→ registry 查找（cnf 优先，injector 回退）→ dispatch 路由 → executor 同步执行脚本 / injector 函数指针。state 持 records（含 params 字段，clean 按参数匹配）+ cJSON 持久化。output 统一 JSON。预检 4 步（无并发检查，允许重复注入）。同步阻塞执行，不区分 bg/sync。

**技术栈：** C11（gnu11），CMake ≥ 3.10，pthread，cJSON（vendored 单文件库），CTest + 极简 `tests/test.h` 断言宏。

**构建环境：** Linux/WSL。`cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure`。当前 Windows 主机无编译器，开发者须在 Linux/WSL 验证 ctest 全绿。

**与当前 SPEC/DESIGN 对齐的关键点（区别于旧计划）：**
- CLI 语法：子命令式 `dcat <sub> [uid] --key=value ...`，`cli_parse(int argc, char **argv, ...)`，非 SQL-like 命令串。
- 预检 4 步（决策 13）：删除"无活跃注入"并发检查；允许同 uid 重复注入（含相同参数）。
- `injection_record_t` 含 `params` 字段；`state_add(uid, params)`；`state_find_by_params(uid, params)` 按 uid+参数匹配多条记录。
- clean 按参数匹配活跃记录，逐条执行 clean 脚本（传记录存储的 inject 参数），某条失败停止，成功条目 mark inactive。
- 所有命令（inject/clean/query）拒绝未在 required_params/optional_params 声明的参数（退出码 3）。
- query 无 uid 由 dcat state 回答（不调脚本）；query 有 uid 走 `executor_run_raw_fault`（system 直通 stdout）→ dcat 打印 `---` + JSON `{confirmed:bool}`。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `CMakeLists.txt` | C11/gnu11，静态链接 cJSON，find_package(Threads)，enable_testing，所有 test_*.c 用 add_test，WORKING_DIRECTORY=源根 |
| `third_party/cjson/cJSON.h` `cJSON.c` | cJSON 单文件库（vendoring，MIT） |
| `tests/test.h` | 极简断言宏：ASSERT_TRUE/STREQ/INT_EQ/STR_CONTAINS + RUN_TEST + 计数 |
| `src/core/types.h` | 公共类型：params_t/result_t/fault_def_t/injection_record_t(含 params) + mock_fn 声明 |
| `src/core/types.c` | params 辅助：params_init/set/find + dcat_key_to_env |
| `src/core/output.h` `output.c` | result_ok/result_err/output_print/result_free（依赖 cJSON） |
| `src/core/config.h` `config.c` | INI 解析 demoncat.conf → config_t{faults[]}；derive_project_root/resolve_script/config_find |
| `src/core/registry.h` `registry.c` | 持 fault_def 静态表；registry_init/find/list/count（未命中回退 injector_find） |
| `src/core/executor.h` `executor.c` | executor_run_fault（fork/exec+pipe+waitpid，可选 timer 超时）/ executor_run_raw_fault（system 直通）/ executor_check_tool / executor_set_mock + build_env |
| `src/core/precheck.h` `precheck.c` | precheck 4 步 + 未声明参数拒绝 + op_in_supported/required_params_present/declared_params_only |
| `src/core/state.h` `state.c` | g_records[32] + pthread_mutex；state_add(uid,params)/find/find_by_params/find_by_id/list/mark_inactive + 持久化 load/save |
| `src/core/dispatch.h` `dispatch.c` | dispatch_route(uid,op,params) + cnf/injector 分流 + inject-only/clean-by-params/query-raw/list |
| `src/injectors/injector.h` | injector_t 接口 + builtin_injectors[] 声明 + injector_find |
| `src/injectors/injectors.c` | builtin_injectors[] 空数组 + injector_find 实现 |
| `src/main.c` | config_load → registry_init → state_load → cli_parse(argv) → dispatch_route → output_print → exit code；/proc/self/exe 配置定位 |
| `tests/test_types.c` | params 辅助 + dcat_key_to_env |
| `tests/test_output.c` | result_ok/err schema（含 inject-only 无 record_id） |
| `tests/test_config.c` | INI 解析 + resolve_script |
| `tests/test_registry.c` | registry_init/find/list |
| `tests/test_executor_mock.c` | mock 捕获 cmd+env；build_env；run_raw mock |
| `tests/test_precheck.c` | 4 步各失败路径 + 未声明参数拒绝 + inject-only |
| `tests/test_state.c` | add/find/find_by_params/mark_inactive + 持久化 roundtrip |
| `tests/test_injectors.c` | 空数组 injector_find 不命中 |
| `tests/test_dispatch.c` | mock executor：inject 写 state、clean 按参数匹配、inject-only 不写 state、list、query 无 uid |
| `tests/test_cli.c` | argv 子命令 + --key=value 解析 |
| `tests/test_faults.c` | 表驱动：3 条示例故障 inject/clean/query 命令串+env（mock） |
| `config/demoncat.conf` | v0.1 示例 3 条：rCPU_overload / rNET_delay / rPROC_exit |
| `config/scripts/cpu/cpu_overload.sh` | 示例占位脚本（echo） |
| `config/scripts/network/net_delay.sh` | 示例占位脚本（echo） |
| `config/scripts/process/proc_exit.sh` | 示例占位脚本（echo） |

**待删除（与决策 9/10 矛盾的过时骨架）：**
- `src/core/safety.c`、`src/core/safety.h`
- `tests/test_autoclean.c`、`tests/test_reaper.c`、`tests/test_safety.c`

---

## 任务 0：清理过时骨架 + 项目骨架（CMakeLists + cJSON + test.h）

**文件：**
- 删除：`src/core/safety.c`、`src/core/safety.h`、`tests/test_autoclean.c`、`tests/test_reaper.c`、`tests/test_safety.c`
- 创建：`CMakeLists.txt`、`tests/test.h`
- vendoring：`third_party/cjson/cJSON.h`、`third_party/cjson/cJSON.c`

- [ ] **步骤 1：删除过时文件**

```bash
rm -f src/core/safety.c src/core/safety.h tests/test_autoclean.c tests/test_reaper.c tests/test_safety.c
```

- [ ] **步骤 2：vendoring cJSON**

从 https://github.com/DaveGamble/cJSON v1.7.18 下载 `cJSON.h` 与 `cJSON.c` 到 `third_party/cjson/`。MIT 许可。

```bash
curl -fsSL https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.h -o third_party/cjson/cJSON.h
curl -fsSL https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.18/cJSON.c -o third_party/cjson/cJSON.c
```

> 若无网络，可手写最小 cJSON 子集（CreateObject/AddStringToObject/AddNumberToObject/AddBoolToObject/AddObjectToObject/AddItemToObject/AddItemToArray/CreateArray/PrintUnformatted/Parse/Delete/GetArrayItem/GetArraySize/GetobjectItem/ArrayForEach）。优先用官方库。

- [ ] **步骤 3：编写极简测试框架 `tests/test.h`**

```c
#ifndef DCAT_TEST_H
#define DCAT_TEST_H
#include <stdio.h>
#include <string.h>
static int g_test_count = 0, g_test_fail = 0;
#define RUN_TEST(fn) do { \
    g_test_count++; \
    fprintf(stderr, "  -> %s ... ", #fn); \
    int r = fn(); \
    if (r) { g_test_fail++; fprintf(stderr, "FAIL\n"); } \
    else fprintf(stderr, "ok\n"); \
} while (0)
/* NULL 防御 + 单次求值（避免带副作用参数二次求值）；失败 return 1 中断当前测试而非崩进程 */
#define ASSERT_TRUE(x) do { if (!(x)) { fprintf(stderr, "ASSERT_TRUE fail: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_INT_EQ(a, b) do { long _ia = (long)(a); long _ib = (long)(b); if (_ia != _ib) { fprintf(stderr, "INT_EQ fail: %ld != %ld at %s:%d\n", _ia, _ib, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_STREQ(a, b) do { const char *_sa = (a); const char *_sb = (b); if (_sa == NULL || _sb == NULL || strcmp(_sa, _sb) != 0) { fprintf(stderr, "STREQ fail: '%s' != '%s' at %s:%d\n", _sa ? _sa : "(null)", _sb ? _sb : "(null)", __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_STR_CONTAINS(hay, needle) do { const char *_h = (hay); const char *_n = (needle); if (_h == NULL || _n == NULL || strstr(_h, _n) == NULL) { fprintf(stderr, "CONTAINS fail: '%s' not in '%s' at %s:%d\n", _n ? _n : "(null)", _h ? _h : "(null)", __FILE__, __LINE__); return 1; } } while (0)
#define TEST_MAIN_RETURN() (g_test_fail ? 1 : 0)
#endif
```

> **审查裁定（2026-07-23）**：原始简报的 ASSERT 宏不防 NULL 且二次求值（SIGSEGV 风险、带副作用参数被算两次）。经用户确认，允许偏离字面简报修复为上述健壮版本。`g_test_count` 仍保留（RUN_TEST 递增，可在 main 末尾打印汇总）。

- [ ] **步骤 4：编写 `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.10)
project(dcat C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # gnu11 扩展（fork/usleep 等）
find_package(Threads REQUIRED)

add_library(cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC third_party/cjson)

set(DCAT_CORE
    src/core/types.c src/core/cli.c src/core/registry.c src/core/executor.c
    src/core/precheck.c src/core/state.c src/core/config.c
    src/core/output.c src/core/dispatch.c
    src/injectors/injectors.c)
add_executable(dcat src/main.c ${DCAT_CORE})
target_include_directories(dcat PRIVATE src src/core src/injectors third_party/cjson)
target_link_libraries(dcat PRIVATE cjson Threads::Threads)
target_compile_options(dcat PRIVATE -Wall -Wextra -Werror)

enable_testing()
set(DCAT_TESTS test_types test_output test_config test_registry test_executor_mock
    test_precheck test_state test_injectors test_dispatch test_cli test_faults)
foreach(t ${DCAT_TESTS})
    if(EXISTS ${CMAKE_SOURCE_DIR}/tests/${t}.c)
        add_executable(${t} tests/${t}.c ${DCAT_CORE})
        target_include_directories(${t} PRIVATE src src/core src/injectors third_party/cjson tests)
        target_link_libraries(${t} PRIVATE cjson Threads::Threads)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()
endforeach()
```

- [ ] **步骤 5：构建验证（cJSON 库可编译）**

```bash
cmake -B build && cmake --build build --target cjson
```
预期：cJSON 库编译通过（src 各 .c 仍空，dcat/test 链接会失败，本步骤只验证 cJSON）。

- [ ] **步骤 6：Commit**

```bash
git add CMakeLists.txt third_party/cjson/ tests/test.h
git add -A  # 含删除的过时文件
git commit -m "chore: drop stale safety/autoclean/reaper scaffolding + add CMakeLists/cJSON/test.h"
```

---

## 任务 1：types.h + types.c（公共类型 + params 辅助）

**文件：** 创建 `src/core/types.h`、`src/core/types.c`、`tests/test_types.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_types.c`**

```c
#include "test.h"
#include "types.h"

int test_params_set_find(void) {
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");
    ASSERT_STREQ(params_find(&p, "iface"), "eth0");
    ASSERT_STREQ(params_find(&p, "loss_pct"), "5");
    ASSERT_TRUE(params_find(&p, "nope") == NULL);
    /* 覆盖更新 */
    params_set(&p, "iface", "eth1");
    ASSERT_STREQ(params_find(&p, "iface"), "eth1");
    return 0;
}

int test_params_count(void) {
    params_t p; params_init(&p);
    ASSERT_INT_EQ(p.count, 0);
    params_set(&p, "a", "1");
    params_set(&p, "b", "2");
    ASSERT_INT_EQ(p.count, 2);
    return 0;
}

int test_key_to_env(void) {
    /* DCAT_PARAM_<KEY>：非字母数字->'_'，大写 */
    ASSERT_STREQ(dcat_key_to_env("loss_pct"), "DCAT_PARAM_LOSS_PCT");
    ASSERT_STREQ(dcat_key_to_env("speed-mbps"), "DCAT_PARAM_SPEED_MBPS");
    ASSERT_STREQ(dcat_key_to_env("cores"), "DCAT_PARAM_CORES");
    return 0;
}

int test_params_equal_subset(void) {
    /* 用于 clean 按参数匹配：用户提供参数是记录参数的子集则匹配 */
    params_t rec; params_init(&rec);
    params_set(&rec, "iface", "eth0");
    params_set(&rec, "loss_pct", "5");
    params_t q; params_init(&q);
    params_set(&q, "iface", "eth0");           /* 子集 → 匹配 */
    ASSERT_TRUE(params_match_subset(&q, &rec));
    params_set(&q, "loss_pct", "5");
    ASSERT_TRUE(params_match_subset(&q, &rec)); /* 完全一致 → 匹配 */
    params_set(&q, "loss_pct", "3");            /* 值不同 → 不匹配 */
    ASSERT_TRUE(!params_match_subset(&q, &rec));
    params_t q2; params_init(&q2);              /* 空 query 匹配所有 */
    ASSERT_TRUE(params_match_subset(&q2, &rec));
    return 0;
}

int main(void) {
    RUN_TEST(test_params_set_find);
    RUN_TEST(test_params_count);
    RUN_TEST(test_key_to_env);
    RUN_TEST(test_params_equal_subset);
    return TEST_MAIN_RETURN();
}
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build build --target test_types 2>&1 | head -20
ctest --test-dir build -R test_types --output-on-failure
```
预期：编译失败（types.h 不存在）。

- [ ] **步骤 3：实现 `src/core/types.h`**

```c
#ifndef DCAT_TYPES_H
#define DCAT_TYPES_H
#include <stddef.h>

#define DCAT_MAX_PARAMS 16
#define DCAT_KEY_LEN    32
#define DCAT_VAL_LEN    64
typedef struct { char key[DCAT_KEY_LEN]; char value[DCAT_VAL_LEN]; } param_kv_t;
typedef struct { param_kv_t items[DCAT_MAX_PARAMS]; int count; } params_t;

/* result_t: 输出边界，json 由 cJSON 堆分配，调用方 result_free */
typedef struct { int code; char *json; } result_t;

/* fault_def: 由 config.c 从 demoncat.conf 载入；registry 持有表 */
typedef struct {
    char uid[64];
    char module[32];
    char desc[128];
    char script[256];
    char supported_ops[64];      /* "inject" | "inject,clean,query" */
    char required_params[128];   /* "iface,loss_pct" */
    char optional_params[128];   /* 可选参数名 */
} fault_def_t;

/* injection_record_t: state 持有，固定数组 — 仅 inject,clean,query 故障创建 */
typedef struct {
    int  record_id;             /* 单调递增 */
    char uid[64];
    params_t params;            /* inject 时用户提供的参数，用于 clean 按参数匹配 */
    long started_at;
    int  active;                /* 1 活跃，0 已清理 */
} injection_record_t;
#define DCAT_MAX_RECORDS 32

/* mock 钩子：捕获 (cmd, env) 不真正执行；返回伪造 result_t（堆分配，调用方 result_free） */
typedef result_t *(*mock_fn)(const char *cmd, const char *const *env);

/* params 辅助 */
void params_init(params_t *p);
int  params_set(params_t *p, const char *key, const char *val);          /* 覆盖更新；满返回 -1 */
const char *params_find(const params_t *p, const char *key);             /* 未找到返回 NULL */
const char *dcat_key_to_env(const char *key);                            /* 返回静态缓冲，DCAT_PARAM_<KEY> */
int  params_match_subset(const params_t *query, const params_t *record); /* query 每个 key 值与 record 一致则 1 */

#endif /* DCAT_TYPES_H */
```

- [ ] **步骤 4：实现 `src/core/types.c`**

```c
#include "types.h"
#include <string.h>
#include <ctype.h>

void params_init(params_t *p) { p->count = 0; }

int params_set(params_t *p, const char *key, const char *val) {
    for (int i = 0; i < p->count; i++) {
        if (strcmp(p->items[i].key, key) == 0) {
            strncpy(p->items[i].value, val, DCAT_VAL_LEN - 1);
            p->items[i].value[DCAT_VAL_LEN - 1] = '\0';
            return 0;
        }
    }
    if (p->count >= DCAT_MAX_PARAMS) return -1;
    strncpy(p->items[p->count].key, key, DCAT_KEY_LEN - 1);
    p->items[p->count].key[DCAT_KEY_LEN - 1] = '\0';
    strncpy(p->items[p->count].value, val, DCAT_VAL_LEN - 1);
    p->items[p->count].value[DCAT_VAL_LEN - 1] = '\0';
    p->count++;
    return 0;
}

const char *params_find(const params_t *p, const char *key) {
    for (int i = 0; i < p->count; i++)
        if (strcmp(p->items[i].key, key) == 0) return p->items[i].value;
    return NULL;
}

const char *dcat_key_to_env(const char *key) {
    static char buf[64];
    int n = 0;
    strcpy(buf, "DCAT_PARAM_");
    n = (int)strlen(buf);
    for (const char *c = key; *c && n < (int)sizeof(buf) - 1; c++) {
        buf[n++] = isalnum((unsigned char)*c) ? (char)toupper((unsigned char)*c) : '_';
    }
    buf[n] = '\0';
    return buf;
}

int params_match_subset(const params_t *query, const params_t *record) {
    /* query 每个 key 在 record 中存在且值一致则匹配；空 query 匹配所有 */
    for (int i = 0; i < query->count; i++) {
        const char *v = params_find(record, query->items[i].key);
        if (!v || strcmp(v, query->items[i].value) != 0) return 0;
    }
    return 1;
}
```

- [ ] **步骤 5：运行测试验证通过**

```bash
cmake --build build --target test_types && ctest --test-dir build -R test_types --output-on-failure
```
预期：PASS。

- [ ] **步骤 6：Commit**

```bash
git add src/core/types.h src/core/types.c tests/test_types.c
git commit -m "feat(types): params_t helpers + fault_def/injection_record(含 params) + subset match"
```

---

## 任务 2：output.c/h（result_t + JSON 输出）

**文件：** 创建 `src/core/output.h`、`src/core/output.c`、`tests/test_output.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_output.c`**

```c
#include "test.h"
#include "output.h"
#include <string.h>

/* 成功（可恢复 inject）：含 record_id */
int test_ok_recoverable_has_record_id(void) {
    result_t *r = result_ok("inject", "rCPU_overload", 3, "ok");
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_STR_CONTAINS(r->json, "\"op\":\"inject\"");
    ASSERT_STR_CONTAINS(r->json, "\"uid\":\"rCPU_overload\"");
    ASSERT_STR_CONTAINS(r->json, "\"record_id\":3");
    ASSERT_STR_CONTAINS(r->json, "\"message\":\"ok\"");
    ASSERT_INT_EQ(r->code, 0);
    result_free(r); return 0;
}

/* 成功（inject-only）：无 record_id 字段 */
int test_ok_inject_only_no_record_id(void) {
    result_t *r = result_ok("inject", "rPROC_exit", 0, "killed");
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_TRUE(strstr(r->json, "record_id") == NULL);
    result_free(r); return 0;
}

/* 失败 */
int test_err(void) {
    result_t *r = result_err("inject", "rCPU_overload", 3, "missing required param: cores");
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"error\"");
    ASSERT_STR_CONTAINS(r->json, "\"code\":3");
    ASSERT_STR_CONTAINS(r->json, "missing required param");
    ASSERT_INT_EQ(r->code, 3);
    result_free(r); return 0;
}

int main(void) {
    RUN_TEST(test_ok_recoverable_has_record_id);
    RUN_TEST(test_ok_inject_only_no_record_id);
    RUN_TEST(test_err);
    return TEST_MAIN_RETURN();
}
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build build --target test_output 2>&1 | head
```
预期：编译失败（output.h 不存在）。

- [ ] **步骤 3：实现 `src/core/output.h`**

```c
#ifndef DCAT_OUTPUT_H
#define DCAT_OUTPUT_H
#include "types.h"
/* record_id<=0 表示 inject-only，不输出 record_id 字段 */
result_t *result_ok(const char *op, const char *uid, int record_id, const char *message);
result_t *result_err(const char *op, const char *uid, int code, const char *msg);
void output_print(result_t *r);
void result_free(result_t *r);
#endif
```

- [ ] **步骤 4：实现 `src/core/output.c`**（用 cJSON 构造 schema，对齐 SPEC §6）

```c
#include "output.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>

result_t *result_ok(const char *op, const char *uid, int record_id, const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", op);
    if (uid) cJSON_AddStringToObject(root, "uid", uid);
    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (message) cJSON_AddStringToObject(data, "message", message);
    if (record_id > 0) cJSON_AddNumberToObject(data, "record_id", record_id);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t));
    r->code = 0; r->json = s; return r;
}

result_t *result_err(const char *op, const char *uid, int code, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "op", op);
    if (uid) cJSON_AddStringToObject(root, "uid", uid);
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg ? msg : "");
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t));
    r->code = code; r->json = s; return r;
}

void output_print(result_t *r) {
    if (r && r->json) printf("%s\n", r->json);
}

void result_free(result_t *r) {
    if (!r) return;
    free(r->json);
    free(r);
}
```

- [ ] **步骤 5：运行测试验证通过**

```bash
cmake --build build --target test_output && ctest --test-dir build -R test_output --output-on-failure
```
预期：PASS。

- [ ] **步骤 6：Commit**

```bash
git add src/core/output.h src/core/output.c tests/test_output.c
git commit -m "feat(output): result_ok/err + JSON schema per SPEC §6 (inject-only 无 record_id)"
```

---

## 任务 3：config.c/h（INI 解析 + 项目根推导 + 脚本路径解析）

**文件：** 创建 `src/core/config.h`、`src/core/config.c`、`tests/test_config.c`；准备 `config/demoncat.conf`

- [ ] **步骤 1：编写 `config/demoncat.conf`**（v0.1 示例 3 条）

```ini
[demoncat]
state_file = ~/.demoncat/state.json
log_level  = warn

[fault.rCPU_overload]
module          = cpu
desc            = CPU 过载（多核 burn）
script          = config/scripts/cpu/cpu_overload.sh
supported_ops   = inject,clean,query
required_params = cores

[fault.rNET_delay]
module          = network
desc            = 网络延时（tc netem delay）
script          = config/scripts/network/net_delay.sh
supported_ops   = inject,clean,query
required_params = iface,delay_ms

[fault.rPROC_exit]
module          = process
desc            = 进程异常退出（kill -9，不可恢复）
script          = config/scripts/process/proc_exit.sh
supported_ops   = inject
required_params = pid
```

- [ ] **步骤 2：编写失败的测试 `tests/test_config.c`**

```c
#include "test.h"
#include "config.h"
#include <string.h>

int test_load_faults(void) {
    config_t cfg;
    int rc = config_load("config/demoncat.conf", &cfg);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(cfg.fault_count, 3);
    const fault_def_t *f = config_find(&cfg, "rNET_delay");
    ASSERT_TRUE(f != NULL);
    ASSERT_STREQ(f->module, "network");
    ASSERT_STREQ(f->supported_ops, "inject,clean,query");
    ASSERT_STR_CONTAINS(f->required_params, "iface");
    ASSERT_STR_CONTAINS(f->required_params, "delay_ms");
    /* inject-only 无 optional_params */
    const fault_def_t *p = config_find(&cfg, "rPROC_exit");
    ASSERT_TRUE(p != NULL);
    ASSERT_STREQ(p->supported_ops, "inject");
    ASSERT_STREQ(p->optional_params, "");
    ASSERT_TRUE(config_find(&cfg, "nope") == NULL);
    return 0;
}

int test_resolve_script(void) {
    char dst[512];
    resolve_script("/opt/dcat", "scripts/network/net_delay.sh", dst, sizeof(dst));
    ASSERT_STREQ(dst, "/opt/dcat/scripts/network/net_delay.sh");
    resolve_script("/opt/dcat", "/abs/path.sh", dst, sizeof(dst));
    ASSERT_STREQ(dst, "/abs/path.sh");            /* 绝对路径不变 */
    resolve_script("/opt/dcat", "~/x.sh", dst, sizeof(dst));
    ASSERT_STREQ(dst, "~/x.sh");                 /* home 相对不变 */
    resolve_script(".", "scripts/x.sh", dst, sizeof(dst));
    ASSERT_STREQ(dst, "scripts/x.sh");           /* root='.' 走 CWD */
    return 0;
}

int test_derive_project_root(void) {
    char root[256];
    derive_project_root("/opt/dcat/config/demoncat.conf", root, sizeof(root));
    ASSERT_STREQ(root, "/opt/dcat");
    derive_project_root("config/demoncat.conf", root, sizeof(root));
    ASSERT_STREQ(root, ".");                      /* 相对配置 → root='.' */
    return 0;
}

int main(void) {
    RUN_TEST(test_load_faults);
    RUN_TEST(test_resolve_script);
    RUN_TEST(test_derive_project_root);
    return TEST_MAIN_RETURN();
}
```

- [ ] **步骤 3：运行测试验证失败**

```bash
cmake --build build --target test_config 2>&1 | head
```

- [ ] **步骤 4：实现 `src/core/config.h`**

```c
#ifndef DCAT_CONFIG_H
#define DCAT_CONFIG_H
#include "types.h"
#define DCAT_MAX_FAULTS 64
typedef struct {
    char state_file[256];
    char log_level[16];
    fault_def_t faults[DCAT_MAX_FAULTS];
    int fault_count;
} config_t;
int  config_load(const char *path, config_t *cfg);                       /* 失败返回 -1 */
const fault_def_t *config_find(const config_t *cfg, const char *uid);
void resolve_script(const char *root, const char *val, char *dst, int cap);  /* 相对→prepend root；绝对/home/`.` 不变 */
void derive_project_root(const char *cfgpath, char *root, int cap);          /* <root>/config/demoncat.conf → <root>；相对→'.' */
#endif
```

- [ ] **步骤 5：实现 `src/core/config.c`**（手写极简 INI 解析）

```c
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
    return s;
}

int config_load(const char *path, config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    char line[512];
    fault_def_t *cur = NULL;
    char section[128] = "";
    while (fgets(line, sizeof(line), fp)) {
        char *p = trim(line);
        if (*p == '#' || *p == ';' || *p == '\0') continue;
        if (*p == '[') {
            char *e = strchr(p, ']');
            if (!e) continue;
            *e = '\0';
            strncpy(section, p + 1, sizeof(section) - 1);
            section[sizeof(section)-1] = '\0';
            cur = NULL;
            if (strncmp(section, "fault.", 6) == 0 && cfg->fault_count < DCAT_MAX_FAULTS) {
                cur = &cfg->faults[cfg->fault_count++];
                strncpy(cur->uid, section + 6, sizeof(cur->uid) - 1);
                cur->uid[sizeof(cur->uid)-1] = '\0';
            }
            continue;
        }
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = trim(p), *v = trim(eq + 1);
        if (strcmp(section, "demoncat") == 0) {
            if (strcmp(k, "state_file") == 0) { strncpy(cfg->state_file, v, sizeof(cfg->state_file)-1); cfg->state_file[sizeof(cfg->state_file)-1]='\0'; }
            else if (strcmp(k, "log_level") == 0) { strncpy(cfg->log_level, v, sizeof(cfg->log_level)-1); cfg->log_level[sizeof(cfg->log_level)-1]='\0'; }
        } else if (cur) {
            if      (strcmp(k, "module") == 0)          { strncpy(cur->module, v, sizeof(cur->module)-1); cur->module[sizeof(cur->module)-1]='\0'; }
            else if (strcmp(k, "desc") == 0)            { strncpy(cur->desc, v, sizeof(cur->desc)-1); cur->desc[sizeof(cur->desc)-1]='\0'; }
            else if (strcmp(k, "script") == 0)          { strncpy(cur->script, v, sizeof(cur->script)-1); cur->script[sizeof(cur->script)-1]='\0'; }
            else if (strcmp(k, "supported_ops") == 0)   { strncpy(cur->supported_ops, v, sizeof(cur->supported_ops)-1); cur->supported_ops[sizeof(cur->supported_ops)-1]='\0'; }
            else if (strcmp(k, "required_params") == 0) { strncpy(cur->required_params, v, sizeof(cur->required_params)-1); cur->required_params[sizeof(cur->required_params)-1]='\0'; }
            else if (strcmp(k, "optional_params") == 0) { strncpy(cur->optional_params, v, sizeof(cur->optional_params)-1); cur->optional_params[sizeof(cur->optional_params)-1]='\0'; }
        }
    }
    fclose(fp);
    return 0;
}

const fault_def_t *config_find(const config_t *cfg, const char *uid) {
    for (int i = 0; i < cfg->fault_count; i++)
        if (strcmp(cfg->faults[i].uid, uid) == 0) return &cfg->faults[i];
    return NULL;
}

void resolve_script(const char *root, const char *val, char *dst, int cap) {
    if (val[0] == '/' || val[0] == '~' || strcmp(root, ".") == 0) {
        strncpy(dst, val, cap - 1); dst[cap - 1] = '\0';
    } else {
        snprintf(dst, cap, "%s/%s", root, val);
    }
}

void derive_project_root(const char *cfgpath, char *root, int cap) {
    /* <root>/config/demoncat.conf → <root>；相对路径 → '.' */
    const char *marker = "/config/demoncat.conf";
    size_t mlen = strlen(marker);
    size_t plen = strlen(cfgpath);
    if (plen >= mlen && strcmp(cfgpath + plen - mlen, marker) == 0) {
        size_t rlen = plen - mlen;
        if (rlen == 0) { strncpy(root, "/", cap-1); root[cap-1]='\0'; return; }
        if (rlen >= (size_t)cap) rlen = cap - 1;
        memcpy(root, cfgpath, rlen); root[rlen] = '\0';
    } else {
        strncpy(root, ".", cap - 1); root[cap - 1] = '\0';
    }
}
```

- [ ] **步骤 6：运行测试验证通过**

```bash
cmake --build build --target test_config && ctest --test-dir build -R test_config --output-on-failure
```
预期：PASS。

- [ ] **步骤 7：Commit**

```bash
git add src/core/config.h src/core/config.c tests/test_config.c config/demoncat.conf
git commit -m "feat(config): INI parser + project-root/script-path resolution"
```

---

## 任务 4：registry.c/h（fault_def 表 + find/list）

**文件：** 创建 `src/core/registry.h`、`src/core/registry.c`、`tests/test_registry.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_registry.c`**

```c
#include "test.h"
#include "registry.h"
#include "config.h"
#include <string.h>

static config_t cfg;
static void setup(void) { config_load("config/demoncat.conf", &cfg); registry_init(&cfg); }

int test_find_and_list(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay");
    ASSERT_TRUE(f != NULL);
    ASSERT_STREQ(f->module, "network");
    ASSERT_TRUE(registry_find("nope") == NULL);
    int n = 0; registry_list(&n);
    ASSERT_INT_EQ(n, cfg.fault_count);
    ASSERT_INT_EQ(registry_count(), cfg.fault_count);
    return 0;
}

int main(void) { RUN_TEST(test_find_and_list); return TEST_MAIN_RETURN(); }
```

- [ ] **步骤 2：运行测试验证失败**

- [ ] **步骤 3：实现 `src/core/registry.h`**

```c
#ifndef DCAT_REGISTRY_H
#define DCAT_REGISTRY_H
#include "types.h"
#include "config.h"
void registry_init(const config_t *cfg);
const fault_def_t *registry_find(const char *uid);   /* 未命中返回 NULL（dispatch 回退 injector_find） */
const fault_def_t *registry_list(int *count);
int registry_count(void);
#endif
```

- [ ] **步骤 4：实现 `src/core/registry.c`**

```c
#include "registry.h"
#include <string.h>

static fault_def_t g_faults[DCAT_MAX_FAULTS];
static int g_count = 0;

void registry_init(const config_t *cfg) {
    g_count = cfg->fault_count < DCAT_MAX_FAULTS ? cfg->fault_count : DCAT_MAX_FAULTS;
    for (int i = 0; i < g_count; i++) g_faults[i] = cfg->faults[i];
}

const fault_def_t *registry_find(const char *uid) {
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_faults[i].uid, uid) == 0) return &g_faults[i];
    return NULL; /* 未命中：dispatch 回退 injector_find（DESIGN §7.4） */
}

const fault_def_t *registry_list(int *count) { if (count) *count = g_count; return g_faults; }
int registry_count(void) { return g_count; }
```

- [ ] **步骤 5：运行测试验证通过**

```bash
cmake --build build --target test_registry && ctest --test-dir build -R test_registry --output-on-failure
```
预期：PASS。

- [ ] **步骤 6：Commit**

```bash
git add src/core/registry.h src/core/registry.c tests/test_registry.c
git commit -m "feat(registry): fault_def table + find/list + injector fallback hook"
```

---

## 任务 5：executor.c/h（run / run_raw / check_tool / set_mock + build_env）

**文件：** 创建 `src/core/executor.h`、`src/core/executor.c`、`tests/test_executor_mock.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_executor_mock.c`**（mock 捕获 cmd+env，断言 build_env 与环境变量集合）

```c
#include "test.h"
#include "executor.h"
#include "types.h"
#include <string.h>

static const char *g_last_cmd = NULL;
static const char *const *g_last_env = NULL;
static result_t *mock_fn(const char *cmd, const char *const *env) {
    g_last_cmd = cmd; g_last_env = env;
    return result_ok("inject", "rNET_loss", 0, "mocked");
}

int test_build_cmd_and_env(void) {
    executor_set_mock(mock_fn);
    fault_def_t f; memset(&f, 0, sizeof(f));
    strcpy(f.uid, "rNET_loss");
    strcpy(f.script, "/x/net_loss.sh");
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");
    result_t *r = executor_run_fault(&f, "inject", &p, 0);  /* timeout=0 不超时 */
    ASSERT_TRUE(g_last_cmd != NULL);
    ASSERT_STR_CONTAINS(g_last_cmd, "/x/net_loss.sh");
    ASSERT_TRUE(g_last_env != NULL);
    int found_op=0, found_uid=0, found_iface=0, found_loss=0;
    for (int i = 0; g_last_env && g_last_env[i]; i++) {
        if (strcmp(g_last_env[i], "DCAT_OP=inject") == 0) found_op = 1;
        if (strcmp(g_last_env[i], "DCAT_UID=rNET_loss") == 0) found_uid = 1;
        if (strcmp(g_last_env[i], "DCAT_PARAM_IFACE=eth0") == 0) found_iface = 1;
        if (strcmp(g_last_env[i], "DCAT_PARAM_LOSS_PCT=5") == 0) found_loss = 1;
    }
    ASSERT_TRUE(found_op && found_uid && found_iface && found_loss);
    result_free(r); return 0;
}

int test_run_raw_mock(void) {
    executor_set_mock(mock_fn);
    fault_def_t f; memset(&f, 0, sizeof(f));
    strcpy(f.uid, "rCPU_overload"); strcpy(f.script, "/x/cpu_overload.sh");
    params_t p; params_init(&p); params_set(&p, "cores", "2");
    int rc = executor_run_raw_fault(&f, "query", &p);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(g_last_cmd != NULL);
    ASSERT_STR_CONTAINS(g_last_cmd, "/x/cpu_overload.sh");
    int found_op = 0;
    for (int i = 0; g_last_env && g_last_env[i]; i++)
        if (strcmp(g_last_env[i], "DCAT_OP=query") == 0) found_op = 1;
    ASSERT_TRUE(found_op);
    return 0;
}

int main(void) {
    RUN_TEST(test_build_cmd_and_env);
    RUN_TEST(test_run_raw_mock);
    return TEST_MAIN_RETURN();
}
```

- [ ] **步骤 2：运行测试验证失败**

- [ ] **步骤 3：实现 `src/core/executor.h`**

```c
#ifndef DCAT_EXECUTOR_H
#define DCAT_EXECUTOR_H
#include "types.h"
/* cnf 故障 inject/clean 路径：fork/exec+pipe 同步，可选 timer 超时；timeout_ms<=0 不超时。
 * 成功返回 result_ok(op,uid,0,message=脚本 stdout 首行)；失败返回 result_err(code=1)。 */
result_t *executor_run_fault(const fault_def_t *f, const char *op, const params_t *params, int timeout_ms);
/* cnf 故障 query 路径：设置 env 后 system() 直通 stdout（继承终端），返回脚本退出码 */
int executor_run_raw_fault(const fault_def_t *f, const char *op, const params_t *params);
int executor_check_tool(const char *path);   /* access/X_OK */
void executor_set_mock(mock_fn fn);
#endif
```

- [ ] **步骤 4：实现 `src/core/executor.c`**

```c
#include "executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

static mock_fn g_mock = NULL;
void executor_set_mock(mock_fn fn) { g_mock = fn; }

/* 构造 env 数组（NULL 结尾，堆分配，调用方 free_env） */
static const char **build_env(const fault_def_t *f, const char *op, const params_t *p, int *out_n) {
    int cap = p->count + 4;
    const char **env = calloc((size_t)cap, sizeof(char*));
    int n = 0;
    char buf[160];
    snprintf(buf, sizeof(buf), "DCAT_OP=%s", op); env[n++] = strdup(buf);
    snprintf(buf, sizeof(buf), "DCAT_UID=%s", f->uid); env[n++] = strdup(buf);
    for (int i = 0; i < p->count; i++) {
        snprintf(buf, sizeof(buf), "%s=%s", dcat_key_to_env(p->items[i].key), p->items[i].value);
        env[n++] = strdup(buf);
    }
    env[n] = NULL;
    if (out_n) *out_n = n;
    return env;
}
static void free_env(const char **env, int n) {
    for (int i = 0; i < n; i++) free((void*)env[i]);
    free(env);
}
static void apply_env(const char *const *env, int n) {
    for (int i = 0; i < n; i++) {
        const char *eq = strchr(env[i], '=');
        if (eq) {
            size_t kl = (size_t)(eq - env[i]);
            char *k = malloc(kl + 1);
            memcpy(k, env[i], kl); k[kl] = '\0';
            setenv(k, eq + 1, 1);
            free(k);
        }
    }
}

/* 可选 timer 超时：超时后 SIGKILL 子进程（DESIGN §3.4） */
static pid_t g_timed_pid = 0;
static void on_timeout(union sigval sv) { (void)sv; if (g_timed_pid > 0) kill(g_timed_pid, SIGKILL); }

result_t *executor_run_fault(const fault_def_t *f, const char *op, const params_t *p, int timeout_ms) {
    int nenv = 0;
    const char **env = build_env(f, op, p, &nenv);
    if (g_mock) {
        result_t *r = g_mock(f->script, env);
        free_env(env, nenv);
        return r;
    }
    int pipefd[2];
    if (pipe(pipefd) < 0) { free_env(env, nenv); return result_err(op, f->uid, 1, "pipe failed"); }
    pid_t pid = fork();
    if (pid < 0) { free_env(env, nenv); close(pipefd[0]); close(pipefd[1]); return result_err(op, f->uid, 1, "fork failed"); }
    if (pid == 0) {
        apply_env(env, nenv);
        dup2(pipefd[1], 1); dup2(pipefd[1], 2); close(pipefd[0]); close(pipefd[1]);
        execl(f->script, f->script, (char*)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    timer_t timer = NULL;
    if (timeout_ms > 0) {
        g_timed_pid = pid;
        struct sigevent sev; memset(&sev, 0, sizeof(sev));
        sev.sigev_notify = SIGEV_THREAD; sev.sigev_notify_function = on_timeout;
        timer_create(CLOCK_MONOTONIC, &sev, &timer);
        struct itimerspec its; memset(&its, 0, sizeof(its));
        its.it_value.tv_sec = timeout_ms / 1000;
        its.it_value.tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
        timer_settime(timer, 0, &its, NULL);
    }
    char out[4096] = {0}; ssize_t m = read(pipefd[0], out, sizeof(out)-1); (void)m;
    close(pipefd[0]);
    int status = 0; waitpid(pid, &status, 0);
    if (timer) { timer_delete(timer); g_timed_pid = 0; }
    free_env(env, nenv);
    if (timeout_ms > 0 && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
        return result_err(op, f->uid, 1, "script timeout");
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    if (code != 0) return result_err(op, f->uid, 1, out[0] ? out : "script failed");
    char *nl = strchr(out, '\n'); if (nl) *nl = '\0';
    return result_ok(op, f->uid, 0, out[0] ? out : NULL);
}

int executor_run_raw_fault(const fault_def_t *f, const char *op, const params_t *p) {
    int nenv = 0;
    const char **env = build_env(f, op, p, &nenv);
    if (g_mock) {
        result_t *r = g_mock(f->script, env);
        int code = r ? r->code : 1;
        result_free(r);
        free_env(env, nenv);
        return code;  /* mock 时用 result.code 模拟脚本退出码 */
    }
    apply_env(env, nenv);
    int rc = system(f->script);   /* 直通 stdout/stderr 到终端 */
    free_env(env, nenv);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : 1;
}

int executor_check_tool(const char *path) {
    return access(path, X_OK);
}
```

> 注：`timer_create` 在某些 musl/老 glibc 需链接 `-lrt`。若链接报 `timer_create` 未定义，在 CMakeLists 的 `target_link_libraries` 增加 `rt`。CMakeLists 已含 `Threads::Threads`，可按需追加。

- [ ] **步骤 5：运行测试验证通过**

```bash
cmake --build build --target test_executor_mock && ctest --test-dir build -R test_executor_mock --output-on-failure
```
预期：PASS（mock 路径不 fork）。

- [ ] **步骤 6：Commit**

```bash
git add src/core/executor.h src/core/executor.c tests/test_executor_mock.c
git commit -m "feat(executor): fork/exec+pipe sync run + system() raw + mock hook + env build"
```

---

## 任务 6：precheck.c/h（预检 4 步 + 未声明参数拒绝）

**文件：** 创建 `src/core/precheck.h`、`src/core/precheck.c`、`tests/test_precheck.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_precheck.c`**（覆盖 4 步各失败路径 + 未声明参数拒绝 + inject-only）

```c
#include "test.h"
#include "precheck.h"
#include "registry.h"
#include "config.h"
#include <string.h>

static config_t cfg;
static void setup(void) { config_load("config/demoncat.conf", &cfg); registry_init(&cfg); }

int test_precheck_uid_not_found(void) {
    setup();
    params_t p; params_init(&p);
    result_t *r = precheck(NULL, "inject", &p);   /* fault=NULL → uid 不存在 */
    ASSERT_INT_EQ(r->code, 4);
    result_free(r); return 0;
}

int test_precheck_op_not_supported(void) {
    setup();
    const fault_def_t *f = registry_find("rPROC_exit"); /* inject-only */
    params_t p; params_init(&p); params_set(&p, "pid", "1");
    result_t *r = precheck(f, "clean", &p);   /* clean 不在 supported_ops(inject) */
    ASSERT_INT_EQ(r->code, 3);
    result_free(r); return 0;
}

int test_precheck_query_on_inject_only_rejected(void) {
    setup();
    const fault_def_t *f = registry_find("rPROC_exit");
    params_t p; params_init(&p); params_set(&p, "pid", "1");
    result_t *r = precheck(f, "query", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r); return 0;
}

int test_precheck_missing_required(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay"); /* required: iface,delay_ms */
    params_t p; params_init(&p); params_set(&p, "iface", "eth0"); /* 缺 delay_ms */
    result_t *r = precheck(f, "inject", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r); return 0;
}

int test_precheck_required_empty_rejected(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay");
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "");   /* 空值视为缺失 */
    result_t *r = precheck(f, "inject", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r); return 0;
}

int test_precheck_undeclared_param_rejected(void) {
    setup();
    const fault_def_t *f = registry_find("rNET_delay"); /* 声明 iface,delay_ms */
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");
    params_set(&p, "foo", "bar");     /* 未声明 → 拒绝 */
    result_t *r = precheck(f, "inject", &p);
    ASSERT_INT_EQ(r->code, 3);
    result_free(r); return 0;
}

int test_precheck_clean_no_required_check(void) {
    /* clean 不校验 required_params（只校验 op∈supported_ops + 脚本 + 声明参数）。
       脚本最终可执行时，clean 缺 delay_ms 应通过预检（返回 NULL），证明 clean 不走 required 校验 */
    setup();
    const fault_def_t *f = registry_find("rNET_delay");
    params_t p; params_init(&p); params_set(&p, "iface", "eth0"); /* clean 不需 delay_ms */
    result_t *r = precheck(f, "clean", &p);
    ASSERT_TRUE(r == NULL);  /* 脚本可执行 → 预检通过，证明 clean 未校验 required_params */
    result_free(r); return 0;
}

int main(void) {
    RUN_TEST(test_precheck_uid_not_found);
    RUN_TEST(test_precheck_op_not_supported);
    RUN_TEST(test_precheck_query_on_inject_only_rejected);
    RUN_TEST(test_precheck_missing_required);
    RUN_TEST(test_precheck_required_empty_rejected);
    RUN_TEST(test_precheck_undeclared_param_rejected);
    RUN_TEST(test_precheck_clean_no_required_check);
    return TEST_MAIN_RETURN();
}
```

- [ ] **步骤 2：运行测试验证失败**

- [ ] **步骤 3：实现 `src/core/precheck.h`**

```c
#ifndef DCAT_PRECHECK_H
#define DCAT_PRECHECK_H
#include "types.h"
/* 执行 SPEC §4.2 预检 4 步 + 未声明参数拒绝。返回 NULL=通过；非 NULL=失败 result_t(code)。
 * fault=NULL 表示 uid 不在 cnf（code 4）。 */
result_t *precheck(const fault_def_t *f, const char *op, const params_t *params);
int op_in_supported(const char *supported_ops, const char *op);
int required_params_present(const fault_def_t *f, const params_t *params);
int declared_params_only(const fault_def_t *f, const params_t *params);  /* 所有用户提供参数在 required∪optional 中声明 */
#endif
```

- [ ] **步骤 4：实现 `src/core/precheck.c`**（SPEC §4.2 4 步；inject 校验 required；所有命令拒绝未声明参数）

```c
#include "precheck.h"
#include "executor.h"
#include <string.h>
#include <stdlib.h>

int op_in_supported(const char *supported_ops, const char *op) {
    char buf[128];
    strncpy(buf, supported_ops ? supported_ops : "", sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    char *tok = strtok(buf, ",");
    while (tok) { if (strcmp(tok, op) == 0) return 1; tok = strtok(NULL, ","); }
    return 0;
}

int required_params_present(const fault_def_t *f, const params_t *params) {
    char buf[128];
    strncpy(buf, f->required_params, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    if (buf[0] == '\0') return 1;  /* 无必填参数 */
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok) {
        const char *v = params_find(params, tok);
        if (!v || !v[0]) return 0;
        tok = strtok_r(NULL, ",", &save);
    }
    return 1;
}

int declared_params_only(const fault_def_t *f, const params_t *params) {
    /* 检查 params 每个 key 在 required_params ∪ optional_params 中声明 */
    for (int i = 0; i < params->count; i++) {
        const char *k = params->items[i].key;
        char req[128], opt[128];
        strncpy(req, f->required_params, sizeof(req)-1); req[sizeof(req)-1]='\0';
        strncpy(opt, f->optional_params, sizeof(opt)-1); opt[sizeof(opt)-1]='\0';
        int found = 0;
        char *save = NULL;
        char *tok = strtok_r(req, ",", &save);
        while (tok) { if (strcmp(tok, k) == 0) { found = 1; break; } tok = strtok_r(NULL, ",", &save); }
        if (!found && opt[0] != '\0') {
            save = NULL;
            tok = strtok_r(opt, ",", &save);
            while (tok) { if (strcmp(tok, k) == 0) { found = 1; break; } tok = strtok_r(NULL, ",", &save); }
        }
        if (!found) return 0;
    }
    return 1;
}

result_t *precheck(const fault_def_t *f, const char *op, const params_t *params) {
    /* 第1步：uid 存在（fault!=NULL 由调用方保证；fault=NULL 视为未找到） */
    if (!f) return result_err(op, "", 4, "uid not found");
    /* 第2步：op∈supported_ops */
    if (!op_in_supported(f->supported_ops, op))
        return result_err(op, f->uid, 3, "op not in supported_ops");
    /* 未声明参数拒绝（所有命令） */
    if (!declared_params_only(f, params))
        return result_err(op, f->uid, 3, "undeclared param");
    /* 第3步：inject 校验 required_params 齐全且非空 */
    if (strcmp(op, "inject") == 0 && !required_params_present(f, params))
        return result_err(op, f->uid, 3, "missing required params");
    /* 第4步：脚本可执行 */
    if (executor_check_tool(f->script) != 0)
        return result_err(op, f->uid, 3, "script not executable");
    return NULL;  /* 通过 */
}
```

- [ ] **步骤 5：运行测试验证通过**

```bash
cmake --build build --target test_precheck && ctest --test-dir build -R test_precheck --output-on-failure
```
预期：PASS。

- [ ] **步骤 6：Commit**

```bash
git add src/core/precheck.h src/core/precheck.c tests/test_precheck.c
git commit -m "feat(precheck): SPEC §4.2 4-step validation + undeclared param rejection"
```

---

## 任务 7：state.c/h（records 含 params + pthread + 持久化）

**文件：** 创建 `src/core/state.h`、`src/core/state.c`、`tests/test_state.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_state.c`**

```c
#include "test.h"
#include "state.h"
#include <string.h>

int test_add_find_by_params_list_inactive(void) {
    state_reset();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");
    int id = state_add("rNET_loss", &p);
    ASSERT_TRUE(id > 0);
    /* 按参数匹配 */
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    int ids[DCAT_MAX_RECORDS]; int n = 0;
    state_find_by_params("rNET_loss", &q, ids, &n);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ids[0], id);
    /* find_by_id */
    const injection_record_t *r = state_find_by_id(id);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->active, 1);
    ASSERT_STREQ(params_find(&r->params, "iface"), "eth0");
    /* list */
    ASSERT_INT_EQ(state_list_active(), 1);
    /* mark inactive */
    state_mark_inactive(id);
    ASSERT_TRUE(state_find_by_id(id) == NULL || !state_find_by_id(id)->active);
    ASSERT_INT_EQ(state_list_active(), 0);
    return 0;
}

int test_concurrent_same_uid_diff_params(void) {
    /* 允许同 uid 不同参数并发注入；clean 按参数只清匹配的 */
    state_reset();
    params_t p1; params_init(&p1); params_set(&p1, "iface", "eth0"); params_set(&p1, "loss_pct", "5");
    params_t p2; params_init(&p2); params_set(&p2, "iface", "eth1"); params_set(&p2, "loss_pct", "3");
    int id1 = state_add("rNET_loss", &p1);
    int id2 = state_add("rNET_loss", &p2);
    ASSERT_TRUE(id1 > 0 && id2 > 0 && id1 != id2);
    ASSERT_INT_EQ(state_list_active(), 2);
    /* clean --iface=eth0 只匹配 id1 */
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    int ids[DCAT_MAX_RECORDS]; int n = 0;
    state_find_by_params("rNET_loss", &q, ids, &n);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ids[0], id1);
    state_mark_inactive(id1);
    ASSERT_INT_EQ(state_list_active(), 1);   /* id2 仍活跃 */
    return 0;
}

int test_persistence_roundtrip(void) {
    state_reset();
    state_set_file("/tmp/dcat-test-state.json");
    params_t p; params_init(&p); params_set(&p, "iface", "eth0");
    int id = state_add("rNET_delay", &p);
    ASSERT_TRUE(id > 0);
    state_save();
    state_reset();
    state_load();
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    int ids[DCAT_MAX_RECORDS]; int n = 0;
    state_find_by_params("rNET_delay", &q, ids, &n);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ids[0], id);
    const injection_record_t *r = state_find_by_id(id);
    ASSERT_TRUE(r != NULL);
    ASSERT_STREQ(params_find(&r->params, "iface"), "eth0");
    return 0;
}

int main(void) {
    RUN_TEST(test_add_find_by_params_list_inactive);
    RUN_TEST(test_concurrent_same_uid_diff_params);
    RUN_TEST(test_persistence_roundtrip);
    return TEST_MAIN_RETURN();
}
```

- [ ] **步骤 2：运行测试验证失败**

- [ ] **步骤 3：实现 `src/core/state.h`**

```c
#ifndef DCAT_STATE_H
#define DCAT_STATE_H
#include "types.h"
void state_reset(void);
int  state_add(const char *uid, const params_t *params);   /* 返回 record_id；仅可恢复故障；满返回 -1 */
/* 按 uid + 用户参数匹配活跃记录，填入 ids[]，返回匹配数。query 为空匹配该 uid 全部活跃 */
int  state_find_by_params(const char *uid, const params_t *query, int *ids, int max_ids);
const injection_record_t *state_find_by_id(int id);       /* 仅活跃记录 */
int  state_list_active(void);                              /* 活跃数 */
void state_mark_inactive(int id);
void state_set_file(const char *path);
void state_save(void);
void state_load(void);
/* 用于 query 无 uid 输出：遍历活跃记录回调 */
typedef void (*state_visit_fn)(const injection_record_t *r, void *ctx);
void state_for_each_active(state_visit_fn fn, void *ctx);
#endif
```

- [ ] **步骤 4：实现 `src/core/state.c`**

```c
#include "state.h"
#include <cJSON.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static injection_record_t g_records[DCAT_MAX_RECORDS];
static int g_next_id = 1;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_file[256] = "~/.demoncat/state.json";

void state_reset(void) {
    pthread_mutex_lock(&g_lock);
    memset(g_records, 0, sizeof(g_records));
    g_next_id = 1;
    pthread_mutex_unlock(&g_lock);
}
void state_set_file(const char *path) {
    pthread_mutex_lock(&g_lock);
    strncpy(g_file, path, sizeof(g_file)-1); g_file[sizeof(g_file)-1]='\0';
    pthread_mutex_unlock(&g_lock);
}

int state_add(const char *uid, const params_t *params) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (!g_records[i].active) {
            g_records[i].record_id = g_next_id++;
            strncpy(g_records[i].uid, uid, sizeof(g_records[i].uid)-1);
            g_records[i].uid[sizeof(g_records[i].uid)-1]='\0';
            g_records[i].params = *params;   /* 值拷贝 params_t */
            g_records[i].started_at = (long)time(NULL);
            g_records[i].active = 1;
            int id = g_records[i].record_id;
            pthread_mutex_unlock(&g_lock);
            return id;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return -1;  /* 满 */
}

int state_find_by_params(const char *uid, const params_t *query, int *ids, int max_ids) {
    pthread_mutex_lock(&g_lock);
    int n = 0;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (g_records[i].active && strcmp(g_records[i].uid, uid) == 0 &&
            params_match_subset(query, &g_records[i].params)) {
            if (n < max_ids) ids[n] = g_records[i].record_id;
            n++;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return n;
}

const injection_record_t *state_find_by_id(int id) {
    pthread_mutex_lock(&g_lock);
    const injection_record_t *r = NULL;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].record_id == id && g_records[i].active) { r = &g_records[i]; break; }
    pthread_mutex_unlock(&g_lock);
    return r;
}

int state_list_active(void) {
    pthread_mutex_lock(&g_lock);
    int n = 0;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) if (g_records[i].active) n++;
    pthread_mutex_unlock(&g_lock);
    return n;
}

void state_mark_inactive(int id) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].record_id == id) { g_records[i].active = 0; break; }
    pthread_mutex_unlock(&g_lock);
}

void state_for_each_active(state_visit_fn fn, void *ctx) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].active) fn(&g_records[i], ctx);
    pthread_mutex_unlock(&g_lock);
}

/* cJSON 序列化 params_t */
static cJSON *params_to_json(const params_t *p) {
    cJSON *o = cJSON_CreateObject();
    for (int i = 0; i < p->count; i++)
        cJSON_AddStringToObject(o, p->items[i].key, p->items[i].value);
    return o;
}
static void json_to_params(const cJSON *o, params_t *p) {
    params_init(p);
    if (!o) return;
    cJSON *k;
    cJSON_ArrayForEach(k, o) {
        if (cJSON_IsString(k)) params_set(p, k->string, k->valuestring);
    }
}

void state_save(void) {
    pthread_mutex_lock(&g_lock);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (!g_records[i].active) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "record_id", g_records[i].record_id);
        cJSON_AddStringToObject(o, "uid", g_records[i].uid);
        cJSON_AddItemToObject(o, "params", params_to_json(&g_records[i].params));
        cJSON_AddNumberToObject(o, "started_at", g_records[i].started_at);
        cJSON_AddBoolToObject(o, "active", g_records[i].active);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "next_id", g_next_id);
    cJSON_AddItemToObject(root, "records", arr);
    char *s = cJSON_Print(root); cJSON_Delete(root);
    FILE *fp = fopen(g_file, "w");
    if (fp) { fputs(s, fp); fclose(fp); }
    free(s);
    pthread_mutex_unlock(&g_lock);
}

void state_load(void) {
    pthread_mutex_lock(&g_lock);
    FILE *fp = fopen(g_file, "r");
    if (!fp) { pthread_mutex_unlock(&g_lock); return; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); pthread_mutex_unlock(&g_lock); return; }
    char *buf = malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, fp); buf[rd] = '\0'; fclose(fp);
    cJSON *root = cJSON_Parse(buf); free(buf);
    if (root) {
        cJSON *nid = cJSON_GetObjectItem(root, "next_id");
        if (nid) g_next_id = nid->valueint;
        cJSON *arr = cJSON_GetObjectItem(root, "records");
        cJSON *o; int i = 0;
        cJSON_ArrayForEach(o, arr) {
            if (i >= DCAT_MAX_RECORDS) break;
            cJSON *rid = cJSON_GetObjectItem(o, "record_id");
            cJSON *uid = cJSON_GetObjectItem(o, "uid");
            cJSON *prms = cJSON_GetObjectItem(o, "params");
            cJSON *sa = cJSON_GetObjectItem(o, "started_at");
            cJSON *ac = cJSON_GetObjectItem(o, "active");
            if (rid) g_records[i].record_id = rid->valueint;
            if (uid) { strncpy(g_records[i].uid, uid->valuestring, sizeof(g_records[i].uid)-1); g_records[i].uid[sizeof(g_records[i].uid)-1]='\0'; }
            if (prms) json_to_params(prms, &g_records[i].params);
            if (sa) g_records[i].started_at = (long)sa->valuedouble;
            if (ac) g_records[i].active = cJSON_IsTrue(ac);
            i++;
        }
        cJSON_Delete(root);
    }
    pthread_mutex_unlock(&g_lock);
}
```

- [ ] **步骤 5：运行测试验证通过**

```bash
cmake --build build --target test_state && ctest --test-dir build -R test_state --output-on-failure
```
预期：PASS。

- [ ] **步骤 6：Commit**

```bash
git add src/core/state.h src/core/state.c tests/test_state.c
git commit -m "feat(state): records with params + find_by_params + pthread + cJSON persistence"
```

---

## 任务 8：injector.h + injectors.c（接口 + 空注册表 + find）

**文件：** 创建 `src/injectors/injector.h`、`src/injectors/injectors.c`、`tests/test_injectors.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_injectors.c`**

```c
#include "test.h"
#include "injectors/injector.h"

int test_injector_find_empty(void) {
    ASSERT_TRUE(injector_find("rMEM_ecc_inject") == NULL);  /* 本期空数组 */
    ASSERT_INT_EQ(builtin_injector_count, 0);
    return 0;
}

int main(void) { RUN_TEST(test_injector_find_empty); return TEST_MAIN_RETURN(); }
```

- [ ] **步骤 2：实现 `src/injectors/injector.h`**（按 DESIGN §7.2）

```c
#ifndef DCAT_INJECTOR_H
#define DCAT_INJECTOR_H
#include "core/types.h"

typedef struct injector_t {
    const char *uid;
    result_t *(*inject)(const params_t *params);
    result_t *(*clean)(const params_t *params);   /* inject-only 注入器为 NULL */
    result_t *(*query)(const params_t *params);    /* inject-only 注入器为 NULL */
    result_t *(*precheck)(const char *op, const params_t *params);
} injector_t;

extern const injector_t *const builtin_injectors[];
extern const int builtin_injector_count;
const injector_t *injector_find(const char *uid);

#endif
```

- [ ] **步骤 3：实现 `src/injectors/injectors.c`**（按 DESIGN §7.4，本期空数组）

```c
#include "injector.h"
#include <string.h>

const injector_t *const builtin_injectors[] = {
    /* 本期为空数组；后续按需添加编译注入器实现 */
};
const int builtin_injector_count =
    (int)(sizeof(builtin_injectors) / sizeof(builtin_injectors[0]));

const injector_t *injector_find(const char *uid) {
    for (int i = 0; i < builtin_injector_count; i++) {
        if (strcmp(builtin_injectors[i]->uid, uid) == 0)
            return builtin_injectors[i];
    }
    return NULL;
}
```

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build build --target test_injectors && ctest --test-dir build -R test_injectors --output-on-failure
```
预期：PASS。

- [ ] **步骤 5：Commit**

```bash
git add src/injectors/injector.h src/injectors/injectors.c tests/test_injectors.c
git commit -m "feat(injector): injector_t interface + empty builtin registry + injector_find"
```

---

## 任务 9：dispatch.c/h（op 路由 + cnf/injector 分流）

**文件：** 创建 `src/core/dispatch.h`、`src/core/dispatch.c`、`tests/test_dispatch.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_dispatch.c`**（mock executor：inject 写 state、clean 按参数匹配、inject-only 不写 state、list、query 无 uid）

```c
#include "test.h"
#include "dispatch.h"
#include "executor.h"
#include "registry.h"
#include "state.h"
#include "config.h"
#include <string.h>

static int g_called = 0;
static const char *g_last_cmd = NULL;
static result_t *mock_ok(const char *cmd, const char *const *env) {
    (void)env; g_called++; g_last_cmd = cmd;
    return result_ok("inject", "x", 0, "ok");
}

static config_t cfg;
static void setup(void) {
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_reset();
    executor_set_mock(mock_ok);
}

int test_dispatch_inject_recoverable_writes_state(void) {
    setup();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");
    result_t *r = dispatch_route("rNET_delay", "inject", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_TRUE(r->code == 0);
    /* 写了 state：按 iface 匹配到 1 条 */
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    int ids[DCAT_MAX_RECORDS]; int n = 0;
    state_find_by_params("rNET_delay", &q, ids, &n);
    ASSERT_INT_EQ(n, 1);
    result_free(r); return 0;
}

int test_dispatch_clean_by_params_marks_inactive(void) {
    setup();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");
    dispatch_route("rNET_delay", "inject", &p);   /* mock 返回 ok → 写 state */
    ASSERT_INT_EQ(state_list_active(), 1);
    /* clean 按 iface 匹配 */
    params_t c; params_init(&c); params_set(&c, "iface", "eth0");
    result_t *r = dispatch_route("rNET_delay", "clean", &c);
    ASSERT_TRUE(r != NULL);
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_INT_EQ(state_list_active(), 0);   /* 已 mark inactive */
    result_free(r); return 0;
}

int test_dispatch_clean_no_match(void) {
    setup();
    params_t c; params_init(&c); params_set(&c, "iface", "eth9");  /* 无匹配记录 */
    result_t *r = dispatch_route("rNET_delay", "clean", &c);
    ASSERT_INT_EQ(r->code, 1);   /* no active injection */
    result_free(r); return 0;
}

int test_dispatch_list(void) {
    setup();
    result_t *r = dispatch_route(NULL, "list", NULL);
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_STR_CONTAINS(r->json, "\"op\":\"list\"");
    ASSERT_STR_CONTAINS(r->json, "rNET_delay");
    ASSERT_STR_CONTAINS(r->json, "rPROC_exit");
    result_free(r); return 0;
}

int test_dispatch_query_no_uid_lists_state(void) {
    setup();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");
    dispatch_route("rNET_delay", "inject", &p);
    result_t *r = dispatch_route(NULL, "query", NULL);   /* 无 uid → state 回答 */
    ASSERT_STR_CONTAINS(r->json, "\"op\":\"query\"");
    ASSERT_STR_CONTAINS(r->json, "rNET_delay");
    ASSERT_STR_CONTAINS(r->json, "\"record_id\"");
    result_free(r); return 0;
}

int test_dispatch_uid_not_found(void) {
    setup();
    params_t p; params_init(&p);
    result_t *r = dispatch_route("nope", "inject", &p);
    ASSERT_INT_EQ(r->code, 4);
    result_free(r); return 0;
}

int main(void) {
    RUN_TEST(test_dispatch_inject_recoverable_writes_state);
    RUN_TEST(test_dispatch_clean_by_params_marks_inactive);
    RUN_TEST(test_dispatch_clean_no_match);
    RUN_TEST(test_dispatch_list);
    RUN_TEST(test_dispatch_query_no_uid_lists_state);
    RUN_TEST(test_dispatch_uid_not_found);
    return TEST_MAIN_RETURN();
}
```

- [ ] **步骤 2：运行测试验证失败**

- [ ] **步骤 3：实现 `src/core/dispatch.h`**

```c
#ifndef DCAT_DISPATCH_H
#define DCAT_DISPATCH_H
#include "types.h"
/* op∈{inject,clean,query,list}；list/query 无 uid 时 uid 可为 NULL */
result_t *dispatch_route(const char *uid, const char *op, const params_t *params);
#endif
```

- [ ] **步骤 4：实现 `src/core/dispatch.c`**（按 DESIGN §5：cnf 优先 → executor；未命中回退 injector_find；state/precheck/inject-only/clean-by-params/query-raw/list）

```c
#include "dispatch.h"
#include "registry.h"
#include "executor.h"
#include "precheck.h"
#include "state.h"
#include "output.h"
#include "config.h"
#include "../injectors/injector.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>

static int is_inject_only(const fault_def_t *f) {
    return strcmp(f->supported_ops, "inject") == 0;
}

/* list 输出：cnf 故障目录 JSON（注入器本期不纳入） */
static result_t *dispatch_list(void) {
    int n = 0; const fault_def_t *list = registry_list(&n);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "uid", list[i].uid);
        cJSON_AddStringToObject(o, "module", list[i].module);
        /* supported_ops 拆为数组 */
        cJSON *ops = cJSON_CreateArray();
        char buf[64]; strncpy(buf, list[i].supported_ops, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
        char *save = NULL, *tok = strtok_r(buf, ",", &save);
        while (tok) { cJSON_AddItemToArray(ops, cJSON_CreateString(tok)); tok = strtok_r(NULL, ",", &save); }
        cJSON_AddItemToObject(o, "supported_ops", ops);
        if (list[i].desc[0]) cJSON_AddStringToObject(o, "desc", list[i].desc);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", "list");
    cJSON_AddItemToObject(root, "data", arr);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t)); r->code = 0; r->json = s; return r;
}

/* query 无 uid：state 回答，输出活跃记录数组（用 state_for_each_active 遍历，健壮无空洞） */
struct list_ctx { cJSON *arr; };
static void push_record(const injection_record_t *r, void *v) {
    struct list_ctx *c = v;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "uid", r->uid);
    cJSON_AddNumberToObject(o, "record_id", r->record_id);
    cJSON_AddNumberToObject(o, "started_at", r->started_at);
    cJSON_AddBoolToObject(o, "active", r->active);
    cJSON *prms = cJSON_CreateObject();
    for (int k = 0; k < r->params.count; k++)
        cJSON_AddStringToObject(prms, r->params.items[k].key, r->params.items[k].value);
    cJSON_AddItemToObject(o, "params", prms);
    cJSON_AddItemToArray(c->arr, o);
}
static result_t *dispatch_query_state(void) {
    cJSON *arr = cJSON_CreateArray();
    struct list_ctx c = { arr };
    state_for_each_active(push_record, &c);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", "query");
    cJSON_AddItemToObject(root, "data", arr);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t)); r->code = 0; r->json = s; return r;
}

/* cnf inject：inject-only 不写 state；可恢复写 state */
static result_t *cnf_inject(const fault_def_t *f, const params_t *params) {
    result_t *r = executor_run_fault(f, "inject", params, 0);
    if (r->code == 0 && !is_inject_only(f)) {
        state_add(f->uid, params);
    }
    return r;
}

/* cnf clean：按参数匹配活跃记录，逐条执行 clean 脚本（传记录存储的 inject 参数），失败停止 */
static result_t *cnf_clean(const fault_def_t *f, const params_t *user_params) {
    int ids[DCAT_MAX_RECORDS]; int n = 0;
    state_find_by_params(f->uid, user_params, ids, DCAT_MAX_RECORDS);
    if (n == 0) return result_err("clean", f->uid, 1, "no active injection");
    for (int i = 0; i < n; i++) {
        const injection_record_t *rec = state_find_by_id(ids[i]);
        if (!rec) continue;
        result_t *r = executor_run_fault(f, "clean", &rec->params, 0);  /* 传记录存储的 inject 参数 */
        if (r->code != 0) return r;  /* 失败停止，不 mark inactive */
        state_mark_inactive(ids[i]);
        result_free(r);
    }
    return result_ok("clean", f->uid, 0, "cleaned");
}

result_t *dispatch_route(const char *uid, const char *op, const params_t *params) {
    if (strcmp(op, "list") == 0) return dispatch_list();
    if (strcmp(op, "query") == 0 && (uid == NULL || uid[0] == '\0'))
        return dispatch_query_state();

    const fault_def_t *f = registry_find(uid);
    if (f) {
        result_t *pc = precheck(f, op, params);
        if (pc) return pc;
        if (strcmp(op, "inject") == 0) return cnf_inject(f, params);
        if (strcmp(op, "clean") == 0)   return cnf_clean(f, params);
        if (strcmp(op, "query") == 0) {
            /* query 有 uid：executor_run_raw 直通脚本 stdout，然后打印 --- + JSON confirmed */
            int rc = executor_run_raw_fault(f, "query", params);
            printf("---\n");
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "ok");
            cJSON_AddStringToObject(root, "op", "query");
            cJSON_AddStringToObject(root, "uid", uid);
            cJSON *data = cJSON_AddObjectToObject(root, "data");
            cJSON_AddBoolToObject(data, "confirmed", rc == 0);
            char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
            result_t *r = malloc(sizeof(result_t)); r->code = 0; r->json = s; return r;
        }
    }
    /* injector 回退（本期空数组，永不命中） */
    const injector_t *inj = injector_find(uid);
    if (inj) {
        result_t *pc = inj->precheck(op, params);
        if (pc && pc->code != 0) return pc;
        if (pc) result_free(pc);
        if (strcmp(op, "inject") == 0) {
            result_t *r = inj->inject(params);
            if (r->code == 0 && inj->clean) state_add(uid, params);
            return r;
        }
        if (strcmp(op, "clean") == 0) {
            int ids[DCAT_MAX_RECORDS]; int n = 0;
            state_find_by_params(uid, params, ids, DCAT_MAX_RECORDS);
            if (n == 0) return result_err("clean", uid, 1, "no active injection");
            for (int i = 0; i < n; i++) {
                const injection_record_t *rec = state_find_by_id(ids[i]);
                if (!rec) continue;
                result_t *r = inj->clean(&rec->params);
                if (r->code != 0) return r;
                state_mark_inactive(ids[i]); result_free(r);
            }
            return result_ok("clean", uid, 0, "cleaned");
        }
        if (strcmp(op, "query") == 0) return inj->query(params);
    }
    return result_err(op, uid ? uid : "", 4, "not found");
}
```

- [ ] **步骤 5：运行测试验证通过**

```bash
cmake --build build --target test_dispatch && ctest --test-dir build -R test_dispatch --output-on-failure
```
预期：PASS。

- [ ] **步骤 6：Commit**

```bash
git add src/core/dispatch.h src/core/dispatch.c tests/test_dispatch.c
git commit -m "feat(dispatch): op routing + cnf/injector split + inject-only/clean-by-params/query-raw/list"
```

---

## 任务 10：cli.c/h + main.c（argv 子命令解析 + 编排）

**文件：** 创建 `src/core/cli.h`、`src/core/cli.c`、`tests/test_cli.c`、`src/main.c`

- [ ] **步骤 1：编写失败的测试 `tests/test_cli.c`**（子命令式 `--key=value`；`--config`/`--help` 不进 params）

```c
#include "test.h"
#include "cli.h"
#include <string.h>

int test_parse_inject_flags(void) {
    const char *argv[] = {"dcat", "inject", "rNET_loss", "--iface=eth0", "--loss_pct=5"};
    parsed_cmd_t pc;
    int rc = cli_parse(5, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "inject");
    ASSERT_STREQ(pc.uid, "rNET_loss");
    ASSERT_STREQ(params_find(&pc.params, "iface"), "eth0");
    ASSERT_STREQ(params_find(&pc.params, "loss_pct"), "5");
    return 0;
}

int test_parse_clean_flags(void) {
    const char *argv[] = {"dcat", "clean", "rNET_loss", "--iface=eth0"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "clean");
    ASSERT_STREQ(params_find(&pc.params, "iface"), "eth0");
    return 0;
}

int test_parse_query_no_uid(void) {
    const char *argv[] = {"dcat", "query"};
    parsed_cmd_t pc;
    int rc = cli_parse(2, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "query");
    ASSERT_TRUE(pc.uid[0] == '\0');
    return 0;
}

int test_parse_query_with_uid(void) {
    const char *argv[] = {"dcat", "query", "rCPU_overload", "--cores=2"};
    parsed_cmd_t pc;
    int rc = cli_parse(4, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "query");
    ASSERT_STREQ(pc.uid, "rCPU_overload");
    ASSERT_STREQ(params_find(&pc.params, "cores"), "2");
    return 0;
}

int test_parse_list(void) {
    const char *argv[] = {"dcat", "list"};
    parsed_cmd_t pc;
    int rc = cli_parse(2, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(pc.op, "list");
    return 0;
}

int test_parse_global_options_excluded(void) {
    /* --config 路径不应进 params */
    const char *argv[] = {"dcat", "inject", "rNET_loss", "--iface=eth0", "--config", "/x.conf"};
    parsed_cmd_t pc;
    int rc = cli_parse(6, (char**)argv, &pc);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STREQ(params_find(&pc.params, "iface"), "eth0");
    ASSERT_TRUE(params_find(&pc.params, "config") == NULL);
    return 0;
}

int main(void) {
    RUN_TEST(test_parse_inject_flags);
    RUN_TEST(test_parse_clean_flags);
    RUN_TEST(test_parse_query_no_uid);
    RUN_TEST(test_parse_query_with_uid);
    RUN_TEST(test_parse_list);
    RUN_TEST(test_parse_global_options_excluded);
    return TEST_MAIN_RETURN();
}
```

- [ ] **步骤 2：运行测试验证失败**

- [ ] **步骤 3：实现 `src/core/cli.h`**

```c
#ifndef DCAT_CLI_H
#define DCAT_CLI_H
#include "types.h"
typedef struct { const char *op; char uid[64]; params_t params; } parsed_cmd_t;
/* 解析 argv：argv[1]=subcommand, argv[2]=uid(可选), 剩余 --key=value。
 * --config/--help 为全局选项（main 处理），不进 params。返回 0 成功，非 0 解析错误。 */
int cli_parse(int argc, char **argv, parsed_cmd_t *out);
#endif
```

- [ ] **步骤 4：实现 `src/core/cli.c`**

```c
#include "cli.h"
#include <string.h>
#include <stdlib.h>

static int is_subcommand(const char *s) {
    return strcmp(s, "inject") == 0 || strcmp(s, "clean") == 0 ||
           strcmp(s, "query") == 0 || strcmp(s, "list") == 0;
}

int cli_parse(int argc, char **argv, parsed_cmd_t *out) {
    memset(out, 0, sizeof(*out));
    params_init(&out->params);
    if (argc < 2) return -1;
    /* argv[1] = subcommand */
    if (!is_subcommand(argv[1])) return -1;
    out->op = argv[1];
    int i = 2;
    /* uid：存在且不以 -- 开头（query/list 可省略） */
    if (i < argc && argv[i][0] != '-' && argv[i][0] != '\0' &&
        strcmp(argv[i], "values") != 0 && strcmp(argv[i], "where") != 0) {
        strncpy(out->uid, argv[i], sizeof(out->uid) - 1);
        out->uid[sizeof(out->uid) - 1] = '\0';
        i++;
    }
    /* 剩余 --key=value 标志；--config/--help 为全局选项跳过（main 处理） */
    for (; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "--help") == 0) {
            /* --config 后跟路径值，跳过下一个 */
            if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) i++;
            continue;
        }
        if (strncmp(argv[i], "--", 2) != 0) return -1;  /* 非 --key=value */
        const char *kv = argv[i] + 2;
        const char *eq = strchr(kv, '=');
        if (!eq) return -1;
        char key[64];
        size_t kl = (size_t)(eq - kv);
        if (kl >= sizeof(key)) return -1;
        memcpy(key, kv, kl); key[kl] = '\0';
        const char *val = eq + 1;
        if (params_set(&out->params, key, val) != 0) return -1;
    }
    return 0;
}
```

- [ ] **步骤 5：实现 `src/main.c`**（argv 编排：subcommand + uid + flags + --config/--help；/proc/self/exe 配置定位）

```c
#include "config.h"
#include "registry.h"
#include "state.h"
#include "dispatch.h"
#include "output.h"
#include "cli.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void print_help(void) {
    printf("usage: dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--help]\n"
           "  subcommand: inject | clean | query | list\n"
           "  inject <uid> --p1=v1 ...     注入故障\n"
           "  clean  <uid> [--k1=v1 ...]   清除活跃注入（按参数匹配）\n"
           "  query  [uid] [--k=v ...]     无 uid 查询全部活跃；有 uid 验证故障生效\n"
           "  list                         列出故障目录\n");
}

int main(int argc, char **argv) {
    const char *cfgpath = NULL;
    int show_help = 0;
    /* 预扫 --config/--help（全局选项，cli_parse 跳过） */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) show_help = 1;
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) cfgpath = argv[++i];
    }
    if (show_help) { print_help(); return 0; }
    if (argc < 2) { print_help(); return 2; }

    /* 配置定位：未指定 --config 时用 <binary_dir>/../config/demoncat.conf（SPEC §7.1） */
    char defcfg[512];
    if (!cfgpath) {
        ssize_t n = readlink("/proc/self/exe", defcfg, sizeof(defcfg) - 1);
        if (n > 0) {
            defcfg[n] = '\0';
            /* 去掉末尾两段：build/dcat → 取 <binary_dir>，再 /../config/demoncat.conf */
            char *slash = strrchr(defcfg, '/');
            if (slash) *slash = '\0';
            char *bslash = strrchr(defcfg, '/');
            if (bslash) *bslash = '\0';
            snprintf(defcfg + strlen(defcfg), sizeof(defcfg) - strlen(defcfg),
                     "/config/demoncat.conf");
            cfgpath = defcfg;
        } else {
            cfgpath = "config/demoncat.conf";  /* 测试/相对路径回退 */
        }
    }

    config_t cfg;
    if (config_load(cfgpath, &cfg) != 0) {
        fprintf(stderr, "config load failed: %s\n", cfgpath);
        return 1;
    }
    registry_init(&cfg);
    state_set_file(cfg.state_file[0] ? cfg.state_file : "~/.demoncat/state.json");
    state_load();

    parsed_cmd_t pc;
    if (cli_parse(argc, argv, &pc) != 0) {
        printf("{\"status\":\"error\",\"op\":\"parse\",\"error\":{\"code\":2,\"message\":\"parse error\"}}\n");
        return 2;
    }
    result_t *r = dispatch_route(pc.uid, pc.op, &pc.params);
    output_print(r);
    int code = r ? r->code : 1;
    result_free(r);
    return code;
}
```

- [ ] **步骤 6：运行测试验证通过**

```bash
cmake --build build --target test_cli && ctest --test-dir build -R test_cli --output-on-failure
```
预期：PASS。

- [ ] **步骤 7：Commit**

```bash
git add src/core/cli.h src/core/cli.c src/main.c tests/test_cli.c
git commit -m "feat(cli+main): argv subcommand + --key=value parse + /proc/self/exe config"
```

---

## 任务 11：集成测试 + ctest 全绿

**文件：** `tests/test_faults.c`（通用表驱动，3 条 v0.1 示例）；示例占位脚本

- [ ] **步骤 1：编写占位脚本**（echo，用于端到端；真实故障脚本在后续按模块计划实现）

```bash
cat > config/scripts/cpu/cpu_overload.sh <<'EOF'
#!/bin/sh
echo "cpu injected: cores=${DCAT_PARAM_CORES:-1}"
EOF
cat > config/scripts/network/net_delay.sh <<'EOF'
#!/bin/sh
echo "net delay: iface=${DCAT_PARAM_IFACE} delay=${DCAT_PARAM_DELAY_MS}"
EOF
cat > config/scripts/process/proc_exit.sh <<'EOF'
#!/bin/sh
echo "proc exit: pid=${DCAT_PARAM_PID}"
EOF
chmod +x config/scripts/cpu/cpu_overload.sh config/scripts/network/net_delay.sh config/scripts/process/proc_exit.sh
```

> **Windows 实现者注意**：Windows 文件系统无 +x 位。改用 git 在 index 中记录可执行位（commit 后 Linux 检出即生效）：
> ```bash
> git add config/scripts/cpu/cpu_overload.sh config/scripts/network/net_delay.sh config/scripts/process/proc_exit.sh
> git update-index --chmod=+x config/scripts/cpu/cpu_overload.sh config/scripts/network/net_delay.sh config/scripts/process/proc_exit.sh
> ```
> 文件内容用 Write 工具写入（等价于上面的 cat heredoc）。用户在 Linux/WSL 跑 ctest 前若仍未可执行，执行 `chmod +x config/scripts/**/*.sh`。

- [ ] **步骤 2：编写表驱动测试 `tests/test_faults.c`**（mock executor，断言下发命令串含期望子串 + env；inject-only 无 record_id）

```c
#include "test.h"
#include "executor.h"
#include "dispatch.h"
#include "registry.h"
#include "state.h"
#include "config.h"
#include <string.h>

static const char *g_cmd; static const char *const *g_env;
static result_t *mock(const char *cmd, const char *const *env) {
    g_cmd = cmd; g_env = env;
    return result_ok("inject", "x", 0, "ok");
}

struct case_t {
    const char *uid; const char *op;
    const char *k1; const char *v1;
    const char *k2; const char *v2;
    const char *expect_cmd_substr; const char *expect_env;
    int expect_state_written;  /* inject-only 不写 state */
};
static struct case_t cases[] = {
    {"rCPU_overload", "inject", "cores", "4", NULL, NULL,
        "cpu_overload.sh", "DCAT_PARAM_CORES=4", 1},
    {"rNET_delay", "inject", "iface", "eth0", "delay_ms", "100",
        "net_delay.sh", "DCAT_PARAM_IFACE=eth0", 1},
    {"rPROC_exit", "inject", "pid", "12345", NULL, NULL,
        "proc_exit.sh", "DCAT_PARAM_PID=12345", 0},  /* inject-only 不写 state */
};

int test_faults_table(void) {
    config_t cfg; config_load("config/demoncat.conf", &cfg); registry_init(&cfg);
    state_reset(); executor_set_mock(mock);
    for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); i++) {
        params_t p; params_init(&p);
        params_set(&p, cases[i].k1, cases[i].v1);
        if (cases[i].k2) params_set(&p, cases[i].k2, cases[i].v2);
        result_t *r = dispatch_route(cases[i].uid, cases[i].op, &p);
        ASSERT_TRUE(r != NULL);
        ASSERT_STR_CONTAINS(g_cmd, cases[i].expect_cmd_substr);
        int found = 0;
        for (int e = 0; g_env && g_env[e]; e++)
            if (strcmp(g_env[e], cases[i].expect_env) == 0) found = 1;
        ASSERT_TRUE(found);
        /* inject-only 不写 state */
        int ids[DCAT_MAX_RECORDS]; int n = 0;
        params_t q; params_init(&q);
        state_find_by_params(cases[i].uid, &q, ids, &n);
        if (cases[i].expect_state_written) ASSERT_TRUE(n >= 1);
        else ASSERT_INT_EQ(n, 0);
        result_free(r);
    }
    return 0;
}

int main(void) { RUN_TEST(test_faults_table); return TEST_MAIN_RETURN(); }
```

- [ ] **步骤 3：全量构建 + ctest**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
预期：所有 test_* 全绿。

- [ ] **步骤 4：端到端冒烟**（真实 dcat 二进制 + 占位脚本）

```bash
./build/dcat list
./build/dcat inject rCPU_overload --cores=4
./build/dcat query
./build/dcat clean rCPU_overload --cores=4
./build/dcat inject rPROC_exit --pid=12345
./build/dcat query
./build/dcat clean rPROC_exit      # 退出码 3（inject-only 拒绝 clean）
```

- [ ] **步骤 5：Commit**

```bash
git add tests/test_faults.c config/scripts/cpu/cpu_overload.sh config/scripts/network/net_delay.sh config/scripts/process/proc_exit.sh
git commit -m "test: table-driven inject coverage for 3 v0.1 sample faults + smoke scripts"
```

---

## 自检

**1. 规格覆盖度**（对照 SPEC/DESIGN 章节）：
- §2 命令格式（子命令式 `--key=value`）→ cli.c（任务 10）✓
- §3 故障目录字段（含 optional_params）→ config.c（任务 3）+ types.h fault_def_t ✓
- §4 预检 4 步 + 未声明参数拒绝 → precheck.c（任务 6）✓
- §5 脚本契约（env/exit/stdout/同步阻塞）→ executor.c build_env + result（任务 5/2）✓
- §6 输出 schema（含 inject-only 无 record_id、query state 数组、list）→ output.c + dispatch.c ✓
- §7 配置定位 /proc/self/exe + derive_project_root/resolve_script → main.c + config.c ✓
- §9 测试策略 mock_executor → executor_set_mock（任务 5）✓
- §11 高级扩展点 injector_t → injector.h + injectors.c（任务 8）✓
- DESIGN §7 注入器 dispatch 回退 → dispatch.c injector_find（任务 9）✓
- 决策 9/10（无 autoclean/safety/reaper/超时自动恢复）→ 删除过时骨架（任务 0）✓
- 决策 11（统一同步阻塞）→ executor 无 spawn/kill ✓
- 决策 12（注入器留位）→ injectors.c 空数组 ✓
- 决策 13（参数匹配 + 并发注入 + 4 步预检）→ state_find_by_params + params_match_subset + precheck 4 步 ✓
- 38 条故障目录 → **本计划不含**，后续按模块计划填充 cnf + 脚本 + test_faults_*.c（本计划用 3 条示例打通框架）

**2. 占位符扫描**：任务 0 步骤 2 的 cJSON vendoring 依赖网络下载；若无网络需手写最小子集（已在步骤注释说明）。任务 11 的占位脚本为 echo，用于端到端冒烟，真实故障脚本在后续按模块计划实现。无其他 TODO/待定。

**3. 类型一致性**：
- `params_t`/`fault_def_t`/`injection_record_t` 在 types.h 定义（含 params 字段），各模块引用一致。
- `result_ok(op, uid, record_id, message)` 签名在 output.h 与所有调用方（executor/dispatch）一致。
- `state_add(uid, params)` / `state_find_by_params(uid, query, ids, max)` / `state_mark_inactive(id)` 签名在 state.h 与 dispatch.c 一致。
- `executor_run_fault(f, op, params, timeout_ms)` / `executor_run_raw_fault(f, op, params)` 签名在 executor.h 与 dispatch 一致。
- `precheck(f, op, params)` 返回 NULL=通过，与 dispatch 用法一致。
- `cli_parse(argc, argv, out)` 签名在 cli.h 与 main.c 一致。
- `mock_fn` 返回 `result_t *`（堆分配，调用方 result_free），与 executor mock 路径一致。

**4. 设计微调说明**（已对齐当前 SPEC/DESIGN，区别于旧计划）：
- 预检 4 步（删并发检查），允许同 uid 重复注入（含相同参数）。
- state 记录含 params，clean 按 `state_find_by_params` 子集匹配多条，逐条执行。
- clean 传**记录存储的 inject 参数**给脚本（非用户 clean 参数）。
- query 无 uid 由 state 回答（不调脚本）；有 uid 走 `executor_run_raw_fault`（system 直通）+ `---` + JSON `{confirmed:bool}`。
- inject-only 不写 state、无 record_id。
- `executor_run_fault` 保留可选 timer 超时（timeout_ms<=0 不超时），dispatch 始终传 0。

---

## 执行交接

计划已完成并保存到 `docs/superpowers/plans/2026-07-23-dcat-core-framework-v2.md`。本计划取代 `2026-07-21-dcat-core-framework.md`（旧计划基于已废弃语法）。

**范围：** 批次 1 = 核心框架 + 3 条示例故障。后续 38 条故障按模块（network→process→cpu/storage→npu）另立计划。

**构建环境：** 需 Linux/WSL（cmake+gcc）。当前 Windows 主机无编译器，开发者须在 Linux/WSL 验证 ctest 全绿。

两种执行方式：

1. **子代理驱动（推荐）** - 每个任务调度一个新的子代理，任务间进行审查，快速迭代
2. **内联执行** - 在当前会话中使用 executing-plans 执行任务，批量执行并设有检查点

**选哪种方式？**
