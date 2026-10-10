-- 0.10 Second system: Nintendo 3DS (Azahar). Libretro system id 3ds, no default core until the admin installs one
-- from the libretro buildbot (ADR 0020). Decrypted dumps only; the Hub never decrypts. No firmware/system files are
-- required or shipped (firmware_mode builtin, no firmware_defs rows). Extensions: elf/axf/app are deliberately left out.

INSERT INTO systems(id, display_name, extensions, preferred_core_id, preferred_core_name, expected_core_version,
                    platforms, provisioning, input_profile, display_profile, firmware_mode, libretro_ids)
VALUES ('3ds', 'Nintendo 3DS', '.3ds,.cci,.cxi,.3dsx,.zcci,.zcxi,.z3dsx', '', '', NULL,
        'windows-x86_64,linux-x86_64', 'Installed by the admin', '3ds', 'dual_screen', 'builtin', '3ds');

INSERT INTO profiled_cores(core_id) VALUES ('azahar');
