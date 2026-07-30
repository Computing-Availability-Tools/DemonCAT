# 设计:reinject 默认拒绝 + --force 原子替换

date: 2026-07-30
status: implementing (TDD)
branch: feat/reinject-default-reject
base: develop (4924f66)

## 1. 目标

对同一资源的重复注入从"隐式并集/覆盖"改为"默认拒绝 + `--force` 原子替换"。

动机场景:`rCPU_overload cores=0,1` 已注入,再 `cores=0-8`(核集相交)应拒绝,而非旧加法并集;需显式 `--force` 才原子替换。

## 2. 范围 (v1)

- **CNF 路径**(config 驱动 36 故障,`registry_find` 命中):apply。
- inject-only 故障(supported_ops="inject",不写 state):天然 0 overlap → 自然免检,无需特殊处理。
- 插件路径(`plugin_dispatch`)、legacy injector 路径(`injector_find`):deferred,本期不动。

## 3. 资源键 (设计决策 i)

- **资源键 = `f->clean_required` 各参数值**(grep 已验 36 条均纯资源标识:cores/device/iface/port/service/pid/chip*),零新增 config 字段。
- **`cores` 硬编码为集合语义参数**(唯一集合参数):走集合交集;其余参数(device/iface/port/service/pid/chip/dev/ip/...)走精确串等;多参键(NPU chip,dev,ip)= 各参精确 AND。
- 备选 (ii) 新增 conf `resource_key` 字段:YAGNI(clean_required 已等价)。

## 4. overlap 算法

对每个同 uid 活动 record R(经 `state_snapshot_by_uid` 锁内拷贝快照,避免回调重入死锁):

```
resource_overlaps(new, R, clean_required):
  for tok in clean_required.split(','):
    nv = params_find(new, tok); rv = params_find(R, tok)
    if !nv or !rv: continue              # 缺参 → 该 param 不贡献 overlap(留给 precheck 报 missing)
    if tok == "cores":
      nb,rb = cores_parse; 若任一解析失败 → continue(不阻塞,留给脚本报错)
      if !cores_intersect(nb, rb): return false   # 核集不相交 → 非同资源
    else:
      if strcmp(nv, rv) != 0: return false          # 标量不等 → 非同资源
  return true                                       # 全部资源参 overlap → 同资源
```

任一 R overlap → REJECT(收所有 overlap record id)。`--force` → 逐个 clean(复用 executor_run_fault clean + state_mark_inactive),全成功后再 inject。clean 失败 → 中止注入(返回 err),已清的丢失;注入失败则旧已清(操作者可重注)。非真原子(两步脚本),文档注明窗口。

## 5. cores 解析器

- spec: `"0,1" | "0-3" | "0,1,4-6" | "0"` → 位图 `unsigned char bits[16]`(128 bit,核 0-127)。
- `cores_parse(spec, bits)`:split `,`,token 含 `-` → lo-hi 区间置位,否则单核置位;越界/非法 → -1。
- `cores_intersect(a, b)`:位 AND,任一位置 1 → 1。

## 6. CLI

- `parsed_cmd_t` 加 `int force;`。
- `cli.c`:`--force` 当布尔 flag(同 `--help`,裸出现 → force=1);`--force=x` → 报错 "`--force` does not take a value"。
- `--force` 仅 inject 路径生效;clean/query/list 出现 → 忽略(无害,兼容历史脚本)。
- `dispatch_route_force(uid, op, params, force)` 新增;`dispatch_route(...)` 退化为 wrapper(force=0)保后向兼容,现有测试零改动编译。

## 7. dispatch 改动

`dispatch_route_force` 的 CNF inject 分支:

```c
if (strcmp(op, "inject") == 0) {
    int ids[DCAT_MAX_RECORDS];
    int n = reinject_find_overlap(f, params, ids, DCAT_MAX_RECORDS);
    if (n > 0 && !force)
        return result_err("inject", f->uid, 5, "resource already injected; use --force to replace");
    if (n > 0 && force)
        for each id: clean rec -> mark inactive; 失败返回 err
    return cnf_inject(f, params);
}
```

error code 5 = reinject conflict。

## 8. 向后兼容 (BREAKING)

- **CPU `cores` 加法并集(PR#15)→ 默认拒绝**:有意 breaking(用户的动机场景)。
  - `0,1` 已注入 → 再 `0,1`(同):旧 idempotent-ok,现 REJECT。
  - `0,1` 已注入 → `0-8`(重叠):旧并集共存,现 REJECT。
- 网络/进程/存储:本就同资源不可并存(tc qdisc/ipset/单 pid),只是把隐式打架显式化为 reject,基本非 breaking。
- 迁移:重注入改加 `--force`;不同资源(不重叠核/不同 iface)仍并发 OK。

## 9. 测试计划 (TDD, tests/test_reinject.c, 先全红)

1. `cores_parse`:"0,1"→{0,1};"0-3"→{0,1,2,3};"0,1,4-6"→{0,1,4,5,6};"0"→{0};非法→-1。
2. `cores_intersect`:{0,1}∩{0,2}={0};{0,1}∩{2,3}=∅;{0,1}∩{0-8}={0,1}。
3. overlap 精确:mock inject rCPU_overload cores=0,1 → 活动;再 0,1(force=0)→ code 5 REJECT;2,3 → OK。
4. overlap 集合:0,1 活动;再 0-8(force=0)→ REJECT;4,5 → OK。
5. --force 替换:0,1 活动;--force 0-8 → 旧 0,1 被清,0-8 活动(state 唯一, cores=0-8)。
6. 网络标量:rNET_delay iface=eth0 delay_ms=100 活动;再 iface=eth0 delay_ms=200(force=0)→ REJECT;iface=eth1 → OK;--force iface=eth0 delay_ms=200 → 替换(state cores... 唯一 iface=eth0)。
7. 多参键(NPU,mock state 不跑脚本):rNPU_ip_change chip=0 dev=eth0 ip=1.1.1.1 活动;再 ip=2.2.2.2 → OK(不同资源);再 ip=1.1.1.1 → REJECT;--force ip=1.1.1.1 → 替换。
8. inject-only 免检:rPROC_exit inject → 无 state;再 inject → OK(0 overlap,不 reject)。
9. CLI:`inject rCPU_overload --cores=0,1 --force` → pc.force==1;无 --force → 0;`--force=x` → error;clean 带 --force → 忽略不报错。

连带(行为变更):
- `test_smoke_cpu.c` test1(同规格重注入 idempotent-ok)→ 改断言 REJECT(无 --force)/ OK replace(--force)。
- `test_smoke_cpu.c` test2(0-1→0 重叠 idempotent-ok)→ 改断言 REJECT(无 --force)。

## 10. deferred

- 插件 / legacy injector 路径未接入 reinject(CNF 路径覆盖 36 故障)。
- clean_required 为空且写 state 的故审(无此 fault):保守判 overlap(任意活动=冲突)。
- 真原子性:两步脚本有窗口,未做事务回滚。
