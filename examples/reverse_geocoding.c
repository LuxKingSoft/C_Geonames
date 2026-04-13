/**
 * @file reverse_geocoding.c
 * @brief Example: Reverse geocoding — finding nearest cities from coordinates.
 *
 * Demonstrates:
 *   1. Loading multiple data sources (worldwide cities + detailed country data)
 *   2. Finding nearest cities to a lat/lon point
 *   3. Using feature class filters (populated places only)
 *   4. Getting location info (timezone, admin codes, country)
 *   5. Single-result convenience lookup
 *
 * Compile:
 *   gcc -o reverse_geocoding reverse_geocoding.c \
 *       -I ../include -L ../build/libs -lgeonames -lcurl -lm
 *
 * Run:
 *   ./reverse_geocoding
 */

#include <stdio.h>
#include <math.h>
#include "cgeonames.h"

/**
 * @brief Print a formatted GeoName result for reverse geocoding.
 */
static void print_nearest(const GeoName* g, int rank, double center_lat, double center_lon) {
    /* Calculate actual distance from search center using haversine. */
    double dlat = (g->latitude - center_lat) * M_PI / 180.0;
    double dlon = (g->longitude - center_lon) * M_PI / 180.0;
    double a = sin(dlat / 2.0) * sin(dlat / 2.0) +
               cos(center_lat * M_PI / 180.0) * cos(g->latitude * M_PI / 180.0) *
               sin(dlon / 2.0) * sin(dlon / 2.0);
    double dist = 2.0 * 6371.0 * atan2(sqrt(a), sqrt(1.0 - a));

    printf("  %d. %-20s (%s)  dist=%.1f km\n",
           rank, g->name, g->country_code, dist);
    printf("      Coords: %.4f, %.4f  |  TZ: %s\n",
           g->latitude, g->longitude, g->timezone);
    printf("      Admin: %s / %s / %s / %s\n",
           g->admin1_code, g->admin2_code,
           g->admin3_code, g->admin4_code);
    printf("      Feature: %c / %s  |  Pop: %d\n",
           g->feature_class, g->feature_code, g->population);
    printf("\n");
}

int main(void) {
    printf("=== cgeonames Reverse Geocoding Example ===\n");
    printf("Library version: %s\n\n", gn_version());

    /* ---------------------------------------------------------------
     * 1. Create engine & load data
     *    Load cities1000 worldwide for broad coverage
     * --------------------------------------------------------------- */
    gn_engine_t* eng = gn_engine_create();
    if (!eng) {
        fprintf(stderr, "ERROR: Failed to create engine\n");
        return 1;
    }

    printf("Loading worldwide cities (>= 1000 pop)...\n");
    int ret = gn_engine_download_and_load(
        eng, GN_SRC_CITIES1000, NULL, NULL, NULL, NULL, 0
    );
    if (ret < 0) {
        fprintf(stderr, "ERROR: %s\n", gn_engine_last_error_str(eng));
        gn_engine_free(eng);
        return 1;
    }
    printf("Loaded %d cities.\n\n", gn_engine_count(eng));

    /* ---------------------------------------------------------------
     * 2. Find 5 nearest cities to Tokyo, Japan (within 50 km)
     * --------------------------------------------------------------- */
    printf("--- Nearest cities to Tokyo, Japan (35.6895, 139.6917) ---\n");

    gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
    opts.limit      = 5;
    opts.radius_km  = 50.0;
    opts.feature_class_filter = 'P';  /* Populated places only */
    opts.fields     = GN_FIELD_LOCATION_INFO;  /* tz + admin + country + coords */

    int n = gn_search_nearby(eng, 35.6895, 139.6917, &opts);
    printf("Found %d city(ies) within %.0f km:\n\n", n, opts.radius_km);

    for (int i = 0; i < n; i++) {
        print_nearest(gn_result_at(eng, i), i + 1, 35.6895, 139.6917);
    }

    /* ---------------------------------------------------------------
     * 3. Quick single-result reverse geocode (convenience function)
     *    Find nearest populated place to the Eiffel Tower, Paris
     * --------------------------------------------------------------- */
    printf("--- Quick lookup: Near the Eiffel Tower, Paris (48.8584, 2.2945) ---\n");

    const GeoName* nearest = gn_reverse_geocode(eng, 48.8584, 2.2945, 25.0);

    if (nearest) {
        printf("  You're near: %s, %s\n", nearest->name, nearest->country_code);
        printf("  Timezone:    %s\n", nearest->timezone);
        printf("  Distance:    %.1f km\n", nearest->relevance);
    } else {
        printf("  No populated place found within 25 km.\n");
    }
    printf("\n");

    /* ---------------------------------------------------------------
     * 4. Search with a larger radius: cities near the equator in Brazil
     * --------------------------------------------------------------- */
    printf("--- Cities near Manaus, Brazil (-3.1190, -60.0217) within 100 km ---\n");

    opts.limit     = 10;
    opts.radius_km = 100.0;
    opts.fields    = GN_FIELD_BASIC;  /* name + coords + tz + pop */

    n = gn_search_nearby(eng, -3.1190, -60.0217, &opts);
    printf("Found %d city(ies):\n\n", n);

    for (int i = 0; i < n; i++) {
        const GeoName* g = gn_result_at(eng, i);
        /* Calculate actual distance from search center. */
        double dlat = (g->latitude - (-3.1190)) * M_PI / 180.0;
        double dlon = (g->longitude - (-60.0217)) * M_PI / 180.0;
        double a = sin(dlat / 2.0) * sin(dlat / 2.0) +
                   cos((-3.1190) * M_PI / 180.0) * cos(g->latitude * M_PI / 180.0) *
                   sin(dlon / 2.0) * sin(dlon / 2.0);
        double dist = 2.0 * 6371.0 * atan2(sqrt(a), sqrt(1.0 - a));
        printf("  %d. %-20s  dist=%5.1f km  pop=%-8d  tz=%s\n",
               i + 1, g->name, dist, g->population, g->timezone);
    }
    printf("\n");

    /* ---------------------------------------------------------------
     * 5. Check which sources are loaded
     * --------------------------------------------------------------- */
    printf("--- Loaded sources ---\n");
    printf("  Has cities1000:   %s\n",
           gn_engine_has_source(eng, GN_SRC_CITIES1000, NULL) ? "yes" : "no");
    printf("  Has Japan:        %s\n",
           gn_engine_has_source(eng, GN_SRC_COUNTRY, "JP") ? "yes" : "no");
    printf("  Has Brazil:       %s\n",
           gn_engine_has_source(eng, GN_SRC_COUNTRY, "BR") ? "yes" : "no");
    printf("\n");

    /* ---------------------------------------------------------------
     * 6. Clean up
     * --------------------------------------------------------------- */
    gn_engine_free(eng);

    printf("Done. Files cached in ~/.cgeonames/ for instant reload.\n");
    return 0;
}
