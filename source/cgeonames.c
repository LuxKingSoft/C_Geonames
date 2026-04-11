/**
 * @file cgeonames.c
 * @brief GeoNames engine — download, parse, fuzzy search.
 *
 * Full implementation of the cgeonames.h public API.
 *
 * Dependencies:
 *   - libcurl  (network download)
 *   - system `unzip` command (archive extraction)
 *   - math library (-lm on Unix)
 */
#include "cgeonames.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

/* ---- Platform-specific includes ---- */
#ifdef _WIN32
  #include <windows.h>
  #include <direct.h>
  #define GN_SEP "\\"
  #define GN_MKDIR(p) _mkdir(p)
  #include <process.h>
#else
  #include <pwd.h>
  #include <unistd.h>
  #include <sys/wait.h>
  #define GN_SEP "/"
  #define GN_MKDIR(p) mkdir(p, 0755)
#endif

/* ---- cURL for downloads ---- */
#include <curl/curl.h>

/* ========================================================================
 *  Internal constants
 * ======================================================================== */

/** Initial capacity of the city array (grows exponentially). */
#define GN_INITIAL_CAP 1024

/** Default data directory name under the user's home folder. */
#define GN_DEFAULT_DATA_DIR ".cgeonames"

/** cURL User-Agent header (GeoNames requests a valid agent). */
#define GN_USER_AGENT "AstroNumerologyChart/1.0"

/* ========================================================================
 *  Internal types
 * ======================================================================== */

/** Metadata about a source file that has been loaded. */
typedef struct {
    gn_source_type_t source;          /**< Which source enum.                */
    char             country_code[3]; /**< ISO code (only for GN_SRC_COUNTRY).*/
    int              record_count;    /**< Records contributed by this file.  */
} gn_loaded_source_t;

/**
 * The engine — holds all loaded city records, source-tracking metadata,
 * and a workspace array for search results.
 */
struct gn_engine {
    GeoName*          cities;         /**< Dynamic array of all loaded cities. */
    int               capacity;       /**< Allocated slots in cities[].        */
    int               count;          /**< Currently used slots.               */

    gn_loaded_source_t* loaded;       /**< Which sources have been loaded.     */
    int                 loaded_cap;   /**< Allocated slots in loaded[].        */
    int                 loaded_count; /**< Number of tracked sources.          */

    /* ---- Search-result workspace (reused across calls) ---- */
    int*  result_indices;             /**< Indices into cities[], sorted.      */
    int   result_count;               /**< Number of results from last search. */
    int   result_cap;                 /**< Allocated slots in result_indices.  */

    /* ---- Download / filesystem paths ---- */
    char  data_dir[512];              /**< Cache directory for .zip / .txt.    */

    /* ---- Last error information ---- */
    gn_error_info_t last_error;       /**< Populated on every failure.        */
};

/* ========================================================================
 *  Helpers — portable case-insensitive string contains
 * ======================================================================== */

/**
 * @brief Convert a string to lower-case into a destination buffer.
 */
static void str_to_lower(char* dst, const char* src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = (char)tolower((unsigned char)src[i]);
        i++;
    }
    dst[i] = '\0';
}

/**
 * @brief Case-insensitive strstr equivalent.
 * @return Pointer into @p haystack where @p needle starts, or NULL.
 */
static const char* stristr(const char* haystack, const char* needle) {
    if (!needle[0]) return haystack;
    for (const char* h = haystack; *h; ++h) {
        const char* a = h;
        const char* b = needle;
        while (*a && *b &&
               tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++; b++;
        }
        if (!*b) return h;
    }
    return NULL;
}

int gn_str_contains_ci(const char* haystack, const char* needle) {
    return stristr(haystack, needle) != NULL;
}

/* ========================================================================
 *  Helpers — Levenshtein distance (case-insensitive)
 * ======================================================================== */

int gn_levenshtein(const char* a, const char* b) {
    if (!a || !b) return -1;
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (la == 0) return lb;
    if (lb == 0) return la;

    int* row = (int*)calloc((size_t)(lb + 1), sizeof(int));
    if (!row) return -1;

    for (int j = 0; j <= lb; j++) row[j] = j;

    int prev_diag, temp;
    for (int i = 1; i <= la; i++) {
        prev_diag = row[0];
        row[0] = i;
        for (int j = 1; j <= lb; j++) {
            temp = row[j];
            int cost = (tolower((unsigned char)a[i - 1]) ==
                        tolower((unsigned char)b[j - 1])) ? 0 : 1;
            int v1 = row[j]     + 1;     /* deletion     */
            int v2 = row[j - 1] + 1;     /* insertion    */
            int v3 = prev_diag  + cost;  /* substitution */
            row[j] = v1 < v2 ? (v1 < v3 ? v1 : v3)
                             : (v2 < v3 ? v2 : v3);
            prev_diag = temp;
        }
    }
    int result = row[lb];
    free(row);
    return result;
}

/* ========================================================================
 *  Helpers — filesystem
 * ======================================================================== */

/**
 * @brief Resolve the current user's home directory.
 */
static const char* get_home_dir(void) {
#ifdef _WIN32
    const char* p = getenv("USERPROFILE");
    return p ? p : ".";
#else
    const char* p = getenv("HOME");
    if (p) return p;
    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;
    return ".";
#endif
}

/**
 * @brief Create a directory and all parent directories (mkdir -p).
 * @return 0 on success, -1 on failure (except EEXIST which is treated as OK).
 */
static int ensure_dir_recursive(const char* path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);

    /* Strip trailing separator */
    if (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
        tmp[len - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            if (GN_MKDIR(tmp) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (GN_MKDIR(tmp) != 0 && errno != EEXIST) return -1;
    return 0;
}

/** @return 1 if the file exists and is readable, 0 otherwise. */
static int file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/**
 * @brief Get file size in bytes.  Returns -1 if the file doesn't exist.
 */
static long file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

/**
 * @brief Quick validation of a downloaded .zip file:
 *   - File exists and is non-empty
 *   - Starts with PK magic bytes (0x50 0x4B) — ZIP signature
 *   - Size > 22 bytes (minimum valid ZIP: End of Central Directory)
 * @return 0 on success, -3 if corrupt/invalid.
 */
static int validate_zip(const char* zip_path, gn_engine_t* engine) {
    long sz = file_size(zip_path);
    if (sz < 0) {
        /* File doesn't exist — download step probably failed silently. */
        if (engine) {
            engine->last_error.code = GN_ERR_IO;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "Downloaded file vanished: %s", zip_path);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", zip_path);
        }
        return -3;
    }

    if (sz < 22) {
        if (engine) {
            engine->last_error.code = GN_ERR_DOWNLOAD;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "Downloaded file too small (%ld bytes): %s — possibly an error page or truncated",
                     sz, zip_path);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", zip_path);
        }
        return -3;
    }

    /* Check PK (0x50 0x4B) magic bytes at offset 0. */
    FILE* fp = fopen(zip_path, "rb");
    if (!fp) {
        if (engine) {
            engine->last_error.code = GN_ERR_IO;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "Cannot open downloaded file for validation: %s", zip_path);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", zip_path);
        }
        return -3;
    }

    unsigned char magic[2];
    if (fread(magic, 1, 2, fp) != 2 || magic[0] != 0x50 || magic[1] != 0x4B) {
        fclose(fp);
        if (engine) {
            /* The file is not a ZIP — likely an HTML error page. */
            engine->last_error.code = GN_ERR_HTTP_404;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "Downloaded file is not a valid ZIP (missing PK signature): %s — the resource may not exist on GeoNames",
                     zip_path);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", zip_path);
        }
        return -3;
    }

    fclose(fp);
    return 0;
}

/**
 * @brief Validate the extracted .txt TSV file:
 *   - File exists and is non-empty (> 100 bytes)
 *   - First non-blank line contains at least 18 tab characters (19 fields)
 *   - First field looks like a numeric geoname_id
 * @return 0 on success, -4 if malformed.
 */
static int validate_tsv(const char* tsv_path, gn_engine_t* engine) {
    long sz = file_size(tsv_path);
    if (sz < 0) {
        if (engine) {
            engine->last_error.code = GN_ERR_UNZIP;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "TSV file missing after unzip: %s", tsv_path);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", tsv_path);
        }
        return -4;
    }

    if (sz < 100) {
        if (engine) {
            engine->last_error.code = GN_ERR_PARSE;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "TSV file suspiciously small (%ld bytes): %s", sz, tsv_path);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", tsv_path);
        }
        return -4;
    }

    /* Read first few lines to validate TSV format. */
    FILE* fp = fopen(tsv_path, "r");
    if (!fp) {
        if (engine) {
            engine->last_error.code = GN_ERR_IO;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "Cannot open TSV file for validation: %s", tsv_path);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", tsv_path);
        }
        return -4;
    }

    char line[2048];
    int valid_lines = 0;
    int checked = 0;

    while (fgets(line, sizeof(line), fp) && checked < 5) {
        /* Skip blank lines and comment lines. */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0' || line[0] == '#')
            continue;

        checked++;

        /* Count tab characters — need at least 18 for 19 fields. */
        int tabs = 0;
        for (const char* p = line; *p && *p != '\n'; p++) {
            if (*p == '\t') tabs++;
        }
        if (tabs < 18) continue;

        /* Check that first field (geonameid) looks numeric. */
        char* first_field = line;
        /* Trim leading whitespace. */
        while (*first_field == ' ' || *first_field == '\t') first_field++;

        int is_numeric = 1;
        const char* cp = first_field;
        if (*cp == '-') cp++;  /* allow negative (shouldn't happen, but be safe) */
        if (!*cp) is_numeric = 0;
        while (*cp && *cp != '\t') {
            if (*cp < '0' || *cp > '9') { is_numeric = 0; break; }
            cp++;
        }

        if (is_numeric) valid_lines++;
    }

    fclose(fp);

    if (valid_lines == 0 && checked > 0) {
        if (engine) {
            engine->last_error.code = GN_ERR_PARSE;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "TSV format validation failed: no lines with 19 tab-separated fields and numeric geonameid in %s — downloaded data may be corrupt or from an unexpected source",
                     tsv_path);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", tsv_path);
        }
        return -4;
    }

    return 0;
}

/**
 * @brief Portable path join using the platform's separator.
 */
static void path_join(char* out, int max,
                      const char* dir, const char* file) {
    snprintf(out, max, "%s" GN_SEP "%s", dir, file);
}

/* ========================================================================
 *  Helpers — cURL download
 * ======================================================================== */

/** Internal state passed to cURL write callback. */
typedef struct {
    FILE* fp;                       /**< Destination file handle.            */
    gn_download_progress_t cb;      /**< User progress callback.             */
    void* ctx;                      /**< User data for progress callback.    */
    long total;                     /**< Total expected bytes (-1 if known). */
    long downloaded;                /**< Bytes written so far.               */
} dl_ctx_t;

/**
 * @brief cURL write callback — appends data to the output file and
 *        fires the progress callback.
 */
static size_t curl_write_cb(void* ptr, size_t size, size_t nmemb,
                            void* user) {
    dl_ctx_t* ctx = (dl_ctx_t*)user;
    size_t written = fwrite(ptr, size, nmemb, ctx->fp);
    ctx->downloaded += (long)written;
    if (ctx->cb) {
        ctx->cb(ctx->downloaded, ctx->total, ctx->ctx);
    }
    return written;
}

/**
 * @brief Download a file from @p url to @p dest.
 *
 * Captures detailed error information including:
 *   - cURL error code (CURLE_*)
 *   - HTTP response code (404, 500, etc.)
 *   - Human-readable error message
 *
 * @return 0 on success, negative error code on failure:
 *         -2  generic download error
 *         -7  HTTP 404 (resource not found)
 *         -8  HTTP 403 (forbidden)
 *         -9  HTTP 500 (server error)
 *         -10 timeout
 *         -11 SSL/TLS failure
 *         -12 DNS resolution failure
 *         -13 Connection refused
 *         -14 Partial download
 *         -6  file I/O error
 */
static int download_file(const char* url, const char* dest,
                         gn_download_progress_t progress_cb,
                         void* progress_ctx,
                         gn_engine_t* engine) {
    CURL* curl = curl_easy_init();
    if (!curl) return -2;

    FILE* fp = fopen(dest, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -6;
    }

    dl_ctx_t ctx = {
        .fp         = fp,
        .cb         = progress_cb,
        .ctx        = progress_ctx,
        .total      = 0,
        .downloaded = 0
    };

    /* --- HEAD request to get Content-Length --- */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)GN_DOWNLOAD_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, GN_USER_AGENT);
    CURLcode head_res = curl_easy_perform(curl);

    /* Check HEAD request for early failures (e.g. 404). */
    if (head_res != CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        fclose(fp);
        curl_easy_cleanup(curl);

        /* Map cURL error to our error codes. */
        gn_error_t err_code;
        const char* msg;

        switch (head_res) {
            case CURLE_OPERATION_TIMEDOUT:
                err_code = GN_ERR_TIMEOUT;
                msg = "Download timed out (server did not respond)";
                break;
            case CURLE_COULDNT_RESOLVE_HOST:
                err_code = GN_ERR_DNS;
                msg = "Could not resolve GeoNames hostname";
                break;
            case CURLE_COULDNT_CONNECT:
                err_code = GN_ERR_CONNECT;
                msg = "Could not connect to GeoNames server";
                break;
            case CURLE_SSL_CONNECT_ERROR:
                err_code = GN_ERR_SSL;
                msg = "SSL/TLS handshake failed";
                break;
            default:
                err_code = GN_ERR_DOWNLOAD;
                msg = "Network error during HEAD request";
                break;
        }

        /* Override with HTTP code if available. */
        if (http_code == 404) {
            err_code = GN_ERR_HTTP_404;
            msg = "HTTP 404: Resource not found on GeoNames server";
        } else if (http_code == 403) {
            err_code = GN_ERR_HTTP_403;
            msg = "HTTP 403: Access forbidden (rate limit or auth)";
        } else if (http_code >= 500) {
            err_code = GN_ERR_HTTP_500;
            msg = "HTTP 5xx: GeoNames server error";
        }

        if (engine) {
            engine->last_error.code = err_code;
            engine->last_error.curl_code = (int)head_res;
            engine->last_error.http_code = http_code;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message), "%s", msg);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", url);
        }
        return err_code;
    }

    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &ctx.total);

    /* --- GET request --- */
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    fclose(fp);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        /* Map cURL error to our error codes. */
        gn_error_t err_code;
        const char* msg;

        switch (res) {
            case CURLE_OPERATION_TIMEDOUT:
                err_code = GN_ERR_TIMEOUT;
                msg = "Download timed out (connection stalled)";
                break;
            case CURLE_COULDNT_RESOLVE_HOST:
                err_code = GN_ERR_DNS;
                msg = "Could not resolve GeoNames hostname";
                break;
            case CURLE_COULDNT_CONNECT:
                err_code = GN_ERR_CONNECT;
                msg = "Could not connect to GeoNames server";
                break;
            case CURLE_SSL_CONNECT_ERROR:
                err_code = GN_ERR_SSL;
                msg = "SSL/TLS handshake failed";
                break;
            case CURLE_PARTIAL_FILE:
                err_code = GN_ERR_PARTIAL;
                msg = "Partial download (connection dropped mid-transfer)";
                break;
            default:
                err_code = GN_ERR_DOWNLOAD;
                msg = curl_easy_strerror(res);
                break;
        }

        /* Override with HTTP code if available. */
        if (http_code == 404) {
            err_code = GN_ERR_HTTP_404;
            msg = "HTTP 404: Resource not found on GeoNames server";
        } else if (http_code == 403) {
            err_code = GN_ERR_HTTP_403;
            msg = "HTTP 403: Access forbidden (rate limit or auth)";
        } else if (http_code >= 500) {
            err_code = GN_ERR_HTTP_500;
            msg = "HTTP 5xx: GeoNames server error";
        }

        if (engine) {
            engine->last_error.code = err_code;
            engine->last_error.curl_code = (int)res;
            engine->last_error.http_code = http_code;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message), "%s", msg);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", url);
        }
        return err_code;
    }

    /* Check for HTTP errors even on successful cURL transfer. */
    if (http_code == 404) {
        if (engine) {
            engine->last_error.code = GN_ERR_HTTP_404;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 404;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "HTTP 404: Resource not found on GeoNames server");
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", url);
        }
        return GN_ERR_HTTP_404;
    } else if (http_code == 403) {
        if (engine) {
            engine->last_error.code = GN_ERR_HTTP_403;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 403;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "HTTP 403: Access forbidden (rate limit or auth)");
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", url);
        }
        return GN_ERR_HTTP_403;
    } else if (http_code >= 500) {
        if (engine) {
            engine->last_error.code = GN_ERR_HTTP_500;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = http_code;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "HTTP %ld: GeoNames server error", http_code);
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", url);
        }
        return GN_ERR_HTTP_500;
    }

    return 0;
}

/* ========================================================================
 *  Helpers — unzip (delegates to system `unzip` command)
 * ======================================================================== */

/**
 * @brief Extract a .zip archive into a directory.
 *
 * On Unix this invokes the `unzip` command-line tool.
 * On Windows it tries PowerShell's Expand-Archive.
 *
 * @return 0 on success, -3 on failure (bad archive or missing tool).
 */
static int unzip_file(const char* zip_path, const char* dest_dir) {
    char cmd[1024];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd),
             "powershell -NoProfile -Command "
             "\"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\" 2>nul",
             zip_path, dest_dir);
#else
    snprintf(cmd, sizeof(cmd),
             "unzip -o '%s' -d '%s' > /dev/null 2>&1", zip_path, dest_dir);
#endif
    int ret = system(cmd);
#ifdef _WIN32
    return (ret == 0) ? 0 : -3;
#else
    return WIFEXITED(ret) && WEXITSTATUS(ret) == 0 ? 0 : -3;
#endif
}

/* ========================================================================
 *  Source URL / filename helpers (public)
 * ======================================================================== */

/* Static buffers — overwritten on each call (standard C library pattern). */
static char g_url_buf[512];
static char g_name_buf[256];

const char* gn_source_url(gn_source_type_t source, const char* country_code) {
    switch (source) {
        case GN_SRC_CITIES15000:
            snprintf(g_url_buf, sizeof(g_url_buf),
                     "%s/cities15000.zip", GEONAMES_BASE_URL);
            break;
        case GN_SRC_CITIES5000:
            snprintf(g_url_buf, sizeof(g_url_buf),
                     "%s/cities5000.zip", GEONAMES_BASE_URL);
            break;
        case GN_SRC_CITIES1000:
            snprintf(g_url_buf, sizeof(g_url_buf),
                     "%s/cities1000.zip", GEONAMES_BASE_URL);
            break;
        case GN_SRC_CITIES500:
            snprintf(g_url_buf, sizeof(g_url_buf),
                     "%s/cities500.zip", GEONAMES_BASE_URL);
            break;
        case GN_SRC_ALL_COUNTRIES:
            snprintf(g_url_buf, sizeof(g_url_buf),
                     "%s/allCountries.zip", GEONAMES_BASE_URL);
            break;
        case GN_SRC_COUNTRY:
            if (!country_code || strlen(country_code) != 2) return NULL;
            snprintf(g_url_buf, sizeof(g_url_buf),
                     "%s/%s.zip", GEONAMES_BASE_URL, country_code);
            break;
        default:
            return NULL;
    }
    return g_url_buf;
}

const char* gn_source_filename(gn_source_type_t source,
                               const char* country_code) {
    switch (source) {
        case GN_SRC_CITIES15000:   return "cities15000.txt";
        case GN_SRC_CITIES5000:    return "cities5000.txt";
        case GN_SRC_CITIES1000:    return "cities1000.txt";
        case GN_SRC_CITIES500:     return "cities500.txt";
        case GN_SRC_ALL_COUNTRIES: return "allCountries.txt";
        case GN_SRC_COUNTRY:
            if (!country_code) return NULL;
            snprintf(g_name_buf, sizeof(g_name_buf), "%s.txt", country_code);
            return g_name_buf;
        default:
            return NULL;
    }
}

/* ========================================================================
 *  TSV parser
 * ======================================================================== */

/**
 * @brief Parse one GeoNames TSV line into a GeoName struct.
 *
 * Expected format (19 tab-separated columns):
 * <pre>
 *  0  geonameid       1  name         2  asciiname     3  alternatenames
 *  4  latitude        5  longitude    6  feature_class  7  feature_code
 *  8  country_code    9  cc2         10  admin1_code   11  admin2_code
 * 12  admin3_code    13  admin4_code 14  population    15  elevation
 * 16  dem            17  timezone    18  mod_date
 * </pre>
 *
 * @param line  One line from the TSV file (modifiable copy preferred).
 * @param out   Destination struct to fill.
 * @return 0 on success, -1 if the line has fewer than 19 columns.
 */
static int parse_line(const char* line, GeoName* out) {
    /* Zero-initialise so empty fields are safe. */
    memset(out, 0, sizeof(*out));
    out->elevation = -1;
    out->dem       = -1;

    /* Work on a writable copy. */
    char buf[2048];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Split on tabs — we need at least 19 fields (indices 0..18). */
    char* fields[19];
    int fc = 0;
    fields[0] = buf;
    for (char* p = buf; *p && fc < 18; p++) {
        if (*p == '\t') {
            *p = '\0';
            fc++;
            fields[fc] = p + 1;
        }
    }
    if (fc < 18) return -1;

    /* --- 0: geonameid --- */
    out->geoname_id = atoi(fields[0]);

    /* --- 1: name --- */
    strncpy(out->name, fields[1], GN_NAME_MAX - 1);
    out->name[GN_NAME_MAX - 1] = '\0';

    /* --- 2: asciiname --- */
    strncpy(out->asciiname, fields[2], GN_NAME_MAX - 1);
    out->asciiname[GN_NAME_MAX - 1] = '\0';

    /* --- 3: alternatenames (comma-separated) --- */
    out->alias_count = 0;
    {
        char alias_buf[1024];
        strncpy(alias_buf, fields[3], sizeof(alias_buf) - 1);
        alias_buf[sizeof(alias_buf) - 1] = '\0';

        char* save = NULL;
        char* token = strtok_r(alias_buf, ",", &save);
        while (token && out->alias_count < GN_MAX_ALIASES) {
            /* Trim leading spaces */
            while (*token == ' ') token++;
            /* Trim trailing whitespace / CR / LF */
            char* end = token + strlen(token) - 1;
            while (end > token &&
                   (*end == ' ' || *end == '\r' || *end == '\n')) {
                *end = '\0';
                end--;
            }
            if (token[0]) {
                strncpy(out->aliases[out->alias_count], token,
                        GN_NAME_MAX - 1);
                out->aliases[out->alias_count][GN_NAME_MAX - 1] = '\0';
                out->alias_count++;
            }
            token = strtok_r(NULL, ",", &save);
        }
    }

    /* --- 4-5: latitude / longitude --- */
    out->latitude  = atof(fields[4]);
    out->longitude = atof(fields[5]);

    /* --- 6-7: feature class / code --- */
    out->feature_class = fields[6][0];
    strncpy(out->feature_code, fields[7], sizeof(out->feature_code) - 1);
    out->feature_code[sizeof(out->feature_code) - 1] = '\0';

    /* --- 8-9: country --- */
    strncpy(out->country_code, fields[8], sizeof(out->country_code) - 1);
    out->country_code[sizeof(out->country_code) - 1] = '\0';
    strncpy(out->cc2, fields[9], sizeof(out->cc2) - 1);
    out->cc2[sizeof(out->cc2) - 1] = '\0';

    /* --- 10-13: admin codes (all four levels) --- */
    strncpy(out->admin1_code, fields[10], sizeof(out->admin1_code) - 1);
    out->admin1_code[sizeof(out->admin1_code) - 1] = '\0';
    strncpy(out->admin2_code, fields[11], sizeof(out->admin2_code) - 1);
    out->admin2_code[sizeof(out->admin2_code) - 1] = '\0';
    strncpy(out->admin3_code, fields[12], sizeof(out->admin3_code) - 1);
    out->admin3_code[sizeof(out->admin3_code) - 1] = '\0';
    strncpy(out->admin4_code, fields[13], sizeof(out->admin4_code) - 1);
    out->admin4_code[sizeof(out->admin4_code) - 1] = '\0';

    /* --- 14: population --- */
    out->population = atoi(fields[14]);

    /* --- 15-16: elevation / dem --- */
    if (fields[15][0] != '\0')
        out->elevation = (short)atoi(fields[15]);
    if (fields[16][0] != '\0')
        out->dem = (short)atoi(fields[16]);

    /* --- 17: timezone (IANA name) --- */
    strncpy(out->timezone, fields[17], GN_TZ_MAX - 1);
    out->timezone[GN_TZ_MAX - 1] = '\0';

    /* --- 18: modification date (YYYY-MM-DD) --- */
    strncpy(out->modification_date, fields[18], sizeof(out->modification_date) - 1);
    out->modification_date[sizeof(out->modification_date) - 1] = '\0';

    /* Strip trailing whitespace / newline from the date field. */
    {
        char* end = out->modification_date + strlen(out->modification_date) - 1;
        while (end >= out->modification_date &&
               (*end == '\r' || *end == '\n' || *end == ' ')) {
            *end = '\0';
            end--;
        }
    }

    return 0;
}

/* ========================================================================
 *  Parse an entire TSV file
 * ======================================================================== */

/**
 * @brief Open a TSV file and append every valid line to the engine.
 * @return 0 on success, -4 on parse failure, -5 on OOM, -6 on I/O error.
 */
static int engine_parse_file(gn_engine_t* eng, const char* tsv_path) {
    FILE* fp = fopen(tsv_path, "r");
    if (!fp) return -6;

    char line[2048];
    int parse_errors = 0;
    int total_lines  = 0;
    int skipped_long = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Skip blank lines */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;

        /* Check if line doesn't end with \n — means it was truncated */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] != '\n') {
            skipped_long++;
            /* Skip rest of this long line */
            int ch;
            while ((ch = fgetc(fp)) != '\n' && ch != EOF);
            continue;
        }

        /* Grow the cities array if necessary */
        if (eng->count >= eng->capacity) {
            int new_cap = eng->capacity * 2;
            GeoName* tmp = (GeoName*)realloc(
                eng->cities, (size_t)new_cap * sizeof(GeoName));
            if (!tmp) { fclose(fp); return -5; }
            eng->cities   = tmp;
            eng->capacity = new_cap;
        }

        if (parse_line(line, &eng->cities[eng->count]) == 0) {
            eng->count++;
        } else {
            parse_errors++;
        }
        total_lines++;
    }

    fclose(fp);

    /* If we got zero records from a non-empty file, it's a parse error. */
    if (eng->count == 0 && total_lines > 0) return -4;

    (void)parse_errors;  /* A few bad lines among millions are acceptable. */
    (void)skipped_long;
    return 0;
}

/* ========================================================================
 *  Source-tracking helpers
 * ======================================================================== */

/** @return 1 if this source (and optional country code) is already loaded. */
static int is_source_loaded(const gn_engine_t* eng, gn_source_type_t source,
                            const char* cc) {
    for (int i = 0; i < eng->loaded_count; i++) {
        if (eng->loaded[i].source != source) continue;
        if (source == GN_SRC_COUNTRY) {
            if (cc && strcmp(eng->loaded[i].country_code, cc) == 0)
                return 1;
        } else {
            return 1;
        }
    }
    return 0;
}

/** Record that a source has been successfully loaded. */
static void mark_source_loaded(gn_engine_t* eng, gn_source_type_t source,
                               const char* cc, int count) {
    if (eng->loaded_count >= eng->loaded_cap) {
        int nc = eng->loaded_cap ? eng->loaded_cap * 2 : 8;
        gn_loaded_source_t* tmp = (gn_loaded_source_t*)realloc(
            eng->loaded, (size_t)nc * sizeof(gn_loaded_source_t));
        if (!tmp) return;
        eng->loaded     = tmp;
        eng->loaded_cap = nc;
    }
    gn_loaded_source_t* ls = &eng->loaded[eng->loaded_count++];
    ls->source = source;
    ls->record_count = count;
    if (cc) {
        strncpy(ls->country_code, cc, sizeof(ls->country_code) - 1);
        ls->country_code[sizeof(ls->country_code) - 1] = '\0';
    } else {
        ls->country_code[0] = '\0';
    }
}

int gn_engine_has_source(const gn_engine_t* engine, gn_source_type_t source,
                         const char* country_code) {
    return is_source_loaded(engine, source, country_code);
}

/* ========================================================================
 *  Engine lifecycle — public API
 * ======================================================================== */

/* Forward declarations of error helpers (defined after gn_engine_get_data_dir). */
static void clear_error(gn_engine_t* eng);

gn_engine_t* gn_engine_create(void) {
    gn_engine_t* eng = (gn_engine_t*)calloc(1, sizeof(gn_engine_t));
    if (!eng) return NULL;

    /* City array */
    eng->capacity = GN_INITIAL_CAP;
    eng->count    = 0;
    eng->cities   = (GeoName*)malloc(
        (size_t)eng->capacity * sizeof(GeoName));
    if (!eng->cities) { free(eng); return NULL; }

    /* Result workspace */
    eng->result_cap   = 64;
    eng->result_count = 0;
    eng->result_indices = (int*)malloc(
        (size_t)eng->result_cap * sizeof(int));
    if (!eng->result_indices) {
        free(eng->cities); free(eng); return NULL;
    }

    /* Source tracking */
    eng->loaded_cap   = 8;
    eng->loaded_count = 0;
    eng->loaded = (gn_loaded_source_t*)calloc(
        (size_t)eng->loaded_cap, sizeof(gn_loaded_source_t));
    if (!eng->loaded) {
        free(eng->result_indices); free(eng->cities); free(eng);
        return NULL;
    }

    /* Default data directory: ~/.cgeonames/ */
    const char* home = get_home_dir();
    snprintf(eng->data_dir, sizeof(eng->data_dir),
             "%s" GN_SEP "%s", home, GN_DEFAULT_DATA_DIR);
    ensure_dir_recursive(eng->data_dir);

    /* Error state starts at NONE. */
    clear_error(eng);

    return eng;
}

void gn_engine_free(gn_engine_t* engine) {
    if (!engine) return;
    free(engine->cities);
    free(engine->loaded);
    free(engine->result_indices);
    free(engine);
}

int gn_engine_count(const gn_engine_t* engine) {
    return engine->count;
}

const char* gn_engine_get_data_dir(const gn_engine_t* engine) {
    return engine->data_dir;
}

/* ========================================================================
 *  Error handling — public API
 * ======================================================================== */

/**
 * @brief Internal helper: clear the last error on the engine.
 */
static void clear_error(gn_engine_t* eng) {
    if (!eng) return;
    eng->last_error.code = GN_ERR_NONE;
    eng->last_error.curl_code = 0;
    eng->last_error.http_code = 0;
    eng->last_error.message[0] = '\0';
    eng->last_error.url[0] = '\0';
}

/**
 * @brief Internal helper: set the last error on the engine.
 */
static void set_error(gn_engine_t* eng, gn_error_t code,
                      int curl_code, long http_code,
                      const char* msg, const char* url) {
    if (!eng) return;
    eng->last_error.code = code;
    eng->last_error.curl_code = curl_code;
    eng->last_error.http_code = http_code;
    if (msg) {
        strncpy(eng->last_error.message, msg,
                sizeof(eng->last_error.message) - 1);
        eng->last_error.message[sizeof(eng->last_error.message) - 1] = '\0';
    } else {
        eng->last_error.message[0] = '\0';
    }
    if (url) {
        strncpy(eng->last_error.url, url,
                sizeof(eng->last_error.url) - 1);
        eng->last_error.url[sizeof(eng->last_error.url) - 1] = '\0';
    } else {
        eng->last_error.url[0] = '\0';
    }
}

gn_error_t gn_engine_last_error(const gn_engine_t* engine) {
    if (!engine) return GN_ERR_INVALID_ARG;
    return engine->last_error.code;
}

static const char* error_to_string(gn_error_t code) {
    switch (code) {
        case GN_ERR_NONE:        return "No error";
        case GN_ERR_INVALID_ARG: return "Invalid argument (NULL pointer or bad enum)";
        case GN_ERR_DOWNLOAD:    return "Generic network/download error";
        case GN_ERR_UNZIP:       return "Failed to extract archive";
        case GN_ERR_PARSE:       return "Failed to parse TSV file";
        case GN_ERR_NOMEM:       return "Out of memory";
        case GN_ERR_IO:          return "Disk I/O error (full disk? permissions?)";
        case GN_ERR_HTTP_404:    return "HTTP 404: Resource not found";
        case GN_ERR_HTTP_403:    return "HTTP 403: Access forbidden";
        case GN_ERR_HTTP_500:    return "HTTP 5xx: Server error";
        case GN_ERR_TIMEOUT:     return "Download timed out";
        case GN_ERR_SSL:         return "SSL/TLS handshake failure";
        case GN_ERR_DNS:         return "DNS resolution failed";
        case GN_ERR_CONNECT:     return "Could not connect to server";
        case GN_ERR_PARTIAL:     return "Partial download (transfer interrupted)";
        case GN_ERR_UNKNOWN:     return "Unknown error";
        default:                 return "Unclassified error";
    }
}

const char* gn_engine_last_error_str(const gn_engine_t* engine) {
    if (!engine) return "Invalid engine handle";
    if (engine->last_error.message[0] != '\0') {
        return engine->last_error.message;
    }
    return error_to_string(engine->last_error.code);
}

int gn_engine_error_info(const gn_engine_t* engine, gn_error_info_t* out) {
    if (!engine || !out) return -1;
    *out = engine->last_error;
    return 0;
}

/* ========================================================================
 *  Download & Load — main entry point
 * ======================================================================== */

int gn_engine_download_and_load(
    gn_engine_t*           engine,
    gn_source_type_t       source,
    const char*            country_code,
    const char*            local_dir,
    gn_download_progress_t progress_cb,
    void*                  progress_ctx,
    int                    force_refresh
) {
    if (!engine) return -1;
    if (source <= GN_SRC_UNKNOWN || source == GN_SRC_LOCAL) return -1;
    if (source == GN_SRC_COUNTRY &&
        (!country_code || strlen(country_code) != 2)) return -1;

    /* Already loaded? (skip unless forced) */
    if (!force_refresh &&
        is_source_loaded(engine, source, country_code)) return 0;

    /* ---- Determine working directory ---- */
    char work_dir[512];
    if (local_dir && local_dir[0]) {
        strncpy(work_dir, local_dir, sizeof(work_dir) - 1);
        work_dir[sizeof(work_dir) - 1] = '\0';
    } else {
        strncpy(work_dir, engine->data_dir, sizeof(work_dir));
    }
    if (ensure_dir_recursive(work_dir) != 0) return -6;

    /* ---- Build URL and local filenames ---- */
    const char* url  = gn_source_url(source, country_code);
    const char* tsv  = gn_source_filename(source, country_code);
    if (!url || !tsv) return -1;

    char zip_path[512];
    char tsv_path[512];

    if (source == GN_SRC_COUNTRY && country_code) {
        path_join(zip_path, sizeof(zip_path), work_dir, country_code);
        /* Append .zip extension */
        size_t zl = strlen(zip_path);
        if (zl + 4 < sizeof(zip_path)) {
            strcat(zip_path, ".zip");
        }
    } else {
        /* zip_path = work_dir/<basename>.zip  (replace .txt with .zip) */
        path_join(zip_path, sizeof(zip_path), work_dir, tsv);
        size_t zl = strlen(zip_path);
        if (zl > 4 && strcmp(zip_path + zl - 4, ".txt") == 0) {
            strcpy(zip_path + zl - 4, ".zip");
        }
    }
    path_join(tsv_path, sizeof(tsv_path), work_dir, tsv);

    /* ---- Download the .zip if missing or forced ---- */
    if (force_refresh || !file_exists(zip_path)) {
        int ret = download_file(url, zip_path, progress_cb, progress_ctx,
                                engine);
        if (ret != 0) return ret;
    }

    /* ---- Validate the downloaded .zip (PK signature, minimum size) ---- */
    {
        int ret = validate_zip(zip_path, engine);
        if (ret != 0) return ret;
    }

    /* ---- Unzip if the .txt is missing ---- */
    if (!file_exists(tsv_path)) {
        int ret = unzip_file(zip_path, work_dir);
        if (ret != 0) {
            if (engine) {
                engine->last_error.code = GN_ERR_UNZIP;
                engine->last_error.curl_code = 0;
                engine->last_error.http_code = 0;
                snprintf(engine->last_error.message,
                         sizeof(engine->last_error.message),
                         "Failed to extract %s (corrupt zip or missing 'unzip' command)",
                         zip_path);
                snprintf(engine->last_error.url,
                         sizeof(engine->last_error.url), "%s", zip_path);
            }
            return ret;
        }
    }

    /* ---- Validate the extracted TSV (format, field count) ---- */
    {
        int ret = validate_tsv(tsv_path, engine);
        if (ret != 0) return ret;
    }

    /* ---- Parse the TSV into the engine ---- */
    int before = engine->count;
    int ret = engine_parse_file(engine, tsv_path);
    if (ret != 0) {
        if (engine) {
            engine->last_error.code = (ret == -5) ? GN_ERR_NOMEM : GN_ERR_PARSE;
            engine->last_error.curl_code = 0;
            engine->last_error.http_code = 0;
            snprintf(engine->last_error.message,
                     sizeof(engine->last_error.message),
                     "Failed to parse %s (%s)",
                     tsv_path,
                     (ret == -5) ? "out of memory" : "malformed TSV or zero records");
            snprintf(engine->last_error.url,
                     sizeof(engine->last_error.url), "%s", tsv_path);
        }
        return ret;
    }

    mark_source_loaded(engine, source, country_code,
                       engine->count - before);

    /* Clear any previous errors on success. */
    clear_error(engine);
    return 0;
}

int gn_engine_load_file(gn_engine_t* engine, const char* path) {
    if (!engine || !path) return -1;
    int before = engine->count;
    int ret = engine_parse_file(engine, path);
    if (ret == 0) {
        mark_source_loaded(engine, GN_SRC_LOCAL, NULL,
                           engine->count - before);
    }
    return ret;
}

int gn_engine_load_memory(gn_engine_t* engine, const char* data, int size) {
    if (!engine || !data || size <= 0) return -1;

    /* Write to a temporary file in the cache directory, then parse. */
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s" GN_SEP "_gn_tmp_%d.txt",
             engine->data_dir, rand());

    FILE* fp = fopen(tmp_path, "w");
    if (!fp) return -6;
    fwrite(data, 1, (size_t)size, fp);
    fclose(fp);

    int ret = gn_engine_load_file(engine, tmp_path);
    remove(tmp_path);  /* Clean up regardless of success/failure. */
    return ret;
}

/* ========================================================================
 *  Fuzzy search
 * ======================================================================== */

/**
 * @brief Score a single city record against the lowercased query.
 *
 * Scoring tiers:
 *   - Exact match:       1000 + log(population + 1)
 *   - Prefix match:       500 + log(population + 1)
 *   - Substring contains: 200 + log(population + 1)
 *   - Levenshtein ≤ 2:     50 - 15 * distance + log(population + 1)
 *
 * @return The best (highest) score across name, asciiname, and all aliases.
 */
static float score_record(const GeoName* city, const char* query_lower) {
    float best = 0.0f;

    /* Collect all candidate name pointers. */
    const char* candidates[GN_MAX_ALIASES + 2];
    int nc = 0;
    candidates[nc++] = city->name;
    if (city->asciiname[0]) candidates[nc++] = city->asciiname;
    for (int i = 0; i < city->alias_count; i++) {
        candidates[nc++] = city->aliases[i];
    }

    char low[GN_NAME_MAX];
    int  qlen = (int)strlen(query_lower);

    for (int i = 0; i < nc; i++) {
        str_to_lower(low, candidates[i], GN_NAME_MAX);

        /* --- Tier 1: Exact match --- */
        if (strcmp(low, query_lower) == 0) {
            return 1000.0f + logf((float)(city->population + 1));
        }

        /* --- Tier 2: Prefix match --- */
        if (strncmp(low, query_lower, (size_t)qlen) == 0) {
            float s = 500.0f + logf((float)(city->population + 1));
            if (s > best) best = s;
            continue;
        }

        /* --- Tier 3: Substring containment --- */
        if (stristr(candidates[i], query_lower)) {
            float s = 200.0f + logf((float)(city->population + 1));
            if (s > best) best = s;
            continue;
        }

        /* --- Tier 4: Levenshtein (only for short queries — fast enough) --- */
        if (qlen <= 8) {
            int dist = gn_levenshtein(low, query_lower);
            if (dist >= 0 && dist <= 2) {
                float s = 50.0f - (float)dist * 15.0f
                          + logf((float)(city->population + 1));
                if (s > best) best = s;
            }
        }
    }

    return best;
}

/**
 * @brief Insert an index into a sorted sub-array (insertion sort step).
 *
 * Both @p indices and @p scores are kept in parallel, sorted by score
 * descending.
 */
static void insert_sorted(int* indices, float* scores, int count,
                          int new_idx, float new_score) {
    int pos = count;
    while (pos > 0 && scores[pos - 1] < new_score) {
        indices[pos] = indices[pos - 1];
        scores[pos]  = scores[pos - 1];
        pos--;
    }
    indices[pos] = new_idx;
    scores[pos]  = new_score;
}

int gn_search(const gn_engine_t* engine, const char* query,
              const gn_search_opts_t* opts) {
    if (!engine || !query || query[0] == '\0') return 0;

    /* Resolve options with sensible defaults. */
    int          limit   = (opts && opts->limit > 0) ? opts->limit : 20;
    int          exact   = (opts) ? opts->exact_first : 0;
    const char*  cc_f    = (opts) ? opts->country_code  : NULL;
    const char*  a1_f    = (opts) ? opts->admin1_code   : NULL;
    int          min_pop = (opts) ? opts->min_population : 0;

    /* Ensure the result workspace is large enough. */
    gn_engine_t* eng = (gn_engine_t*)(const void*)engine;
    if (eng->result_cap < limit) {
        eng->result_cap = limit * 2;
        int* ri = (int*)realloc(eng->result_indices,
                                (size_t)eng->result_cap * sizeof(int));
        if (!ri) return 0;
        eng->result_indices = ri;
    }

    /* Parallel scores array for sorting. */
    float* scores = (float*)malloc((size_t)limit * sizeof(float));
    if (!scores) return 0;

    eng->result_count = 0;

    char query_lower[256];
    str_to_lower(query_lower, query, sizeof(query_lower));

    /* ---- Pass 1: exact matches only (if exact_first) ---- */
    if (exact) {
        for (int i = 0;
             i < engine->count && eng->result_count < limit; i++) {
            const GeoName* c = &engine->cities[i];
            if (min_pop > 0 && c->population < min_pop) continue;
            if (cc_f && strcasecmp(c->country_code, cc_f) != 0) continue;
            if (a1_f && strcasecmp(c->admin1_code, a1_f) != 0) continue;

            float sc = score_record(c, query_lower);
            if (sc >= 1000.0f) {  /* exact match threshold */
                eng->cities[i].relevance = sc;
                insert_sorted(eng->result_indices, scores,
                              eng->result_count, i, sc);
                eng->result_count++;
            }
        }
        if (eng->result_count > 0) { free(scores); return eng->result_count; }
    }

    /* ---- Pass 2: scan ALL records, keep top N by score ---- */
    float min_score = 0.0f;

    for (int i = 0; i < engine->count; i++) {
        const GeoName* c = &engine->cities[i];
        if (min_pop > 0 && c->population < min_pop) continue;
        if (cc_f && strcasecmp(c->country_code, cc_f) != 0) continue;
        if (a1_f && strcasecmp(c->admin1_code, a1_f) != 0) continue;

        float sc = score_record(c, query_lower);
        if (sc <= min_score) continue;

        if (eng->result_count < limit) {
            eng->cities[i].relevance = sc;
            insert_sorted(eng->result_indices, scores,
                          eng->result_count, i, sc);
            eng->result_count++;
            if (eng->result_count >= limit) {
                min_score = scores[limit - 1];
            }
        } else {
            eng->result_count--;
            eng->cities[i].relevance = sc;
            insert_sorted(eng->result_indices, scores,
                          eng->result_count, i, sc);
            eng->result_count++;
            min_score = scores[limit - 1];
        }
    }

    free(scores);
    return eng->result_count;
}

const GeoName* gn_result_at(const gn_engine_t* engine, int index) {
    if (!engine || index < 0 || index >= engine->result_count) return NULL;
    return &engine->cities[engine->result_indices[index]];
}

/* ========================================================================
 *  Helpers — Haversine distance
 * ======================================================================== */

/**
 * @brief Convert degrees to radians.
 */
static double deg2rad(double deg) {
    return deg * (M_PI / 180.0);
}

/**
 * @brief Calculate great-circle distance using the Haversine formula.
 * @return Distance in kilometres.
 */
static double haversine_km(double lat1, double lon1,
                           double lat2, double lon2) {
    double dlat = deg2rad(lat2 - lat1);
    double dlon = deg2rad(lon2 - lon1);
    double a = sin(dlat / 2.0) * sin(dlat / 2.0) +
               cos(deg2rad(lat1)) * cos(deg2rad(lat2)) *
               sin(dlon / 2.0) * sin(dlon / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return 6371.0 * c;  /* Earth radius ≈ 6371 km */
}

/* ========================================================================
 *  Helpers — field-filtered TSV parser
 * ======================================================================== */

/**
 * @brief Parse one GeoNames TSV line, but only populate fields
 *        specified by the @p fields bitmask.
 *
 * This is a lightweight version of parse_line() that skips unwanted
 * fields to save parsing time and keep memory cleaner.
 *
 * @param line   One line from the TSV file.
 * @param out    Destination struct to fill (partially, per mask).
 * @param fields Bitmask of GN_FIELD_* flags. GN_FIELD_ALL = everything.
 * @return 0 on success, -1 if the line has fewer than 19 columns.
 */
static int parse_line_with_fields(const char* line, GeoName* out,
                                  uint32_t fields) {
    /* Zero-initialise so empty fields are safe. */
    memset(out, 0, sizeof(*out));
    out->elevation = -1;
    out->dem       = -1;

    /* If ALL fields requested, use the full parser. */
    if (fields == GN_FIELD_ALL) {
        return parse_line(line, out);
    }

    /* Work on a writable copy. */
    char buf[2048];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* We always need at least the first few fields to find tabs.
     * Split on tabs — max 19 fields (indices 0..18). */
    char* fields_arr[19];
    int fc = 0;
    fields_arr[0] = buf;
    for (char* p = buf; *p && fc < 18; p++) {
        if (*p == '\t') {
            *p = '\0';
            fc++;
            fields_arr[fc] = p + 1;
        }
    }
    if (fc < 18) return -1;

    /* --- Conditional field population --- */

    if (fields & GN_FIELD_GEONAME_ID) {
        out->geoname_id = atoi(fields_arr[0]);
    }

    if (fields & GN_FIELD_NAME) {
        strncpy(out->name, fields_arr[1], GN_NAME_MAX - 1);
        out->name[GN_NAME_MAX - 1] = '\0';
    }

    if (fields & GN_FIELD_ASCINAME) {
        strncpy(out->asciiname, fields_arr[2], GN_NAME_MAX - 1);
        out->asciiname[GN_NAME_MAX - 1] = '\0';
    }

    if (fields & GN_FIELD_ALIASES) {
        out->alias_count = 0;
        char alias_buf[1024];
        strncpy(alias_buf, fields_arr[3], sizeof(alias_buf) - 1);
        alias_buf[sizeof(alias_buf) - 1] = '\0';

        char* save = NULL;
        char* token = strtok_r(alias_buf, ",", &save);
        while (token && out->alias_count < GN_MAX_ALIASES) {
            while (*token == ' ') token++;
            char* end = token + strlen(token) - 1;
            while (end > token &&
                   (*end == ' ' || *end == '\r' || *end == '\n')) {
                *end = '\0';
                end--;
            }
            if (token[0]) {
                strncpy(out->aliases[out->alias_count], token,
                        GN_NAME_MAX - 1);
                out->aliases[out->alias_count][GN_NAME_MAX - 1] = '\0';
                out->alias_count++;
            }
            token = strtok_r(NULL, ",", &save);
        }
    }

    if (fields & GN_FIELD_COORDINATES) {
        out->latitude  = atof(fields_arr[4]);
        out->longitude = atof(fields_arr[5]);
    }

    if (fields & GN_FIELD_FEATURE) {
        out->feature_class = fields_arr[6][0];
        strncpy(out->feature_code, fields_arr[7],
                sizeof(out->feature_code) - 1);
        out->feature_code[sizeof(out->feature_code) - 1] = '\0';
    }

    if (fields & GN_FIELD_COUNTRY) {
        strncpy(out->country_code, fields_arr[8],
                sizeof(out->country_code) - 1);
        out->country_code[sizeof(out->country_code) - 1] = '\0';
        strncpy(out->cc2, fields_arr[9], sizeof(out->cc2) - 1);
        out->cc2[sizeof(out->cc2) - 1] = '\0';
    }

    if (fields & GN_FIELD_ADMIN_CODES) {
        strncpy(out->admin1_code, fields_arr[10],
                sizeof(out->admin1_code) - 1);
        out->admin1_code[sizeof(out->admin1_code) - 1] = '\0';
        strncpy(out->admin2_code, fields_arr[11],
                sizeof(out->admin2_code) - 1);
        out->admin2_code[sizeof(out->admin2_code) - 1] = '\0';
        strncpy(out->admin3_code, fields_arr[12],
                sizeof(out->admin3_code) - 1);
        out->admin3_code[sizeof(out->admin3_code) - 1] = '\0';
        strncpy(out->admin4_code, fields_arr[13],
                sizeof(out->admin4_code) - 1);
        out->admin4_code[sizeof(out->admin4_code) - 1] = '\0';
    }

    if (fields & GN_FIELD_POPULATION) {
        out->population = atoi(fields_arr[14]);
    }

    if (fields & GN_FIELD_ELEVATION) {
        if (fields_arr[15][0] != '\0')
            out->elevation = (short)atoi(fields_arr[15]);
        if (fields_arr[16][0] != '\0')
            out->dem = (short)atoi(fields_arr[16]);
    }

    if (fields & GN_FIELD_TIMEZONE) {
        strncpy(out->timezone, fields_arr[17], GN_TZ_MAX - 1);
        out->timezone[GN_TZ_MAX - 1] = '\0';
    }

    if (fields & GN_FIELD_MOD_DATE) {
        strncpy(out->modification_date, fields_arr[18],
                sizeof(out->modification_date) - 1);
        out->modification_date[sizeof(out->modification_date) - 1] = '\0';

        char* end = out->modification_date +
                    strlen(out->modification_date) - 1;
        while (end >= out->modification_date &&
               (*end == '\r' || *end == '\n' || *end == ' ')) {
            *end = '\0';
            end--;
        }
    }

    return 0;
}

/* ========================================================================
 *  Extended search with field filtering
 * ======================================================================== */

/**
 * @brief Score a single city record against the lowercased query
 *        (for gn_search_ex — uses the same scoring as score_record).
 */
static float score_record_ex(const GeoName* city, const char* query_lower) {
    float best = 0.0f;

    const char* candidates[GN_MAX_ALIASES + 2];
    int nc = 0;
    if (city->name[0]) candidates[nc++] = city->name;
    if (city->asciiname[0]) candidates[nc++] = city->asciiname;
    for (int i = 0; i < city->alias_count; i++) {
        candidates[nc++] = city->aliases[i];
    }

    char low[GN_NAME_MAX];
    int  qlen = (int)strlen(query_lower);

    for (int i = 0; i < nc; i++) {
        str_to_lower(low, candidates[i], GN_NAME_MAX);

        if (strcmp(low, query_lower) == 0) {
            return 1000.0f + logf((float)(city->population + 1));
        }

        if (strncmp(low, query_lower, (size_t)qlen) == 0) {
            float s = 500.0f + logf((float)(city->population + 1));
            if (s > best) best = s;
            continue;
        }

        if (stristr(candidates[i], query_lower)) {
            float s = 200.0f + logf((float)(city->population + 1));
            if (s > best) best = s;
            continue;
        }

        if (qlen <= 8) {
            int dist = gn_levenshtein(low, query_lower);
            if (dist >= 0 && dist <= 2) {
                float s = 50.0f - (float)dist * 15.0f
                          + logf((float)(city->population + 1));
                if (s > best) best = s;
            }
        }
    }

    return best;
}

/**
 * @brief Internal core search that both gn_search() and gn_search_ex() use.
 */
static int search_core(const gn_engine_t* engine, const char* query,
                       const gn_search_opts_ex_t* opts_ex,
                       const gn_search_opts_t* opts_old) {
    if (!engine || !query || query[0] == '\0') return 0;

    /* Resolve options — prefer opts_ex if provided. */
    int          limit   = 20;
    int          exact   = 0;
    const char*  cc_f    = NULL;
    const char*  a1_f    = NULL;
    int          min_pop = 0;
    uint32_t     fields  = GN_FIELD_ALL;

    if (opts_ex) {
        limit   = (opts_ex->limit > 0) ? opts_ex->limit : 20;
        exact   = opts_ex->exact_first;
        cc_f    = opts_ex->country_code;
        a1_f    = opts_ex->admin1_code;
        min_pop = opts_ex->min_population;
        fields  = opts_ex->fields;
    } else if (opts_old) {
        limit   = (opts_old->limit > 0) ? opts_old->limit : 20;
        exact   = opts_old->exact_first;
        cc_f    = opts_old->country_code;
        a1_f    = opts_old->admin1_code;
        min_pop = opts_old->min_population;
    }

    /* Ensure the result workspace is large enough. */
    gn_engine_t* eng = (gn_engine_t*)(const void*)engine;
    if (eng->result_cap < limit) {
        eng->result_cap = limit * 2;
        int* ri = (int*)realloc(eng->result_indices,
                                (size_t)eng->result_cap * sizeof(int));
        if (!ri) return 0;
        eng->result_indices = ri;
    }

    float* scores = (float*)malloc((size_t)limit * sizeof(float));
    if (!scores) return 0;

    eng->result_count = 0;

    char query_lower[256];
    str_to_lower(query_lower, query, sizeof(query_lower));

    /* ---- Pass 1: exact matches only (if exact_first) ---- */
    if (exact) {
        for (int i = 0;
             i < engine->count && eng->result_count < limit; i++) {
            const GeoName* c = &engine->cities[i];
            if (min_pop > 0 && c->population < min_pop) continue;
            if (cc_f && strcasecmp(c->country_code, cc_f) != 0) continue;
            if (a1_f && strcasecmp(c->admin1_code, a1_f) != 0) continue;

            float sc = score_record_ex(c, query_lower);
            if (sc >= 1000.0f) {
                if (fields & GN_FIELD_RELEVANCE) {
                    eng->cities[i].relevance = sc;
                }
                insert_sorted(eng->result_indices, scores,
                              eng->result_count, i, sc);
                eng->result_count++;
            }
        }
        if (eng->result_count > 0) { free(scores); return eng->result_count; }
    }

    /* ---- Pass 2: scan ALL records, keep top N by score ---- */
    /* Use a min-heap approach: keep the best 'limit' results. */
    float  min_score = 0.0f;

    for (int i = 0; i < engine->count; i++) {
        const GeoName* c = &engine->cities[i];
        if (min_pop > 0 && c->population < min_pop) continue;
        if (cc_f && strcasecmp(c->country_code, cc_f) != 0) continue;
        if (a1_f && strcasecmp(c->admin1_code, a1_f) != 0) continue;

        float sc = score_record_ex(c, query_lower);
        if (sc <= min_score) continue;

        if (eng->result_count < limit) {
            /* Still filling — just insert. */
            if (fields & GN_FIELD_RELEVANCE) {
                eng->cities[i].relevance = sc;
            }
            insert_sorted(eng->result_indices, scores,
                          eng->result_count, i, sc);
            eng->result_count++;
            /* Update minimum score threshold. */
            if (eng->result_count >= limit) {
                min_score = scores[limit - 1];
            }
        } else {
            /* Array is full — replace the worst if new score is better. */
            /* Remove the last (lowest) entry and insert new one. */
            eng->result_count--;
            if (fields & GN_FIELD_RELEVANCE) {
                eng->cities[i].relevance = sc;
            }
            insert_sorted(eng->result_indices, scores,
                          eng->result_count, i, sc);
            eng->result_count++;
            min_score = scores[limit - 1];
        }
    }

    free(scores);
    return eng->result_count;
}

int gn_search_ex(const gn_engine_t* engine, const char* query,
                 const gn_search_opts_ex_t* opts) {
    return search_core(engine, query, opts, NULL);
}

/* ========================================================================
 *  Reverse geocoding — search by coordinates
 * ======================================================================== */

/**
 * @brief Insert an index into a sorted sub-array sorted by distance
 *        ascending (lower distance = better).
 */
static void insert_sorted_by_dist(int* indices, float* dists, int count,
                                  int new_idx, float new_dist) {
    int pos = count;
    while (pos > 0 && dists[pos - 1] > new_dist) {
        indices[pos] = indices[pos - 1];
        dists[pos]   = dists[pos - 1];
        pos--;
    }
    indices[pos] = new_idx;
    dists[pos]   = new_dist;
}

int gn_search_nearby(const gn_engine_t* engine,
                     double latitude, double longitude,
                     const gn_search_opts_ex_t* opts) {
    if (!engine) return 0;

    /* Resolve options with defaults. */
    gn_search_opts_ex_t defaults = GN_SEARCH_OPTS_EX_INIT;
    if (!opts) opts = &defaults;

    int          limit   = (opts->limit > 0) ? opts->limit : 10;
    double       radius  = (opts->radius_km > 0) ? opts->radius_km : 50.0;
    char         fc_filter = opts->feature_class_filter;
    uint32_t     fields  = opts->fields;

    /* Ensure the result workspace is large enough. */
    gn_engine_t* eng = (gn_engine_t*)(const void*)engine;
    if (eng->result_cap < limit) {
        eng->result_cap = limit * 2;
        int* ri = (int*)realloc(eng->result_indices,
                                (size_t)eng->result_cap * sizeof(int));
        if (!ri) return 0;
        eng->result_indices = ri;
    }

    float* dists = (float*)malloc((size_t)limit * sizeof(float));
    if (!dists) return 0;

    eng->result_count = 0;

    /* ---- Scan all loaded cities, find those within radius ---- */
    for (int i = 0; i < engine->count; i++) {
        const GeoName* c = &engine->cities[i];

        /* Skip if coordinates are invalid (zero = unknown). */
        if (c->latitude == 0.0 && c->longitude == 0.0) continue;

        /* Feature class filter. */
        if (fc_filter && c->feature_class != fc_filter) continue;

        double dist = haversine_km(latitude, longitude,
                                   c->latitude, c->longitude);
        if (dist > radius) continue;

        /* Store result: relevance = distance (lower = better). */
        if (fields & GN_FIELD_RELEVANCE) {
            eng->cities[i].relevance = (float)dist;
        }
        if (fields & GN_FIELD_COORDINATES) {
            /* Already populated from loaded data. */
        }

        insert_sorted_by_dist(eng->result_indices, dists,
                              eng->result_count, i, (float)dist);
        eng->result_count++;

        if (eng->result_count >= limit) break;
    }

    free(dists);
    return eng->result_count;
}

const GeoName* gn_reverse_geocode(const gn_engine_t* engine,
                                   double latitude, double longitude,
                                   double radius_km) {
    gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
    opts.limit = 1;
    opts.radius_km = (radius_km > 0) ? radius_km : 50.0;
    opts.feature_class_filter = 'P';  /* Populated places by default. */

    int n = gn_search_nearby(engine, latitude, longitude, &opts);
    if (n > 0) {
        return gn_result_at(engine, 0);
    }
    return NULL;
}

/* ========================================================================
 *  Version string
 * ======================================================================== */

static char g_ver[32];

const char* gn_version(void) {
    snprintf(g_ver, sizeof(g_ver), "%d.%d.%d",
             CGEONAMES_VERSION_MAJOR,
             CGEONAMES_VERSION_MINOR,
             CGEONAMES_VERSION_PATCH);
    return g_ver;
}
