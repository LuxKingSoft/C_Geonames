/**
 * @file basic_search.c
 * @brief Example: Basic city search with fuzzy matching.
 *
 * Demonstrates the core workflow:
 *   1. Create the engine
 *   2. Download & load city data (Japan, cities >= 1000 pop)
 *   3. Search for "Tokio" (fuzzy matches "Tokyo")
 *   4. Print results with relevance scores
 *   5. Clean up
 *
 * Compile:
 *   gcc -o basic_search basic_search.c \
 *       -I ../include -L ../build/libs -lgeonames -lcurl -lm
 *
 * Run:
 *   ./basic_search
 */

#include <stdio.h>
#include "cgeonames.h"

int main(void) {
    printf("=== cgeonames Basic Search Example ===\n");
    printf("Library version: %s\n\n", gn_version());

    /* ---------------------------------------------------------------
     * 1. Create the search engine
     * --------------------------------------------------------------- */
    gn_engine_t* eng = gn_engine_create();
    if (!eng) {
        fprintf(stderr, "ERROR: Failed to create engine (out of memory)\n");
        return 1;
    }

    /* ---------------------------------------------------------------
     * 2. Download & load Japan's city data
     *    - GN_SRC_COUNTRY means "per-country file"
     *    - "JP" is the ISO 3166-1 alpha-2 code for Japan
     *    - NULL local_dir uses the default ~/.geonames/ cache
     * --------------------------------------------------------------- */
    printf("Downloading Japan city data (cities >= 1000 pop)...\n");
    int ret = gn_engine_download_and_load(
        eng,              /* engine handle           */
        GN_SRC_COUNTRY,   /* source type             */
        "JP",             /* country code (Japan)    */
        NULL,             /* use default cache dir   */
        NULL,             /* no progress callback    */
        NULL,             /* no progress context     */
        0                 /* don't force re-download */
    );

    if (ret < 0) {
        /* Detailed error reporting */
        gn_error_info_t info;
        gn_engine_error_info(eng, &info);
        fprintf(stderr, "ERROR: Failed to load data\n");
        fprintf(stderr, "  Code:  %d\n", info.code);
        fprintf(stderr, "  Msg:   %s\n", info.message);
        fprintf(stderr, "  URL:   %s\n", info.url);
        fprintf(stderr, "  HTTP:  %ld\n", info.http_code);
        gn_engine_free(eng);
        return 1;
    }

    printf("Loaded %d cities.\n\n", gn_engine_count(eng));

    /* ---------------------------------------------------------------
     * 3. Search for "Tokio" (intentionally misspelled)
     *    The fuzzy search will match "Tokyo" via Levenshtein distance
     * --------------------------------------------------------------- */
    printf("--- Search: \"Tokio\" (fuzzy match) ---\n");
    int n = gn_search(eng, "Tokio", NULL);
    printf("Found %d result(s):\n\n", n);

    for (int i = 0; i < n; i++) {
        const GeoName* g = gn_result_at(eng, i);
        printf("  %-20s  lat=%8.4f  lon=%9.4f  tz=%-20s  pop=%-8d  score=%.0f\n",
               g->name, g->latitude, g->longitude,
               g->timezone, g->population, g->relevance);
    }

    /* ---------------------------------------------------------------
     * 4. Search with options: limit, min population, exact first
     * --------------------------------------------------------------- */
    printf("\n--- Search: \"osaka\" (limit=5, min_pop=100000, exact first) ---\n");

    gn_search_opts_t opts = {
        .limit          = 5,
        .country_code   = "JP",
        .min_population = 100000,
        .exact_first    = 1
    };

    n = gn_search(eng, "osaka", &opts);
    printf("Found %d result(s):\n\n", n);

    for (int i = 0; i < n; i++) {
        const GeoName* g = gn_result_at(eng, i);
        printf("  %-20s  lat=%8.4f  lon=%9.4f  tz=%-20s  pop=%-8d  score=%.0f\n",
               g->name, g->latitude, g->longitude,
               g->timezone, g->population, g->relevance);
    }

    /* ---------------------------------------------------------------
     * 5. Clean up
     * --------------------------------------------------------------- */
    gn_engine_free(eng);

    printf("\nDone. Data cached in ~/.geonames/ — next load is instant.\n");
    return 0;
}
