-- 0.10 The Player no longer ships a core (ADR 0020): cores are installed by the admin from the libretro buildbot.
UPDATE systems SET provisioning = 'Installed by the admin' WHERE provisioning = 'Included in the Player';
