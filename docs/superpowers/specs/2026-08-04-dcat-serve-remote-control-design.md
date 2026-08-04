# dcat serve — 远程故障注入控制平面

- 日期: 2026-08-04
- 状态: 设计(§1 架构 + history/audit 澄清已确认;用户授权直接进入实现)
- 决策来源: brainstorming 流程(SSH 隧道兜底安全;B 方案 in-dcat serve)

## 背景与目标

dcat 是一次性 C CLI(纯 C11 + cjson + threads + dl 插件),已具备:
- `dispatch_route(uid, op, params)` 4 操作面(inject/clean/query/list)+ `dispatch_clean_all()`
- `state.c` 全套状态跟踪(`state_add`/`list_active`/`mark_inactive`/`save`/`load`,持久化 `~/.demoncat/state.json`)
- `output.c` 原生 JSON 输出;`reinject.c` 资源重叠检测;`registry` 故障目录

**缺口**:无网络层;前端(`features/showcase`)是静态目录页,只构造命令不执行。

**目标**:服务器上以 `dcat serve` 长驻,本机经 SSH 隧道访问 `localhost:PORT`,通过浏览器**查看已注入(active + history)** + **远程执行故障注入/清理**。

## 非目标 (YAGNI)

- 多机集群 / fleet registry(单台 MVP)
- TLS / 鉴权 / CORS(由 SSH 隧道兜底;服务端明文 + 仅监听 127.0.0.1)
- WebSocket / SSE 实时推送(前端定时轮询足够)
- 无限审计日志(history 受 `DCAT_MAX_RECORDS=32` 上限;未来升级 append-only `audit.jsonl`)
- 暴露 `query<uid>`(其路径 `printf` 到 stdout,污染 HTTP body;MVP 跳过)

## 架构(§1 已确认)

新增 `dcat serve --port 8080 --bind 127.0.0.1 --webroot <dir>` 子命令,进入 `serve_run()` 长驻 HTTP 循环(新文件 `src/core/serve.c`)。`main.c` 共用 config/registry/state/plugin 装配,在 dispatch 前分叉:`op=="serve"` → `serve_run`,否则原 one-shot 路径零回归。

`serve_run` 单端口双职:
1. **静态前端**:从 `--webroot`(默认 `<repo>/features/showcase`)serve `index.html`/`app.js`/`style.css`。同源 → 无 CORS,省独立静态服务器。
2. **API**:暴露 `/api/*` 端点(见下),in-process 调 `dispatch_route` + `state_*`。

HTTP 层手写 HTTP/1.1(`GET` + `POST application/json` + `Content-Length` 读 body;不做 chunked/multipart),无外部依赖;JSON 用 cjson。单线程串行 accept(MVP);mutating 请求后 `state_save()` 做崩溃恢复。`SIGTERM`/`SIGINT` 优雅退出(`state_save` + `plugin_fini` + close)。

**output 适配**:拆 `output_to_json(result_t*) → malloc'd char*`(加 timestamp),`output_print` 用它,serve 用它拼 body。零回归。

**写权限默认关闭(方案 4 安全模型)**:`dcat serve` 默认**只读** —— 只暴露 GET 端点(health/catalog/state/history),POST `/api/inject|clean` 返回 **403**。`dcat serve --allow-write` 才开启写端点(单机可信操作者用)。前端据 `/api/health` 的 `writable` 字段显隐"执行注入"/"清理"按钮 + 注入前 `confirm()` 二次确认。危险动作默认留在 CLI(shell history + sudo 边界 + 故意摩擦);查看放 Web(零风险)。

## API 端点

| 方法 | 路径 | 行为 |
|---|---|---|
| GET | `/api/health` | `{status,service,version,faults:N,active:M,writable:bool}` |
| GET | `/api/catalog` | `dispatch_route(NULL,"list",NULL)` → 目录 |
| GET | `/api/state` | `dispatch_route(NULL,"query",NULL)` → 活跃注入列表(monitor) |
| GET | `/api/history` | `state_for_each_all` → 全部记录(活跃+已清理),按 record_id 倒序 |
| POST | `/api/inject` | **需 `--allow-write`**(否则 403);body `{uid, params:{k:v}, force?}` → `dispatch_route_force(uid,"inject",&params,force)`; state_save |
| POST | `/api/clean` | **需 `--allow-write`**(否则 403);body `{uid?, params:{}}` → 无 uid:`dispatch_clean_all()`;否则 `dispatch_route(uid,"clean",&params)`; state_save |
| GET | `/` 及静态 | 从 webroot serve `index.html`/`app.js`/`style.css`/... |

**错误约定**:dispatch 返回 `result_t.code!=0` → HTTP 200 + body 是 dcat error JSON(与 CLI 一致,前端按 `status` 判断);HTTP 层错误(404 路径不存在 / 400 坏 JSON body / 403 写未开启(`--allow-write`) / 405 方法错)返回对应状态码 + `{status:error,error:{code,message}}`。

## 组件改动清单

**新增**:
- `src/core/serve.h`, `src/core/serve.c` — HTTP/1.1 server + 路由 + 静态文件 + API handlers

**改**:
- `src/core/output.{c,h}` — 加 `output_to_json(result_t*) → char*`(加 timestamp);`output_print` 改用它
- `src/core/state.{c,h}` — `state_save` 写全部记录(去 `if(!active) continue`);`state_add` 优先空槽(`record_id==0`)以保留历史;加 `state_for_each_all(fn,ctx)` 访问全部记录(活跃+已清理)
- `src/core/cli.{c,h}` — `valid_subcommands` 加 `"serve"`;`parsed_cmd_t` 加 `port`/`bind`/`webroot`/`allow_write` 字段;`cli_parse` 处理 `--port/--bind/--webroot/--allow-write`
- `src/main.c` — serve 分叉(装配后、dispatch 前);serve 路径不跑结尾 `state_save`(serve 自管)
- `CMakeLists.txt` — `DCAT_CORE` 加 `src/core/serve.c`

**前端**:
- `features/showcase/app.js` — API 客户端(fetch)、活跃注入面板、history 面板、命令构造器"执行注入"按钮(POST /api/inject)、轮询(3s);据 `/api/health.writable` 显隐执行/清理按钮 + 注入前 `confirm()` 二次确认
- `features/showcase/index.html` — 加"远程控制"区段(主机指示 + 活跃 + 历史列表)+ 命令构造器加执行按钮

## 数据流(注入为例)

浏览器 → `POST /api/inject {uid:"rCPU_overload", params:{cores:"0,1"}}` →（serve 须 `--allow-write` 启动,否则 403）→（SSH 隧道）→ dcat serve localhost:8080 → 解析 JSON body → `dispatch_route_force("rCPU_overload","inject",&params,0)` → executor 跑脚本 + `state_add` → result_t(JSON) → `output_to_json` → HTTP 200 body → 浏览器渲染 + 下次 `/api/state` 轮询看到新记录。

## 错误处理

- HTTP 解析失败 → 400 + error JSON
- 路径不在 `/api/*` 且非静态文件 → 404
- `/api/*` 方法错(如 `GET /api/inject`)→ 405
- dispatch 返回 `code!=0` → 200 + dcat error JSON(前端按 `status` 判断)
- 并发:MVP 串行,无并发隐患;`state.c` 已加锁
- 崩溃:mutating 后 `state_save`;SIGTERM 优雅退出再 save
- 输入校验:uid 必须在 registry(否则 dispatch 返 code 4);params 走 precheck(已有护栏);**命令注入风险**:params 值最终进 shell 脚本 —— dcat 已有 precheck/SEC 测试覆盖(SEC 类 50 用例),serve 不新增攻击面(同样的 dispatch 路径)

## 测试

- ctest 新增 `test_serve`(待补):启 `serve_run` 在测试端口 → curl `/api/health`(断言 `writable`)/ `/api/catalog` / `/api/state` / `/api/history`;默认只读下 POST `/api/inject` 断言 403;`--allow-write` 下 POST 断言 200。
- 手动:`dcat serve --port 8080`(只读)→ 浏览器查看 + 复制命令到终端执行;`dcat serve --port 8080 --allow-write` → 浏览器直接注入(有 confirm)+ 观察 active/history;经 `ssh -L 8080:localhost:8080 服务器` 访问。
- 回归:24 个 ctest 全绿(`output_print` 重构 + `state_save` 改动 + serve.c 不破坏)。

## 已知限制 / 未来升级

- history 上限 32 条(`DCAT_MAX_RECORDS`);满后复用最旧 inactive 槽 → 升级 append-only `audit.jsonl` 做真审计。
- 单线程;高并发需线程池。
- 不暴露 `query<uid>`(stdout 污染);未来重写 query 路径走 result_t。
- 无 TLS/鉴权(依赖 SSH 隧道);如需公网直连,前置 nginx + 令牌。
- 写权限仅靠 `--allow-write` 开关(无 per-user 鉴权/RBAC);多人共享场景应前置 nginx + 真鉴权,而非依赖内置 serve 的 `--allow-write`。
