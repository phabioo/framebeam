# ADR 0003: FrameBeam Player in Phase 2

- Status: angenommen
- Datum: 2026-10-05
- Entscheider: Fabio (Vorschlag des Orchestrators, für den PoC bestätigt am 2026-10-05)

## Kontext

Phase 2 ("Spielbarer Durchstich") baut den FrameBeam Player: Hub-Profil, Pairing, Library, ROM-Cache, `LibretroBackend` mit melonDS DS und minimale Qt-Oberfläche. Die Architektur lässt Toolchain, Core-Beschaffung, Ablage und Credential-Speicher offen oder weicht davon ab. Dieser ADR hält die Festlegungen fest; Fabio hat sie am 2026-10-05 für den PoC angenommen. Die Architekturdokumente bleiben unverändert.

## Entscheidungen

- **Qt:** Qt >= 6.4, nicht über vcpkg. Linux: apt (6.4.2, Ubuntu noble). Windows: `install-qt-action` (6.8 LTS). Der Code nutzt nur die 6.4-API. Grund: Ein Qt-Build über vcpkg dauert in der CI Stunden, und die Cloud kann vcpkg-Quellen nicht laden. vcpkg bleibt für spätere Pakete.
- **melonDS DS:** Version v1.4.0, Commit gepinnt (`scripts/melonds-ds.pin`). Linux baut den Core aus Quellen per git (`scripts/fetch-melonds-ds.sh`). Windows nutzt das offizielle Release-Asset `melondsds_libretro-win32-x86_64-Release.zip` mit gepinntem SHA-256 (`scripts/fetch-melonds-ds.ps1`); dessen Build ist MinGW, das Laden per `LoadLibrary` über die C-ABI ist unkritisch. Das Windows-CI-Artefakt `framebeam-player-windows-x64` enthält Player, Qt-Laufzeit, den Core unter `cores/` und den GPL-Hinweis.
- **Audio:** In Phase 2 über Qt Multimedia. SDL3 kommt mit den Gamepads (Phase 5).
- **HTTP/TLS:** über QtNetwork. Vertrauen ausschließlich über den Leaf-Fingerprint (SHA-256 über DER, Format wie im Hub), auch bei CA-signierten Zertifikaten. Der Erstkontakt zeigt den Fingerprint und verlangt Bestätigung (Architektur 10; Abweichung zu Mock-up 3b). Eine Abweichung des Fingerprints blockiert die Verbindung. Folge: Ein Zertifikatswechsel (auch Let's Encrypt hinter Reverse Proxy) erfordert, das Profil zu entfernen und neu zu verbinden, bis der bestätigte Pin-Wechsel spezifiziert ist.
- **Ablage:** `AppDataLocation` (Ersetzt durch ADR 0004.) `profiles.json` und `device.json` enthalten keine Secrets. Die Device-ID wird lokal erzeugt. Hub-spezifische Daten liegen unter `hubs/<hub_id>/`, auch das Save-Verzeichnis des Cores (der Sync folgt in Phase 3). Der ROM-Cache ist inhaltsadressiert und hubübergreifend: `cache/roms/<sha256>.<ext>`. Begründung: Der Inhalt ist per Hash eindeutig, die Dateien enthalten keine hub-zuordenbaren Daten. Der Download läuft in eine `.part`-Datei mit Range-Resume; der Hash wird vor dem atomaren Umbenennen geprüft; ein Sidecar mit Größe und mtime vermeidet erneutes Hashen.
- **Credentials:** Windows: Credential Manager. Linux/macOS: in Phase 2 nur im Speicher (nicht im PoC-Scope); dort ist nach einem Neustart ein neues Pairing nötig.
- **Emulation:** `EmulatorBackend` und `LibretroBackend`. Pro Prozess gibt es nur eine Core-Instanz (libretro-Globals). Software-Renderer; Hardware-Rendering ist abgelehnt. Systeme kommen per Manifest (`client/emulation/manifests/nds.json`) inklusive Core-Options-Defaults (`render_mode` software, Layout `top-bottom`, `boot_mode` direct). Für Homebrew ist keine Firmware nötig (FreeBIOS); der Firmware-Pfad folgt in Phase 5. Tests nutzen eine zur Build-Zeit selbst erzeugte Homebrew-Test-ROM, nie eine Datei im Repo.
- **Pairing-Abbruch:** nur lokal. Die API hat kein Cancel; die Anfrage verfällt nach 10 min (ADR 0002).

## Offen

- Bestätigter Pin-Wechsel bei Zertifikatserneuerung (wie in ADR 0002).
- Credential-Store für Linux und macOS (Secret Service bzw. Keychain).

## Verworfen

- **Qt über vcpkg:** Build-Zeit in der CI, Quellen in der Cloud nicht ladbar.
- **Core für Windows aus Quellen mit MSVC:** Das offizielle MinGW-Release-Asset genügt über die C-ABI.
- **SDL3-Audio in Phase 2:** kein Gewinn vor den Gamepads; Qt Multimedia reicht.
- **Hardware-Rendering des Cores:** Software-Renderer ist für den Durchstich ausreichend und einfacher zu testen.
- **ROM-Cache pro Hub:** Duplikate ohne Nutzen, da der Hash den Inhalt eindeutig bestimmt.
- **Stilles Übernehmen eines geänderten Zertifikats:** widerspricht den harten Regeln (Abweichung blockiert).

## Folgen

- Der Player-Code in `client/` folgt diesen Festlegungen; Änderungen nur über einen neuen ADR.
- Linux/macOS-Player verlieren das Pairing beim Neustart, bis der Credential-Store umgesetzt ist.
- Zertifikatswechsel am Hub erzwingt Neu-Verbinden.
