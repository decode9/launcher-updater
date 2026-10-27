/*
 * testlib.h - Tiny test framework for the core (host build, with libc).
 *
 *     TEST(name) { CHECK(expr); CHECK_EQ_U64(a, b); CHECK_STR(a, b); }
 *     int main(void) { RUN(name); ...; return test_report(); }
 */
#ifndef LAUNCHER_TESTLIB_H
#define LAUNCHER_TESTLIB_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int test_failures, test_checks, test_count;

#define TEST(name) static void name(void)
#define RUN(name) (test_count++, name())

#define CHECK(expr)                                                                   \
    do {                                                                              \
        test_checks++;                                                                \
        if (!(expr)) {                                                                \
            test_failures++;                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr);  \
        }                                                                             \
    } while (0)

#define CHECK_EQ_U64(a, b)                                                                     \
    do {                                                                                       \
        uint64_t a_ = (uint64_t)(a), b_ = (uint64_t)(b);                                       \
        test_checks++;                                                                         \
        if (a_ != b_) {                                                                        \
            test_failures++;                                                                   \
            fprintf(stderr, "%s:%d: %s == %llu, expected %llu\n", __FILE__, __LINE__, #a,      \
                    (unsigned long long)a_, (unsigned long long)b_);                           \
        }                                                                                      \
    } while (0)

#define CHECK_STR(a, b)                                                                        \
    do {                                                                                       \
        const char *a_ = (a), *b_ = (b);                                                       \
        test_checks++;                                                                         \
        if (strcmp(a_, b_) != 0) {                                                             \
            test_failures++;                                                                   \
            fprintf(stderr, "%s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a,  \
                    a_, b_);                                                                   \
        }                                                                                      \
    } while (0)

static int test_report_named(const char *file)
{
    const char *slash = strrchr(file, '/');
    if (slash) file = slash + 1;
    printf("%-22s %3d tests %4d checks  %s\n", file, test_count, test_checks,
           test_failures ? "FAIL" : "ok");
    return test_failures ? 1 : 0;
}

#define test_report() test_report_named(__FILE__)

#endif
