/**
 * @file test_engine.c
 * @brief Unit tests for cgeonames engine lifecycle and core functions.
 *
 * Tests use the Unity Test Framework (C-native, lightweight, no C++ required).
 * Run with: ctest --output-on-failure
 */

#include "unity.h"
#include "cgeonames.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 *  Test fixtures
 * ======================================================================== */

static gn_engine_t* engine = NULL;

/* ========================================================================
 *  Setup / Teardown
 * ======================================================================== */

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
 *  Engine Lifecycle Tests
 * ======================================================================== */

void test_engine_create_returns_valid_handle(void) {
    TEST_ASSERT_NOT_NULL(engine);
}

void test_engine_count_starts_at_zero(void) {
    int count = gn_engine_count(engine);
    TEST_ASSERT_EQUAL_INT(0, count);
}

void test_engine_free_handles_null_safely(void) {
    /* Should not crash */
    gn_engine_free(NULL);
}

void test_engine_get_data_dir_returns_path(void) {
    const char* dir = gn_engine_get_data_dir(engine);
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_GREATER_THAN_INT(0, strlen(dir));
}

/* ========================================================================
 *  Error Handling Tests
 * ======================================================================== */

void test_engine_last_error_starts_at_none(void) {
    gn_error_t err = gn_engine_last_error(engine);
    TEST_ASSERT_EQUAL_INT(GN_ERR_NONE, err);
}

void test_engine_error_info_with_null_params(void) {
    gn_error_info_t info;
    int ret = gn_engine_error_info(NULL, &info);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = gn_engine_error_info(engine, NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_invalid_country_code_returns_error(void) {
    /* Invalid country code (should fail validation) */
    int ret = gn_engine_download_and_load(
        engine, GN_SRC_COUNTRY, "INVALID_CODE_TOO_LONG",
        NULL, NULL, NULL, 0
    );
    TEST_ASSERT_LESS_THAN_INT(0, ret);
}

void test_invalid_source_returns_error(void) {
    int ret = gn_engine_download_and_load(
        engine, GN_SRC_UNKNOWN, NULL,
        NULL, NULL, NULL, 0
    );
    TEST_ASSERT_LESS_THAN_INT(0, ret);
}

void test_null_engine_returns_error(void) {
    int ret = gn_engine_download_and_load(
        NULL, GN_SRC_COUNTRY, "CU",
        NULL, NULL, NULL, 0
    );
    TEST_ASSERT_LESS_THAN_INT(0, ret);
}

/* ========================================================================
 *  Search Tests (with in-memory data)
 * ======================================================================== */

void test_search_with_null_engine_returns_zero(void) {
    int n = gn_search(NULL, "Havana", NULL);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_search_with_empty_engine_returns_zero(void) {
    gn_search_opts_t opts = { .limit = 10 };
    int n = gn_search(engine, "Havana", &opts);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_result_at_with_invalid_index(void) {
    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NULL(result);

    result = gn_result_at(engine, -1);
    TEST_ASSERT_NULL(result);

    result = gn_result_at(engine, 999);
    TEST_ASSERT_NULL(result);
}

/* ========================================================================
 *  Memory Loading Tests
 * ======================================================================== */

void test_load_memory_with_null_engine_fails(void) {
    const char* data = "1234567\tTest\tTest\t\t40.0\t-4.0\tP\tPPL\tES\t\t53\t\t\t\t1000\t100\t100\tEurope/Madrid\t2024-01-01\n";
    int ret = gn_engine_load_memory(NULL, data, strlen(data));
    TEST_ASSERT_LESS_THAN_INT(0, ret);
}

void test_load_memory_with_invalid_data_fails(void) {
    const char* data = "invalid\tdata\n";
    int ret = gn_engine_load_memory(engine, data, strlen(data));
    TEST_ASSERT_LESS_THAN_INT(0, ret);
}

void test_load_memory_with_valid_tsv_succeeds(void) {
    /* Minimal valid TSV: 19 tab-separated columns matching GeoNames format */
    const char* data = 
        "1234567\tTestCity\tTestCity\tAlias1,Alias2\t40.4168\t-3.7038\tP\tPPL\tES\t\t53\t\t\t\t1000000\t100\t100\tEurope/Madrid\t2024-01-01\n"
        "7654321\tAnotherCity\tAnotherCity\t\t37.7749\t-122.4194\tP\tPPL\tUS\t\tCA\t\t\t\t800000\t50\t50\tAmerica/Los_Angeles\t2024-01-02\n";
    
    int ret = gn_engine_load_memory(engine, data, strlen(data));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(2, gn_engine_count(engine));
}

void test_search_after_load_memory_finds_city(void) {
    const char* data = 
        "1234567\tMadrid\tMadrid\tMadrit,Matrit\t40.4168\t-3.7038\tP\tPPL\tES\t\t53\t\t\t\t3223000\t100\t100\tEurope/Madrid\t2024-01-01\n"
        "7654321\tBarcelona\tBarcelona\tBarna\t41.3851\t2.1734\tP\tPPL\tES\t\tCT\t\t\t\t1620000\t50\t50\tEurope/Madrid\t2024-01-02\n";
    
    gn_engine_load_memory(engine, data, strlen(data));
    
    gn_search_opts_t opts = { .limit = 10 };
    int n = gn_search(engine, "Madrid", &opts);
    
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    TEST_ASSERT_LESS_OR_EQUAL_INT(10, n);
    
    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Madrid", result->name);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0, result->relevance);
}

/* ========================================================================
 *  Extended Search Tests
 * ======================================================================== */

void test_search_ex_with_null_engine_returns_zero(void) {
    gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
    int n = gn_search_ex(NULL, "Havana", &opts);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_search_ex_field_filtering(void) {
    const char* data = 
        "1234567\tTokyo\tTokyo\tTōkyō\t35.6895\t139.6917\tP\tPPLC\tJP\t\t40\t\t\t\t13960000\t40\t40\tAsia/Tokyo\t2024-01-01\n";
    
    gn_engine_load_memory(engine, data, strlen(data));
    
    gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
    opts.fields = GN_FIELD_NAME | GN_FIELD_COORDINATES | GN_FIELD_TIMEZONE;
    opts.limit = 5;
    
    int n = gn_search_ex(engine, "Tokyo", &opts);
    TEST_ASSERT_EQUAL_INT(1, n);
    
    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Tokyo", result->name);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 35.6895, result->latitude);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 139.6917, result->longitude);
    TEST_ASSERT_EQUAL_STRING("Asia/Tokyo", result->timezone);
}

/* ========================================================================
 *  Reverse Geocoding Tests
 * ======================================================================== */

void test_search_nearby_with_null_engine_returns_zero(void) {
    int n = gn_search_nearby(NULL, 40.4168, -3.7038, NULL);
    TEST_ASSERT_EQUAL_INT(0, n);
}

void test_search_nearby_finds_closest_city(void) {
    const char* data = 
        "1234567\tMadrid\tMadrid\t\t40.4168\t-3.7038\tP\tPPL\tES\t\t53\t\t\t\t3223000\t100\t100\tEurope/Madrid\t2024-01-01\n"
        "7654321\tBarcelona\tBarcelona\t\t41.3851\t2.1734\tP\tPPL\tES\t\tCT\t\t\t\t1620000\t50\t50\tEurope/Madrid\t2024-01-02\n";
    
    gn_engine_load_memory(engine, data, strlen(data));
    
    gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
    opts.radius_km = 100.0;
    opts.fields = GN_FIELD_BASIC;
    
    /* Search near Madrid coordinates */
    int n = gn_search_nearby(engine, 40.4200, -3.7100, &opts);
    
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    
    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Madrid", result->name);
}

/* ========================================================================
 *  Source Tracking Tests
 * ======================================================================== */

void test_engine_has_source_initially_false(void) {
    /* Engine starts empty, should not have any source loaded */
    int has = gn_engine_has_source(engine, GN_SRC_COUNTRY, "CU");
    TEST_ASSERT_EQUAL_INT(0, has);
    
    has = gn_engine_has_source(engine, GN_SRC_CITIES1000, NULL);
    TEST_ASSERT_EQUAL_INT(0, has);
}

/* ========================================================================
 *  Fuzzy Matching Tests
 * ======================================================================== */

void test_fuzzy_match_with_typo_finds_city(void) {
    const char* data = 
        "1234567\tHavana\tHavana\tLa Habana\t23.1136\t-82.3666\tP\tPPLC\tCU\t\t03\t\t\t\t2100000\t50\t50\tAmerica/Havana\t2024-01-01\n";
    
    gn_engine_load_memory(engine, data, strlen(data));
    
    gn_search_opts_t opts = { .limit = 10, .exact_first = 1 };
    
    /* Search with common typo */
    int n = gn_search(engine, "Habana", &opts);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    
    const GeoName* result = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Havana", result->name);
}

void test_exact_match_scores_higher_than_fuzzy(void) {
    const char* data = 
        "1234567\tParis\tParis\t\t48.8566\t2.3522\tP\tPPLC\tFR\t\t11\t\t\t\t2200000\t50\t50\tEurope/Paris\t2024-01-01\n"
        "7654321\tParrisville\tParrisville\t\t35.1234\t-85.6789\tP\tPPL\tUS\t\tTN\t\t\t\t1000\t100\t100\tAmerica/New_York\t2024-01-02\n";
    
    gn_engine_load_memory(engine, data, strlen(data));
    
    gn_search_opts_t opts = { .limit = 10 };
    int n = gn_search(engine, "Paris", &opts);
    
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, n);
    
    const GeoName* first = gn_result_at(engine, 0);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_STRING("Paris", first->name);
}

/* ========================================================================
 *  Filter Tests
 * ======================================================================== */

void test_country_code_filter(void) {
    const char* data = 
        "1234567\tSpringfield\tSpringfield\t\t39.7817\t-89.6501\tP\tPPL\tUS\t\tIL\t\t\t\t100000\t100\t100\tAmerica/Chicago\t2024-01-01\n"
        "7654321\tSpringfield\tSpringfield\t\t42.1015\t-72.5898\tP\tPPL\tUS\t\tMA\t\t\t\t150000\t50\t50\tAmerica/New_York\t2024-01-02\n";
    
    gn_engine_load_memory(engine, data, strlen(data));
    
    gn_search_opts_t opts = { .limit = 10, .country_code = "US" };
    int n = gn_search(engine, "Springfield", &opts);
    
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    TEST_ASSERT_LESS_OR_EQUAL_INT(10, n);
}

void test_min_population_filter(void) {
    const char* data = 
        "1234567\tBigCity\tBigCity\t\t40.0\t-4.0\tP\tPPL\tES\t\t\t\t\t\t500000\t100\t100\tEurope/Madrid\t2024-01-01\n"
        "7654321\tSmallTown\tSmallTown\t\t41.0\t-5.0\tP\tPPL\tES\t\t\t\t\t\t500\t50\t50\tEurope/Madrid\t2024-01-02\n";
    
    gn_engine_load_memory(engine, data, strlen(data));
    
    gn_search_opts_t opts = { .limit = 10, .min_population = 100000 };
    int n = gn_search(engine, "City", &opts);
    
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, n);
    
    /* Verify all results meet minimum population */
    for (int i = 0; i < n; i++) {
        const GeoName* result = gn_result_at(engine, i);
        TEST_ASSERT_NOT_NULL(result);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(100000, result->population);
    }
}

/* ========================================================================
 *  Main entry point for Unity test runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();
    
    /* Engine Lifecycle */
    RUN_TEST(test_engine_create_returns_valid_handle);
    RUN_TEST(test_engine_count_starts_at_zero);
    RUN_TEST(test_engine_free_handles_null_safely);
    RUN_TEST(test_engine_get_data_dir_returns_path);
    
    /* Error Handling */
    RUN_TEST(test_engine_last_error_starts_at_none);
    RUN_TEST(test_engine_error_info_with_null_params);
    RUN_TEST(test_invalid_country_code_returns_error);
    RUN_TEST(test_invalid_source_returns_error);
    RUN_TEST(test_null_engine_returns_error);
    
    /* Search Basics */
    RUN_TEST(test_search_with_null_engine_returns_zero);
    RUN_TEST(test_search_with_empty_engine_returns_zero);
    RUN_TEST(test_result_at_with_invalid_index);
    
    /* Memory Loading */
    RUN_TEST(test_load_memory_with_null_engine_fails);
    RUN_TEST(test_load_memory_with_invalid_data_fails);
    RUN_TEST(test_load_memory_with_valid_tsv_succeeds);
    RUN_TEST(test_search_after_load_memory_finds_city);
    
    /* Extended Search */
    RUN_TEST(test_search_ex_with_null_engine_returns_zero);
    RUN_TEST(test_search_ex_field_filtering);
    
    /* Reverse Geocoding */
    RUN_TEST(test_search_nearby_with_null_engine_returns_zero);
    RUN_TEST(test_search_nearby_finds_closest_city);
    
    /* Source Tracking */
    RUN_TEST(test_engine_has_source_initially_false);
    
    /* Fuzzy Matching */
    RUN_TEST(test_fuzzy_match_with_typo_finds_city);
    RUN_TEST(test_exact_match_scores_higher_than_fuzzy);
    
    /* Filters */
    RUN_TEST(test_country_code_filter);
    RUN_TEST(test_min_population_filter);
    
    return UNITY_END();
}
