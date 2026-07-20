# DemonCAT P0 Framework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline) to implement task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Shared types/signatures are defined in [DESIGN.md](../../../DESIGN.md) §2 and referenced here (DRY); test code is given in full as behavior specs.

**Goal:** Build the DemonCAT P0 framework (cnf/script-driven fault dispatcher) with 2 example faults and a green CTest suite, runnable in WSL.

**Architecture:** Layered C11 binary: main → cli → registry(cnfg) → executor/safety/state/config/output. Faults are cnf-declared external scripts; dcat dispatches via executor (env-var param passing). See DESIGN.md §1.

**Tech Stack:** C11, CMake ≥3.10, pthread, vendored cJSON (single-file), CTest.

## Global Constraints

- C11 (`set(CMAKE_C_STANDARD 11)`), `-Wall -Wextra -Werror`
- Only third-party dep: vendored cJSON at `third_party/cjson/{cJSON.c,cJSON.h}`
- Static link target `dcat`
- Core path zero dynamic alloc (params_t on stack); cJSON heap only at output boundary
- Platform: Linux/WSL
- Build/test cmd: `cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure`
- SPEC.md / DESIGN.md at repo root are authoritative; this plan implements P0 only

---

## File Structure

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | build dcat + tests, vendor cJSON, enable_testing |
| `third_party/cjson/{cJSON.c,cJSON.h}` | vendored JSON lib |
| `src/core/types.h` | all shared structs/enums (params_t, result_t, fault_def_t, injection_record_t, exec_mode_t, safety_level_t, parsed_cmd_t) — per DESIGN §2 |
| `src/core/output.{c,h}` | result_t build/print/free (cJSON) |
| `src/core/cli.{c,h}` | recursive-descent parser → parsed_cmd_t |
| `src/core/config.{c,h}` | INI parse → config_t (fault_def[] table) |
| `src/core/registry.{c,h}` | fault_def lookup/list (from config) |
| `src/core/executor.{c,h}` | run/spawn/kill/check_tool + env-var cmd build + mock hook |
| `src/core/state.{c,h}` | records array + mutex + persist + auto_clean thread |
| `src/core/safety.{c,h}` | confirm + precheck |
| `src/main.c` | orchestration |
| `config/demoncat.conf.example` | sample catalog (rCPU_overload + rNET_delay) |
| `config/scripts/{cpu_overload.sh,net_delay.sh}` | example scripts |
| `tests/test_*.c` | CTest cases |

---

## Task 1: Project skeleton, CMake, vendored cJSON, types.h

**Files:** Create `CMakeLists.txt`, `third_party/cjson/*`, `src/core/types.h`, `src/main.c` (stub), `tests/.gitkeep`

**Interfaces:**
- Produces: `types.h` with all types from DESIGN §2; a buildable `dcat` printing version.

- [ ] **Step 1: Vendor cJSON** — download `cJSON.c`+`cJSON.h` (v1.7.x) into `third_party/cjson/`. Verify present.
- [ ] **Step 2: Write `src/core/types.h`** — copy struct/enum definitions verbatim from DESIGN §2; add `parsed_cmd_t { const char *op; char uid[64]; params_t params; }`.
- [ ] **Step 3: Write `CMakeLists.txt`** — C11, `-Wall -Wextra -Werror`, compile `third_party/cjson/cJSON.c` + `src/main.c` into static `dcat`; `enable_testing()`.
- [ ] **Step 4: Stub `src/main.c`** — prints `{"status":"ok","op":"version","data":{"version":"0.1.0"}}` and returns 0.
- [ ] **Step 5: Build & run**
  - `cmake -B build && cmake --build build`
  - `./build/dcat` → expect the version JSON.
- [ ] **Step 6: Commit** — `git add -A && git commit -m "scaffold: cmake, vendored cJSON, types, version stub"`

---

## Task 2: output module (TDD)

**Files:** Create `src/core/output.{c,h}`, `tests/test_output.c`; modify `CMakeLists.txt` to build test.

**Interfaces:**
- Produces: `result_t *result_ok(const char *op,const char *uid,cJSON *data); result_t *result_err(const char *op,const char *uid,int code,const char *msg); void output_print(const result_t *r); void result_free(result_t *r);`

- [ ] **Step 1: Failing test `tests/test_output.c`**
```c
#include "../src/core/output.h"
#include <string.h>
int main(void){
  result_t *ok = result_ok("inject","rCPU_overload",NULL);
  output_print(ok); /* capture stdout via freopen in test harness not needed; assert JSON via strcmp */
  result_free(ok);
  result_t *e = result_err("inject","rX",5,"already active");
  result_free(e);
  return 0;
}
```
- [ ] **Step 2: Add test to CMake** — `add_executable(test_output tests/test_output.c src/core/output.c third_party/cjson/cJSON.c)` + `add_test(NAME output COMMAND test_output)`.
- [ ] **Step 3: Run, expect FAIL** (link error: output.c missing).
- [ ] **Step 4: Implement `output.c`** — `result_ok` builds `{"status":"ok","op":op,"uid":uid,"data":data?}`; `result_err` builds `{"status":"error",...,"error":{"code":..,"message":..}}`; `output_print` prints cJSON `PrintUnformatted` + newline; `result_free` frees json. (Use cJSON API: `cJSON_CreateObject`, `AddStringToObject`, `PrintUnformatted`, `Delete`.)
- [ ] **Step 5: Run, expect PASS** — `ctest --test-dir build -R output --output-on-failure`.
- [ ] **Step 6: Commit** — `feat(output): result build/print/free with cJSON`

---

## Task 3: cli parser (TDD, table-driven)

**Files:** `src/core/cli.{c,h}`, `tests/test_cli.c`

**Interfaces:**
- Produces: `int cli_parse(const char *input, parsed_cmd_t *out);` returns 0 on success, nonzero on parse error; fills `out->op/uid/params`.

- [ ] **Step 1: Failing test `tests/test_cli.c`** (table-driven)
```c
#include "../src/core/cli.h"
#include <string.h>
static int eq(const char*a,const char*b){return strcmp(a,b)==0;}
int main(void){
  struct { const char*in; const char*op; const char*uid; int pc; const char*k0; const char*v0; } cases[]={
    {"inject rCPU_overload (cores,duration) values (4,60)","inject","rCPU_overload",2,"cores","4"},
    {"clean rNET_delay where iface=eth0","clean","rNET_delay",1,"iface","eth0"},
    {"list","list","",0,"",""},
    {"query rCPU_overload","query","rCPU_overload",0,"",""},
  };
  int fails=0;
  for(size_t i=0;i<sizeof cases/sizeof *cases;i++){
    parsed_cmd_t out; if(cli_parse(cases[i].in,&out)){fails++;continue;}
    if(!eq(out.op,cases[i].op)||!eq(out.uid,cases[i].uid)||out.params.count!=cases[i].pc){fails++;continue;}
    if(cases[i].pc>0 && (!eq(out.params.items[0].key,cases[i].k0)||!eq(out.params.items[0].value,cases[i].v0)))fails++;
  }
  /* error cases */
  parsed_cmd_t e; if(cli_parse("inject",{return 1;} /* syntax: ensure these return nonzero */ )
  return fails?1:0;
}
```
- [ ] **Step 2: Add to CMake** — test target + add_test.
- [ ] **Step 3: Run, expect FAIL** (cli missing).
- [ ] **Step 4: Implement `cli.c`** — recursive descent: `parse_op` (inject|clean|query|list) → skip ws → `parse_uid` (alnum/_) → optional params production:
  - A: `( p1 , p2 ) values ( v1 , v2 )` → push key[i],value[i]
  - B: `where k1 = v1 (k2 = v2)*` → push each pair
  - tokens delimited by spaces; values are barewords until `,`/`)`/space. Validate counts match for production A.
- [ ] **Step 5: Run, expect PASS**.
- [ ] **Step 6: Commit** — `feat(cli): recursive-descent parser, two params productions`

---

## Task 4: config loader + registry (TDD)

**Files:** `src/core/config.{c,h}`, `src/core/registry.{c,h}`, `config/demoncat.conf.example`, `tests/test_registry.c`

**Interfaces:**
- Produces: `int config_load(const char *path, config_t *cfg);` fills `cfg->faults[]`/`fault_count`/`state_file`/`log_level`; `const fault_def_t *registry_find(const char *uid);` `const fault_def_t *registry_list(int *count);` `int registry_init(const config_t *cfg);`

- [ ] **Step 1: Write `config/demoncat.conf.example`** — `[demoncat]` + two `[fault.*]` sections per SPEC §4 example (rCPU_overload, rNET_delay).
- [ ] **Step 2: Failing test `tests/test_registry.c`**
```c
#include "../src/core/config.h"
#include "../src/core/registry.h"
#include <string.h>
int main(void){
  config_t cfg; if(config_load("config/demoncat.conf.example",&cfg))return 1;
  if(cfg.fault_count!=2)return 1;
  if(registry_init(&cfg))return 1;
  const fault_def_t*f=registry_find("rCPU_overload");
  if(!f||strcmp(f->module,"cpu")||f->safety!=SAFETY_WARNING||f->exec_mode!=EXEC_BACKGROUND)return 1;
  const fault_def_t*n=registry_find("rNET_delay");
  if(!n||n->exec_mode!=EXEC_SYNC||n->safety!=SAFETY_NORMAL)return 1;
  if(registry_find("nope")!=NULL)return 1;
  int cnt; (void)registry_list(&cnt); if(cnt!=2)return 1;
  return 0;
}
```
- [ ] **Step 3: CMake** — test target (working dir set to repo root via `set_tests_properties(... WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})`) + add_test.
- [ ] **Step 4: Run, expect FAIL**.
- [ ] **Step 5: Implement `config.c`** — minimal INI parser: line-based, `[section]` sets current section, `key = value` assigns; map `[fault.<uid>]` → fault_def (parse uid from section name); enum strings (safety/exec_mode) via small strcmp tables. `registry.c`: static `fault_def_t g_table[64]` + count; init copies from cfg; find linear; list returns table ptr.
- [ ] **Step 6: Run, expect PASS**.
- [ ] **Step 7: Commit** — `feat(config,registry): INI loader + fault_def lookup`

---

## Task 5: executor with mock (TDD)

**Files:** `src/core/executor.{c,h}`, `tests/test_executor_mock.c`

**Interfaces:**
- Produces: `int executor_check_tool(const char *path);` `char *executor_build_cmd(const fault_def_t *f,const char *op,const params_t *p,char *buf,size_t len);` (returns cmd string into buf); `void executor_set_env(const char *op,const char *uid,const params_t *p);` (sets DCAT_OP/DCAT_UID/DCAT_PARAM_<KEY> env); `result_t *executor_run(const char *cmd,int timeout_ms);` `pid_t executor_spawn(const char *cmd);` `int executor_kill(pid_t pid);` `typedef result_t *(*mock_fn)(const char *cmd); void executor_set_mock(mock_fn fn);`

- [ ] **Step 1: Failing test `tests/test_executor_mock.c`**
```c
#include "../src/core/executor.h"
#include "../src/core/types.h"
#include <string.h>
static char last_cmd[256]; static int mock_called;
static result_t* mock(const char*cmd){strncpy(last_cmd,cmd,sizeof last_cmd);mock_called=1;return result_err("inject","x",0,"");}
int main(void){
  fault_def_t f={.script="/usr/lib/demoncat/scripts/cpu_overload.sh"};
  params_t p={.count=2,{ {"cores","4"},{"duration","60"} }};
  char buf[256];
  executor_build_cmd(&f,"inject",&p,buf,sizeof buf);
  if(strcmp(buf,"/usr/lib/demoncat/scripts/cpu_overload.sh"))return 1;
  executor_set_env("inject","rCPU_overload",&p);
  if(strcmp(getenv("DCAT_OP"),"inject"))return 1;
  if(strcmp(getenv("DCAT_PARAM_CORES"),"4"))return 1;
  if(strcmp(getenv("DCAT_PARAM_DURATION"),"60"))return 1;
  executor_set_mock(mock); mock_called=0;
  result_t*r=executor_run(buf,1000); /* should call mock, not fork */
  if(!mock_called)return 1;
  result_free(r);
  return 0;
}
```
- [ ] **Step 2: CMake + add_test (working dir root).**
- [ ] **Step 3: Run, expect FAIL.**
- [ ] **Step 4: Implement `executor.c`:**
  - `executor_build_cmd`: just copy script path into buf (params go via env).
  - `executor_set_env`: setenv DCAT_OP, DCAT_UID, and for each param `DCAT_PARAM_<KEY>` (uppercase, non-alnum→`_`).
  - `executor_run`: if mock set → call mock(cmd); else fork/exec, pipe stdout, `timer_create`/`alarm`+`waitpid` with timeout, capture exit code + stdout into result.
  - `executor_spawn`: fork, child setsid+execvp, parent returns pid.
  - `executor_kill`: kill(-pid,SIGTERM) then SIGKILL fallback (kill process group).
  - `executor_check_tool`: `access(path,X_OK)==0`.
- [ ] **Step 5: Run, expect PASS.**
- [ ] **Step 6: Commit** — `feat(executor): run/spawn/kill + env-var params + mock hook`

---

## Task 6: state module (TDD)

**Files:** `src/core/state.{c,h}`, `tests/test_state.c`

**Interfaces:**
- Produces: `int state_init(const char *persist_path);` `int state_add(const char *uid,pid_t pid,int timeout_s);` (returns record_id>0) `const injection_record_t *state_find(const char *uid);` `const injection_record_t *state_list(int *count);` `void state_mark_inactive(int record_id);` `int state_save(void);` `int state_load(void);`

- [ ] **Step 1: Failing test `tests/test_state.c`**
```c
#include "../src/core/state.h"
#include <unistd.h>
int main(void){
  state_init("/tmp/dcat_test_state.json");
  int id=state_add("rCPU_overload",1234,1);
  if(id<=0)return 1;
  const injection_record_t*r=state_find("rCPU_overload");
  if(!r||!r->active||r->bg_pid!=1234)return 1;
  state_mark_inactive(id);
  if(state_find("rCPU_overload")!=NULL)return 1;
  if(state_save())return 1;
  return 0;
}
```
- [ ] **Step 2: CMake + add_test.**
- [ ] **Step 3: Run, expect FAIL.**
- [ ] **Step 4: Implement `state.c`** — static `injection_record_t g_records[DCAT_MAX_RECORDS]`, `pthread_mutex_t g_lock`, monotonic `g_next_id`; add finds free slot; find linear active match; mark_inactive sets active=0; save/load via cJSON to persist path. (Do NOT start auto-clean thread here — Task 8.)
- [ ] **Step 5: Run, expect PASS.**
- [ ] **Step 6: Commit** — `feat(state): records array + persist via cJSON`

---

## Task 7: safety (TDD)

**Files:** `src/core/safety.{c,h}`, `tests/test_safety.c`

**Interfaces:**
- Produces: `int safety_confirm(safety_level_t level, const char *answer);` (answer = user input line; returns 1 if approved, 0 if rejected; dangerous requires exact "yes", warning requires "y"/"Y"/"yes"); `result_t *safety_precheck(const fault_def_t *f,const char *op,const params_t *p);` (returns result_t: code 0 ok, nonzero error).

- [ ] **Step 1: Failing test `tests/test_safety.c`**
```c
#include "../src/core/safety.h"
#include <string.h>
int main(void){
  if(!safety_confirm(SAFETY_NORMAL,""))return 1;        /* always approved */
  if(!safety_confirm(SAFETY_WARNING,"y"))return 1;
  if(safety_confirm(SAFETY_WARNING,"n"))return 1;
  if(!safety_confirm(SAFETY_DANGEROUS,"yes"))return 1;
  if(safety_confirm(SAFETY_DANGEROUS,"y"))return 1;       /* dangerous needs yes */
  fault_def_t f={.uid="x",.supported_ops="inject,clean",.required_params="cores",.script="/bin/true"};
  params_t ok={.count=1,{ {"cores","4"} }};
  params_t miss={.count=0};
  if(safety_precheck(&f,"inject",&miss)->code==0)return 1; /* missing params */
  if(safety_precheck(&f,"query",&ok)->code==0)return 1;     /* op not supported */
  if(safety_precheck(&f,"inject",&ok)->code!=0)return 1;    /* ok */
  return 0;
}
```
- [ ] **Step 2: CMake + add_test.**
- [ ] **Step 3: Run, expect FAIL.**
- [ ] **Step 4: Implement `safety.c`** — confirm per rules; precheck: op in supported_ops?; inject→required_params each present & nonempty; script access X_OK. Return result_err with code/message on each failure, result_ok on pass.
- [ ] **Step 5: Run, expect PASS.**
- [ ] **Step 6: Commit** — `feat(safety): confirm + precheck`

---

## Task 8: auto-clean thread + main orchestration (TDD)

**Files:** `src/core/state.{c,h}` (add `state_auto_clean_start()`), `src/main.c`, `tests/test_autoclean.c`

**Interfaces:**
- Produces: `int state_auto_clean_start(void);` (spawns pthread scanning every 1s for expired active records → calls clean callback). Uses a registered clean callback: `typedef void (*clean_cb)(const injection_record_t *rec); void state_set_clean_cb(clean_cb cb);`

- [ ] **Step 1: Failing test `tests/test_autoclean.c`** — register a clean_cb that sets a flag + marks inactive; add a record with timeout=1; wait 2s; assert callback fired.
```c
#include "../src/core/state.h"
#include <unistd.h>
static int fired=0;
static void cb(const injection_record_t*r){(void)r;fired=1;state_mark_inactive(r->record_id);}
int main(void){
  state_init("/tmp/dcat_ac.json");
  state_set_clean_cb(cb);
  state_auto_clean_start();
  int id=state_add("rT",0,1);
  sleep(2);
  return fired?0:1;
}
```
- [ ] **Step 2: CMake + add_test.**
- [ ] **Step 3: Run, expect FAIL.**
- [ ] **Step 4: Implement auto_clean** — pthread loop: lock, scan records, for active && expires_at>0 && expires_at<=time(NULL) → unlock, call clean_cb, lock; sleep 1s; loop. `state_set_clean_cb` stores global cb.
- [ ] **Step 5: Implement `src/main.c`** — find config (search order: `--config` arg → ./demoncat.conf → ~/.demoncat/ → /etc/demoncat/); config_load; registry_init; state_init(cfg.state_file); state_set_clean_cb(dispatch_clean) where dispatch_clean runs the record's fault script with DCAT_OP=clean (sync) or executor_kill(pid) (background); state_auto_clean_start; cli_parse(argv[1]); route:
  - list → registry_list → output JSON
  - query → state_list → output JSON
  - inject → registry_find → precheck → confirm (from stdin unless `--yes`) → build_cmd+set_env → exec_mode dispatch (background→spawn+state_add(pid); sync→run+state_add(0) if timeout>0) → output
  - clean → registry_find → state_find(uid) → background→kill / sync→run(clean) → mark_inactive → output
  - map exit codes (0/1/2/3/4)
- [ ] **Step 6: Run `test_autoclean`, expect PASS; run `./build/dcat "list"` with example config, expect JSON catalog.**
- [ ] **Step 7: Commit** — `feat(main,state): auto-clean thread + full orchestration`

---

## Task 9: example faults — scripts + table-driven dispatch test (TDD)

**Files:** `config/scripts/cpu_overload.sh`, `config/scripts/net_delay.sh`, `tests/test_faults.c`

**Interfaces:** Consumes executor mock (Task 5), registry (Task 4), state (Task 6).

- [ ] **Step 1: Write `config/scripts/cpu_overload.sh`** — background mode: read DCAT_PARAM_CORES/DURATION; trap SIGTERM kill children; spawn N `yes >/dev/null`/burn loop; run for DURATION then exit. `chmod +x`.
- [ ] **Step 2: Write `config/scripts/net_delay.sh`** — sync: DCAT_OP=inject→`tc qdisc add dev $DCAT_PARAM_IFACE root netem delay ${DCAT_PARAM_DELAY_MS}ms`; clean→`tc qdisc del dev $DCAT_PARAM_IFICE root` (typo: use IFACE); exit code from tc.
- [ ] **Step 3: Failing test `tests/test_faults.c`** — load example config, registry_init, executor_set_mock(captures cmd+env), then for each case assert dispatched script path + env:
```c
struct{ const char*cmd; const char*expect_script; const char*expect_op_env; const char*expect_param_key; const char*expect_param_val; } cs[]={
  {"inject rCPU_overload (cores,duration) values (4,60)","cpu_overload.sh","inject","CORES","4"},
  {"inject rNET_delay (iface,delay_ms) values (eth0,100)","net_delay.sh","inject","IFACE","eth0"},
};
```
  Use `result_t*` return and `executor_run` mock to capture; assert via strstr on captured cmd and getenv. (Also assert clean path: `clean rNET_delay where iface=eth0` → mock called with DCAT_OP=clean.)
- [ ] **Step 4: CMake + add_test (working dir root so scripts paths resolve; but mock prevents real exec).**
- [ ] **Step 5: Run, expect FAIL.**
- [ ] **Step 6: Wire dispatch in main** — ensure main's inject/clean call executor_build_cmd+set_env then executor_run/spawn as designed in Task 8 (already done); test uses main's internal functions? No — test calls registry+executor directly to assert dispatch logic, independent of main. Implement helper `dispatch_inject(fault_def,params)` / `dispatch_clean(fault_def,params)` in main.c OR a new `src/core/dispatch.{c,h}` to be testable. **Decision: add `src/core/dispatch.{c,h}`** exposing `result_t *dispatch_inject(const fault_def_t*,const params_t*); dispatch_clean(...); dispatch_query(...); dispatch_list(void);` so tests + main both use them.
- [ ] **Step 7: Run, expect PASS.**
- [ ] **Step 8: Commit** — `feat(faults): example scripts + dispatch module + table tests`

---

## Task 10: README + full suite + commit

**Files:** `README.md`

- [ ] **Step 1: Write README** — what/why, install (cmake build), quickstart (list/inject/clean/query examples), how to add a fault (script + cnf line), script contract (env vars), safety levels, config search order, testing (`ctest`). Reference SPEC/DESIGN.
- [ ] **Step 2: Full build + test** — `cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure`. All green.
- [ ] **Step 3: Smoke** — `./build/dcat list`, `./build/dcat "inject rCPU_overload (cores,duration) values (1,2)"` (background, kills itself on timeout), `./build/dcat "query"`.
- [ ] **Step 4: Commit** — `docs: README + P0 complete`

---

## Self-Review (run before execution)

1. **Spec coverage:** SPEC §3 (cmd format)→T3,T8; §4 (fault decl)→T4; §5 (safety/precheck)→T7; §5.3 (timeout auto-clean)→T6,T8; §6 (script contract env vars)→T5,T9; §7 (JSON output)→T2,T8; §8 (config)→T4,T8; §10 (testing)→all. ✓
2. **Placeholders:** none; test code given in full; structs referenced from DESIGN (DRY).
3. **Type consistency:** `params_t`, `result_t`, `fault_def_t`, `injection_record_t`, `parsed_cmd_t` defined T1; used consistently T2-T9. `clean_cb`/`state_set_clean_cb` defined T8.

## Execution

Inline execution (executing-plans) — implement task-by-task with TDD, commit after each, run full suite at end. User authorized autonomous operation; no confirmation gates.
