#ifndef DCAT_TEST_H
#define DCAT_TEST_H
#include <stdio.h>
#include <string.h>
static int g_test_count = 0, g_test_fail = 0;
#define RUN_TEST(fn) do { \
    g_test_count++; \
    fprintf(stderr, "  -> %s ... ", #fn); \
    int r = fn(); \
    if (r) { g_test_fail++; fprintf(stderr, "FAIL\n"); } \
    else fprintf(stderr, "ok\n"); \
} while (0)
#define ASSERT_TRUE(x) do { if (!(x)) { fprintf(stderr, "ASSERT_TRUE fail: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_INT_EQ(a, b) do { if ((a) != (b)) { fprintf(stderr, "INT_EQ fail: %d != %d at %s:%d\n", (a), (b), __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_STREQ(a, b) do { if (strcmp((a),(b)) != 0) { fprintf(stderr, "STREQ fail: '%s' != '%s' at %s:%d\n", (a),(b), __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_STR_CONTAINS(hay, needle) do { if (strstr((hay),(needle)) == NULL) { fprintf(stderr, "CONTAINS fail: '%s' not in '%s' at %s:%d\n", (needle),(hay), __FILE__, __LINE__); return 1; } } while (0)
#define TEST_MAIN_RETURN() (g_test_fail ? 1 : 0)
#endif
