/**
 * @file cgeonames.h
 * @brief GeoNames offline city lookup with auto-download.
 *
 * Parses GeoNames TSV files (per-country, cities1000, allCountries, etc.)
 * and provides fuzzy-search by city name, returning coordinates and
 * IANA timezone for astrological chart calculations.
 *
 * Data license: GeoNames — CC-BY 4.0 — http://www.geonames.org/
 * Attribution required in your app's "About" or documentation.
 *
 * @section usage Usage
 * @code
 *   gn_engine_t* eng = gn_engine_create();
 *
 *   // Download and load a country file (e.g. Cuba)
 *   gn_engine_download_and_load(eng, GN_SRC_COUNTRY, "CU", NULL, NULL, NULL, 0);
 *
 *   // Search
 *   gn_search_opts_t opts = { .limit = 10 };
 *   int n = gn_search(eng, "Havana", &opts);
 *
 *   for (int i = 0; i < n; i++) {
 *       const GeoName* g = gn_result_at(eng, i);
 *       printf("%s  lat=%.4f  lon=%.4f  tz=%s  pop=%d\n",
 *              g->name, g->latitude, g->longitude, g->timezone, g->population);
 *   }
 *
 *   gn_engine_free(eng);
 * @endcode
 *
 * @section threads Thread safety
 * The engine is NOT thread-safe. Create one engine per thread or protect
 * with a mutex.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 *  Version
 * ======================================================================== */

#define CGEONAMES_VERSION_MAJOR 1
#define CGEONAMES_VERSION_MINOR 0
#define CGEONAMES_VERSION_PATCH 0

/* ========================================================================
 *  Constants
 * ======================================================================== */

/** Base URL for all GeoNames downloads. */
#define GEONAMES_BASE_URL "http://download.geonames.org/export/dump"

/** Maximum alternatenames stored per city (split from comma-separated field). */
#define GN_MAX_ALIASES 32

/** Maximum length of a single name or alias string. */
#define GN_NAME_MAX 256

/** Maximum IANA timezone string length. */
#define GN_TZ_MAX 64

/** Default download timeout in seconds. */
#define GN_DOWNLOAD_TIMEOUT_SEC 60

/* ========================================================================
 *  Public types
 * ======================================================================== */

/**
 * @enum gn_source_type_t
 * @brief Identifies which GeoNames source file to download.
 *
 * Each value maps to a well-known GeoNames dump file. The library builds the
 * correct URL and local filename automatically from this enum.
 */
typedef enum {
    GN_SRC_UNKNOWN        = 0,   /**< Invalid / uninitialised.           */

    /* ---- Pre-built city population filters ---- */
    GN_SRC_CITIES15000    = 1,   /**< ~28k entries, cities ≥ 15 000 pop. */
    GN_SRC_CITIES5000     = 2,   /**< ~50k entries, cities ≥ 5 000 pop.  */
    GN_SRC_CITIES1000     = 3,   /**< ~130k entries, cities ≥ 1 000 pop. */
    GN_SRC_CITIES500      = 4,   /**< ~185k entries, cities ≥ 500 pop.   */

    /* ---- Global ---- */
    GN_SRC_ALL_COUNTRIES  = 5,   /**< ~25M entries, all features.        */

    /* ---- Per-country — set country_code separately ---- */
    GN_SRC_COUNTRY        = 10,  /**< e.g. "CU" → CU.zip                 */

    /* ---- Custom / local TSV file (not downloaded) ---- */
    GN_SRC_LOCAL          = 99   /**< Loaded from disk, no URL.          */
} gn_source_type_t;

/**
 * @struct GeoName
 * @brief A single city / populated-place record from GeoNames.
 *
 * All fields correspond directly to columns in the GeoNames TSV dump.
 * The `relevance` field is populated by gn_search() and is not part of
 * the original data.
 */
typedef struct {
    /* ---- GeoNames TSV columns (all 19 columns represented) ---- */

    int         geoname_id;          /**< Unique GeoNames integer ID.            */
    char        name[GN_NAME_MAX];   /**< Primary name (UTF-8).                  */
    char        asciiname[GN_NAME_MAX]; /**< ASCII transliteration.              */

    /**
     * Alternate names, split from the comma-separated alternatenames column.
     * Includes language-agnostic aliases, ASCII transliterations, and common
     * variants. Parsed at load time — see alias_count for the actual count.
     */
    char        aliases[GN_MAX_ALIASES][GN_NAME_MAX];
    int         alias_count;         /**< Number of valid entries in aliases.    */

    double      latitude;            /**< Decimal degrees, WGS84.                */
    double      longitude;           /**< Decimal degrees, WGS84.                */

    /**
     * Feature class — broad category of the geographical feature.
     * Common values:
     *   'A' — country / state / region
     *   'H' — stream / lake
     *   'L' — park / area
     *   'P' — city / village / populated place
     *   'R' — road / railroad
     *   'S' — spot / building / farm
     *   'T' — mountain / hill / rock
     *   'U' — undersea feature
     *   'V' — forest / heath
     */
    char        feature_class;

    /**
     * Feature code — specific type within the feature class.
     * Examples: PPLC (capital of a country), PPLA (seat of an administrative
     * division), PPL (populated place), ADM1 (first-order admin division).
     * Full reference: http://www.geonames.org/export/codes.html
     */
    char        feature_code[16];

    char        country_code[3];     /**< ISO 3166-1 alpha-2, e.g. "CU".         */

    /** Alternate country codes, comma-separated (rarely populated). */
    char        cc2[64];

    /**
     * Admin 1 code — state / province / fips code.
     * Most adm1 codes are FIPS codes. ISO codes are used for US, CH, BE, ME.
     * Display names are in admin1CodesASCII.txt.
     */
    char        admin1_code[32];

    /**
     * Admin 2 code — county / municipality.
     * Display names are in admin2Codes.txt.
     */
    char        admin2_code[80];

    /**
     * Admin 3 code — third-level administrative division.
     * Populated for some countries (e.g. Germany, Spain, Japan).
     */
    char        admin3_code[32];

    /**
     * Admin 4 code — fourth-level administrative division.
     * Populated for some countries.
     */
    char        admin4_code[32];

    int         population;          /**< Population count (0 if unknown).       */

    /**
     * Elevation in metres above sea level.
     * -1 if unknown.
     */
    short       elevation;

    /**
     * Digital elevation model (SRTM3 or GTOPO30).
     * Average elevation of a 3"x3" (~90 m x 90 m) or 30"x30"
     * (~900 m x 900 m) area in metres. Data processed by CGIAR/CIAT.
     * -1 if unknown.
     */
    short       dem;

    /**
     * IANA timezone identifier.
     * Examples: "America/Havana", "Europe/Madrid", "Asia/Tokyo".
     * Maps to the timezone column in the GeoNames TSV.
     * Cross-reference with acetimec's zone registry for DST-aware conversions.
     */
    char        timezone[GN_TZ_MAX];

    /**
     * Last modification date of this record in the GeoNames database.
     * Format: "YYYY-MM-DD", or an empty string if unavailable.
     */
    char        modification_date[16];

    /* ---- Filled by gn_search() — not part of original GeoNames data ---- */
    float       relevance;           /**< Fuzzy-match score (higher = better).   */
} GeoName;

/**
 * @struct gn_engine
 * @brief Opaque search-engine handle.  Created by gn_engine_create().
 */
typedef struct gn_engine gn_engine_t;

/**
 * @struct gn_search_opts_t
 * @brief Optional parameters for gn_search().
 *
 * Zero-initialise the struct and override the fields you care about.
 * Pass NULL to gn_search() to use all defaults (limit = 20, no filters).
 */
typedef struct {
    int          limit;              /**< Max results returned (default 20).   */
    const char*  country_code;       /**< Filter by ISO code, NULL = any.      */
    const char*  admin1_code;        /**< Filter by state code, NULL = any.    */
    int          min_population;     /**< Minimum population, 0 = no filter.   */
    int          exact_first;        /**< 1 = exact matches on top (default 0).*/
} gn_search_opts_t;

/**
 * @enum gn_result_field_t
 * @brief Bitmask flags for controlling which fields are populated in results.
 *
 * Use these flags with gn_search_opts_ex_t::fields to control memory usage
 * and parsing overhead. By default, all fields are populated (GN_FIELD_ALL).
 *
 * @section field_usage Example
 * @code
 *   // Only get timezone and aliases (saves memory)
 *   gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
 *   opts.fields = GN_FIELD_TIMEZONE | GN_FIELD_ALIASES;
 *   gn_search_ex(engine, "Havana", &opts);
 * @endcode
 */
typedef enum {
    GN_FIELD_GEONAME_ID     = (1 << 0),   /**< geoname_id               */
    GN_FIELD_NAME           = (1 << 1),   /**< name                     */
    GN_FIELD_ASCINAME       = (1 << 2),   /**< asciiname                */
    GN_FIELD_ALIASES        = (1 << 3),   /**< aliases + alias_count    */
    GN_FIELD_COORDINATES    = (1 << 4),   /**< latitude + longitude     */
    GN_FIELD_FEATURE        = (1 << 5),   /**< feature_class + code     */
    GN_FIELD_COUNTRY        = (1 << 6),   /**< country_code + cc2       */
    GN_FIELD_ADMIN_CODES    = (1 << 7),   /**< admin1-4 codes           */
    GN_FIELD_POPULATION     = (1 << 8),   /**< population               */
    GN_FIELD_ELEVATION      = (1 << 9),   /**< elevation + dem          */
    GN_FIELD_TIMEZONE       = (1 << 10),  /**< timezone                 */
    GN_FIELD_MOD_DATE       = (1 << 11),  /**< modification_date        */
    GN_FIELD_RELEVANCE      = (1 << 12),  /**< relevance (search score) */

    /** All fields populated (default behaviour). */
    GN_FIELD_ALL            = 0xFFFFFFFF,

    /** Common subset: name, coordinates, timezone, population. */
    GN_FIELD_BASIC          = GN_FIELD_NAME | GN_FIELD_COORDINATES |
                              GN_FIELD_TIMEZONE | GN_FIELD_POPULATION,

    /** Minimal: just name and coordinates. */
    GN_FIELD_MINIMAL        = GN_FIELD_NAME | GN_FIELD_COORDINATES,

    /** For reverse geocoding: timezone + admin codes + country. */
    GN_FIELD_LOCATION_INFO  = GN_FIELD_TIMEZONE | GN_FIELD_ADMIN_CODES |
                              GN_FIELD_COUNTRY | GN_FIELD_COORDINATES,

    /** For autocomplete: just names and aliases. */
    GN_FIELD_NAMES_ONLY     = GN_FIELD_NAME | GN_FIELD_ASCINAME |
                              GN_FIELD_ALIASES
} gn_result_field_t;

/**
 * @struct gn_search_opts_ex_t
 * @brief Extended search options with field filtering and reverse geocoding.
 *
 * Use GN_SEARCH_OPTS_EX_INIT to zero-initialise, then override fields.
 * This struct supersedes gn_search_opts_t and adds field filtering support.
 */
typedef struct {
    int          limit;              /**< Max results returned (default 20).   */
    const char*  country_code;       /**< Filter by ISO code, NULL = any.      */
    const char*  admin1_code;        /**< Filter by state code, NULL = any.    */
    int          min_population;     /**< Minimum population, 0 = no filter.   */
    int          exact_first;        /**< 1 = exact matches on top (default 0).*/

    /**
     * Bitmask of gn_result_field_t flags controlling which fields are
     * populated in returned GeoName structs. Default: GN_FIELD_ALL.
     *
     * Use GN_FIELD_BASIC for common use cases, or combine individual flags.
     */
    uint32_t     fields;

    /**
     * For reverse geocoding: maximum search radius in kilometres.
     * Only used with gn_search_nearby(). Default: 50 km.
     */
    double       radius_km;

    /**
     * For reverse geocoding: filter by feature class.
     * NULL = any, 'P' = populated places only, 'A' = administrative, etc.
     */
    char         feature_class_filter;
} gn_search_opts_ex_t;

/** Convenience macro to initialise gn_search_opts_ex_t with defaults. */
#define GN_SEARCH_OPTS_EX_INIT { \
    .limit = 20, \
    .country_code = NULL, \
    .admin1_code = NULL, \
    .min_population = 0, \
    .exact_first = 0, \
    .fields = GN_FIELD_ALL, \
    .radius_km = 50.0, \
    .feature_class_filter = '\0' \
}

/* ========================================================================
 *  Error codes
 * ======================================================================== */

/**
 * @enum gn_error_t
 * @brief Detailed error codes returned by gn_engine_last_error().
 *
 * Every function that returns a negative error code also stores
 * a gn_error_t inside the engine, queryable via gn_engine_last_error()
 * and gn_engine_last_error_str().
 */
typedef enum {
    GN_ERR_NONE             =  0,  /**< No error (success).                */
    GN_ERR_INVALID_ARG      = -1,  /**< NULL pointer / bad enum value.     */
    GN_ERR_DOWNLOAD         = -2,  /**< cURL / network failure.            */
    GN_ERR_UNZIP            = -3,  /**< Corrupt zip / missing `unzip`.     */
    GN_ERR_PARSE            = -4,  /**< Malformed TSV / zero records.      */
    GN_ERR_NOMEM            = -5,  /**< malloc / realloc failed.           */
    GN_ERR_IO               = -6,  /**< Disk full / cannot write.          */
    GN_ERR_HTTP_404         = -7,  /**< Resource not found on server.      */
    GN_ERR_HTTP_403         = -8,  /**< Access forbidden (rate limit?).    */
    GN_ERR_HTTP_500         = -9,  /**< Server internal error.             */
    GN_ERR_TIMEOUT          = -10, /**< Download exceeded timeout.         */
    GN_ERR_SSL              = -11, /**< TLS / SSL handshake failure.       */
    GN_ERR_DNS              = -12, /**< Could not resolve hostname.        */
    GN_ERR_CONNECT          = -13, /**< Could not connect to server.       */
    GN_ERR_PARTIAL          = -14, /**< Partial file downloaded.           */
    GN_ERR_UNKNOWN          = -99  /**< Unclassified error.                */
} gn_error_t;

/**
 * @struct gn_error_info_t
 * @brief Extended error information populated by gn_engine_last_error().
 */
typedef struct {
    gn_error_t  code;            /**< Primary error code (same as return).  */
    int         curl_code;       /**< cURL error code (CURLE_*), or 0.      */
    long        http_code;       /**< HTTP response code (404, 500, etc.).   */
    char        message[256];    /**< Human-readable description (EN).       */
    char        url[512];        /**< URL that failed (empty if N/A).        */
} gn_error_info_t;

/**
 * @typedef gn_download_progress_t
 * @brief Callback signature for download progress notifications.
 *
 * @param downloaded_bytes  Bytes written to disk so far.
 * @param total_bytes       Total expected size (-1 if unknown).
 * @param user_data         Opaque pointer from the caller.
 */
typedef void (*gn_download_progress_t)(
    long downloaded_bytes,
    long total_bytes,
    void* user_data
);

/* ========================================================================
 *  Engine lifecycle
 * ======================================================================== */

/**
 * @brief Create and initialise a search engine.
 * @return Opaque handle, or NULL on allocation failure.
 *
 * The engine starts empty. Call gn_engine_download_and_load() or
 * gn_engine_load_file() to populate it with data.
 *
 * The default data directory is set to `~/.cgeonames/` and is used for
 * caching downloaded files.
 */
gn_engine_t* gn_engine_create(void);

/**
 * @brief Free all memory and destroy the engine.
 * @param engine  The engine handle.  NULL is safely ignored.
 */
void gn_engine_free(gn_engine_t* engine);

/**
 * @brief Return the number of loaded city records.
 * @param engine  The engine handle.
 * @return Total records across all loaded sources.
 */
int gn_engine_count(const gn_engine_t* engine);

/**
 * @brief Return the local cache directory.
 * @param engine  The engine handle.
 * @return Path string (owned by the engine).
 */
const char* gn_engine_get_data_dir(const gn_engine_t* engine);

/* ========================================================================
 *  Error handling
 * ======================================================================== */

/**
 * @brief Get the last error code from the engine.
 *
 * Every function that returns a negative error code also stores
 * a detailed gn_error_t inside the engine. Call this after any
 * failure to get the specific error type (GN_ERR_HTTP_404,
 * GN_ERR_TIMEOUT, GN_ERR_DNS, etc.).
 *
 * @param engine  The engine handle.
 * @return The last gn_error_t code (0 = no error, negative = error).
 *
 * @section err_example Example
 * @code
 *   int ret = gn_engine_download_and_load(engine, GN_SRC_COUNTRY, "XX",
 *                                          NULL, NULL, NULL, 0);
 *   if (ret < 0) {
 *       gn_error_t err = gn_engine_last_error(engine);
 *       const char* msg = gn_engine_last_error_str(engine);
 *       fprintf(stderr, "Failed: %s (code=%d)\n", msg, err);
 *
 *       // Full details available:
 *       gn_error_info_t info;
 *       gn_engine_error_info(engine, &info);
 *       fprintf(stderr, "  URL: %s\n", info.url);
 *       fprintf(stderr, "  HTTP: %ld, cURL: %d\n", info.http_code, info.curl_code);
 *   }
 * @endcode
 */
gn_error_t gn_engine_last_error(const gn_engine_t* engine);

/**
 * @brief Get a human-readable string for the last error.
 * @param engine  The engine handle.
 * @return Static error message string (e.g. "HTTP 404: Resource not found").
 */
const char* gn_engine_last_error_str(const gn_engine_t* engine);

/**
 * @brief Get full extended error information.
 * @param engine  The engine handle.
 * @param out     Pointer to gn_error_info_t struct to fill (must not be NULL).
 * @return 0 on success, -1 if engine or out is NULL.
 */
int gn_engine_error_info(const gn_engine_t* engine, gn_error_info_t* out);

/* ========================================================================
 *  Download & Load
 * ======================================================================== */

/**
 * @brief Download a GeoNames file, unzip it, validate, and load records.
 *
 * This is the primary entry point. It handles the full pipeline:
 *   1. Build the download URL from @p source (and @p country_code).
 *   2. Check whether the `.zip` and `.txt` already exist in @p local_dir.
 *   3. If missing (or @p force_refresh), download via cURL.
 *   4. **Validate** the .zip file (PK signature check, minimum size).
 *   5. Unzip the archive using the system `unzip` command.
 *   6. **Validate** the extracted TSV (field count, format sanity).
 *   7. Parse the TSV and append records to the engine.
 *
 * The file is cached on disk. Subsequent calls for the same source are
 * no-ops unless @p force_refresh is non-zero.
 *
 * After every call, detailed error information is available via
 * gn_engine_last_error() and gn_engine_error_info().
 *
 * @param engine          The engine handle.
 * @param source          Which GeoNames source to download.
 * @param country_code    ISO 3166-1 alpha-2 code (required when
 *                        source == @c GN_SRC_COUNTRY, ignored otherwise).
 * @param local_dir       Directory for downloaded files.
 *                        NULL uses the engine's default (`~/.cgeonames/`).
 * @param progress_cb     Optional progress callback (NULL = silent).
 * @param progress_ctx    User data forwarded to @p progress_cb.
 * @param force_refresh   Non-zero to force re-download even if cached.
 * @return 0 on success, negative error code on failure:
 *         -1   invalid argument (NULL engine, bad source, bad country code)
 *         -2   generic download error (see gn_engine_last_error() for details)
 *         -3   unzip failed (corrupt zip or missing `unzip` command)
 *         -4   parse error (malformed TSV, format validation failed)
 *         -5   out of memory
 *         -6   disk full / cannot write to local_dir
 *         -7   HTTP 404 — resource does not exist on GeoNames server
 *         -8   HTTP 403 — access forbidden (rate limit or auth)
 *         -9   HTTP 5xx — GeoNames server error
 *         -10  download timed out
 *         -11  SSL/TLS handshake failure
 *         -12  DNS resolution failed (cannot resolve download.geonames.org)
 *         -13  could not connect to server
 *         -14  partial download (connection dropped mid-transfer)
 *
 * @section example Example
 * @code
 *   // Download Cuba's data, default cache dir, no progress callback.
 *   int ret = gn_engine_download_and_load(engine, GN_SRC_COUNTRY, "CU",
 *                               NULL, NULL, NULL, 0);
 *   if (ret < 0) {
 *       gn_error_info_t info;
 *       gn_engine_error_info(engine, &info);
 *       fprintf(stderr, "Failed: %s\n  URL: %s\n  HTTP: %ld\n",
 *               info.message, info.url, info.http_code);
 *   }
 *
 *   // Download cities1000 worldwide with a progress callback.
 *   gn_engine_download_and_load(engine, GN_SRC_CITIES1000, NULL,
 *                               NULL, my_progress, my_ctx, 0);
 * @endcode
 */
int gn_engine_download_and_load(
    gn_engine_t*           engine,
    gn_source_type_t       source,
    const char*            country_code,
    const char*            local_dir,
    gn_download_progress_t progress_cb,
    void*                  progress_ctx,
    int                    force_refresh
);

/**
 * @brief Load a local, already-unzipped TSV file.
 * @param engine  The engine handle.
 * @param path    Absolute path to a GeoNames-format `.txt` file.
 * @return 0 on success, -4 on parse error, -5 on OOM.
 */
int gn_engine_load_file(gn_engine_t* engine, const char* path);

/**
 * @brief Load from an in-memory TSV buffer.
 *
 * A temporary file is created in the cache directory, written, parsed,
 * and then deleted.  This is a convenience wrapper around
 * gn_engine_load_file().
 *
 * @param engine  The engine handle.
 * @param data    Pointer to TSV text (owned by caller, copied internally).
 * @param size    Size in bytes.
 * @return 0 on success, -4 on parse error, -5 on OOM.
 */
int gn_engine_load_memory(gn_engine_t* engine, const char* data, int size);

/**
 * @brief Check whether a source has already been loaded.
 * @param engine        The engine handle.
 * @param source        The source type to check.
 * @param country_code  Country code (only meaningful for GN_SRC_COUNTRY).
 * @return 1 if already loaded, 0 otherwise.
 */
int gn_engine_has_source(const gn_engine_t* engine, gn_source_type_t source,
                         const char* country_code);

/* ========================================================================
 *  Search
 * ======================================================================== */

/**
 * @brief Fuzzy-search cities by name.
 *
 * Scoring combines:
 *   1. Exact match on name / asciiname / alias → 1000 + log(population)
 *   2. Prefix match (name starts with query)   → 500 + log(population)
 *   3. Substring containment (case-insensitive) → 200 + log(population)
 *   4. Levenshtein distance ≤ 2                 → 50 - 15*dist + log(pop)
 *
 * Results are sorted by descending relevance.  Filters from @p opts
 * (country, admin1, min_population) are applied before scoring.
 *
 * @param engine  The engine handle.
 * @param query   Search string, e.g. "Habana", "tokyo", "nueva york".
 * @param opts    Search options, or NULL for defaults (limit = 20).
 * @return Number of results found (0 .. opts->limit).
 */
int gn_search(const gn_engine_t* engine, const char* query,
              const gn_search_opts_t* opts);

/**
 * @brief Get a result from the most recent search.
 * @param engine  The engine handle.
 * @param index   Zero-based index; must be &lt; gn_search() return value.
 * @return Pointer to the GeoName record, or NULL if index is out of range.
 *         The pointer is owned by the engine and is valid until the next
 *         gn_search() or gn_engine_free().
 */
const GeoName* gn_result_at(const gn_engine_t* engine, int index);

/* ========================================================================
 *  Extended Search (with field filtering)
 * ======================================================================== */

/**
 * @brief Extended fuzzy-search with field filtering control.
 *
 * Same scoring as gn_search(), but allows controlling which fields are
 * populated in the returned GeoName structs via opts->fields bitmask.
 *
 * @param engine  The engine handle.
 * @param query   Search string, e.g. "Habana", "tokyo", "nueva york".
 * @param opts    Extended search options, or NULL for defaults (all fields).
 * @return Number of results found (0 .. opts->limit).
 *
 * @section example Example — only get timezone + aliases
 * @code
 *   gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
 *   opts.fields = GN_FIELD_TIMEZONE | GN_FIELD_ALIASES;
 *   opts.limit = 5;
 *   int n = gn_search_ex(engine, "Havana", &opts);
 *   for (int i = 0; i < n; i++) {
 *       const GeoName* g = gn_result_at(engine, i);
 *       printf("tz=%s, aliases=%d\n", g->timezone, g->alias_count);
 *   }
 * @endcode
 */
int gn_search_ex(const gn_engine_t* engine, const char* query,
                 const gn_search_opts_ex_t* opts);

/* ========================================================================
 *  Reverse Geocoding
 * ======================================================================== */

/**
 * @brief Reverse geocode: find nearest cities by latitude/longitude.
 *
 * Searches all loaded city records and returns the closest matches
 * sorted by distance. Uses the Haversine formula for great-circle distance.
 *
 * @param engine      The engine handle.
 * @param latitude    Decimal degrees, WGS84 (e.g. 23.1136 for Havana).
 * @param longitude   Decimal degrees, WGS84 (e.g. -82.3666 for Havana).
 * @param opts        Extended options, or NULL for defaults:
 *                    - limit: max results (default 10)
 *                    - radius_km: search radius (default 50 km)
 *                    - feature_class_filter: e.g. 'P' for populated places
 *                    - fields: which fields to populate (default GN_FIELD_ALL)
 * @return Number of results found within radius (0 .. opts->limit).
 *
 * @section revgeo_example Example
 * @code
 *   gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
 *   opts.limit = 5;
 *   opts.radius_km = 30.0;
 *   opts.fields = GN_FIELD_LOCATION_INFO;  // tz + admin codes + country
 *
 *   int n = gn_search_nearby(engine, 23.1136, -82.3666, &opts);
 *   for (int i = 0; i < n; i++) {
 *       const GeoName* g = gn_result_at(engine, i);
 *       printf("%s, %s  tz=%s  dist=%.1f km\n",
 *              g->name, g->country_code, g->timezone, g->relevance);
 *   }
 * @endcode
 *
 * @note The relevance field contains the distance in kilometres
 *       (lower = closer = better) when returned from this function.
 */
int gn_search_nearby(const gn_engine_t* engine,
                     double latitude, double longitude,
                     const gn_search_opts_ex_t* opts);

/**
 * @brief Reverse geocode to the single nearest city.
 *
 * Convenience wrapper around gn_search_nearby() with limit=1.
 *
 * @param engine      The engine handle.
 * @param latitude    Decimal degrees, WGS84.
 * @param longitude   Decimal degrees, WGS84.
 * @param radius_km   Maximum search radius in kilometres (0 = default 50).
 * @return Pointer to the nearest GeoName record, or NULL if none found
 *         within radius. Owned by engine, valid until next search/free.
 */
const GeoName* gn_reverse_geocode(const gn_engine_t* engine,
                                   double latitude, double longitude,
                                   double radius_km);

/* ========================================================================
 *  Utility
 * ======================================================================== */

/**
 * @brief Compute the Levenshtein edit distance between two strings
 *        (case-insensitive).
 * @param a  First string.
 * @param b  Second string.
 * @return Number of single-character edits, or -1 if either is NULL.
 */
int gn_levenshtein(const char* a, const char* b);

/**
 * @brief Case-insensitive substring search.
 * @param haystack  String to search within.
 * @param needle    Substring to find.
 * @return 1 if found, 0 otherwise.
 */
int gn_str_contains_ci(const char* haystack, const char* needle);

/**
 * @brief Get the library version as a human-readable string.
 * @return Static string like "1.0.0".
 */
const char* gn_version(void);

/**
 * @brief Build the full download URL for a source.
 * @param source        The source type.
 * @param country_code  Country code for GN_SRC_COUNTRY (ignored otherwise).
 * @return Static string pointer, or NULL on invalid input.
 *         Overwritten on the next call.
 */
const char* gn_source_url(gn_source_type_t source, const char* country_code);

/**
 * @brief Get the expected local TSV filename for a source.
 * @param source        The source type.
 * @param country_code  Country code for GN_SRC_COUNTRY (ignored otherwise).
 * @return Static string pointer, or NULL on invalid input.
 *         Overwritten on the next call.
 */
const char* gn_source_filename(gn_source_type_t source, const char* country_code);

#ifdef __cplusplus
}
#endif
