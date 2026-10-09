-- 0.8 ADR 0020 "Cores from the libretro buildbot": systems keep libretro system ids and a default core; the admin
-- installs cores per system. Package rows (core_packages / core_package_files) stay as they are.

ALTER TABLE systems ADD COLUMN libretro_ids    TEXT NOT NULL DEFAULT '';
ALTER TABLE systems ADD COLUMN default_core_id TEXT;
UPDATE systems SET libretro_ids = 'nds' WHERE id = 'nds';

-- Installed cores per system: one build per core. build_date is the upstream date (YYYY-MM-DD) of the newest
-- platform build, empty for legacy packages. The rest is the upstream core info at install time.
CREATE TABLE system_cores (
    system_id       TEXT NOT NULL REFERENCES systems(id) ON DELETE CASCADE,
    core_id         TEXT NOT NULL,
    version         TEXT NOT NULL,
    installed_at    INTEGER NOT NULL,
    origin          TEXT NOT NULL,
    display_name    TEXT NOT NULL,
    license         TEXT NOT NULL,
    required_hw_api TEXT NOT NULL DEFAULT '',
    build_date      TEXT NOT NULL DEFAULT '',
    crc32           TEXT NOT NULL DEFAULT '',
    info            TEXT NOT NULL DEFAULT '{}',
    PRIMARY KEY (system_id, core_id)
);

-- Cores that have a FrameBeam Player profile (manifests/cores/<core_id>.json); every other core is experimental.
CREATE TABLE profiled_cores (core_id TEXT PRIMARY KEY);
INSERT INTO profiled_cores(core_id) VALUES ('melondsds'), ('desmume');

-- Packages of the retired signed source are legacy (origin framebeam).
UPDATE core_packages SET origin = 'framebeam' WHERE origin <> 'libretro-buildbot';

-- Existing Hubs keep working: a completely cached melonds_ds package becomes the installed default core of nds
-- (the expected version if it is cached, else the most recently synced one).
INSERT INTO system_cores(system_id, core_id, version, installed_at, origin, display_name, license)
SELECT s.id, s.preferred_core_id,
       (SELECT p.version FROM core_packages p WHERE p.core_id = s.preferred_core_id
          AND NOT EXISTS (SELECT 1 FROM core_package_files f WHERE f.core_id = p.core_id AND f.version = p.version AND f.cached_at IS NULL)
          AND EXISTS (SELECT 1 FROM core_package_files f WHERE f.core_id = p.core_id AND f.version = p.version AND f.platform = p.platform)
        ORDER BY (p.version = (SELECT x.expected_core_version FROM systems x WHERE x.id = 'nds')) DESC, p.synced_at DESC, p.version DESC LIMIT 1),
       CAST(strftime('%s','now') AS INTEGER), 'framebeam', s.preferred_core_name,
       COALESCE((SELECT p.license FROM core_packages p WHERE p.core_id = s.preferred_core_id LIMIT 1), '')
FROM systems s
WHERE s.id = 'nds' AND EXISTS (
    SELECT 1 FROM core_packages p WHERE p.core_id = s.preferred_core_id
      AND NOT EXISTS (SELECT 1 FROM core_package_files f WHERE f.core_id = p.core_id AND f.version = p.version AND f.cached_at IS NULL)
      AND EXISTS (SELECT 1 FROM core_package_files f WHERE f.core_id = p.core_id AND f.version = p.version AND f.platform = p.platform));
UPDATE systems SET default_core_id = (SELECT c.core_id FROM system_cores c WHERE c.system_id = systems.id) WHERE id = 'nds';
