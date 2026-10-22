# Try the main features

A short tour for a server with two apps, using only homebrew ROMs or dummy files.

1. Server, page Users: create an invite (the code is shown only once). In a second app choose "I have an invite code" and enter it.
2. Start a Session on the first app with Private and with server users; check that the second user is rejected, then let in ([sessions-over-the-internet.md](sessions-over-the-internet.md)).
3. Server, Settings: enable "Allow users to upload games"; upload a homebrew ROM from the second app.
4. Server, Systems & Cores: switch NDS to native firmware mode without files; the app shows "Firmware required" / "Firmware missing".
5. Upload your own BIOS/firmware dumps (`bios7.bin`, `bios9.bin`, `firmware.bin`; never in the repo) and launch.
6. Connect a gamepad and remap it on Controllers.
7. Install the app via the `framebeam-windows-x64-msi` artifact.

Keyboard defaults: see [player.md](player.md).
