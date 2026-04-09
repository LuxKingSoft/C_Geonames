/**
 * @file advanced_features.c
 * @brief Example: Advanced features — field filtering, progress callbacks,
 *        error handling, multiple sources, and memory loading.
 *
 * Demonstrates:
 *   1. Download progress callback with animated feedback
 *   2. Extended search with field filtering (save CPU & memory)
 *   3. Loading multiple sources and combining data
 *   4. Loading data from an in-memory TSV buffer
 *   5. Comprehensive error handling and recovery
 *   6. Levenshtein distance utility
 *
 * Compile:
 *   gcc -o advanced_features advanced_features.c \
 *       -I ../include -L ../build/libs -lgeonames -lcurl -lm
 *
 * Run:
 *   ./advanced_features
 */

#include <stdio.h>
#include <string.h>
#include "cgeonames.h"

/* ========================================================================
 *  1. Progress callback — shows download percentage
 * ======================================================================== */

/**
 * @brief Simple progress callback for download feedback.
 */
static void progress_cb(long downloaded, long total, void* user_data) {
    (void)user_data;

    if (total <= 0) {
        printf("\r  Downloaded %ld bytes (size unknown)...      ", downloaded);
        fflush(stdout);
        return;
    }

    int pct = (int)((double)downloaded / (double)total * 100.0);
    int bars = pct / 2;  /* 50-char wide bar */

    printf("\r  [");
    for (int i = 0; i < 50; i++) {
        putchar(i < bars ? '=' : (i == bars ? '>' : ' '));
    }
    printf("] %3d%%  (%.1f / %.1f MB)",
           pct,
           (double)downloaded / (1024.0 * 1024.0),
           (double)total / (1024.0 * 1024.0));
    fflush(stdout);
}

/* ========================================================================
 *  2. Helper to print search results with only requested fields
 * ======================================================================== */

static void print_result_compact(const GeoName* g, int rank, uint32_t fields) {
    printf("  %d. ", rank);

    if (fields & GN_FIELD_NAME) {
        printf("%-20s", g->name);
    }

    if (fields & GN_FIELD_COUNTRY) {
        printf(" (%s)", g->country_code);
    }

    if (fields & GN_FIELD_COORDINATES) {
        printf("  %.4f, %.4f", g->latitude, g->longitude);
    }

    if (fields & GN_FIELD_TIMEZONE) {
        printf("  tz=%s", g->timezone);
    }

    if (fields & GN_FIELD_POPULATION) {
        printf("  pop=%d", g->population);
    }

    if (fields & GN_FIELD_ALIASES && g->alias_count > 0) {
        printf("  aliases=%d", g->alias_count);
    }

    if (fields & GN_FIELD_RELEVANCE) {
        printf("  score=%.0f", g->relevance);
    }

    printf("\n");
}

/* ========================================================================
 *  Main
 * ======================================================================== */

int main(void) {
    printf("=== cgeonames Advanced Features Example ===\n");
    printf("Library version: %s\n\n", gn_version());

    gn_engine_t* eng = gn_engine_create();
    if (!eng) {
        fprintf(stderr, "ERROR: Failed to create engine\n");
        return 1;
    }

    /* ---------------------------------------------------------------
     * A. Download with progress callback
     *    Load Cuba's data (small file, good for demonstration)
     * --------------------------------------------------------------- */
    printf("--- Downloading Cuba city data with progress bar ---\n");

    int ret = gn_engine_download_and_load(
        eng,
        GN_SRC_COUNTRY,
        "CU",             /* Cuba */
        NULL,
        progress_cb,      /* progress callback */
        NULL,             /* user data (optional) */
        0
    );
    printf("\n");  /* newline after progress bar */

    if (ret < 0) {
        fprintf(stderr, "ERROR: %s\n", gn_engine_last_error_str(eng));
        /* Don't exit — demonstrate error recovery below */
    } else {
        printf("Success! Loaded %d cities from Cuba.\n\n", gn_engine_count(eng));
    }

    /* ---------------------------------------------------------------
     * B. Extended search with field filtering (GN_FIELD_BASIC)
     *    Only populates: name, coords, timezone, population
     * --------------------------------------------------------------- */
    printf("--- Search: \"Havana\" (field-filtered: BASIC) ---\n");

    gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
    opts.limit  = 5;
    opts.fields = GN_FIELD_BASIC;  /* name + coords + tz + pop */

    int n = gn_search_ex(eng, "Havana", &opts);
    printf("Found %d result(s):\n\n", n);

    for (int i = 0; i < n; i++) {
        print_result_compact(gn_result_at(eng, i), i + 1, opts.fields);
    }
    printf("\n");

    /* ---------------------------------------------------------------
     * C. Names-only search — useful for autocomplete/typeahead
     *    Only populates: name, asciiname, aliases
     * --------------------------------------------------------------- */
    printf("--- Search: \"Santiago\" (names only, for autocomplete) ---\n");

    opts.fields = GN_FIELD_NAMES_ONLY;
    opts.limit  = 8;

    n = gn_search_ex(eng, "Santiago", &opts);
    printf("Found %d result(s):\n\n", n);

    for (int i = 0; i < n; i++) {
        print_result_compact(gn_result_at(eng, i), i + 1, opts.fields);
    }
    printf("\n");

    /* ---------------------------------------------------------------
     * D. Load from memory — simulate a custom TSV buffer
     *    Useful when you have your own data or a curated subset
     * --------------------------------------------------------------- */
    printf("--- Loading custom data from memory ---\n");

    /* Mini TSV with 3 custom records (tab-separated, 19 columns each) */
    const char* custom_tsv =
        "9990001\tCustomCity\tCustomCity\tCiudad Personalizada,City\t"
        "23.5000\t-82.5000\tP\tPPL\tCU\t\t01\t\t\t\t"
        "50000\t100\t100\tAmerica/Havana\t2024-01-01\n"
        "9990002\tAnotherTown\tAnotherTown\tOtro Pueblo,Town\t"
        "23.1000\t-82.3000\tP\tPPL\tCU\t\t01\t\t\t\t"
        "25000\t80\t80\tAmerica/Havana\t2024-01-01\n"
        "9990003\tVillageX\tVillageX\tAldea X\t"
        "22.8000\t-81.9000\tP\tPPL\tCU\t\t01\t\t\t\t"
        "5000\t50\t50\tAmerica/Havana\t2024-01-01\n";

    int custom_len = (int)strlen(custom_tsv);
    int before = gn_engine_count(eng);

    ret = gn_engine_load_memory(eng, custom_tsv, custom_len);
    if (ret == 0) {
        int loaded = gn_engine_count(eng) - before;
        printf("Loaded %d custom records from memory.\n", loaded);

        /* Search for custom data */
        opts.fields = GN_FIELD_ALL;
        opts.limit  = 10;
        n = gn_search_ex(eng, "CustomCity", &opts);
        if (n > 0) {
            const GeoName* g = gn_result_at(eng, 0);
            printf("  Found: %s (pop=%d, aliases=%d)\n",
                   g->name, g->population, g->alias_count);
        }
    } else {
        printf("  Failed to load from memory (code=%d).\n", ret);
    }
    printf("\n");

    /* ---------------------------------------------------------------
     * E. Comprehensive error handling — try loading an invalid country
     * --------------------------------------------------------------- */
    printf("--- Error handling demo: invalid country code \"XX\" ---\n");

    ret = gn_engine_download_and_load(
        eng, GN_SRC_COUNTRY, "XX", NULL, NULL, NULL, 0
    );

    if (ret < 0) {
        gn_error_info_t info;
        gn_engine_error_info(eng, &info);

        printf("  Error caught successfully!\n");
        printf("  Code:       %d\n", info.code);
        printf("  Message:    %s\n", info.message);
        printf("  URL:        %s\n", info.url);
        printf("  HTTP code:  %ld\n", info.http_code);
        printf("  cURL code:  %d\n", info.curl_code);
        printf("\n  Engine is still usable — %d cities loaded.\n\n",
               gn_engine_count(eng));
    }

    /* ---------------------------------------------------------------
     * F. Levenshtein distance utility — show edit distances
     * --------------------------------------------------------------- */
    printf("--- Levenshtein distance examples ---\n");

    const char* pairs[][2] = {
        {"Tokyo", "Tokio"},
        {"Havana", "Habana"},
        {"New York", "Nueva York"},
        {"Beijing", "Peking"},
        {"Mumbai", "Bombay"}
    };

    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        int dist = gn_levenshtein(pairs[i][0], pairs[i][1]);
        printf("  \"%s\" <-> \"%s\"  =>  distance = %d\n",
               pairs[i][0], pairs[i][1], dist);
    }
    printf("\n");

    /* ---------------------------------------------------------------
     * G. Source URL / filename helpers
     * --------------------------------------------------------------- */
    printf("--- Source URL/filename helpers ---\n");

    const char* url = gn_source_url(GN_SRC_COUNTRY, "JP");
    const char* fn  = gn_source_filename(GN_SRC_COUNTRY, "JP");
    printf("  Japan: URL=%s\n", url ? url : "(null)");
    printf("         File=%s\n", fn ? fn : "(null)");

    url = gn_source_url(GN_SRC_CITIES1000, NULL);
    fn  = gn_source_filename(GN_SRC_CITIES1000, NULL);
    printf("  Cities1000: URL=%s\n", url ? url : "(null)");
    printf("              File=%s\n", fn ? fn : "(null)");
    printf("\n");

    /* ---------------------------------------------------------------
     * H. Summary
     * --------------------------------------------------------------- */
    printf("--- Summary ---\n");
    printf("  Total cities loaded: %d\n", gn_engine_count(eng));
    printf("  Data directory:      %s\n", gn_engine_get_data_dir(eng));
    printf("  Has Cuba data:       %s\n",
           gn_engine_has_source(eng, GN_SRC_COUNTRY, "CU") ? "yes" : "no");
    printf("\n");

    /* ---------------------------------------------------------------
     * I. Clean up
     * --------------------------------------------------------------- */
    gn_engine_free(eng);

    printf("Done. All cached files remain in ~/.geonames/ for reuse.\n");
    return 0;
}
