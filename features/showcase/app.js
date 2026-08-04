/* ============================================================
 * DemonCAT Showcase — app.js
 * 数据驱动渲染:故障目录 / E2E 矩阵 / 模块概览
 * ============================================================ */

"use strict";

/* ----------- 模块定义 ----------- */
const MODULES = [
  { key: "cpu",  name: "CPU",   color: "#3b82f6", icon: "⚙" },
  { key: "disk", name: "存储",  color: "#f59e0b", icon: "💾" },
  { key: "net",  name: "网络",  color: "#10b981", icon: "🌐" },
  { key: "proc", name: "进程",  color: "#8b5cf6", icon: "⚙" },
  { key: "npu",  name: "NPU",   color: "#ef4444", icon: "🧠" },
];

const MODULE_MAP = Object.fromEntries(MODULES.map(m => [m.key, m]));

/* ----------- 故障目录(36 条, 来自 README) ----------- */
// 字段: module, uid, required[], optional[{name, default}], desc, cleanup(可选)
const FAULT_CATALOG = [
  /* CPU 2 */
  { module:"cpu", uid:"rCPU_overload", required:["cores"], optional:[],
    desc:"CPU 核心满载(支持多核心同时满载,perl 纯用户态)" },
  { module:"cpu", uid:"rCPU_core_offline", required:["cores"], optional:[],
    desc:"CPU 核离线(sysfs)" },

  /* 存储 1 */
  { module:"disk", uid:"rDISK_write_overload", required:["device"],
    optional:[{name:"workers",default:"4"},{name:"size_mb",default:"200"}],
    desc:"磁盘写压(dd 多实例)" },

  /* 网络 11 */
  { module:"net", uid:"rNET_delay", required:["iface","delay_ms"], optional:[],
    desc:"网络延迟(tc netem)" },
  { module:"net", uid:"rNET_loss", required:["iface","loss_pct"], optional:[],
    desc:"网络丢包(tc netem)" },
  { module:"net", uid:"rNET_reorder", required:["iface","reorder_pct"], optional:[],
    desc:"网络乱序(tc netem)" },
  { module:"net", uid:"rNET_down", required:["iface"], optional:[],
    desc:"网卡 down(ip link)" },
  { module:"net", uid:"rNET_degrade", required:["iface"],
    optional:[{name:"speed_mbps",default:"10"}],
    desc:"网卡降速(ethtool)" },
  { module:"net", uid:"rNET_port_occupy", required:["port"],
    optional:[{name:"protocol",default:"tcp"}],
    desc:"端口占用(socket holder)" },
  { module:"net", uid:"rNET_service_stop", required:["service"], optional:[],
    desc:"服务停止(systemctl)" },
  { module:"net", uid:"rNET_link_flap", required:["iface"],
    optional:[{name:"cycle_sec",default:"2"},{name:"count",default:"10"}],
    desc:"链路闪断(ip link 循环)" },
  { module:"net", uid:"rNET_bw_limit", required:["iface","rate_kbps"], optional:[],
    desc:"带宽限制(tc tbf)" },
  { module:"net", uid:"rNET_jitter", required:["iface","delay_ms","jitter_ms"], optional:[],
    desc:"延迟抖动(tc netem)" },
  { module:"net", uid:"rNET_tcp_loss", required:["port"],
    optional:[{name:"direction",default:"both"}],
    desc:"TCP 丢包(iptables DROP)" },

  /* 进程 3 */
  { module:"proc", uid:"rPROC_exit", required:["pid"], optional:[],
    desc:"进程退出(kill -9,不可恢复,inject-only)" },
  { module:"proc", uid:"rPROC_hang", required:["pid"], optional:[],
    desc:"进程挂起(SIGSTOP)" },
  { module:"proc", uid:"rPROC_zstate", required:["pid"], optional:[],
    desc:"僵尸进程(kill 目标进程 → 僵尸,clean 杀父进程回收,不可恢复)" },

  /* NPU 19 */
  { module:"npu", uid:"rNPU_link_down", required:["chip"], optional:[],
    desc:"RoCE 链路 down", cleanup:"cfg recovery" },
  { module:"npu", uid:"rNPU_ip_change", required:["chip","address","netmask"], optional:[],
    desc:"RoCE IP 变更", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_gw_change", required:["chip","gateway"], optional:[],
    desc:"RoCE 网关变更", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_netdetect_change", required:["chip","address"], optional:[],
    desc:"Netdetect IP 变更", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_arp_poison", required:["chip","dev","ip","mac"], optional:[],
    desc:"ARP 毒化(add wrong mac)", cleanup:"reverse op" },
  { module:"npu", uid:"rNPU_arp_del", required:["chip","dev","ip"], optional:[],
    desc:"ARP 条目删除", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_route_add", required:["chip","address","netmask","gateway"], optional:[],
    desc:"添加 RoCE 路由", cleanup:"reverse op" },
  { module:"npu", uid:"rNPU_route_del", required:["chip","address","netmask"], optional:[],
    desc:"删除 RoCE 路由", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_route_clear", required:["chip"], optional:[],
    desc:"清空路由表", cleanup:"cfg recovery" },
  { module:"npu", uid:"rNPU_iprule_add", required:["chip","dir","ip","table"], optional:[],
    desc:"添加 ip rule", cleanup:"reverse op" },
  { module:"npu", uid:"rNPU_iprule_del", required:["chip","dir","ip"], optional:[],
    desc:"删除 ip rule", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_iproute_add", required:["chip","ip","ip_mask","via","dev","table"], optional:[],
    desc:"添加 ip route", cleanup:"reverse op" },
  { module:"npu", uid:"rNPU_iproute_del", required:["chip","ip","ip_mask","table"], optional:[],
    desc:"删除 ip route", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_bw_limit", required:["chip","bw_limit"], optional:[],
    desc:"RoCE 带宽限速", cleanup:"set to max" },
  { module:"npu", uid:"rNPU_mtu_mismatch", required:["chip","size"], optional:[],
    desc:"RoCE MTU 变更", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_dscp_tc_change", required:["chip","dscp","tc"], optional:[],
    desc:"DSCP→TC 映射变更", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_prio_tc_change", required:["chip","map"], optional:[],
    desc:"Prio→TC 映射变更", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_pfc_change", required:["chip","bitmap"], optional:[],
    desc:"PFC 位图变更", cleanup:"sidecar replay" },
  { module:"npu", uid:"rNPU_roce_port_change", required:["chip","port"], optional:[],
    desc:"RoCE UDP 端口变更", cleanup:"sidecar replay" },
];

/* ----------- E2E 8 类分类(来自 README) ----------- */
const E2E_CATEGORIES = [
  { prefix:"FUNC",  name:"功能基线",   count:150,
    desc:"37 故障 inject→verify→clean→query 全链路 + query<uid> confirmed + 插件",
    dim:"功能基线" },
  { prefix:"BOUND", name:"边界值",     count:40,
    desc:"每参数类型系统性覆盖(整数越界/空值/格式错误/枚举非法)",
    dim:"边界值" },
  { prefix:"SEC",   name:"安全",       count:45,
    desc:"命令注入(inject+clean+query) + 权限边界 + 主机安全 + symlink 攻击",
    dim:"安全" },
  { prefix:"STATE", name:"状态一致性", count:25,
    desc:"clean×2/--force/reinject 拒绝/query 幂等/并发 inject 同/不同资源",
    dim:"状态一致性" },
  { prefix:"RES",   name:"韧性/自愈",  count:20,
    desc:"state 丢失/损坏/孤儿/幽灵/clean --all 幂等/state 表满",
    dim:"韧性" },
  { prefix:"CLI",   name:"CLI 接口",   count:25,
    desc:"解析错误 + 帮助 + 退出码 + --config + 未知 uid",
    dim:"接口" },
  { prefix:"CONC",  name:"并发竞争",   count:9,
    desc:"同时 inject+clean / 双进程写 state / clean --all + inject",
    dim:"并发" },
  { prefix:"INTER", name:"故障交互",   count:15,
    desc:"多故障叠加 / clean 一个不影响其他 / clean --all 后逐 verify",
    dim:"交互" },
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
    ${params.length ? `
      <h4>参数 ${f.required.length ? `<span style="color:var(--crit)">* 必填</span>` : ""}</h4>
      <div class="modal-params">
        ${params.map(p => `
          <div class="modal-param">
            <div class="modal-param-name">${p.name}<span class="${p.required ? "req" : "opt"}">${p.required ? "*必填" : "可选"}</span></div>
            <input data-param="${p.name}" value="${p.default}" placeholder="${p.required ? "必填" : "默认 "+p.default}" />
          </div>
        `).join("")}
      </div>
    ` : `<p class="muted" style="font-size:13px">该故障无需参数。</p>`}
    <h4>命令预览</h4>
    <div class="modal-cmd" id="modalCmd"><button class="modal-copy" id="modalCopy">复制</button><div id="cmdText"></div></div>
    ${f.cleanup ? `<p class="muted" style="font-size:12px;margin-top:10px">清理策略: <code>${f.cleanup}</code></p>` : ""}
  `;

  const updateCmd = () => {
    const inputs = body.querySelectorAll("input[data-param]");
    const args = [];
    inputs.forEach(inp => {
      const val = inp.value.trim();
      if (val) args.push(`--${inp.dataset.param}=${val}`);
    });
    const cmd = `dcat inject ${f.uid}${args.length ? " " + args.join(" ") : ""}`;
    document.getElementById("cmdText").innerHTML = renderCmdPreview(cmd);
    document.getElementById("modalCopy").onclick = () => copyText(cmd, "已复制命令");
  };

  body.querySelectorAll("input[data-param]").forEach(inp => {
    inp.addEventListener("input", updateCmd);
  });
  updateCmd();

  modal.hidden = false;
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

// 结构化拆分,不走正则拼 HTML(避免 class 属性里的 = 被误匹配导致 HTML 破坏)
// 输出多行:主命令一行,每个 --flag=val 独立一行缩进展示
function renderCmdPreview(cmd) {
  const tokens = cmd.split(/\s+/).filter(Boolean);
  if (tokens.length < 3) {
    return `<div class="cmd-main"><span class="cmd-prefix">${escapeHtml(cmd)}</span></div>`;
  }
  const prefix = `${tokens[0]} ${tokens[1]}`;
  const uid   = tokens[2];
  const args  = tokens.slice(3);

  const mainHtml = `<div class="cmd-main"><span class="cmd-prefix">${escapeHtml(prefix)}</span> <span class="cmd-uid">${escapeHtml(uid)}</span></div>`;

  let argsHtml = "";
  if (args.length) {
    argsHtml = `<div class="cmd-args">${args.map(arg => {
      const eq = arg.indexOf("=");
      if (eq === -1) {
        return `<div class="cmd-arg"><span class="cmd-flag">${escapeHtml(arg)}</span></div>`;
      }
      const flag = arg.slice(0, eq);
      const val  = arg.slice(eq + 1);
      return `<div class="cmd-arg"><span class="cmd-flag">${escapeHtml(flag)}</span><span class="cmd-eq">=</span><span class="cmd-val">${escapeHtml(val)}</span></div>`;
    }).join("")}</div>`;
  }
  return mainHtml + argsHtml;
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
 * E2E 矩阵渲染
 * ============================================================ */
function renderE2E() {
  const grid = document.getElementById("e2eGrid");
  const total = E2E_CATEGORIES.reduce((s, c) => s + c.count, 0);
  grid.innerHTML = E2E_CATEGORIES.map(c => `
    <div class="e2e-card">
      <div class="e2e-card-head">
        <span class="e2e-card-prefix">${c.prefix}-</span>
        <span class="e2e-card-count">${c.count}</span>
      </div>
      <h3 class="e2e-card-title">${c.name}</h3>
      <p class="e2e-card-desc">${c.desc}</p>
      <div class="e2e-card-dim">${c.dim}</div>
    </div>
  `).join("") + `
    <div class="e2e-card" style="border-left-color:var(--text-sub);background:linear-gradient(135deg,#f8fafc 0%,#eef2ff 100%)">
      <div class="e2e-card-head">
        <span class="e2e-card-prefix" style="color:var(--text-sub);background:var(--bg-alt)">Σ</span>
        <span class="e2e-card-count">${total}</span>
      </div>
      <h3 class="e2e-card-title">总计</h3>
      <p class="e2e-card-desc">CSV 驱动自动生成 + 串行执行</p>
      <div class="e2e-card-dim">8 类混沌工程矩阵</div>
    </div>
  `;
}

/* ============================================================
 * 模块概览渲染(类 CATMonitor)
 * ============================================================ */
function renderModules() {
  const grid = document.getElementById("moduleGrid");
  grid.innerHTML = MODULES.map(m => {
    const faults = FAULT_CATALOG.filter(f => f.module === m.key);
    const preview = faults.slice(0, 4).map(f => `<code>${f.uid.replace(/^r(NET|NPU|CPU|DISK|PROC)_/, "")}</code>`).join(" · ");
    const more = faults.length > 4 ? ` +${faults.length - 4}` : "";
    return `
      <div class="module-card" data-module="${m.key}" style="--module-color:${m.color}">
        <div class="module-head">
          <span class="module-name">${m.name}</span>
          <span class="module-chip"><span class="chip-dot"></span>稳定</span>
        </div>
        <div class="module-count" style="color:${m.color}">${faults.length}</div>
        <div class="module-faults">${preview}<span class="muted">${more}</span></div>
      </div>
    `;
  }).join("");
  grid.querySelectorAll(".module-card").forEach(card => {
    card.addEventListener("click", () => {
      currentModule = card.dataset.module;
      renderCatalogTabs();
      renderCatalog();
      document.getElementById("catalog").scrollIntoView({ behavior:"smooth" });
    });
  });
}

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
renderE2E();
renderModules();
