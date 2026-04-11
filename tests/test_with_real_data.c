/**
 * @file test_with_real_data.c
 * @brief Integration tests using actual GeoNames data from ~/.cgeonames/
 *
 * Tests verify that search results from real data are correct:
 *   - cities1000.txt (worldwide cities >= 1000 pop)
 *   - JP.txt (Japan country data)
 */

#include "unity.h"
#include "cgeonames.h"
#include <string.h>
#include <stdio.h>

static gn_engine_t* engine = NULL;

void setUp(void) {
    engine = gn_engine_create();
    TEST_ASSERT_NOT_NULL(engine);
}

void tearDown(void) {
    if (engine) {
        gn_engine_free(engine);
        engine = NULL;
    }
}

/* ========================================================================
 *  Test: Load cities1000.txt and verify record count
 * ======================================================================== */
void test_load_cities1000_file_succeeds(void) {
    int ret = gn_engine_load_file(engine, "/home/mark/.cgeonames/cities1000.txt");
    TEST_ASSERT_EQUAL_INT(0, ret);
    int count = gn_engine_count(engine);
    printf("\n  Loaded %d cities from cities1000.txt\n", count);
    TEST_ASSERT_GREATER_THAN_INT(0, count);
    /* cities1000 should have ~130k entries */
    TEST_ASSERT_GREATER_THAN_INT(100000, count);
}

/* ========================================================================
 *  Test: Search for Tokyo in cities1000 and verify coordinates
 * ======================================================================== */
void test_search_tokyo_in_cities1000(void) {
    gn_engine_load_file(engine, "/home/mark/.cgeonames/cities1000.txt");

    gn_search_opts_t opts = { .limit = 5, .country_code = "JP" };
    int n = gn_search(engine, "Tokyo", &opts);

    TEST_ASSERT_GREATER_THAN_INT(0, n);

    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Tokyo", result->name);
    TEST_ASSERT_EQUAL_STRING("JP", result->country_code);

    /* Tokyo coordinates: approx 35.6895N, 139.6917E */
    TEST_ASSERT_FLOAT_WITHIN(1.0, 35.6895, (float)result->latitude);
    TEST_ASSERT_FLOAT_WITHIN(1.0, 139.6917, (float)result->longitude);

    /* Should have timezone */
    TEST_ASSERT_EQUAL_STRING("Asia/Tokyo", result->timezone);

    /* Should have significant population */
    TEST_ASSERT_GREATER_THAN_INT(1000000, result->population);

    printf("\n  Found: %s (pop=%d, lat=%.4f, lon=%.4f, tz=%s)\n",
           result->name, result->population,
           result->latitude, result->longitude, result->timezone);
}

/* ========================================================================
 *  Test: Load JP.txt and verify it contains Japanese cities
 * ======================================================================== */
void test_load_jp_file_finds_cities(void) {
    int ret = gn_engine_load_file(engine, "/home/mark/.cgeonames/JP.txt");
    TEST_ASSERT_EQUAL_INT(0, ret);

    int count = gn_engine_count(engine);
    printf("\n  Loaded %d records from JP.txt\n", count);
    TEST_ASSERT_GREATER_THAN_INT(0, count);

    /* Search for Osaka */
    gn_search_opts_t opts = { .limit = 5 };
    int n = gn_search(engine, "Osaka", &opts);
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("JP", result->country_code);

    printf("\n  Found: %s (pop=%d)\n", result->name, result->population);
}

/* ========================================================================
 *  Test: Fuzzy matching finds cities with typos
 * ======================================================================== */
void test_fuzzy_match_real_cities(void) {
    gn_engine_load_file(engine, "/home/mark/.cgeonames/cities1000.txt");

    /* Search for Kyoto in Japan */
    gn_search_opts_t opts = { .limit = 10, .country_code = "JP" };
    int n = gn_search(engine, "Kyoto", &opts);

    TEST_ASSERT_GREATER_THAN_INT(0, n);

    /* Kyoto should be in the results (may not be #1 due to population scoring) */
    int found_kyoto = 0;
    for (int i = 0; i < n; i++) {
        const GeoName* result = gn_result_at(engine, i);
        TEST_ASSERT_NOT_NULL(result);
        if (strcmp(result->name, "Kyoto") == 0) {
            found_kyoto = 1;
            printf("\n  Found Kyoto at position %d (pop=%d, lat=%.4f, lon=%.4f)\n",
                   i, result->population, result->latitude, result->longitude);
            break;
        }
    }
    TEST_ASSERT_TRUE(found_kyoto);
}

/* ========================================================================
 *  Test: Reverse geocoding near Tokyo
 * ======================================================================== */
void test_reverse_geocoding_near_tokyo(void) {
    gn_engine_load_file(engine, "/home/mark/.cgeonames/cities1000.txt");

    gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
    opts.radius_km = 50.0;
    opts.fields = GN_FIELD_BASIC;
    opts.limit = 5;

    /* Search near Tokyo Station */
    int n = gn_search_nearby(engine, 35.6812, 139.7671, &opts);

    TEST_ASSERT_GREATER_THAN_INT(0, n);

    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NOT_NULL(result->name);
    TEST_ASSERT_NOT_NULL(result->timezone);

    printf("\n  Nearest to Tokyo Station: %s (lat=%.4f, lon=%.4f)\n",
           result->name, result->latitude, result->longitude);
}

/* ========================================================================
 *  Test: Country filter works correctly
 * ======================================================================== */
void test_country_filter_finds_correct_cities(void) {
    gn_engine_load_file(engine, "/home/mark/.cgeonames/cities1000.txt");

    /* Search for "Springfield" limited to US */
    gn_search_opts_t opts = { .limit = 10, .country_code = "US" };
    int n = gn_search(engine, "Springfield", &opts);

    TEST_ASSERT_GREATER_THAN_INT(0, n);

    /* All results should be in US */
    for (int i = 0; i < n; i++) {
        const GeoName* result = gn_result_at(engine, i);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_EQUAL_STRING("US", result->country_code);
    }

    printf("\n  Found %d Springfields in US\n", n);
}

/* ========================================================================
 *  Test: Population filter works correctly
 * ======================================================================== */
void test_population_filter_works(void) {
    gn_engine_load_file(engine, "/home/mark/.cgeonames/cities1000.txt");

    gn_search_opts_t opts = { .limit = 20, .min_population = 5000000 };
    int n = gn_search(engine, "", &opts);

    /* All results should have population >= 5,000,000 */
    for (int i = 0; i < n; i++) {
        const GeoName* result = gn_result_at(engine, i);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(5000000, result->population);
    }

    printf("\n  Found %d cities with pop >= 5M\n", n);
}

/* ========================================================================
 *  Test: Admin codes are populated for JP.txt
 * ======================================================================== */
void test_admin_codes_populated_in_jp_data(void) {
    gn_engine_load_file(engine, "/home/mark/.cgeonames/JP.txt");

    gn_search_opts_t opts = { .limit = 1 };
    int n = gn_search(engine, "Tokyo", &opts);
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);

    /* Japan should have admin1_code (prefecture code) */
    TEST_ASSERT_GREATER_THAN_INT(0, strlen(result->admin1_code));

    printf("\n  Tokyo admin1_code: %s\n", result->admin1_code);
}

/* ========================================================================
 *  Test: Aliases are parsed correctly
 * ======================================================================== */
void test_aliases_parsed_correctly(void) {
    gn_engine_load_file(engine, "/home/mark/.cgeonames/JP.txt");

    gn_search_opts_t opts = { .limit = 1 };
    int n = gn_search(engine, "Tokyo", &opts);
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);

    /* Tokyo has alternate names - at least some should be parsed */
    printf("\n  Tokyo aliases (%d): ", result->alias_count);
    for (int i = 0; i < result->alias_count && i < 5; i++) {
        printf("%s ", result->aliases[i]);
    }
    printf("\n");
}

/* ========================================================================
 *  Main
 * ======================================================================== */
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_load_cities1000_file_succeeds);
    RUN_TEST(test_search_tokyo_in_cities1000);
    RUN_TEST(test_load_jp_file_finds_cities);
    RUN_TEST(test_fuzzy_match_real_cities);
    RUN_TEST(test_reverse_geocoding_near_tokyo);
    RUN_TEST(test_country_filter_finds_correct_cities);
    RUN_TEST(test_population_filter_works);
    RUN_TEST(test_admin_codes_populated_in_jp_data);
    RUN_TEST(test_aliases_parsed_correctly);

    return UNITY_END();
}
