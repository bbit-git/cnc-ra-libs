/**
 * Minimal test framework — no external dependencies.
 * Each test file is a standalone executable.
 */

#ifndef RENDER_TEST_FRAMEWORK_H
#define RENDER_TEST_FRAMEWORK_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name)                                                  \
    static void test_##name();                                      \
    struct TestReg_##name {                                         \
        TestReg_##name() {                                          \
            g_tests_run++;                                          \
            printf("  %-50s ", #name);                              \
            fflush(stdout);                                         \
            test_##name();                                          \
        }                                                           \
    };                                                              \
    static TestReg_##name g_reg_##name;                             \
    static void test_##name()

#define EXPECT_TRUE(expr)                                           \
    do {                                                            \
        if (!(expr)) {                                              \
            printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_EQ(a, b)                                             \
    do {                                                            \
        auto _a = (a); auto _b = (b);                               \
        if (_a != _b) {                                             \
            printf("FAIL\n    %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

#define EXPECT_NE(a, b)                                             \
    do {                                                            \
        auto _a = (a); auto _b = (b);                               \
        if (_a == _b) {                                             \
            printf("FAIL\n    %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

#define EXPECT_GT(a, b)                                             \
    do {                                                            \
        auto _a = (a); auto _b = (b);                               \
        if (!(_a > _b)) {                                           \
            printf("FAIL\n    %s:%d: %s not > %s\n", __FILE__, __LINE__, #a, #b); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

#define EXPECT_GE(a, b)                                             \
    do {                                                            \
        auto _a = (a); auto _b = (b);                               \
        if (!(_a >= _b)) {                                          \
            printf("FAIL\n    %s:%d: %s not >= %s\n", __FILE__, __LINE__, #a, #b); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

#define EXPECT_LT(a, b)                                             \
    do {                                                            \
        auto _a = (a); auto _b = (b);                               \
        if (!(_a < _b)) {                                           \
            printf("FAIL\n    %s:%d: %s not < %s\n", __FILE__, __LINE__, #a, #b); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

#define EXPECT_NEAR(a, b, eps)                                      \
    do {                                                            \
        auto _a = (a); auto _b = (b);                               \
        if (std::fabs(_a - _b) > (eps)) {                           \
            printf("FAIL\n    %s:%d: |%s - %s| > %s\n", __FILE__, __LINE__, #a, #b, #eps); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

#define EXPECT_NULL(ptr)    EXPECT_TRUE((ptr) == nullptr)
#define EXPECT_NOT_NULL(ptr) EXPECT_TRUE((ptr) != nullptr)

#define EXPECT_STR_EQ(a, b)                                         \
    do {                                                            \
        if (strcmp((a), (b)) != 0) {                                \
            printf("FAIL\n    %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

#define EXPECT_MEM_EQ(a, b, n)                                      \
    do {                                                            \
        if (memcmp((a), (b), (n)) != 0) {                           \
            printf("FAIL\n    %s:%d: memory mismatch (%zu bytes)\n", __FILE__, __LINE__, (size_t)(n)); \
            g_tests_failed++;                                       \
            return;                                                 \
        }                                                           \
    } while (0)

/* Place PASS at end of each test body that reaches the end without failure */
#define PASS()                                                      \
    do {                                                            \
        g_tests_passed++;                                           \
        printf("OK\n");                                             \
    } while (0)

static inline int test_main_result() {
    printf("\n%d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf("\n");
    return g_tests_failed > 0 ? 1 : 0;
}

/* Use as: int main() { return RUN_TESTS(); } */
#define RUN_TESTS() test_main_result()

#endif // RENDER_TEST_FRAMEWORK_H
