/* tests/ut/test_serve.c — 通过 #include "serve.c" 直接测其 static 纯函数(无 socket)
 *
 * 覆盖分支:status_text / content_type_for / find_content_length /
 *   is_known_api_path / parse_params_object / cmp_record_desc /
 *   handle_api(404 unknown / 405 method-mismatch / 403 write-disabled)
 *
 * 注:此 target 在 CMake 中不再单独编译 serve.c(本 TU 已 #include 进来),
 * 避免重复定义;其余 core 文件照常链接以满足 dispatch/state/registry 符号。 */

/* _GNU_SOURCE 必须在所有系统头之前定义:本 TU 经 test.h(<stdio.h>)首次拉入 glibc <features.h>,
 * 若晚于该首次包含再定义(serve.c 内的 #define 已来不及),realpath 等符号不声明
 * → -Werror=implicit-function-declaration(ubuntu 宽松可过,openEuler/NPU glibc 严格会挂)。
 * 故置于此处,先于 #include "test.h"。serve.c 内的同名 #define 是同值重定义,合法。 */
#define _GNU_SOURCE
#include "test.h"
#include "../../src/core/serve.c" /* 引入 static 函数与全部依赖 */
#include <cJSON.h>

/* ---- status_text:每个 code + default 分支 ---- */
int test_status_text(void) {
    ASSERT_STREQ(status_text(200), "OK");
    ASSERT_STREQ(status_text(400), "Bad Request");
    ASSERT_STREQ(status_text(403), "Forbidden");
    ASSERT_STREQ(status_text(404), "Not Found");
    ASSERT_STREQ(status_text(405), "Method Not Allowed");
    ASSERT_STREQ(status_text(413), "Payload Too Large");
    ASSERT_STREQ(status_text(500), "Internal Server Error");
    ASSERT_STREQ(status_text(999), "OK"); /* default 分支 */
    ASSERT_STREQ(status_text(0), "OK");   /* default 分支 */
    return 0;
}

/* ---- content_type_for:每个扩展名 + 无 dot + 未知扩展 ---- */
int test_content_type(void) {
    ASSERT_STREQ(content_type_for("a.html"), "text/html; charset=utf-8");
    ASSERT_STREQ(content_type_for("a.htm"), "text/html; charset=utf-8");
    ASSERT_STREQ(content_type_for("a.js"), "application/javascript; charset=utf-8");
    ASSERT_STREQ(content_type_for("a.css"), "text/css; charset=utf-8");
    ASSERT_STREQ(content_type_for("a.json"), "application/json");
    ASSERT_STREQ(content_type_for("a.png"), "image/png");
    ASSERT_STREQ(content_type_for("a.svg"), "image/svg+xml");
    ASSERT_STREQ(content_type_for("a.ico"), "image/x-icon");
    ASSERT_STREQ(content_type_for("noext"), "application/octet-stream");        /* 无 dot */
    ASSERT_STREQ(content_type_for("a.unknownext"), "application/octet-stream"); /* 未知 ext */
    ASSERT_STREQ(content_type_for(".hidden"), "application/octet-stream");      /* dot 开头,未知 */
    return 0;
}

/* ---- find_content_length:正常/大小写/缺失/无结束符/短行跳过/多 header ---- */
int test_find_content_length(void) {
    const char *r1 = "POST /api/inject HTTP/1.1\r\nHost: x\r\nContent-Length: 42\r\n\r\nbody";
    ASSERT_INT_EQ(find_content_length(r1), 42);

    const char *r2 = "POST /api/inject HTTP/1.1\r\ncontent-length: 7\r\n\r\n";
    ASSERT_INT_EQ(find_content_length(r2), 7); /* 大小写不敏感 */

    const char *r3 = "GET /api/health HTTP/1.1\r\nHost: x\r\n\r\n";
    ASSERT_INT_EQ(find_content_length(r3), -1); /* 缺失 */

    ASSERT_INT_EQ(find_content_length("GET / HTTP/1.1"), -1); /* 无 \r\n\r\n 且无 CL */

    /* 短行(<15)应被跳过,继续扫描到下一行的 Content-Length */
    const char *r5 = "short\r\nContent-Length: 100\r\n\r\n";
    ASSERT_INT_EQ(find_content_length(r5), 100);

    /* Content-Length 在第二行(多 header) */
    const char *r6 = "POST /x HTTP/1.1\r\nHost: y\r\nContent-Length: 5\r\n\r\n";
    ASSERT_INT_EQ(find_content_length(r6), 5);
    return 0;
}

/* ---- is_known_api_path:6 个已知 + 未知/边界 ---- */
int test_is_known_api_path(void) {
    ASSERT_INT_EQ(is_known_api_path("/api/health"), 1);
    ASSERT_INT_EQ(is_known_api_path("/api/catalog"), 1);
    ASSERT_INT_EQ(is_known_api_path("/api/state"), 1);
    ASSERT_INT_EQ(is_known_api_path("/api/history"), 1);
    ASSERT_INT_EQ(is_known_api_path("/api/inject"), 1);
    ASSERT_INT_EQ(is_known_api_path("/api/clean"), 1);
    ASSERT_INT_EQ(is_known_api_path("/api/unknown"), 0);
    ASSERT_INT_EQ(is_known_api_path("/"), 0);
    ASSERT_INT_EQ(is_known_api_path("/api/"), 0);
    return 0;
}

/* ---- parse_params_object:NULL / 非 object / 合法 object ---- */
int test_parse_params_object(void) {
    params_t p;
    /* NULL → 0,空 params(params_init 先调用) */
    ASSERT_INT_EQ(parse_params_object(NULL, &p), 0);
    ASSERT_INT_EQ(p.count, 0);

    /* 非 object(数组)→ 0,空 params */
    cJSON *arr = cJSON_CreateArray();
    ASSERT_INT_EQ(parse_params_object(arr, &p), 0);
    ASSERT_INT_EQ(p.count, 0);
    cJSON_Delete(arr);

    /* 合法 object:两个字符串键 */
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "cores", "0,1");
    cJSON_AddStringToObject(obj, "duration", "10");
    ASSERT_INT_EQ(parse_params_object(obj, &p), 0);
    ASSERT_INT_EQ(p.count, 2);
    ASSERT_STREQ(params_find(&p, "cores"), "0,1");
    ASSERT_STREQ(params_find(&p, "duration"), "10");
    cJSON_Delete(obj);
    return 0;
}

/* ---- cmp_record_desc:降序比较的三分支 ---- */
int test_cmp_record_desc(void) {
    injection_record_t a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.record_id = 5;
    b.record_id = 10;
    ASSERT_INT_EQ(cmp_record_desc(&a, &b), 1); /* a<b → 1(降序在前) */
    a.record_id = 10;
    b.record_id = 5;
    ASSERT_INT_EQ(cmp_record_desc(&a, &b), -1); /* a>b → -1 */
    a.record_id = 7;
    b.record_id = 7;
    ASSERT_INT_EQ(cmp_record_desc(&a, &b), 0); /* 相等 → 0 */
    return 0;
}

/* ---- handle_api 路由分支(不触达 dispatch/state) ---- */
int test_handle_api_404(void) {
    resp_t r = handle_api("GET", "/api/nope", "");
    ASSERT_INT_EQ(r.code, 404);
    ASSERT_STR_CONTAINS(r.body, "unknown API path");
    resp_free(&r);
    return 0;
}

int test_handle_api_405_get_on_post_only(void) {
    /* /api/inject 仅 POST;GET → 405 */
    resp_t r = handle_api("GET", "/api/inject", "");
    ASSERT_INT_EQ(r.code, 405);
    resp_free(&r);
    return 0;
}

int test_handle_api_405_post_on_get_only(void) {
    /* /api/health 仅 GET;POST → 405 */
    resp_t r = handle_api("POST", "/api/health", "");
    ASSERT_INT_EQ(r.code, 405);
    resp_free(&r);
    return 0;
}

int test_handle_api_405_unknown_method(void) {
    /* PUT 到已知 API → 既非 GET 也非 POST → 405 */
    resp_t r = handle_api("PUT", "/api/health", "");
    ASSERT_INT_EQ(r.code, 405);
    resp_free(&r);
    return 0;
}

int test_handle_api_403_write_disabled(void) {
    /* g_allow_write 默认 0 → POST /api/inject 与 /api/clean 均 403,不触达 inject/clean */
    ASSERT_INT_EQ(g_allow_write, 0);
    resp_t r = handle_api("POST", "/api/inject", "{\"uid\":\"x\"}");
    ASSERT_INT_EQ(r.code, 403);
    ASSERT_STR_CONTAINS(r.body, "write disabled");
    resp_free(&r);

    resp_t r2 = handle_api("POST", "/api/clean", "{}");
    ASSERT_INT_EQ(r2.code, 403);
    resp_free(&r2);
    return 0;
}

int main(void) {
    RUN_TEST(test_status_text);
    RUN_TEST(test_content_type);
    RUN_TEST(test_find_content_length);
    RUN_TEST(test_is_known_api_path);
    RUN_TEST(test_parse_params_object);
    RUN_TEST(test_cmp_record_desc);
    RUN_TEST(test_handle_api_404);
    RUN_TEST(test_handle_api_405_get_on_post_only);
    RUN_TEST(test_handle_api_405_post_on_get_only);
    RUN_TEST(test_handle_api_405_unknown_method);
    RUN_TEST(test_handle_api_403_write_disabled);
    return TEST_MAIN_RETURN();
}
