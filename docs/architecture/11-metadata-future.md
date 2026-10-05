# Architecture: central metadata (future)

## 15. Central game metadata and artwork [Future – outside the PoC]

The later **Hub metadata service** sources and normalizes game information centrally. A **metadata provider abstraction** encapsulates external APIs, matching and the translation of results. ScreenScraper and IGDB are merely possible provider examples; no vendor is hard-wired or a prerequisite for FrameBeam. Selection and concrete integration follow later based on the available interfaces and terms of use.

```text
Hub library / ROM analysis
          ↓
Hub metadata service
          ↓
Provider abstraction → optional external providers
          ↓
Normalized FrameBeam model + manual overrides
          ↓
Hub: SQLite metadata cache + artwork file cache
          ↓
Player: library / game view via Hub API
```

### Internal metadata model and matching

The internal model is independent of provider data formats and supplements the existing technical ROM/library data. The first later metadata expansion comprises:

| Field | Content |
|---|---|
| Display title | Display name of the game |
| Release date / year | Release date or year only, depending on the known precision |
| Developer / publisher | Developer and publisher |
| Genre | One or more normalized genres |
| Short description | Language-dependent short game description |
| Region | Region of the assigned release or ROM edition |
| Box art | Reference to an artwork asset provided by the Hub |

In addition, the Hub records provider references, origin, language/region, matching status and update time. Missing values remain permissible. Screenshot, fan art, logos, ratings, videos and further fields are not a prerequisite for this first expansion.

Matching is **hash-based** wherever possible and takes system and ROM edition into account. SHA-256 remains FrameBeam's integrity hash; additional matching hashes such as CRC32, MD5 or SHA-1 can be computed during later ROM analysis as the provider requires. Not every provider has to support hash matching. Hashless or unsuccessful assignment can fall back to system/title/region search; ambiguous hits require a manual choice instead of a silent wrong assignment.

Library entries can have states such as "Matched", "No match" and "Multiple matches". Manual selection and per-field **overrides** allow corrections. Overrides take precedence over provider data and are preserved on a refresh until deliberately removed. Technical ROM identity and hashes are not changed by metadata changes.

### Cache, preferences and UI

Normalized metadata is stored in the Hub; artwork lives in the Hub file cache, for example under `/var/lib/framebeam/metadata/artwork/`. Cache/refresh rules take the respective provider terms into account. Already cached content can be served without renewed external access, as far as these rules allow.

The Player obtains **metadata and artwork exclusively via its active Hub** and knows no external provider credentials. The Hub API delivers the normalized model and Hub-owned asset references instead of external provider URLs. Provider outages do not prevent launching existing ROMs; simple library titles and artwork placeholders remain available.

Under **Hub Settings → Metadata** live provider configuration and credentials, connection test as well as preferred language/region and their fallbacks. The actual ROM/release region remains distinguishable from a preferred display region. Available matching variants are preferred; missing variants fall back in a controlled way to the configured alternatives.

Directly in the **library entry** live the matching status and actions such as "Refresh", "Edit Metadata" and "Change Match". There is no new Hub main page for metadata. The Player later shows box art and base metadata in the library and game view; this presentation does not extend its main navigation.

**The entire metadata service including provider integration, matching, overrides, preferences and artwork/metadata cache is explicitly outside FrameBeam 0.1 / PoC.** In the PoC, the existing technical library data, a simple game title and placeholders suffice. Neither external provider access nor additional matching hashes or metadata management actions are required for the PoC.
