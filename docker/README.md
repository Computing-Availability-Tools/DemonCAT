# DemonCAT 容器化使用文档

## 1. 概述

DemonCAT 容器化方案将编译依赖、运行时工具链全部打包进一个镜像，免去用户手动安装依赖的繁琐过程。

| 特性 | 说明 |
|------|------|
| 基础镜像 | `debian:bookworm-slim`（glibc，为未来 dcmi 链接预留） |
| 构建方式 | 多阶段：builder 编译 `dcat` → runtime 精简运行时 |
| 运行模式 | `--privileged` + `--network host` + `--pid host` |
| NPU 支持 | 通过 compose override 挂载 Ascend driver + 设备 |

## 2. 构建

```bash
cd DemonCAT
docker/build.sh
```

脚本自动构建镜像并检测 NPU 环境，输出对应的 compose 启动命令。

也可手动构建：

```bash
docker build -f docker/Dockerfile -t demoncat .
```

## 3. 启动

### 方式一：Docker Compose（推荐）

```bash
# 非 NPU 环境
docker compose -f docker/docker-compose.yml up -d

# NPU 环境（build.sh 自动检测并追加 override）
docker compose \
  -f docker/docker-compose.yml \
  -f docker/docker-compose.npu.yml \
  up -d
```

启动后浏览器访问 `http://<server-ip>:8080`。

### 方式二：手动 docker run

```bash
# Web UI 模式
docker run -d --name demoncat --privileged --network host --pid host \
  -v dcat-state:/var/lib/demoncat \
  demoncat serve --port 8080

# CLI 模式（执行完自动退出）
docker run --rm --privileged --network host --pid host \
  demoncat list

docker run --rm --privileged --network host --pid host \
  demoncat inject rCPU_overload --cores=0,1
```

### NPU 环境 docker run

```bash
docker run -d --name demoncat --privileged --network host --pid host \
  -v /usr/local/Ascend/driver:/usr/local/Ascend/driver:ro \
  -v /usr/local/Ascend/nnae:/usr/local/Ascend/nnae:ro \
  --device /dev/davinci0 \
  -e LD_LIBRARY_PATH=/usr/local/Ascend/driver/lib64/driver:/usr/local/Ascend/nnae/latest/lib64 \
  -e PATH=/usr/local/Ascend/driver/tools:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  -v dcat-state:/var/lib/demoncat \
  demoncat serve --port 8080
```

## 4. 镜像内包含的运行时依赖

| 包 | 用途 |
|---|---|
| iproute2 | `tc`、`ip`（网络故障注入） |
| ethtool | 网卡速率控制 |
| iptables | TCP 丢包（iptables DROP） |
| perl | CPU 过载脚本 |
| python3 | 部分脚本工具 |
| util-linux | `taskset`（CPU 绑核） |
| coreutils | `dd`、`sort` 等基础工具 |
| procps | `ps`、`kill`（进程故障） |
| ipmitool | NPU 降频 / BMC 错误注入（未来） |
| smartmontools | 磁盘 SMART 查询（未来） |
| dmidecode | 硬件信息查询（未来） |

## 5. 关键设计

### 为什么用 `--privileged` + `--network host` + `--pid host`？

| Docker 参数 | 原因 |
|---|---|
| `--privileged` | tc/iptables 需要 CAP_NET_ADMIN；磁盘操作需要 /dev/sd*；NPU 需要 /dev/davinci* |
| `--network host` | tc/iptables 直接操作宿主机网络接口（不是容器虚拟网卡） |
| `--pid host` | systemctl 可达宿主机 systemd（rNET_service_stop 需要）；进程故障可操作宿主机 PID |

### state.json 持久化

容器内 `HOME=/var/lib/demoncat`，state.json 落盘到 `/var/lib/demoncat/.demoncat/state.json`。docker-compose 中挂载了 `dcat-state` 卷持久化此目录。

### 二进制路径解析

`dcat` 通过 `/proc/self/exe` 解析项目树路径：
- 二进制：`/app/build/dcat`
- 配置：`/app/config/demoncat.conf`（`/app/build/../config/`）
- 脚本：`/app/src/scripts/`（`/app/build/../src/scripts/`）
- Web：`/app/src/web/`（`/app/build/../src/web/`）
- 插件：`/app/plugins/`（`/app/build/../plugins/`）

镜像中已 COPY 上述全部目录。

## 6. NPU 环境配置

### 设备挂载

```bash
ls /dev/davinci*    # 查看可用设备
```

根据实际卡数调整 `docker-compose.npu.yml` 中的 `devices` 列表。

### 驱动挂载

容器需要以只读方式挂载宿主机的 Ascend 驱动目录：
- `/usr/local/Ascend/driver` — 含 `hccn_tool`、`libdcmi.so`（未来）
- `/usr/local/Ascend/nnae` — 含 `libc_sec.so`、`libmmpa.so` 依赖

### PATH 和 LD_LIBRARY_PATH

| 环境变量 | 值 | 作用 |
|---|---|---|
| `PATH` | `/usr/local/Ascend/driver/tools:...` | 让 `hccn_tool` 可被找到 |
| `LD_LIBRARY_PATH` | `/usr/local/Ascend/driver/lib64/driver:/usr/local/Ascend/nnae/latest/lib64` | 运行时库链接（未来 dcmi 用） |

## 7. 验证

```bash
# 容器内验证
docker exec demoncat dcat --help
docker exec demoncat dcat list

# Web UI
curl -s -o /dev/null -w '%{http_code}\n' http://localhost:8080/   # 200

# CPU 故障注入测试
docker exec demoncat dcat inject rCPU_overload --cores=0
docker exec demoncat dcat query rCPU_overload --cores=0
docker exec demoncat dcat clean rCPU_overload --cores=0

# 网络故障注入测试（需要宿主机有 eth0 等网卡）
docker exec demoncat dcat inject rNET_delay --iface=lo --delay_ms=100
docker exec demoncat dcat query rNET_delay --iface=lo
docker exec demoncat dcat clean rNET_delay --iface=lo
```

## 8. 停止与清理

```bash
# 停止容器
docker compose -f docker/docker-compose.yml down

# NPU 环境
docker compose -f docker/docker-compose.yml -f docker/docker-compose.npu.yml down

# 清理数据卷
docker volume rm demoncat_dcat-state

# 删除镜像
docker rmi demoncat
```

## 9. 常见问题

### Q: systemctl 相关故障（rNET_service_stop）不生效

容器使用 `--pid host` 模式，systemctl 会直接操作宿主机的 systemd。确保：
1. compose 中已配置 `pid: host`
2. 宿主机已安装 systemd 并以 systemd 作为 PID 1

### Q: tc 命令报 "Operation not permitted"

确保使用 `--privileged` 模式。compose 中已默认配置。

### Q: NPU 故障报 "hccn_tool not found in PATH"

确保 `docker-compose.npu.yml` 中设置了 `PATH` 环境变量，且宿主机 `/usr/local/Ascend/driver/tools/` 下有 `hccn_tool`。

### Q: 容器内看不到宿主机网卡

确保使用 `--network host`（compose 中已配置 `network_mode: host`）。容器将直接使用宿主机网络栈。

### Q: 如何自定义 serve 端口

```bash
docker run -d --privileged --network host --pid host \
  demoncat serve --port 9090
```

`--network host` 模式下不需要端口映射，端口直接在宿主机上监听。
