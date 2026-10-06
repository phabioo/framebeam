# ADR 0004: Portable data directory of the FrameBeam Player

- Status: accepted
- Date: 2026-10-05
- Decided by: Fabio (requested after the first Windows test)

## Context

ADR 0003 places Player data under `AppDataLocation`. After the first Windows test, Fabio wants everything in one place, next to the FrameBeam Player, so that the player folder can be moved or copied as a whole. This ADR replaces the storage location decision of ADR 0003. The remaining storage rules (content-addressed ROM cache, `hubs/<hub_id>/` layout, no secrets in files) stay as they are.

## Decision

- **Location:** Player data lives in `<directory of the player executable>/data` (portable mode). This covers the ROM cache (`cache/`), `profiles.json`, `device.json`, `hubs/<hub_id>/` (including core saves), `system/` and `probe/`. Portable mode applies only if the folder can be created and passes a real write test (a file is written and removed, not just a permission check).
- **Precedence:** `--data-dir` > environment variable `FRAMEBEAM_DATA_DIR` > portable folder > `QStandardPaths::AppDataLocation` as fallback. The fallback applies, for example, to an installation under `C:\Program Files`, which normal users cannot write to. The chosen directory and the reason are logged under the category `framebeam.profiles`.
- **One-time migration:** If the portable folder has no `profiles.json` and the old AppData folder has data, everything except `cache/` is copied (ROMs are downloaded again). Existing files are never overwritten, and the source is never deleted. `device_id` is preserved, so existing pairings stay valid.
- **Credentials:** remain in the Windows Credential Manager. No secrets in files.
- **Implementation:** `ProfileStore::defaultBaseDir`, `chooseBaseDir` and `migrateLegacyData` in `client/core/profilestore.cpp`.

## Consequences

- Data travels with the player folder.
- Updating the Player by replacing the folder must keep `data/`.
- An earlier portable run that already created `device.json` without `profiles.json` keeps its new `device_id`; the migration does not replace it. This only affects unpaired devices.
- Credentials are not part of `data/`: after moving the folder to another machine, pairing must be repeated there.

## Rejected

- **Always AppData:** contradicts the goal of keeping everything in one place.
- **Moving instead of copying:** risk of losing saves if the migration is interrupted.
- **Requiring admin rights to write into Program Files:** unreasonable for normal users; the AppData fallback covers this case.
