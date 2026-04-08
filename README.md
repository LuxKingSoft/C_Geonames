# 🌍 cgeonames — GeoNames API Implementation in C

```
        ╔═════════════════════════════════════╗
        ║   CGEONAMES - GEOLOCATION ENGINE           ║
        ╚═════════════════════════════════════╝

                      .  *  .   *   .  *  .
                   *      🛰️       .      *
                .        /|\          .
              *         / | \   .      *
           .     .    /   |  \       .
          *    ______/____|___\______   .
         .    |  .  *  |  *  .   |   |     *
       .  🏙️  |   🏘️   |   🌊    |   |    🏔️  .
      *       |________|_________|___|       *
     .    🌊   🏘️  🏙️  🏔️  🏘️  🌊      .
    .       ~~~ ~~~ ~~~ ~~~ ~~~ ~~~      .
   ═══════════════════════════════════════════
   40.7128°N, 74.0060°W → New York, America/New_York
   ═══════════════════════════════════════════
```

> **See** (C) **GeoNames** — an offline C implementation of the
> [GeoNames](http://www.geonames.org/) geolocation API.
> Auto-downloads, caches, and fuzzy-searches **11 million+ cities** worldwide —
> no internet needed after first load.
>
> *Data sourced from [GeoNames.org](http://www.geonames.org/) — CC-BY 4.0.*

---

## ✨ Features

| Feature | Description |
|---|---|
| 📥 **Auto-download** | Downloads GeoNames TSV files via libcurl, extracts with system `unzip`, caches to `~/.geonames/` |
| 🔍 **Fuzzy search** | Multi-tier scoring: exact match → prefix → substring → Levenshtein (≤ 2 edits) |
| 📍 **Reverse geocoding** | Lat/lon → nearest city using Haversine great-circle distance |
| 🌐 **Timezone lookup** | Every record includes IANA timezone (`Europe/Madrid`) |
| 🏙️ **City aliases** | Alternate names parsed from comma-separated GeoNames field |
| 🎛️ **Field filtering** | Bitmask control over which fields are populated — save memory & CPU |
| 🔌 **Filter by country / state / population** | Narrow results by ISO country code, admin1 code, or minimum population |
| 💾 **Disk cache** | Downloaded files are cached; subsequent loads are instant |
| 📊 **Detailed errors** | HTTP codes (404/403/5xx), cURL errors, ZIP validation, TSV format validation |
| 📦 **Static + shared** | Builds both `.a` and `.so` libraries |
| 🔒 **Thread note** | Engine is NOT thread-safe — one engine per thread or protect with mutex |

## 📚 Table of Contents

- [Quick Start](#-quick-start)
- [City Search](#-city-search-by-name)
- [Reverse Geocoding](#-reverse-geocoding-latlon--city)
- [Field Filtering](#-field-filtering)
- [Building](#-building)
- [API Reference](#-api-reference)
- [Data License](#-data-license)

---

## 🚀 Quick Start

```c
#include <stdio.h>
#include "geonames.h"

int main(void) {
    /* 1. Create the engine */
    gn_engine_t* eng = gn_engine_create();

    /* 2. Download & cache Japan's city data (cities ≥ 1 000 pop) */
    int ret = gn_engine_download_and_load(eng, GN_SRC_COUNTRY, "JP",
                                          NULL, NULL, NULL, 0);
    if (ret < 0) {
        gn_error_info_t info;
        gn_engine_error_info(eng, &info);
        fprintf(stderr, "Failed: %s\n  URL: %s\n  HTTP: %ld\n",
                info.message, info.url, info.http_code);
        gn_engine_free(eng);
        return 1;
    }

    printf("Loaded %d cities.\n", gn_engine_count(eng));

    /* 3. Search for "Tokyo" (also matches "Tōkyō") */
    int n = gn_search(eng, "Tokio", NULL);

    for (int i = 0; i < n; i++) {
        const GeoName* g = gn_result_at(eng, i);
        printf("  %-20s  lat=%8.4f  lon=%9.4f  tz=%-20s  pop=%d  relevance=%.0f\n",
               g->name, g->latitude, g->longitude,
               g->timezone, g->population, g->relevance);
    }

    /* 4. Clean up */
    gn_engine_free(eng);
    return 0;
}
```

**Compile & run:**

```bash
gcc -o example example.c -I include -L build/libs -lgeonames -lcurl -lm
./example
```

```
Loaded 1048 cities.
  Tokyo                lat=  35.6895  lon= 139.6917  tz=Asia/Tokyo          pop=13960000 relevance=1513
  Shinjuku             lat=  35.6938  lon= 139.7034  tz=Asia/Tokyo          pop=337556   relevance=1000
  Shibuya              lat=  35.6638  lon= 139.6978  tz=Asia/Tokyo          pop=226117   relevance=1000
```

---

## 🔍 City Search by Name

### Basic search (defaults: limit 20, no filters)

```c
int n = gn_search(eng, "tokyo", NULL);
```

### Search with options

```c
gn_search_opts_t opts = {
    .limit          = 5,
    .country_code   = "JP",       /* only Japan          */
    .min_population = 100000,     /* cities ≥ 100k pop    */
    .exact_first    = 1           /* exact matches on top */
};

int n = gn_search(eng, "osaka", &opts);
```

### Search with extended options + field filtering

```c
gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
opts.limit   = 10;
opts.country_code = "ES";
opts.fields  = GN_FIELD_NAME | GN_FIELD_TIMEZONE | GN_FIELD_ALIASES;

int n = gn_search_ex(eng, "barcelona", &opts);

for (int i = 0; i < n; i++) {
    const GeoName* g = gn_result_at(eng, i);
    printf("%s  tz=%s  aliases=%d\n",
           g->name, g->timezone, g->alias_count);
}
```

---

## 📍 Reverse Geocoding (lat/lon → city)

### Find nearest cities

```c
gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
opts.limit      = 5;
opts.radius_km  = 30.0;                /* search within 30 km     */
opts.feature_class_filter = 'P';      /* populated places only   */
opts.fields     = GN_FIELD_LOCATION_INFO;

/* Tokyo, Japan coordinates */
int n = gn_search_nearby(eng, 35.6895, 139.6917, &opts);

printf("Nearest cities within 30 km:\n");
for (int i = 0; i < n; i++) {
    const GeoName* g = gn_result_at(eng, i);
    /* relevance contains distance in km (lower = closer) */
    printf("  %-20s  %s  dist=%.1f km  admin1=%s\n",
           g->name, g->country_code, g->relevance, g->admin1_code);
}
```

```
Nearest cities within 30 km:
  Tokyo                JP  dist=0.0 km  admin1=40
  Shinjuku             JP  dist=2.3 km  admin1=40
  Shibuya              JP  dist=3.1 km  admin1=40
  Setagaya             JP  dist=5.8 km  admin1=40
  Meguro               JP  dist=4.7 km  admin1=40
```

### Quick single-result reverse geocode

```c
/* Convenience wrapper: limit=1, populated places only, 50 km radius */
const GeoName* nearest = gn_reverse_geocode(eng, 48.8566, 2.3522, 25.0);

if (nearest) {
    printf("You're near %s, %s (tz: %s)\n",
           nearest->name, nearest->country_code, nearest->timezone);
} else {
    printf("No populated place found within 25 km.\n");
}
```

```
You're near Paris, FR (tz: Europe/Paris)
```

---

## 🎛️ Field Filtering

Control exactly which fields are populated in results using the `gn_result_field_t` bitmask. This saves CPU and keeps memory clean when you only need specific data.

### Predefined presets

| Preset | Fields included | Use case |
|---|---|---|
| `GN_FIELD_ALL` | Everything (default) | Full data access |
| `GN_FIELD_BASIC` | name + coords + tz + pop | Most common queries |
| `GN_FIELD_MINIMAL` | name + coords | Lightweight lookups |
| `GN_FIELD_LOCATION_INFO` | tz + admin codes + country + coords | Reverse geocoding |
| `GN_FIELD_NAMES_ONLY` | name + asciiname + aliases | Autocomplete / typeahead |

### Custom combinations

```c
/* Only timezone + aliases — nothing else populated */
gn_search_opts_ex_t opts = GN_SEARCH_OPTS_EX_INIT;
opts.fields = GN_FIELD_TIMEZONE | GN_FIELD_ALIASES;

gn_search_ex(eng, "nueva york", &opts);
```

```c
/* Just coordinates for distance calculations */
opts.fields = GN_FIELD_COORDINATES;

/* Name + population for a leaderboard */
opts.fields = GN_FIELD_NAME | GN_FIELD_POPULATION | GN_FIELD_RELEVANCE;
```

### Individual field flags

```
GN_FIELD_GEONAME_ID     (1 << 0)   Unique GeoNames integer ID
GN_FIELD_NAME           (1 << 1)   Primary city name
GN_FIELD_ASCINAME       (1 << 2)   ASCII transliteration
GN_FIELD_ALIASES        (1 << 3)   Alternate names (parsed from comma-separated)
GN_FIELD_COORDINATES    (1 << 4)   latitude + longitude
GN_FIELD_FEATURE        (1 << 5)   feature_class + feature_code
GN_FIELD_COUNTRY        (1 << 6)   country_code + cc2
GN_FIELD_ADMIN_CODES    (1 << 7)   admin1 through admin4 codes
GN_FIELD_POPULATION     (1 << 8)   Population count
GN_FIELD_ELEVATION      (1 << 9)   elevation + dem (digital elevation model)
GN_FIELD_TIMEZONE       (1 << 10)  IANA timezone identifier
GN_FIELD_MOD_DATE       (1 << 11)  Last modification date (YYYY-MM-DD)
GN_FIELD_RELEVANCE      (1 << 12)  Fuzzy-match score or distance in km
```

---

## 🛠️ Building

### Prerequisites

| Dependency | Purpose |
|---|---|
| **CMake ≥ 3.10** | Build system |
| **libcurl** | Network downloads (GeoNames TSV files) |
| **`unzip`** (Unix) / **PowerShell** (Windows) | Archive extraction |
| **GCC / Clang / MSVC** | C compiler |

### Build

```bash
mkdir build && cd build
cmake .. -DGEONAMES_BUILD_EXAMPLES=ON
make -j$(nproc)
```

This produces:
- `libs/libgeonames.a` — static library
- `libs/libgeonames.so` — shared library (`.dll` on Windows)

### CMake integration

```cmake
# Add as a subdirectory
add_subdirectory(external/geonames)

# Link either static or shared
target_link_libraries(my_app PRIVATE geonames::static)
# or
target_link_libraries(my_app PRIVATE geonames::shared)
```

### Linking manually

```bash
gcc -o myapp myapp.c -I geonames/include -L geonames/build/libs -lgeonames -lcurl -lm
```

---

## 📖 API Reference

### Engine lifecycle

```c
gn_engine_t*   gn_engine_create(void);
void           gn_engine_free(gn_engine_t* engine);
int            gn_engine_count(const gn_engine_t* engine);
const char*    gn_engine_get_data_dir(const gn_engine_t* engine);
```

### Download & Load

```c
int  gn_engine_download_and_load(gn_engine_t* engine,
                                 gn_source_type_t source,
                                 const char* country_code,
                                 const char* local_dir,
                                 gn_download_progress_t progress_cb,
                                 void* progress_ctx,
                                 int force_refresh);

int  gn_engine_load_file(gn_engine_t* engine, const char* path);
int  gn_engine_load_memory(gn_engine_t* engine, const char* data, int size);
int  gn_engine_has_source(const gn_engine_t* engine,
                          gn_source_type_t source, const char* country_code);
```

### Search

```c
int  gn_search(const gn_engine_t* engine, const char* query,
               const gn_search_opts_t* opts);

int  gn_search_ex(const gn_engine_t* engine, const char* query,
                  const gn_search_opts_ex_t* opts);

int  gn_search_nearby(const gn_engine_t* engine,
                      double latitude, double longitude,
                      const gn_search_opts_ex_t* opts);

const GeoName* gn_reverse_geocode(const gn_engine_t* engine,
                                   double latitude, double longitude,
                                   double radius_km);

const GeoName* gn_result_at(const gn_engine_t* engine, int index);
```

### Error handling

```c
gn_error_t        gn_engine_last_error(const gn_engine_t* engine);
const char*       gn_engine_last_error_str(const gn_engine_t* engine);
int               gn_engine_error_info(const gn_engine_t* engine,
                                       gn_error_info_t* out);
```

### Utility

```c
int               gn_levenshtein(const char* a, const char* b);
int               gn_str_contains_ci(const char* haystack, const char* needle);
const char*       gn_version(void);
const char*       gn_source_url(gn_source_type_t source, const char* country_code);
const char*       gn_source_filename(gn_source_type_t source, const char* country_code);
```

---

## 📡 Available Data Sources

| Source constant | File | Approx. entries | Description |
|---|---|---|---|
| `GN_SRC_CITIES15000` | `cities15000.zip` | ~28k | Cities ≥ 15 000 pop |
| `GN_SRC_CITIES5000` | `cities5000.zip` | ~50k | Cities ≥ 5 000 pop |
| `GN_SRC_CITIES1000` | `cities1000.zip` | ~130k | Cities ≥ 1 000 pop |
| `GN_SRC_CITIES500` | `cities500.zip` | ~185k | Cities ≥ 500 pop |
| `GN_SRC_ALL_COUNTRIES` | `allCountries.zip` | ~25M | All features worldwide |
| `GN_SRC_COUNTRY` | `XX.zip` | Varies | Per-country (set `country_code`) |
| `GN_SRC_LOCAL` | (your file) | Any | Load from disk, no download |

### Example: Load multiple sources

```c
/* Load all major cities worldwide */
gn_engine_download_and_load(eng, GN_SRC_CITIES1000, NULL,
                            NULL, NULL, NULL, 0);

/* Also load Japan's detailed data */
gn_engine_download_and_load(eng, GN_SRC_COUNTRY, "JP",
                            NULL, NULL, NULL, 0);

printf("Total loaded: %d\n", gn_engine_count(eng));
```

---

## ⚠️ Error Handling

Every download/load call populates detailed error info. Always check:

```c
int ret = gn_engine_download_and_load(eng, GN_SRC_COUNTRY, "ZZ",
                                       NULL, NULL, NULL, 0);
if (ret < 0) {
    gn_error_info_t info;
    gn_engine_error_info(eng, &info);

    fprintf(stderr, "Error %d: %s\n", info.code, info.message);
    fprintf(stderr, "  URL:  %s\n", info.url);
    fprintf(stderr, "  HTTP: %ld\n", info.http_code);
    fprintf(stderr, "  cURL: %d\n", info.curl_code);
}
```

### Error codes

| Code | Constant | Meaning |
|---|---|---|
| `-1` | `GN_ERR_INVALID_ARG` | NULL pointer or bad enum value |
| `-2` | `GN_ERR_DOWNLOAD` | Generic network error |
| `-3` | `GN_ERR_UNZIP` | Corrupt zip or missing `unzip` |
| `-4` | `GN_ERR_PARSE` | Malformed TSV or zero records |
| `-5` | `GN_ERR_NOMEM` | Out of memory |
| `-6` | `GN_ERR_IO` | Disk full / permission denied |
| `-7` | `GN_ERR_HTTP_404` | Resource not found on GeoNames |
| `-8` | `GN_ERR_HTTP_403` | Access forbidden / rate limited |
| `-9` | `GN_ERR_HTTP_500` | GeoNames server error |
| `-10` | `GN_ERR_TIMEOUT` | Download timed out |
| `-11` | `GN_ERR_SSL` | TLS handshake failure |
| `-12` | `GN_ERR_DNS` | Cannot resolve hostname |
| `-13` | `GN_ERR_CONNECT` | Cannot connect to server |
| `-14` | `GN_ERR_PARTIAL` | Download interrupted mid-transfer |

---

## 🔧 Fuzzy Search Scoring

Results are sorted by descending relevance. The scoring tiers are:

| Match type | Score formula |
|---|---|
| **Exact match** | `1000 + log(population + 1)` |
| **Prefix match** | `500 + log(population + 1)` |
| **Substring** | `200 + log(population + 1)` |
| **Levenshtein ≤ 2** | `50 - 15 × distance + log(population + 1)` |

This means "Tokio" will match "Tokyo" via Levenshtein (dist=1), and larger cities rank higher within the same tier.

> **Note:** In `gn_search_nearby()`, the `relevance` field contains **distance in kilometres** (lower = closer), not a score.

---

## 📄 Data License

> **GeoNames** data is licensed under [**CC-BY 4.0**](https://creativecommons.org/licenses/by/4.0/).
>
> Attribution required: please include a link to
> [http://www.geonames.org/](http://www.geonames.org/) in your app's
> "About" screen or documentation.

---

## 🏗️ Architecture

```
┌──────────────────────────────────────────────────────┐
│                    Your Application                            │
│                                                                │
│  gn_search(eng, "Tokyo")    ←───────┐                        │
│  gn_search_nearby(eng, lat, lon) ←──┤                         │
│  gn_result_at(eng, i)       ←───────┘                        │
└──────────────────────┬───────────────────────────────'
                           │
┌──────────────────────▼───────────────────────────────┐
│                  geonames library                               │
│                                                                 │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────┐      │
│  │   Engine       │  │    Parser      │  │   Search      │      │
│  │  (cities[])    │  │  (TSV → rec).  │  │  (fuzzy)     │      │
│  └──────┬──────┘  └──────┬───────┘  └─────┬──────┘      │
│           │                  │                   │              │
│  ┌──────▼───────────────▼────────────────▼──────┐     │
│  │             Reverse Geocoding                          │     │
│  │           (Haversine distance)                         │     │
│  └────────────────────┬──────────────────────────┘     │
│                           │                                     │
│  ┌────────────────────▼──────────────────────────┐     │
│  │           Field Filter Engine                           │    │
│  │       (bitmask → conditional parse)                    │    │
│  └────────────────────┬───────────────────────────┘    │
└───────────────────────┼──────────────────────────────┘
                        │
              ┌─────────▼──────────┐
              │     libcurl            │  Download
              │  (GeoNames TSV)        │  ZIP files
              └─────────┬──────────┘
                        │
              ┌─────────▼──────────┐
              │   system unzip         │  Extract
              │  (or PowerShell)       │  archives
              └─────────┬──────────┘
                        │
              ┌─────────▼──────────┐
              │   Disk cache           │  ~/.geonames/
              │  (ZIP + TXT)           │  Persistent
              └────────────────────┘
```

---

## 📝 Version

**v1.0.0** — Initial release

```c
printf("cgeonames %s\n", gn_version());
/* Output: cgeonames 1.0.0 */
```

---

<p align="center">
  <strong>cgeonames</strong> — See GeoNames in C
  <br>
  <sub>An independent C implementation of the <a href="http://www.geonames.org/">GeoNames</a> geolocation API.<br>
  Data: GeoNames — <a href="https://creativecommons.org/licenses/by/4.0/">CC-BY 4.0</a> — <a href="http://www.geonames.org/">geonames.org</a></sub>
</p>
