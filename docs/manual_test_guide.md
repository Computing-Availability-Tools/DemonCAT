# 手动测试清单

> 以下故障可能导致**环境不可用且无法远程恢复**，不能纳入自动化测试。
> 其余故障已由 `ctest` + `tests/smoke_root.sh` 自动化覆盖。

---

## 必须手动测试（可能导致硬件不可用 / 远程失联）

| UID | 模块 | 原因 | 如果出问题怎么恢复 |
|---|---|---|---|
| `rNPU_link_down` | npu | RoCE 链路 down，若 `-cfg recovery` 失败，NPU 卡可能需要物理重启 | 物理重启 NPU / 重新加载驱动 |
| `rNPU_ip_change` | npu | RoCE IP 变更，若 clean 失败则 NPU IP 错误且不可达 | `hccn_tool -i <chip> -cfg recovery` 或物理重启 |
| `rNPU_gw_change` | npu | 网关变更，若 clean 失败则 NPU 无法路由 | `hccn_tool -i <chip> -cfg recovery` |
| `rNPU_mtu_mismatch` | npu | MTU 变更可能导致大包全部丢弃，NPU 业务静默中断 | `hccn_tool -i <chip> -mtu -s size 1500` 手动恢复 |
| `rNPU_roce_port_change` | npu | RoCE UDP 端口变更导致 RoCE 流量全部中断 | `hccn_tool -i <chip> -udp -s port 4791` 手动恢复 |
| `rNET_down` | network | 若在管理网卡上执行，SSH 立即断连 | 带外管理（IPMI/串口）恢复 `ip link set <iface> up` |
| `rNET_service_stop` | network | 若停止 sshd/networking 服务，完全失联 | 物理登录重启服务 |
| `rNET_degrade` | network | 限速后带宽大幅降低，大流量场景下可能导致拥塞 | `tc qdisc del dev <iface> root` |

> 其余 29 条故障均由自动化测试覆盖（`ctest` 14 项 + `smoke_root.sh` 10+ 项）。
> NPU 硬件就绪时，16 条 rNPU_* 也可纳入 `smoke_root.sh` 自动化测试。

## 手动测试流程

```bash
# 1. 确保有带外管理（IPMI/串口）或物理访问权限
# 2. 注入
dcat inject <uid> --param=value ...
# 3. 验证
dcat query <uid> --param=value ...
# 4. 检查环境
ping <目标IP> / systemctl is-active <service>
# 5. 清除
dcat clean <uid> --param=value ...
# 6. 确认恢复
dcat query <uid> --param=value ...
```
