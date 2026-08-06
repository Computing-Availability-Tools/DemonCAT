#define _GNU_SOURCE  /* realpath */
/* serve.c — dcat HTTP 控制平面(长驻模式)
 *
 * 单端口双职:静态前端 + /api 端点(包装 dispatch_route 与 state)。
 * MVP:单线程串行 accept,手写 HTTP/1.1(GET/POST + Content-Length),
 * 明文 + 仅监听 127.0.0.1(安全由 SSH 隧道兜底)。 */
#include "serve.h"
#include "dispatch.h"
#include "registry.h"
#include "state.h"
#include "output.h"
#include "types.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVE_VERSION "0.1.0"
#define READ_BUF_CAP  (256 * 1024)

static volatile sig_atomic_t g_stop = 0;
static int g_allow_write = 0;   /* --allow-write: 默认只读,不暴露 POST /api/inject|clean */
static void on_signal(int sig) { (void)sig; g_stop = 1; }

/* ---------- 响应 ---------- */
typedef struct { int code; const char *status; const char *ct; char *body; size_t body_len; } resp_t;

static const char *status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

static resp_t resp_json(int code, char *body /* takes ownership */) {
    resp_t r;
    r.code = code; r.status = status_text(code); r.ct = "application/json";
    r.body = body;
    r.body_len = body ? strlen(body) : 0;
    return r;
}

static resp_t resp_from_result(result_t *res) {
    char *s = output_to_json(res);
    result_free(res);
    if (!s) s = strdup("{\"status\":\"error\",\"error\":{\"code\":1,\"message\":\"no output\"}}");
    return resp_json(200, s);
}

static resp_t resp_err(int code, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    return resp_json(code, s);
}

static void resp_free(resp_t *r) {
    if (r->body) free(r->body);
    r->body = NULL;
}

/* ---------- HTTP 读取/解析 ---------- */
static long find_content_length(const char *buf) {
    const char *end = strstr(buf, "\r\n\r\n");
    size_t hdr_len = end ? (size_t)(end - buf) : strlen(buf);
    for (size_t i = 0; i < hdr_len; ) {
        size_t eol = i;
        while (eol < hdr_len && buf[eol] != '\n') eol++;
        if (eol - i >= 15 && strncasecmp(buf + i, "Content-Length:", 15) == 0)
            return strtol(buf + i + 15, NULL, 10);
        i = eol + 1;
    }
    return -1;
}

/* 读完整 HTTP 请求(headers + body)。成功 0,total 写入 *out_total。 */
static int read_request(int fd, char *buf, size_t cap, size_t *out_total) {
    size_t total = 0;
    char *hdr_end = NULL;
    for (;;) {
        if (total >= cap - 1) break;
        ssize_t r = recv(fd, buf + total, cap - 1 - total, 0);
        if (r < 0) {
            if (errno == EINTR) { if (g_stop) break; continue; }
            return -1;
        }
        if (r == 0) break;
        total += (size_t)r;
        buf[total] = '\0';
        if (!hdr_end) {
            hdr_end = strstr(buf, "\r\n\r\n");
            if (!hdr_end) continue;
        }
        size_t hoff = (size_t)(hdr_end - buf) + 4;
        long clen = find_content_length(buf);
        if (clen < 0) clen = 0;
        if (total - hoff >= (size_t)clen) break;   /* body 完整 */
    }
    buf[total] = '\0';
    *out_total = total;
    return (total > 0 && hdr_end) ? 0 : -1;
}

static void send_response(int fd, int code, const char *ct, const char *body, size_t body_len) {
    char header[512];
    int h = snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        code, status_text(code), ct, body_len);
    if (h > 0) {
        size_t sent = 0;
        while (sent < (size_t)h) {
            ssize_t s = send(fd, header + sent, (size_t)h - sent, 0);
            if (s <= 0) break;
            sent += (size_t)s;
        }
    }
    if (body && body_len) {
        size_t sent = 0;
        while (sent < body_len) {
            ssize_t s = send(fd, body + sent, body_len - sent, 0);
            if (s <= 0) break;
            sent += (size_t)s;
        }
    }
}

/* ---------- 静态文件 ---------- */
static const char *content_type_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".json") == 0) return "application/json";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".svg") == 0) return "image/svg+xml";
    if (strcmp(dot, ".ico") == 0) return "image/x-icon";
    return "application/octet-stream";
}

static void serve_static(int fd, const char *webroot, const char *raw_path) {
    char path[1024];
    const char *q = strchr(raw_path, '?');
    size_t plen = q ? (size_t)(q - raw_path) : strlen(raw_path);
    if (plen == 0 || plen >= sizeof(path)) { send_response(fd, 404, "text/plain", "bad path", 8); return; }
    memcpy(path, raw_path, plen); path[plen] = '\0';
    if (strcmp(path, "/") == 0) strcpy(path, "/index.html");
    /* Path traversal check: literal ".." and URL-encoded variants */
    if (strstr(path, "..") || strstr(path, "%2e") || strstr(path, "%2E")) {
        send_response(fd, 403, "text/plain", "forbidden", 9); return;
    }
    char full[1536];
    int n = snprintf(full, sizeof full, "%s%s", webroot, path);
    if (n <= 0 || (size_t)n >= sizeof full) { send_response(fd, 404, "text/plain", "path too long", 13); return; }
    /* Verify resolved path is under webroot */
    char real_full[1536], real_webroot[1536];
    if (realpath(full, real_full) && realpath(webroot, real_webroot)) {
        if (strncmp(real_full, real_webroot, strlen(real_webroot)) != 0) {
            send_response(fd, 403, "text/plain", "forbidden", 9); return;
        }
    }
    FILE *fp = fopen(full, "rb");
    if (!fp) { send_response(fd, 404, "text/plain", "not found", 9); return; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); send_response(fd, 500, "text/plain", "read error", 10); return; }
    char *body = malloc((size_t)sz ? (size_t)sz : 1);
    if (!body) { fclose(fp); send_response(fd, 500, "text/plain", "oom", 3); return; }
    size_t rd = fread(body, 1, (size_t)sz, fp); fclose(fp);
    send_response(fd, 200, content_type_for(path), body, rd);
    free(body);
}

/* ---------- API handlers ---------- */
static resp_t api_health(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "service", "dcat-serve");
    cJSON_AddStringToObject(root, "version", SERVE_VERSION);
    cJSON_AddNumberToObject(root, "faults", (double)registry_count());
    cJSON_AddNumberToObject(root, "active", (double)state_list_active());
    cJSON_AddBoolToObject(root, "writable", g_allow_write);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    return resp_json(200, s);
}

static resp_t api_catalog(void) {
    params_t empty; params_init(&empty);
    return resp_from_result(dispatch_route(NULL, "list", &empty));
}

static resp_t api_state(void) {
    params_t empty; params_init(&empty);
    return resp_from_result(dispatch_route(NULL, "query", &empty));
}

static void append_record_json(const injection_record_t *r, cJSON *arr) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "uid", r->uid);
    cJSON_AddNumberToObject(o, "record_id", (double)r->record_id);
    cJSON_AddStringToObject(o, "started_at", r->started_at);
    cJSON_AddBoolToObject(o, "active", r->active);
    cJSON *p = cJSON_CreateObject();
    for (int i = 0; i < r->params.count; i++)
        cJSON_AddStringToObject(p, r->params.items[i].key, r->params.items[i].value);
    cJSON_AddItemToObject(o, "params", p);
    cJSON_AddItemToArray(arr, o);
}

struct hist_ctx { injection_record_t arr[DCAT_MAX_RECORDS]; int n; };
static void collect_record(const injection_record_t *r, void *ctx) {
    struct hist_ctx *c = (struct hist_ctx *)ctx;
    if (c->n < DCAT_MAX_RECORDS) c->arr[c->n++] = *r;
}
static int cmp_record_desc(const void *a, const void *b) {
    long long ra = ((const injection_record_t *)a)->record_id;
    long long rb = ((const injection_record_t *)b)->record_id;
    if (ra < rb) return 1;
    if (ra > rb) return -1;
    return 0;
}
static resp_t api_history(void) {
    struct hist_ctx c; c.n = 0;
    state_for_each_all(collect_record, &c);
    qsort(c.arr, (size_t)c.n, sizeof(injection_record_t), cmp_record_desc);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < c.n; i++) append_record_json(&c.arr[i], arr);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", "history");
    cJSON_AddItemToObject(root, "data", arr);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    return resp_json(200, s);
}

static int parse_params_object(cJSON *pobj, params_t *out) {
    params_init(out);
    if (!pobj || !cJSON_IsObject(pobj)) return 0;
    cJSON *k;
    cJSON_ArrayForEach(k, pobj) {
        if (cJSON_IsString(k))
            if (params_set(out, k->string, k->valuestring) != 0) return -1;
    }
    return 0;
}

static resp_t api_inject(const char *body) {
    cJSON *root = cJSON_Parse(body);
    if (!root) return resp_err(400, "invalid JSON body");
    cJSON *uid_j = cJSON_GetObjectItem(root, "uid");
    if (!uid_j || !cJSON_IsString(uid_j) || !uid_j->valuestring[0]) {
        cJSON_Delete(root); return resp_err(400, "missing 'uid'");
    }
    params_t params;
    if (parse_params_object(cJSON_GetObjectItem(root, "params"), &params) != 0) {
        cJSON_Delete(root); return resp_err(400, "too many params");
    }
    int force = 0;
    cJSON *fj = cJSON_GetObjectItem(root, "force");
    if (fj && cJSON_IsBool(fj)) force = cJSON_IsTrue(fj) ? 1 : 0;
    result_t *r = dispatch_route_force(uid_j->valuestring, "inject", &params, force);
    cJSON_Delete(root);
    resp_t resp = resp_from_result(r);
    state_save();   /* 持久化,崩溃恢复 */
    return resp;
}

static resp_t api_clean(const char *body) {
    cJSON *root = cJSON_Parse(body);
    if (!root) return resp_err(400, "invalid JSON body");
    params_t params;
    if (parse_params_object(cJSON_GetObjectItem(root, "params"), &params) != 0) {
        cJSON_Delete(root); return resp_err(400, "too many params");
    }
    cJSON *uid_j = cJSON_GetObjectItem(root, "uid");
    result_t *r;
    if (!uid_j || !cJSON_IsString(uid_j) || !uid_j->valuestring[0])
        r = dispatch_clean_all();   /* 无 uid = clean --all */
    else
        r = dispatch_route(uid_j->valuestring, "clean", &params);
    cJSON_Delete(root);
    resp_t resp = resp_from_result(r);
    state_save();
    return resp;
}

/* ---------- 路由 ---------- */
static int is_known_api_path(const char *path) {
    return strcmp(path, "/api/health")==0 || strcmp(path, "/api/catalog")==0
        || strcmp(path, "/api/state")==0 || strcmp(path, "/api/history")==0
        || strcmp(path, "/api/inject")==0 || strcmp(path, "/api/clean")==0;
}

static resp_t handle_api(const char *method, const char *path, const char *body) {
    if (!is_known_api_path(path)) return resp_err(404, "unknown API path");
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/health")  == 0) return api_health();
        if (strcmp(path, "/api/catalog") == 0) return api_catalog();
        if (strcmp(path, "/api/state")   == 0) return api_state();
        if (strcmp(path, "/api/history") == 0) return api_history();
        /* inject/clean 是 POST-only → 落到 405 */
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/inject") == 0) {
            if (!g_allow_write) return resp_err(403, "write disabled — restart with --allow-write to enable inject/clean");
            return api_inject(body);
        }
        if (strcmp(path, "/api/clean")  == 0) {
            if (!g_allow_write) return resp_err(403, "write disabled — restart with --allow-write to enable inject/clean");
            return api_clean(body);
        }
        /* health/catalog/state/history 是 GET-only → 落到 405 */
    }
    return resp_err(405, "method not allowed");
}

/* ---------- 单连接 ---------- */
static void handle_conn(int fd, const char *webroot) {
    char *buf = malloc(READ_BUF_CAP);
    if (!buf) { send_response(fd, 500, "text/plain", "oom", 3); return; }
    size_t total = 0;
    if (read_request(fd, buf, READ_BUF_CAP, &total) != 0) {
        send_response(fd, 400, "text/plain", "bad request", 11);
        free(buf); return;
    }
    char *line_end = strstr(buf, "\r\n");
    if (!line_end) { send_response(fd, 400, "text/plain", "bad request", 11); free(buf); return; }
    *line_end = '\0';
    char method[16] = {0}, path[1024] = {0}, version[16] = {0};
    int n = sscanf(buf, "%15s %1023s %15s", method, path, version);
    *line_end = '\r';   /* 恢复,供后续 strstr 找 \r\n\r\n */
    if (n < 2) { send_response(fd, 400, "text/plain", "bad request line", 16); free(buf); return; }

    char *hdr_end = strstr(buf, "\r\n\r\n");
    const char *body = hdr_end ? hdr_end + 4 : "";
    /* 按 Content-Length 截断 body(防 trailing 字节污染 cJSON_Parse) */
    long clen = find_content_length(buf);
    if (clen >= 0 && hdr_end) {
        size_t boff = (size_t)(hdr_end - buf) + 4;
        if (boff + (size_t)clen < READ_BUF_CAP) buf[boff + (size_t)clen] = '\0';
    }
    (void)total;

    if (strncmp(path, "/api/", 5) == 0) {
        resp_t resp = handle_api(method, path, body);
        send_response(fd, resp.code, resp.ct, resp.body, resp.body_len);
        resp_free(&resp);
    } else if (strcmp(method, "GET") == 0) {
        serve_static(fd, webroot, path);
    } else {
        resp_t resp = resp_err(405, "method not allowed");
        send_response(fd, resp.code, resp.ct, resp.body, resp.body_len);
        resp_free(&resp);
    }
    free(buf);
}

/* ---------- webroot 派生 + serve_run ---------- */
static void derive_default_webroot(char *out, size_t cap) {
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n > 0) {
        out[n] = '\0';
        char *slash = strrchr(out, '/');
        if (slash) *slash = '\0';   /* strip "dcat" → exe_dir (build/) */
        size_t len = strlen(out);
        snprintf(out + len, cap - len, "/../src/web");
        return;
    }
    snprintf(out, cap, "src/web");
}

int serve_run(int port, const char *bind_addr, const char *webroot_in, int allow_write) {
    g_allow_write = allow_write ? 1 : 0;
    if (port <= 0) port = 8080;
    if (!bind_addr || !bind_addr[0]) bind_addr = "127.0.0.1";
    char webroot_buf[1024];
    const char *webroot = webroot_in;
    if (!webroot || !webroot[0]) {
        derive_default_webroot(webroot_buf, sizeof webroot_buf);
        webroot = webroot_buf;
    }

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in addr; memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) {
        fprintf(stderr, "dcat serve: invalid bind address '%s'\n", bind_addr);
        close(sfd); return 1;
    }
    if (bind(sfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        fprintf(stderr, "dcat serve: bind %s:%d failed: %s\n", bind_addr, port, strerror(errno));
        close(sfd); return 1;
    }
    if (listen(sfd, 16) < 0) {
        fprintf(stderr, "dcat serve: listen failed: %s\n", strerror(errno));
        close(sfd); return 1;
    }

    fprintf(stderr, "dcat serve: listening on %s:%d (webroot=%s, %s) — Ctrl+C to stop\n",
            bind_addr, port, webroot, g_allow_write ? "read-write" : "read-only");

    for (;;) {
        if (g_stop) break;
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) { if (g_stop) break; continue; }
            continue;
        }
        handle_conn(cfd, webroot);
        close(cfd);
    }
    close(sfd);
    state_save();
    fprintf(stderr, "dcat serve: stopped (state saved)\n");
    return 0;
}
