/* Standalone C regressions with deterministic allocation and work counters. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <fenv.h>
#include <float.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
#include <xlocale.h>
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static size_t allocation_calls, fail_at, live_allocations, comparisons;
static int fail_locale;

static void *test_malloc(size_t size)
{
    void *ptr;
    if (++allocation_calls == fail_at) {
        return NULL;
    }
    ptr = malloc(size);
    if (ptr != NULL) {
        live_allocations++;
    }
    return ptr;
}

static void test_free(void *ptr)
{
    if (ptr != NULL) {
        CHECK(live_allocations > 0);
        live_allocations--;
        free(ptr);
    }
}

static int test_strcmp(const char *left, const char *right)
{
    comparisons++;
    return strcmp(left, right);
}

#ifdef _WIN32
static _locale_t test_create_locale(int category, const char *name)
{
    return fail_locale ? NULL : _create_locale(category, name);
}
#define _create_locale test_create_locale
#else
static locale_t test_newlocale(int mask, const char *name, locale_t base)
{
    return fail_locale ? (locale_t) 0 : newlocale(mask, name, base);
}
#define newlocale test_newlocale
#endif

#define LP_MALLOC test_malloc
#define LP_FREE test_free
#define strcmp test_strcmp
#include "../src/line_protocol_parser.c"
#undef strcmp

static void test_allocations(void)
{
    const char *lines[] = {
        "m,t=a,u=b f=1i,g=2i,h=\"hello\"",
        "m,t=a,u=b f=1.5,g=18446744073709551615u,h=true",
        "m f=1i,g=2i,h=\"hello\" 123",
        "m,t=a,u=b f=1i,g=\"bad",
        "m,t=a,u=b f=1i,g=2i invalid-time",
        "m,t=a,u=b,t=c f=1i",
        "m,t=a,u=b,v= f=1i",
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(*lines); i++) {
        int status = 0;
        struct LP_Point *point;
        size_t total;
        allocation_calls = 0;
        point = LP_parse_line(lines[i], &status);
        total = allocation_calls;
        LP_free_point(point);
        CHECK(live_allocations == 0);
        for (fail_at = 1; fail_at <= total; fail_at++) {
            allocation_calls = 0;
            status = 0;
            point = LP_parse_line(lines[i], &status);
            CHECK(point == NULL);
            CHECK(status == LP_MEMORY_ERROR || status == LP_SET_KEY_ERROR ||
                  status == LP_SET_VALUE_ERROR);
            CHECK(live_allocations == 0);
        }
        fail_at = 0;
    }
    /* Locale setup is a separate libc allocation, outside LP_MALLOC. */
    fail_locale = 1;
    int status = 0;
    struct LP_Point *point = LP_parse_line("m,t=a f=1i,g=1.5", &status);
    CHECK(point == NULL);
    CHECK(status == LP_MEMORY_ERROR);
    CHECK(live_allocations == 0);
    fail_locale = 0;
}

static void test_tags(void)
{
    const size_t counts[] = {6000, 32000};
    for (size_t c = 0; c < sizeof(counts) / sizeof(*counts); c++) {
        size_t count = counts[c], offset = 1;
        char *line = malloc(count * 9 + 16);
        CHECK(line != NULL);
        line[0] = 'm';
        for (size_t i = 0; i < count; i++) {
            offset += (size_t) sprintf(line + offset, ",t%05zu=v", i);
        }
        strcpy(line + offset, " f=1i");
        int status = 0;
        comparisons = 0;
        struct LP_Point *point = LP_parse_line(line, &status);
        if (count == 6000) {
            CHECK(point != NULL);
            size_t parsed_count = 0;
            for (struct LP_Item *item = point->tags; item; item = item->next_item) {
                parsed_count++;
            }
            CHECK(parsed_count == count);
        } else {
            CHECK(point == NULL);
            CHECK(status == LP_KEY_TOO_LONG_ERROR);
        }
        /* A broad operation budget catches quadratic duplicate searches
           without relying on machine speed or a particular sorting algorithm. */
        CHECK(comparisons < count * 64);
        LP_free_point(point);
        CHECK(live_allocations == 0);
        free(line);
    }
}

static void test_locale(const char *name)
{
    CHECK(setlocale(LC_NUMERIC, name) != NULL);
    CHECK(strcmp(localeconv()->decimal_point, ",") == 0);
    const char *current = setlocale(LC_NUMERIC, NULL);
    char *saved = malloc(strlen(current) + 1);
    CHECK(saved != NULL);
    strcpy(saved, current);
    int status = 0;
    struct LP_Point *point = LP_parse_line("m f=1.5,g=5e-324", &status);
    CHECK(point != NULL);
    CHECK(point->fields->value.f == 5e-324);
    CHECK(point->fields->next_item->value.f == 1.5);
    CHECK(strcmp(setlocale(LC_NUMERIC, NULL), saved) == 0);
    CHECK(strcmp(localeconv()->decimal_point, ",") == 0);
    LP_free_point(point);
    CHECK(live_allocations == 0);
    free(saved);
}

static void test_rounding(int negative)
{
    const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    const char *overflow[] = {"1e400", "1.8e308"};
    const char *subnormal[] = {"5e-324", "1e-310", "2.2250738585072012e-308"};
    int original_mode = fegetround();
    char line[80];
    CHECK(original_mode != -1);

    for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); i++) {
        CHECK(fesetround(modes[i]) == 0);
        for (size_t j = 0; j < sizeof(overflow) / sizeof(*overflow); j++) {
            int status = 0;
            snprintf(line, sizeof(line), "m f=%s%s", negative ? "-" : "", overflow[j]);
            struct LP_Point *point = LP_parse_line(line, &status);
            CHECK(point == NULL);
            CHECK(status == LP_FIELD_VALUE_TYPE_ERROR);
            CHECK(live_allocations == 0);
        }
        for (size_t j = 0; j < sizeof(subnormal) / sizeof(*subnormal); j++) {
            int status = 0;
            snprintf(line, sizeof(line), "m f=%s%s", negative ? "-" : "", subnormal[j]);
            struct LP_Point *point = LP_parse_line(line, &status);
            CHECK(point != NULL);
            CHECK(point->fields->type == LP_FLOAT);
            double value = point->fields->value.f;
            CHECK(fabs(value) > 0.0 && fabs(value) <= DBL_MIN);
            CHECK((value < 0.0) == negative);
            LP_free_point(point);
            CHECK(live_allocations == 0);
        }
        /* A previous conversion's ERANGE must not reject a normal value.
           Use an integer-valued float token: the Windows CRT can convert 1.5
           one ULP away from exact under directed rounding. */
        int status = 0;
        errno = ERANGE;
        struct LP_Point *point = LP_parse_line(negative ? "m f=-1" : "m f=1", &status);
        CHECK(point != NULL);
        CHECK(point->fields->type == LP_FLOAT);
        if (point->fields->value.f != (negative ? -1.0 : 1.0)) {
            fprintf(stderr, "rounding=%d negative=%d value=%a expected=%a\n",
                    modes[i], negative, point->fields->value.f, negative ? -1.0 : 1.0);
        }
        CHECK(point->fields->value.f == (negative ? -1.0 : 1.0));
        LP_free_point(point);
        CHECK(live_allocations == 0);
        CHECK(fegetround() == modes[i]);
    }
    CHECK(fesetround(original_mode) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc >= 2);
    if (strcmp(argv[1], "allocations") == 0) {
        test_allocations();
    } else if (strcmp(argv[1], "tags") == 0) {
        test_tags();
    } else if (strcmp(argv[1], "locale") == 0) {
        CHECK(argc == 3);
        test_locale(argv[2]);
    } else if (strcmp(argv[1], "rounding-positive") == 0) {
        test_rounding(0);
    } else if (strcmp(argv[1], "rounding-negative") == 0) {
        test_rounding(1);
    } else {
        return 1;
    }
    return 0;
}
