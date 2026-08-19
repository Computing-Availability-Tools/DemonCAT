/* ============================================================
 * DemonCAT Showcase — app.js
 * 数据驱动渲染:故障目录 / E2E 矩阵 / 模块概览
 * ============================================================ */

"use strict";

/* ----------- 模块定义 ----------- */
const MODULES = [
  { key: "cpu",        name: "CPU",      color: "#3b82f6", icon: "⚙" },
  { key: "storage",    name: "存储",     color: "#f59e0b", icon: "💾" },
  { key: "network",    name: "网络",     color: "#10b981", icon: "🌐" },
  { key: "process",    name: "进程",     color: "#8b5cf6", icon: "⚙" },
  { key: "memory",     name: "内存",     color: "#ec4899", icon: "💾" },
  { key: "filesystem", name: "文件系统", color: "#14b8a6", icon: "📁" },
  { key: "docker",     name: "Docker",   color: "#06b6d4", icon: "🐳" },
  { key: "npu",        name: "NPU",      color: "#ef4444", icon: "🧠" },
  { key: "system",     name: "系统",     color: "#6b7280", icon: "🖥" },
];

const MODULE_MAP = Object.fromEntries(MODULES.map(m => [m.key, m]));

/* ----------- 故障目录(57 条, 9 模块, 来自 config/demoncat.conf) ----------- */
// 字段: module, uid, required[], optional[{name, default}], desc
const FAULT_CATALOG = [
  /* CPU 5 */
  { module:"cpu", uid:"rCPU_overload", required:["cores"], optional:[{name:"load_pct",default:"100"}],
    desc:"CPU overload (multi-core burn, pure user-space)" },
  { module:"cpu", uid:"rCPU_core_offline", required:["cores"], optional:[],
    desc:"CPU core offline via sysfs" },
  { module:"cpu", uid:"rCPU_quota", required:["cores","quota_pct"], optional:[],
    desc:"CPU core max utilization via cgroup (1-99%)" },
  { module:"cpu", uid:"rCPU_freq", required:["cores","freq_mhz"], optional:[],
    desc:"CPU scaling_max_freq underclock per core" },
  { module:"cpu", uid:"rCPU_core_hang", required:["cores"], optional:[],
    desc:"CPU core hang (RT-priority busy loop starves normal scheduling)" },

  /* 存储 5 */
  { module:"storage", uid:"rDISK_write_overload", required:["device"],
    optional:[{name:"workers",default:"4"},{name:"size_mb",default:"200"}],
    desc:"Disk write IO overload (dd writers)" },
  { module:"storage", uid:"rDISK_part_full", required:["path"],
    optional:[{name:"size",default:""}],
    desc:"Partition fill (fallocate/dd a large file in path)" },
  { module:"storage", uid:"rDISK_inode_exhaust", required:["path"],
    optional:[{name:"count",default:""}],
    desc:"Partition inode exhaustion (many tiny files)" },
  { module:"storage", uid:"rDISK_io_delay", required:["device","delay_ms"], optional:[],
    desc:"Disk IO delay (device-mapper delay target)" },
  { module:"storage", uid:"rDISK_io_error", required:["device"], optional:[],
    desc:"Disk IO error (device-mapper error target)" },

  /* 网络 13 */
  { module:"network", uid:"rNET_delay", required:["iface","delay_ms"], optional:[],
    desc:"Network egress delay via tc netem" },
  { module:"network", uid:"rNET_loss", required:["iface","loss_pct"], optional:[],
    desc:"Network packet loss (tc netem)" },
  { module:"network", uid:"rNET_reorder", required:["iface","reorder_pct"], optional:[],
    desc:"Network packet reorder (tc netem)" },
  { module:"network", uid:"rNET_down", required:["iface"], optional:[],
    desc:"NIC down (ip link)" },
  { module:"network", uid:"rNET_degrade", required:["iface"],
    optional:[{name:"speed_mbps",default:"10"}],
    desc:"NIC speed degrade (tc tbf rate-limit)" },
  { module:"network", uid:"rNET_port_occupy", required:["port"],
    optional:[{name:"protocol",default:"tcp"}],
    desc:"Port occupation (socket holder)" },
  { module:"network", uid:"rNET_service_stop", required:["service"], optional:[],
    desc:"Service stop (systemctl)" },
  { module:"network", uid:"rNET_link_flap", required:["iface"],
    optional:[{name:"cycle_sec",default:"2"},{name:"count",default:"10"}],
    desc:"Link flap (ip link down/up loop)" },
  { module:"network", uid:"rNET_bw_limit", required:["iface","rate_kbps"], optional:[],
    desc:"Bandwidth limit (tc tbf)" },
  { module:"network", uid:"rNET_jitter", required:["iface","delay_ms","jitter_ms"], optional:[],
    desc:"Delay + jitter (tc netem)" },
  { module:"network", uid:"rNET_tcp_loss", required:["port"],
    optional:[{name:"direction",default:"both"}],
    desc:"TCP packet loss (iptables DROP)" },
  { module:"network", uid:"rNET_corrupt", required:["iface","corrupt_pct"], optional:[],
    desc:"Network packet corruption (tc netem corrupt)" },
  { module:"network", uid:"rNET_conn_exhaust", required:["target"],
    optional:[{name:"count",default:""}],
    desc:"Connection exhaustion (hold N outbound TCP connections)" },

  /* 进程 5 */
  { module:"process", uid:"rPROC_exit", required:["pid"], optional:[],
    desc:"Process exit (kill -9, irreversible)" },
  { module:"process", uid:"rPROC_hang", required:["pid"], optional:[],
    desc:"Process hang (SIGSTOP)" },
  { module:"process", uid:"rPROC_zstate", required:["pid"], optional:[],
    desc:"Zombie process (kill target → zombie, clean kills parent to reap)" },
  { module:"process", uid:"rPROC_fork_bomb", required:["count"], optional:[],
    desc:"Process count flood (controlled fork bomb)" },
  { module:"process", uid:"rPROC_fd_exhaust", required:[],
    optional:[{name:"count",default:""}],
    desc:"Per-process fd exhaustion (open to RLIMIT_NOFILE)" },

  /* 内存 4 */
  { module:"memory", uid:"rMEM_leak", required:["size_mb"], optional:[],
    desc:"Memory leak (hold size_mb)" },
  { module:"memory", uid:"rMEM_oom", required:[],
    optional:[{name:"rate_mb",default:""}],
    desc:"Memory OOM (unbounded allocation until OOM killer)" },
  { module:"memory", uid:"rMEM_fragment", required:[],
    optional:[{name:"blocks",default:""},{name:"block_kb",default:""}],
    desc:"Memory fragmentation (alloc N blocks, free every other)" },
  { module:"memory", uid:"rMEM_swap_overload", required:["size_mb"], optional:[],
    desc:"Swap overload (allocate > free RAM, force swap-out)" },

  /* 文件系统 2 */
  { module:"filesystem", uid:"rFS_file_lock", required:["path","mode"], optional:[],
    desc:"File lock (noread/nowrite/norw/nodelete via chmod/chattr/mount)" },
  { module:"filesystem", uid:"rFS_iowait_high", required:["path"],
    optional:[{name:"workers",default:"4"}],
    desc:"High iowait on a mount point (multi-worker small-block sync dd into path)" },

  /* Docker 2 */
  { module:"docker", uid:"rDOCKER_kill", required:["container"], optional:[],
    desc:"Kill (stop) a docker container; clean restarts it" },
  { module:"docker", uid:"rDOCKER_mem_overload", required:["container","size"], optional:[],
    desc:"Container memory overload (allocate size inside container; supports M/G)" },

  /* NPU 19 */
  { module:"npu", uid:"rNPU_link_down", required:["chip"], optional:[],
    desc:"RoCE link down (hccn_tool -cfg recovery)" },
  { module:"npu", uid:"rNPU_ip_change", required:["chip","address","netmask"], optional:[],
    desc:"RoCE IP change" },
  { module:"npu", uid:"rNPU_gw_change", required:["chip","gateway"], optional:[],
    desc:"RoCE gateway change" },
  { module:"npu", uid:"rNPU_netdetect_change", required:["chip","address"], optional:[],
    desc:"Netdetect IP change" },
  { module:"npu", uid:"rNPU_arp", required:["chip","dev","ip","mac"], optional:[],
    desc:"ARP entry poisoning (inject=add, clean=del)" },
  { module:"npu", uid:"rNPU_route", required:["chip","address","netmask","gateway"], optional:[],
    desc:"RoCE route poisoning (inject=add, clean=del)" },
  { module:"npu", uid:"rNPU_iprule", required:["chip","dir","ip","table"], optional:[],
    desc:"ip rule poisoning (inject=add, clean=del)" },
  { module:"npu", uid:"rNPU_iproute", required:["chip","ip","ip_mask","table","via","dev"], optional:[],
    desc:"ip route poisoning (inject=add, clean=del)" },
  { module:"npu", uid:"rNPU_bw_limit", required:["chip","bw_limit"], optional:[],
    desc:"RoCE bandwidth shaping limit" },
  { module:"npu", uid:"rNPU_mtu_mismatch", required:["chip","size"], optional:[],
    desc:"RoCE MTU mismatch" },
  { module:"npu", uid:"rNPU_dscp_tc_change", required:["chip","dscp","tc"], optional:[],
    desc:"DSCP-to-TC mapping change" },
  { module:"npu", uid:"rNPU_roce_port_change", required:["chip","port"], optional:[],
    desc:"RoCE UDP port change" },
  { module:"npu", uid:"rNPU_pcie_down", required:["npu_id"],
    optional:[{name:"gen",default:""}],
    desc:"NPU PCIe link speed downgrade (Gen4->Gen1, setpci)" },
  { module:"npu", uid:"rNPU_aic_load", required:["chip"],
    optional:[{name:"load_pct",default:"100"}],
    desc:"NPU AICore stress (aclnnMatmul FP16)" },
  { module:"npu", uid:"rNPU_aiv_load", required:["chip"],
    optional:[{name:"load_pct",default:"100"}],
    desc:"NPU AIVector stress (aclnnExp FP16)" },
  { module:"npu", uid:"rNPU_hbm_load", required:["chip","size"], optional:[],
    desc:"NPU HBM memory stress" },
  { module:"npu", uid:"rNPU_chip_reset", required:["npu_id"],
    optional:[{name:"core",default:""}],
    desc:"NPU chip reset via npu-smi (service disruption)" },
  { module:"npu", uid:"rNPU_driver_unbind", required:["chip"], optional:[],
    desc:"NPU driver unbind (simulate driver abnormality)" },
  { module:"npu", uid:"rNPU_pcie_remove", required:["chip"], optional:[],
    desc:"NPU PCIe remove (simulate NPU card loss)" },

  /* 系统 2 */
  { module:"system", uid:"rSYS_panic", required:[], optional:[],
    desc:"Kernel panic via sysrq 'c' (inject-only, irreversible)" },
  { module:"system", uid:"rSYS_poweroff", required:["mode"], optional:[],
    desc:"Machine power off / reboot (inject-only, irreversible; mode=0 reboot, 1 poweroff)" },
];

/* ============================================================
 * 渲染:故障目录
 * ============================================================ */
let currentModule = "all";
let currentQuery = "";

function renderCatalogTabs() {
  const tabs = document.getElementById("catalogTabs");
  const counts = { all: FAULT_CATALOG.length };
  for (const m of MODULES) {
    counts[m.key] = FAULT_CATALOG.filter(f => f.module === m.key).length;
  }
  const allTabs = [
    { key:"all", name:"全部", color:"#6b7280" },
    ...MODULES.map(m => ({ key:m.key, name:m.name, color:m.color }))
  ];
  tabs.innerHTML = allTabs.map(t => `
    <button class="tab ${t.key === currentModule ? "active" : ""}" data-module="${t.key}">
      <span class="tab-dot" style="background:${t.color}"></span>
      ${t.name}
      <span class="tab-count">${counts[t.key]}</span>
    </button>
  `).join("");
  tabs.querySelectorAll(".tab").forEach(btn => {
    btn.addEventListener("click", () => {
      currentModule = btn.dataset.module;
      renderCatalogTabs();
      renderCatalog();
    });
  });
}

function renderCatalog() {
  const table = document.getElementById("catalogTable");
  const empty = document.getElementById("catalogEmpty");
  const countEl = document.getElementById("catalogCount");

  let filtered = FAULT_CATALOG.slice();
  if (currentModule !== "all") {
    filtered = filtered.filter(f => f.module === currentModule);
  }
  if (currentQuery) {
    const q = currentQuery.toLowerCase();
    filtered = filtered.filter(f =>
      f.uid.toLowerCase().includes(q) ||
      f.desc.toLowerCase().includes(q) ||
      f.module.toLowerCase().includes(q) ||
      f.required.some(p => p.toLowerCase().includes(q)) ||
      (f.optional || []).some(p => p.name.toLowerCase().includes(q))
    );
  }

  countEl.textContent = `显示 ${filtered.length} / ${FAULT_CATALOG.length} 条`;

  if (filtered.length === 0) {
    table.innerHTML = "";
    empty.hidden = false;
    return;
  }
  empty.hidden = true;

  table.innerHTML = `
    <thead>
      <tr>
        <th>UID</th>
        <th>必填</th>
        <th>可选</th>
        <th>说明</th>
        <th></th>
      </tr>
    </thead>
    <tbody>
      ${filtered.map(f => {
        const m = MODULE_MAP[f.module];
        const reqHtml = f.required.length ? f.required.map(p => `<span class="param-required">${p}</span>`).join(" ") : "—";
        const optHtml = (f.optional && f.optional.length)
          ? f.optional.map(p => `<span class="param-optional">${p.name}=${p.default}</span>`).join(" ")
          : "—";
        return `
          <tr data-uid="${f.uid}">
            <td class="uid-cell"><span class="mod-dot" style="background:${m.color}"></span>${f.uid}</td>
            <td class="param-cell">${reqHtml}</td>
            <td class="param-cell">${optHtml}</td>
            <td class="desc-cell">${f.desc}${f.cleanup ? ` <span class="cleanup-tag">[${f.cleanup}]</span>` : ""}</td>
            <td class="row-action"><button data-uid="${f.uid}" class="cmd-trigger">构造命令</button></td>
          </tr>
        `;
      }).join("")}
    </tbody>
  `;

  // 行点击打开命令构造器
  table.querySelectorAll("tr[data-uid]").forEach(tr => {
    tr.addEventListener("click", (e) => {
      if (e.target.classList.contains("copy-btn")) return;
      openCmdBuilder(tr.dataset.uid);
    });
  });
}

/* 参数中文提示(命令构造器用;未列出的参数只显示参数名) */
const PARAM_HINTS = {
  cores: "CPU 核心编号,如 0,1 或 0-3",
  device: "磁盘设备,如 /dev/sda 或 sda",
  workers: "并发 worker 数(默认 4)",
  size_mb: "单次写入大小 MB(默认 200)",
  iface: "网卡名,如 eth0 / enp3s0",
  delay_ms: "延迟毫秒数,如 100",
  loss_pct: "丢包率 %,如 5",
  reorder_pct: "乱序率 %,如 10",
  speed_mbps: "限速 Mbps,如 10",
  protocol: "协议:tcp / udp",
  port: "端口号,如 8080",
  service: "服务名,如 nginx",
  cycle_sec: "闪断周期秒,如 2",
  count: "闪断次数,如 10",
  rate_kbps: "限速 kbps,如 1000",
  jitter_ms: "抖动毫秒,如 20",
  direction: "方向:in / out / both",
  pid: "进程 PID,如 1234",
  chip: "NPU 芯片编号,如 0 / 1",
  address: "IP 地址,如 192.168.1.10",
  netmask: "子网掩码,如 255.255.255.0",
  gateway: "网关 IP,如 192.168.1.1",
  dev: "网卡设备名,如 eth0",
  ip: "IP 地址",
  mac: "MAC 地址,如 aa:bb:cc:dd:ee:ff",
  dir: "方向:in / out",
  table: "路由表编号,如 100",
  ip_mask: "IP/掩码,如 192.168.1.0/24",
  via: "下一跳 IP,如 192.168.1.1",
  bw_limit: "带宽上限,如 100",
  size: "MTU 大小,如 1500",
  dscp: "DSCP 值,如 46",
  tc: "TC 值,如 3",
  quota_pct: "CPU 配额百分比 1-99,如 80",
  freq_mhz: "CPU 最高频率 MHz,如 2000",
  path: "路径,如 /data 或 /mnt/data",
  corrupt_pct: "损坏率 %,如 5",
  target: "目标地址 host:port 或 IP,如 10.0.0.1:80",
  rate_mb: "分配速率 MB/s,如 100",
  blocks: "内存块数,如 4096",
  block_kb: "每块大小 KB,如 64",
  mode: "锁定模式 noread/nowrite/norw/nodelete;sys_poweroff:0=重启 1=关机",
  container: "容器名或 ID,如 webapp",
  npu_id: "NPU PCIe 序号,如 0",
  gen: "PCIe 代数,如 1",
  core: "NPU core 序号,如 0",
};

/* ============================================================
 * 命令构造器 modal
 * ============================================================ */
function openCmdBuilder(uid) {
  const f = FAULT_CATALOG.find(x => x.uid === uid);
  if (!f) return;
  const modal = document.getElementById("cmdModal");
  document.getElementById("cmdUid").textContent = f.uid;
  document.getElementById("cmdDesc").textContent = f.desc;

  const params = [
    ...f.required.map(p => ({ name:p, required:true, default:"" })),
    ...(f.optional || []).map(p => ({ name:p.name, required:false, default:p.default }))
  ];

  const body = document.getElementById("cmdBody");
  body.innerHTML = `
    <div class="modal-error" id="modalError" hidden></div>
    ${params.length ? `
      <h4>参数 ${f.required.length ? `<span style="color:var(--crit)">* 必填</span>` : ""}</h4>
      <div class="modal-params">
        ${params.map(p => {
          const hint = PARAM_HINTS[p.name] || "";
          return `
          <div class="modal-param">
            <div class="modal-param-name">${p.name}<span class="${p.required ? "req" : "opt"}">${p.required ? "*必填" : "可选"}</span></div>
            ${hint ? `<div class="modal-param-hint">${hint}</div>` : ""}
            <input data-param="${p.name}" value="${p.default}" placeholder="${hint || (p.required ? "必填" : "默认 "+p.default)}" />
          </div>`;
        }).join("")}
      </div>
    ` : `<p class="muted" style="font-size:13px">该故障无需参数。</p>`}
    <h4>命令预览</h4>
    <div class="modal-cmd-bar">
      <label class="modal-force"><input type="checkbox" id="modalForce" /> 强制替换(覆盖同资源)</label>
      <span class="modal-cmd-actions">
        <button class="modal-copy" id="modalCopy">复制</button>
        <button class="modal-exec" id="modalExec" type="button">执行注入</button>
      </span>
    </div>
    <div class="modal-cmd" id="modalCmd"><div id="cmdText"></div></div>
    ${f.cleanup ? `<p class="muted" style="font-size:12px;margin-top:10px">清理策略: <code>${f.cleanup}</code></p>` : ""}
  `;

  const forceChk = document.getElementById("modalForce");
  const updateCmd = () => {
    const inputs = body.querySelectorAll("input[data-param]");
    const args = [];
    inputs.forEach(inp => {
      const val = inp.value.trim();
      if (val) args.push(`--${inp.dataset.param}=${val}`);
    });
    const force = forceChk && forceChk.checked;
    const cmd = `dcat inject ${f.uid}${force ? " --force" : ""}${args.length ? " " + args.join(" ") : ""}`;
    document.getElementById("cmdText").innerHTML = renderCmdPreview(cmd);
    document.getElementById("modalCopy").onclick = () => copyText(cmd, "已复制命令");
  };

  body.querySelectorAll("input[data-param]").forEach(inp => {
    inp.addEventListener("input", updateCmd);
  });
  if (forceChk) forceChk.addEventListener("change", updateCmd);
  updateCmd();

  /* 执行注入按钮:收集参数 → POST /api/inject(仅连 dcat serve 时可用) */
  const execBtn = document.getElementById("modalExec");
  if (execBtn) {
    execBtn.onclick = () => {
      if (!remoteConnected) { showToast("未连接 dcat serve(需经 SSH 隧道访问本页)"); return; }
      const requiredSet = new Set(f.required);
      const inputs = body.querySelectorAll("input[data-param]");
      const paramsObj = {};
      let missing = false;
      inputs.forEach(inp => {
        const val = inp.value.trim();
        if (val) paramsObj[inp.dataset.param] = val;
        else if (requiredSet.has(inp.dataset.param)) missing = true;
      });
      if (missing) { showToast("必填参数不能为空"); return; }
      const force = !!(forceChk && forceChk.checked);
      hideModalError();
      doInject(f.uid, paramsObj, force).then(ok => { if (ok) modal.hidden = true; });
    };
  }

  modal.hidden = false;
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

// 单行命令预览(可整行复制到终端执行)
function renderCmdPreview(cmd) {
  return `<code class="cmd-line">${escapeHtml(cmd)}</code>`;
}

document.addEventListener("click", (e) => {
  const modal = document.getElementById("cmdModal");
  if (modal.hidden) return;
  if (e.target.dataset.close !== undefined || e.target === modal) {
    modal.hidden = true;
  }
});
document.addEventListener("keydown", (e) => {
  if (e.key === "Escape") {
    document.getElementById("cmdModal").hidden = true;
  }
});

/* ============================================================
 * 复制 / toast
 * ============================================================ */
function copyText(text, toastMsg) {
  const fallback = () => {
    const ta = document.createElement("textarea");
    ta.value = text; ta.style.position = "fixed"; ta.style.opacity = "0";
    document.body.appendChild(ta); ta.select();
    try { document.execCommand("copy"); } catch (e) {}
    document.body.removeChild(ta);
    showToast(toastMsg || "已复制");
  };
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(text)
      .then(() => showToast(toastMsg || "已复制"))
      .catch(fallback);
  } else {
    fallback();
  }
}

let toastTimer = null;
function showToast(msg) {
  const t = document.getElementById("toast");
  t.textContent = msg;
  t.hidden = false;
  if (toastTimer) clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 1800);
}

/* 静态复制按钮(快速开始 code block) */
document.addEventListener("click", (e) => {
  const btn = e.target.closest(".copy-btn");
  if (!btn) return;
  e.stopPropagation();
  e.preventDefault();
  copyText(btn.dataset.copy, "已复制命令");
  const original = btn.textContent;
  btn.textContent = "✓";
  btn.classList.add("copied");
  setTimeout(() => { btn.textContent = original; btn.classList.remove("copied"); }, 1500);
});

/* 搜索框 */
document.getElementById("catalogSearch").addEventListener("input", (e) => {
  currentQuery = e.target.value.trim();
  renderCatalog();
});

/* ============================================================
 * 初始化
 * ============================================================ */
renderCatalogTabs();
renderCatalog();

/* ============================================================
 * 远程控制(API 客户端 + 活跃/历史面板 + 执行/清理)
 * 仅当页面由 dcat serve 同源提供(/api/health 可达)时激活;
 * 静态打开(file:// 或无后端)时降级为原展示页,不报错。
 * ============================================================ */
const DCAT_API = {
  async get(path) {
    const r = await fetch(path);
    if (!r.ok) throw new Error("HTTP " + r.status);
    return r.json();
  },
  async post(path, body) {
    const r = await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    return r.json();   /* dcat error 也返 200 + status:error */
  },
  health:  () => DCAT_API.get("/api/health"),
  state:   () => DCAT_API.get("/api/state"),
  history: () => DCAT_API.get("/api/history"),
  inject:  (uid, params, force) => DCAT_API.post("/api/inject", { uid, params, force: !!force }),
  clean:   (uid, params) => DCAT_API.post("/api/clean", uid ? { uid, params } : {}),
};

let remoteConnected = false;
let remoteWritable = false;
let pollTimer = null;

/* ---- 连接状态 pill(顶栏)---- */
function setConn(state, text) {
  const pill = document.getElementById("connPill");
  const txt = document.getElementById("connText");
  if (!pill || !txt) return;
  const dot = pill.querySelector(".dot");
  if (dot) dot.className = "dot " + (state === "off" ? "dot-off" : "dot-ok");
  pill.className = "conn-pill " + (state === "off" ? "conn-off" : (remoteWritable ? "conn-rw" : "conn-ro"));
  txt.textContent = text + (state === "off" ? "" : (remoteWritable ? " · 可写" : " · 只读"));
}

async function initRemote() {
  const cleanAllBtn = document.getElementById("cleanAllBtn");
  try {
    const h = await DCAT_API.health();
    remoteConnected = true;
    remoteWritable = !!h.writable;
    setConn("on", "已连接");
    document.querySelectorAll(".modal-exec").forEach(b => b.style.display = remoteWritable ? "" : "none");
    if (cleanAllBtn) { cleanAllBtn.hidden = !remoteWritable; cleanAllBtn.onclick = doCleanAll; }
    await refreshDash();
    pollTimer = setInterval(refreshDash, 3000);
  } catch (e) {
    remoteConnected = false;
    setConn("off", "未连接");
    document.querySelectorAll(".modal-exec").forEach(b => b.style.display = "none");
    if (cleanAllBtn) cleanAllBtn.hidden = true;
    renderSummary(0, 0);
    renderActive([]);
    renderHistoryTable([]);
  }
}

async function refreshDash() {
  if (!remoteConnected) return;
  try {
    const [st, hi] = await Promise.all([DCAT_API.state(), DCAT_API.history()]);
    const active = st.data || [];
    const hist = hi.data || [];
    renderSummary(active.length, hist.length);
    renderActive(active);
    renderHistoryTable(hist);
  } catch (e) {
    setConn("off", "刷新失败:" + e.message);
  }
}

function renderSummary(activeN, histN) {
  const cleaned = histN - activeN;
  const el = document.getElementById("statCards");
  if (el) {
    el.innerHTML = `
      <div class="stat-card"><div class="stat-num" style="color:var(--ok)">${activeN}</div><div class="stat-label">活跃注入</div></div>
      <div class="stat-card"><div class="stat-num">${histN}</div><div class="stat-label">总历史</div></div>
      <div class="stat-card"><div class="stat-num" style="color:var(--text-muted)">${cleaned}</div><div class="stat-label">已清理</div></div>
      <div class="stat-card"><div class="stat-num" style="color:var(--accent)">${FAULT_CATALOG.length}</div><div class="stat-label">故障总数</div></div>
    `;
  }
  const ac = document.getElementById("activeCount");
  const hc = document.getElementById("historyCount");
  if (ac) ac.textContent = activeN;
  if (hc) hc.textContent = histN;
}

function recParamsToText(rec) {
  const p = rec.params || {};
  return Object.keys(p).map(k => `${k}=${p[k]}`).join(" ");
}
function recParamsToObj(rec) {
  return rec.params ? { ...rec.params } : {};
}
function recToInjectCmd(r) {
  const p = r.params || {};
  const a = Object.keys(p).map(k => `--${k}=${p[k]}`).join(" ");
  return `dcat inject ${r.uid}${a ? " " + a : ""}`;
}
function recToCleanCmd(r) {
  const p = r.params || {};
  const a = Object.keys(p).map(k => `--${k}=${p[k]}`).join(" ");
  return `dcat clean ${r.uid}${a ? " " + a : ""}`;
}

function renderActive(records) {
  const el = document.getElementById("activeList");
  if (!el) return;
  if (!records.length) {
    el.innerHTML = '<div class="empty-hint">无活跃注入 — 点下方「故障目录」选故障注入。</div>';
    return;
  }
  el.innerHTML = records.map(r => `
    <div class="active-row" data-uid="${escapeHtml(r.uid)}">
      <div class="active-row-main">
        <code class="active-uid">${escapeHtml(r.uid)}</code>
        <span class="active-rid">#${r.record_id}</span>
      </div>
      <div class="active-row-params">${escapeHtml(recParamsToText(r)) || '—'}</div>
      <div class="active-row-time">${escapeHtml(r.started_at || '')}</div>
      <div class="active-row-action">
        ${remoteWritable
          ? `<button class="btn btn-sm btn-warn remote-clean" data-uid="${escapeHtml(r.uid)}" type="button">清理</button>`
          : `<span class="muted" style="font-size:11px">只读</span>`}
      </div>
    </div>
  `).join("");
  el.querySelectorAll(".remote-clean").forEach(btn => {
    btn.onclick = () => {
      const rec = records.find(x => x.uid === btn.dataset.uid);
      doClean(btn.dataset.uid, recParamsToObj(rec));
    };
  });
}

function renderHistoryTable(records) {
  const table = document.getElementById("historyTable");
  const empty = document.getElementById("historyEmpty");
  if (!table) return;
  if (!records.length) {
    table.innerHTML = "";
    if (empty) { empty.hidden = false; empty.textContent = "暂无历史记录。"; }
    return;
  }
  if (empty) empty.hidden = true;
  const shown = records.slice(0, 5);   /* 仅最近 5 条,防页面过长 */
  table.innerHTML = `
    <thead><tr><th>#</th><th>UID</th><th>注入命令</th><th>清理命令</th><th>时间</th><th>状态</th></tr></thead>
    <tbody>
      ${shown.map(r => `
        <tr>
          <td>${r.record_id}</td>
          <td class="uid-cell"><code>${escapeHtml(r.uid)}</code></td>
          <td class="param-cell"><code style="font-size:11px">${escapeHtml(recToInjectCmd(r))}</code>
            <button class="copy-btn" data-copy="${escapeHtml(recToInjectCmd(r))}">复制</button></td>
          <td class="param-cell"><code style="font-size:11px">${escapeHtml(recToCleanCmd(r))}</code>
            <button class="copy-btn" data-copy="${escapeHtml(recToCleanCmd(r))}">复制</button></td>
          <td class="active-row-time">${escapeHtml(r.started_at || "")}</td>
          <td>${r.active ? '<span class="pill pill-on">活跃</span>' : '<span class="pill pill-off">已清理</span>'}</td>
        </tr>`).join("")}
    </tbody>`;
  const note = document.getElementById("historyNote");
  if (note) note.textContent = records.length > 5 ? `仅显示最近 5 条(共 ${records.length} 条)` : "";
}

async function doInject(uid, paramsObj, force) {
  if (!remoteConnected) { showToast("未连接 dcat serve"); return false; }
  if (!remoteWritable) { showToast("只读模式 — 用「复制」按钮拷命令到终端执行"); return false; }
  const paramStr = Object.keys(paramsObj).map(k => `${k}=${paramsObj[k]}`).join(" ");
  if (!confirm(`确认在服务器注入故障:\n  ${uid}${force ? " (--force)" : ""}\n参数: ${paramStr}\n\n点击「确定」执行,「取消」放弃。`)) return false;
  try {
    const r = await DCAT_API.inject(uid, paramsObj, force);
    if (r.status === "ok") {
      const rid = (r.data && r.data.record_id) ? r.data.record_id : "-";
      showToast(`注入成功:${uid} (rid=${rid})`);
      hideModalError();
      await refreshDash();
      return true;
    }
    const code = r.error && r.error.code;
    const msg = (r.error && r.error.message) ? r.error.message : "unknown";
    if (code === 5) {
      showModalError(uid, paramsObj, msg);
    } else {
      showToast(`注入失败:${msg}`);
    }
    return false;
  } catch (e) {
    showToast("注入请求失败:" + e.message);
    return false;
  }
}

/* 冲突错误横幅(modal 内,code 5 时显示;带「强制替换」一键重试) */
function showModalError(uid, paramsObj, msg) {
  const el = document.getElementById("modalError");
  if (!el) { showToast("注入失败:" + msg); return; }
  el.hidden = false;
  el.innerHTML = `
    <div class="modal-error-msg">⚠ 该资源已注入,需 --force 替换。<span class="modal-error-detail">${escapeHtml(msg)}</span></div>
    <button class="modal-error-btn" type="button">强制替换</button>
  `;
  el.querySelector(".modal-error-btn").onclick = () => {
    doInject(uid, paramsObj, true).then(ok => { if (ok) document.getElementById("cmdModal").hidden = true; });
  };
}
function hideModalError() {
  const el = document.getElementById("modalError");
  if (el) { el.hidden = true; el.innerHTML = ""; }
}

async function doClean(uid, paramsObj) {
  if (!remoteConnected) { showToast("未连接 dcat serve"); return; }
  try {
    const r = await DCAT_API.clean(uid, paramsObj);
    if (r.status === "ok") {
      showToast(`清理成功:${uid}`);
      await refreshDash();
    } else {
      const msg = (r.error && r.error.message) ? r.error.message : "unknown";
      showToast(`清理失败:${msg}`);
    }
  } catch (e) {
    showToast("清理请求失败:" + e.message);
  }
}

/* 清理全部活跃注入(clean --all,无 uid → 后端 dispatch_clean_all) */
async function doCleanAll() {
  if (!remoteConnected || !remoteWritable) return;
  if (!confirm("确定清理全部活跃注入?")) return;
  try {
    const r = await DCAT_API.clean("", {});
    if (r.status === "ok") {
      showToast("已清理全部活跃注入");
      await refreshDash();
    } else {
      const msg = (r.error && r.error.message) ? r.error.message : "unknown";
      showToast(`清理失败:${msg}`);
    }
  } catch (e) {
    showToast("清理请求失败:" + e.message);
  }
}

/* 特性 strip(上下文简介,不抢主视觉) */
const FEATURES = [
  { icon: "⚙", name: "9 模块覆盖", desc: "CPU/存储/网络/进程/内存/文件系统/Docker/NPU/系统 共 57 条故障" },
  { icon: "🔌", name: "插件化故障", desc: "加一个故障 = 加脚本 + 配置一行" },
  { icon: "🛡", name: "预检护栏", desc: "参数校验 + 同资源重注入拒绝" },
  { icon: "📊", name: "状态跟踪", desc: "state.json 持久化,断电不丢" },
  { icon: "🌐", name: "远程控制", desc: "dcat serve + SSH 隧道远程注入/查看" },
  { icon: "⚡", name: "极简依赖", desc: "纯 C11 + CMake + bash" },
];
function renderFeatureStrip() {
  const el = document.getElementById("featureStrip");
  if (!el) return;
  el.innerHTML = FEATURES.map(f =>
    `<div class="feature-chip"><span class="feature-chip-icon">${f.icon}</span><div class="feature-chip-text"><div class="feature-chip-name">${f.name}</div><div class="feature-chip-desc">${f.desc}</div></div></div>`
  ).join("");
}

/* 手动刷新按钮(dash-header) */
const _dashRefresh = document.getElementById("dashRefresh");
if (_dashRefresh) _dashRefresh.addEventListener("click", refreshDash);

renderFeatureStrip();
initRemote();
