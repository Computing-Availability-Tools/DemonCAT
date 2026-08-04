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

/* ----------- 故障目录(33 条, 来自 README) ----------- */
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

  /* NPU 16 */
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
  { module:"npu", uid:"rNPU_roce_port_change", required:["chip","port"], optional:[],
    desc:"RoCE UDP 端口变更", cleanup:"sidecar replay" },
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
    <div class="modal-cmd" id="modalCmd"><button class="modal-copy" id="modalCopy">复制</button><button class="modal-exec" id="modalExec" type="button">执行注入</button><div id="cmdText"></div></div>
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
      doInject(f.uid, paramsObj).then(ok => { if (ok) modal.hidden = true; });
    };
  }

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

async function initRemote() {
  const sec = document.getElementById("remote");
  if (!sec) return;
  const dot = document.getElementById("remoteDot");
  const txt = document.getElementById("remoteStatusText");
  const meta = document.getElementById("remoteMeta");
  try {
    const h = await DCAT_API.health();
    remoteConnected = true;
    remoteWritable = !!h.writable;
    if (dot) dot.className = "dot dot-ok remote-dot";
    if (txt) txt.textContent = remoteWritable ? "已连接 dcat serve(可写)" : "已连接 dcat serve(只读)";
    if (meta) meta.textContent = `v${h.version} · ${h.faults} 故障 · ${h.active} 活跃 · ${remoteWritable ? "可注入/清理" : "只读 — 复制命令到终端执行"}`;
    sec.classList.add("remote-on");
    document.querySelectorAll(".modal-exec").forEach(b => b.style.display = remoteWritable ? "" : "none");
    await refreshRemote();
    pollTimer = setInterval(refreshRemote, 3000);
  } catch (e) {
    remoteConnected = false;
    if (dot) dot.className = "dot dot-off remote-dot";
    if (txt) txt.textContent = "未连接(静态展示模式)";
    if (meta) meta.textContent = "运行 dcat serve 并经 SSH 隧道访问以启用远程控制";
    sec.classList.add("remote-off");
    document.querySelectorAll(".modal-exec").forEach(b => b.style.display = "none");
    const repEmpty = document.getElementById("repEmpty");
    if (repEmpty) { repEmpty.hidden = false; repEmpty.textContent = "未连接 dcat serve,无报告数据。"; }
  }
}

async function refreshRemote() {
  if (!remoteConnected) return;
  try {
    const [st, hi] = await Promise.all([DCAT_API.state(), DCAT_API.history()]);
    renderActive(st.data || []);
    renderHistory(hi.data || []);
    renderReport(hi.data || []);
  } catch (e) {
    const txt = document.getElementById("remoteStatusText");
    if (txt) txt.textContent = "刷新失败:" + e.message;
  }
}

function recParamsToText(rec) {
  const p = rec.params || {};
  return Object.keys(p).map(k => `${k}=${p[k]}`).join(" ");
}
function recParamsToObj(rec) {
  return rec.params ? { ...rec.params } : {};
}
function activeBadge(active) {
  return active ? '<span class="pill pill-on">活跃</span>'
               : '<span class="pill pill-off">已清理</span>';
}

function renderActive(records) {
  const el = document.getElementById("activeList");
  if (!el) return;
  document.getElementById("activeCount").textContent = records.length;
  if (!records.length) {
    el.innerHTML = '<div class="remote-empty">无活跃注入。</div>';
    return;
  }
  el.innerHTML = records.map(r => `
    <div class="remote-row" data-uid="${escapeHtml(r.uid)}" data-rid="${r.record_id}">
      <div class="remote-row-main">
        <code class="remote-uid">${escapeHtml(r.uid)}</code>
        <span class="remote-rid">#${r.record_id}</span>
      </div>
      <div class="remote-row-params">${escapeHtml(recParamsToText(r)) || '—'}</div>
      <div class="remote-row-time">${escapeHtml(r.started_at || '')}</div>
      <div class="remote-row-action">
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

function renderHistory(records) {
  const el = document.getElementById("historyList");
  if (!el) return;
  document.getElementById("historyCount").textContent = records.length;
  if (!records.length) {
    el.innerHTML = '<div class="remote-empty">无历史记录。</div>';
    return;
  }
  el.innerHTML = records.map(r => `
    <div class="remote-row remote-row-hist">
      <div class="remote-row-main">
        <code class="remote-uid">${escapeHtml(r.uid)}</code>
        <span class="remote-rid">#${r.record_id}</span>
        ${activeBadge(r.active)}
      </div>
      <div class="remote-row-params">${escapeHtml(recParamsToText(r)) || '—'}</div>
      <div class="remote-row-time">${escapeHtml(r.started_at || '')}</div>
    </div>
  `).join("");
}

/* ---- 注入报告(摘要卡 + 历史表,重构 inject/clean 命令) ---- */
function recToInjectCmd(r) {
  const p = r.params || {};
  const args = Object.keys(p).map(k => `--${k}=${p[k]}`).join(" ");
  return `dcat inject ${r.uid}${args ? " " + args : ""}`;
}
function recToCleanCmd(r) {
  const p = r.params || {};
  const args = Object.keys(p).map(k => `--${k}=${p[k]}`).join(" ");
  return `dcat clean ${r.uid}${args ? " " + args : ""}`;
}
function renderReport(records) {
  const active = records.filter(r => r.active).length;
  const cleaned = records.length - active;
  const sumEl = document.getElementById("repSummary");
  if (sumEl) {
    sumEl.innerHTML = `
      <div class="rep-card"><div class="rep-card-num">${records.length}</div><div class="rep-card-label">总注入</div></div>
      <div class="rep-card"><div class="rep-card-num" style="color:var(--ok)">${active}</div><div class="rep-card-label">活跃</div></div>
      <div class="rep-card"><div class="rep-card-num" style="color:var(--text-muted)">${cleaned}</div><div class="rep-card-label">已清理</div></div>
    `;
  }
  const table = document.getElementById("repTable");
  const empty = document.getElementById("repEmpty");
  if (!table) return;
  document.getElementById("repCount").textContent = records.length;
  if (!records.length) {
    table.innerHTML = "";
    if (empty) { empty.hidden = false; empty.textContent = "暂无注入记录。"; }
    return;
  }
  if (empty) empty.hidden = true;
  table.innerHTML = `
    <thead>
      <tr><th>#</th><th>UID</th><th>注入命令</th><th>清理命令</th><th>时间</th><th>状态</th></tr>
    </thead>
    <tbody>
      ${records.map(r => `
        <tr>
          <td>${r.record_id}</td>
          <td class="uid-cell"><code>${escapeHtml(r.uid)}</code></td>
          <td class="param-cell"><code style="font-size:11px">${escapeHtml(recToInjectCmd(r))}</code>
            <button class="copy-btn" data-copy="${escapeHtml(recToInjectCmd(r))}">复制</button></td>
          <td class="param-cell"><code style="font-size:11px">${escapeHtml(recToCleanCmd(r))}</code>
            <button class="copy-btn" data-copy="${escapeHtml(recToCleanCmd(r))}">复制</button></td>
          <td class="remote-row-time">${escapeHtml(r.started_at || "")}</td>
          <td>${r.active ? '<span class="pill pill-on">活跃</span>' : '<span class="pill pill-off">已清理</span>'}</td>
        </tr>
      `).join("")}
    </tbody>
  `;
}

/* 返回 true=注入成功(供调用方关闭 modal) */
async function doInject(uid, paramsObj) {
  if (!remoteConnected) { showToast("未连接 dcat serve"); return false; }
  if (!remoteWritable) { showToast("只读模式 — 用「复制」按钮拷命令到 SSH 终端执行"); return false; }
  const paramStr = Object.keys(paramsObj).map(k => `${k}=${paramsObj[k]}`).join(" ");
  if (!confirm(`确认在服务器注入故障:\n  ${uid}\n参数: ${paramStr}\n\n点击「确定」执行,「取消」放弃。`)) return false;
  try {
    const r = await DCAT_API.inject(uid, paramsObj);
    if (r.status === "ok") {
      const rid = (r.data && r.data.record_id) ? r.data.record_id : "-";
      showToast(`注入成功:${uid} (rid=${rid})`);
      await refreshRemote();
      return true;
    }
    const msg = (r.error && r.error.message) ? r.error.message : "unknown";
    showToast(`注入失败:${msg}`);
    return false;
  } catch (e) {
    showToast("注入请求失败:" + e.message);
    return false;
  }
}

async function doClean(uid, paramsObj) {
  if (!remoteConnected) { showToast("未连接 dcat serve"); return; }
  try {
    const r = await DCAT_API.clean(uid, paramsObj);
    if (r.status === "ok") {
      showToast(`清理成功:${uid}`);
      await refreshRemote();
    } else {
      const msg = (r.error && r.error.message) ? r.error.message : "unknown";
      showToast(`清理失败:${msg}`);
    }
  } catch (e) {
    showToast("清理请求失败:" + e.message);
  }
}

document.addEventListener("click", (e) => {
  if (e.target && e.target.id === "remoteRefresh") refreshRemote();
});

initRemote();
