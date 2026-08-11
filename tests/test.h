#ifndef DCAT_TEST_H
#define DCAT_TEST_H
#include <stdio.h>
#include <string.h>
static int g_test_count = 0, g_test_fail = 0;
#define RUN_TEST(fn)                          \
    do {                                      \
        g_test_count++;                       \
        fprintf(stderr, "  -> %s ... ", #fn); \
        int r = fn();                         \
        if (r) {                              \
            g_test_fail++;                    \
            fprintf(stderr, "FAIL\n");        \
        } else                                \
            fprintf(stderr, "ok\n");          \
    } while (0)
/* NULL 防御 + 单次求值（避免带副作用参数二次求值）；失败 return 1 中断当前测试而非崩进程 */
#define ASSERT_TRUE(x)                                                        \
    do {                                                                      \
        if (!(x)) {                                                           \
            fprintf(stderr, "ASSERT_TRUE fail: %s:%d\n", __FILE__, __LINE__); \
            return 1;                                                         \
        }                                                                     \
    } while (0)
#define ASSERT_INT_EQ(a, b)                                                                        \
    do {                                                                                           \
        long long _ia = (long long)(a);                                                            \
        long long _ib = (long long)(b);                                                            \
        if (_ia != _ib) {                                                                          \
            fprintf(stderr, "INT_EQ fail: %lld != %lld at %s:%d\n", _ia, _ib, __FILE__, __LINE__); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define ASSERT_STREQ(a, b)                                                                                                          \
    do {                                                                                                                            \
        const char *_sa = (a);                                                                                                      \
        const char *_sb = (b);                                                                                                      \
        if (_sa == NULL || _sb == NULL || strcmp(_sa, _sb) != 0) {                                                                  \
            fprintf(stderr, "STREQ fail: '%s' != '%s' at %s:%d\n", _sa ? _sa : "(null)", _sb ? _sb : "(null)", __FILE__, __LINE__); \
            return 1;                                                                                                               \
        }                                                                                                                           \
    } while (0)
#define ASSERT_STR_CONTAINS(hay, needle)                                                                                               \
    do {                                                                                                                               \
        const char *_h = (hay);                                                                                                        \
        const char *_n = (needle);                                                                                                     \
        if (_h == NULL || _n == NULL || strstr(_h, _n) == NULL) {                                                                      \
            fprintf(stderr, "CONTAINS fail: '%s' not in '%s' at %s:%d\n", _n ? _n : "(null)", _h ? _h : "(null)", __FILE__, __LINE__); \
            return 1;                                                                                                                  \
        }                                                                                                                              \
    } while (0)
#define TEST_MAIN_RETURN() (g_test_fail ? 1 : 0)
#endif
