# FrameBeam – Architektur und PoC-Scope

Stand: 05.10.2026 · Architekturentwurf 0.1  
Grundlage: Architekturgespräch „Konzept für Retro Streaming“.

## 1. Ziel und Grundprinzip

FrameBeam verbindet eine zentrale ROM-Bibliothek und versionierte Spielstände mit lokaler Emulation und dem Teilen laufender Sessions. FrameBeam Player und FrameBeam Hub sollen leicht, leistungsfähig und langfristig unter Windows, Linux und macOS nutzbar sein. Weitere Emulatoren sollen ohne grundlegenden Umbau hinzukommen.

> **Der Hub verwaltet. Der Player emuliert. Audio und Video laufen möglichst direkt zwischen Playern.**

```text
                   FrameBeam Hub
          Library · Saves · Auth · Registry
          Presence / Session-Metadaten intern
                 HTTPS/JSON + WSS
                    /           \
             Player A         Player B
             Emulator         Emulator
             Rendering        Rendering
                    \           /
                     WebRTC/P2P
                     Video + Audio
```

## 2. Verantwortlichkeiten und Tech-Stack

| Bereich | Entscheidung | Aufgabe |
|---|---|---|
| Hub | Go, ohne großes Framework | HTTPS-API, Dateiverwaltung, Authentifizierung, Registry, interne Session-Metadaten und Signaling |
| Verwaltungsdaten | SQLite | Spiele, ROM-Hashes, Hub-lokale Nutzer, Geräte, Token-Zuordnungen, Save-Versionen, Save-Konflikte und Registry; Session-Metadaten intern |
| Dateispeicher | Normale Dateien | ROMs, Spielstände und Admin-bereitgestellte Firmware; keine Binärdaten in SQLite |
| Player | C++23 + CMake | Emulation, Medienverarbeitung, Netzwerk und Anwendungslogik |
| Oberfläche | Qt 6 + QML | GPU-beschleunigte Library, Settings und mehrere Video-Surfaces |
| Emulator-Anbindung | `EmulatorBackend` → `LibretroBackend` | Austauschbare Emulator-Cores |
| Erster Core | melonDS DS über Libretro | Nintendo-DS-Emulation |
| Input/Audio | SDL3 | Plattformübergreifende Input-/Audio-Schicht, insbesondere Gamepads |
| Streaming | libdatachannel | WebRTC-Verbindungen zwischen Clients |
| Medienformate | H.264 + Opus | Video- und Audioübertragung |
| Windows-Encoding | NVENC / QSV / AMF; Software-H.264 als Fallback | Hardware-Encoding, soweit verfügbar |

Der Server führt keine Emulation, kein Encoding, kein Decoding und kein Multiview-Rendering aus. Der Client enthält Library-Zugriff, getrennte ROM-/Core-Caches, lokale Emulator-/Controller-Einstellungen, Save-Sync, Session-Manager, Emulator-Backend, Rendering, Audio, Encoder/Decoder und Input.

Der Hub verwaltet Hub-lokale Benutzer und Geräte-Pairing. Der Player verwaltet lokale Geräteidentität und gespeicherte Hub-Profile; eine verbundene Player-Instanz verwendet genau einen aktiven Hub (siehe Abschnitt 14). Ein zentraler Metadata Service mit Provider-Abstraktion und Artwork-Cache ist ausschließlich als spätere Erweiterung vorgesehen (siehe Abschnitt 15).

Der Server wird als einzelner Dienst betrieben, unter Linux beispielsweise über `systemd`, mit SQLite und Daten unter `/var/lib/framebeam/`. Docker ist optional. Node.js, Redis, PostgreSQL und Kubernetes sind für die geplante Basis nicht erforderlich.

## 3. Protokolle und Datenflüsse

Die API wird unter `/api/v1/...` versioniert. Zusätzlich existiert eine eigenständige `protocol_version`, getrennt von Player- und Hub-Produktversion. Kompatibilität richtet sich primär nach der unterstützten Protokollversion, nicht nach exakt gleichen Produktversionen. Eine komplexe RPC-Schicht oder gRPC ist nicht vorgesehen.

| Verbindung | Inhalt |
|---|---|
| Client ↔ Server: HTTPS/JSON | Library und Metadaten, ROM-Download, Save-Download/-Upload, Geräte und Sessions |
| Client ↔ Server: WSS | Presence, Session-Updates und WebRTC-Signaling |
| Client ↔ Client: WebRTC | H.264-Video und Opus-Audio; DataChannel gegebenenfalls später |

Vorgesehene API-Bereiche: `/games`, `/roms`, `/saves`, `/devices` und `/sessions` unter dem versionierten Präfix. Hinzu kommen ein öffentlicher FrameBeam-Info-Endpunkt zur Hub-Identifikation sowie API-Bereiche für Pairing und Token-Widerruf (siehe Abschnitt 14). Exakte Endpunkte und Nachrichtenformate sind noch zu spezifizieren.

### Protocol Handshake und Capability Negotiation [PoC]

Beim Connect meldet der Player mindestens `platform`, `arch`, Player-Version, `protocol_version`, verfügbare Core-IDs und Core-Versionen, H.264-Encode-/Decode-Fähigkeit und verfügbare Encoder, Opus-Fähigkeit sowie Input-Capabilities. Der Hub antwortet mit Hub-Version, `protocol_version` und Kompatibilitätsstatus. Der Handshake ersetzt keine Authentifizierung oder Gerätefreigabe.

Unterschiedliche Produktversionen dürfen bei kompatiblem Protokoll zusammenarbeiten. Klare Fehlerzustände unterscheiden „Player zu alt“, „Hub zu alt“, „Core fehlt“, „Core-Version mismatch“ und „Codec/Capability fehlt“. Fehlende Fähigkeiten sperren den betroffenen Start- oder Streamingpfad; genaue Kompatibilitätsregeln und Nachrichtenformate bleiben zu spezifizieren.

### Spielstart und ROM-Cache

1. Der Player verbindet sich mit dem ausgewählten Hub und authentifiziert sich mit einem kurzlebigen Access Token seines autorisierten Geräts und führt den Handshake aus. Er lädt die Library-Daten einschließlich Spielbezeichnung, ROM-Größe und SHA-256. Externe Spielmetadaten und Boxart sind im PoC nicht erforderlich.
2. Er prüft, ob die ROM mit diesem Hash bereits im lokalen Cache liegt.
3. Bei einem validierten Treffer verwendet er die lokale Datei; andernfalls lädt er die ROM herunter, prüft SHA-256 und legt sie erst nach erfolgreicher Prüfung im Cache ab.
4. Er prüft Core-Kompatibilität und benötigte Firmware, lädt und validiert bei Bedarf vom Admin bereitgestellte Firmware (siehe Abschnitt 6). Er lädt den aktuellen Spielstand und startet den Emulator mit lokalen Dateien.

```text
Server-Metadaten → Cache-Prüfung → ggf. ROM-Download → lokale Emulation
Server-Spielstand ────────────────────────────────────┘
```

ROMs werden nicht während der Emulation über ein Netzwerk-Dateisystem gelesen. Wiederholte Starts einer gecachten ROM benötigen keinen erneuten ROM-Transfer. Cache-Limit, Bereinigung und Download-Fehlerbehandlung sind noch festzulegen.

### Save-Sync, Checkpoints und Versionierung [PoC]

```text
Start-Sync:      Hub → Current Checkpoint → Player → Emulator
Auto-Checkpoint: geänderter Save → Player → Hub → Current Checkpoint
Final-Sync:      Pause / Stop / sauberes Beenden → Player → Hub
History:        dauerhafte Version bei relevanten Ereignissen
```

Spielstände liegen zentral im Dateisystem; SQLite hält Zuordnung, Checkpoint-Revisionen und Versionsmetadaten einschließlich Herkunftsgerät und Zeitstempel. Save-Dateien werden nur bei tatsächlich geändertem Inhalt übertragen (Hash-/Dirty-Erkennung). Nach einer Änderung folgt ein kurzer Debounce, beispielsweise 10–15 Sekunden; periodische Uploads erfolgen frühestens ungefähr alle 60 Sekunden. Bei unverändertem Hash erfolgt kein Upload. Pause, Stop und sauberes App-Beenden lösen unabhängig vom periodischen Intervall einen sofortigen Final-Sync geänderter Saves aus.

Auto-Checkpoints aktualisieren den **Current Checkpoint**, ohne bei jedem Upload eine permanente History-Version zu erzeugen. Dauerhafte History-Versionen entstehen bei relevanten Ereignissen wie Session-Ende, Gerätewechsel, vor Konfliktauflösung oder manuellem Snapshot. Konkrete Retention und Ausdünnung bleiben später zu spezifizieren.

Die bestehende `base_version`-Konfliktlogik gilt auch für Checkpoints: Jede Änderung des Current Checkpoint erhält eine neue Revision für den nächsten Abgleich, selbst wenn keine dauerhafte History-Version entsteht. Ein Upload gegen eine veraltete Basis darf eine konkurrierende Änderung nicht still überschreiben (Abschnitt 13).

Bei Hub-Ausfall oder fehlgeschlagenem Upload wird der Save lokal sicher als **pending sync** mit Hub-, User-, Spiel-/Slot-Zuordnung und Basisversion gepuffert. Wiederholung erfolgt ausschließlich zum ursprünglichen Hub; niemals wird ein Save an einen anderen Hub umgeleitet. Checkpoints begrenzen möglichen Fortschrittsverlust bei einem Crash, garantieren jedoch bei Ausfällen oder ausstehenden Änderungen keine feste maximale Verlustdauer. Save States bleiben ein getrennter Mechanismus und liegen außerhalb des PoC; automatisches Zusammenführen binärer Spielstände ist nicht vereinbart.

## 4. Session-Sharing und WebRTC/P2P

Beim alleinigen Spielen gehen Bild und Ton direkt in die lokale Ausgabe; der Streaming-Encoder bleibt aus. Erst beim Teilen einer Session wird zusätzlich der Medienpfad aktiviert:

```text
Emulator ──→ lokale Bild-/Tonausgabe
        └──→ Encoder → WebRTC → Remote-Client → Decoder → Ausgabe
```

Der Server verwaltet Session-Metadaten, Sichtbarkeit und Presence und vermittelt den Verbindungsaufbau über Signaling. Er transportiert und verarbeitet selbst kein Video oder Audio.

Direkte P2P-Verbindungen bilden die Basis. TURN kann später als gesonderter Relay-Dienst ergänzt werden, wenn NAT oder Firewalls direkte Verbindungen verhindern. Ein TURN-Relay wäre ein zusätzlicher Medienpfad; der FrameBeam-Anwendungsserver bleibt für Verwaltung und Signaling zuständig. Die PoC-Demonstration benötigt daher eine Umgebung, in der direkte Verbindungen funktionieren. Die konkrete ICE-/STUN-Konfiguration ist noch festzulegen.

Session-Sharing bedeutet zunächst die Übertragung von Bild und Ton. Remote-Steuerung, synchronisierte Multiplayer-Emulation oder NDS-Link-/WLAN-Emulation sind damit nicht zugesagt.

### Session-Sichtbarkeit und Einladungen

| Sichtbarkeit | Zugriffsregel | FrameBeam 0.1 / PoC |
|---|---|---|
| Private | Nur der Session-Owner | Funktionsfähig |
| Hub users | Alle authentifizierten User desselben aktiven Hubs dürfen beitreten | Funktionsfähig |
| Invite only | Explizite `allowed_user_ids` / Session-ACL desselben Hubs | Datenmodell und Flow im PoC vorsehen; vollständige Umsetzung keine Pflicht |

Der Hub prüft Sichtbarkeit und Berechtigungen beim Beitritt und bei Änderungen. Zuschauer erhalten zunächst ausschließlich `view_video=true`, `hear_audio=true`, `send_input=false`. Nur der Session-Owner darf Einladungen beziehungsweise ACL ändern und Viewer entfernen; ein Viewer darf die Session nicht weiterfreigeben. Änderungen müssen laufenden Zugriff entsprechend entziehen, auch bei bereits aufgebautem Medienpfad.

Der vorgesehene Invite-only-Flow lautet: Owner wählt bereits bekannte User des aktiven Hubs → Hub führt `allowed_user_ids` → eingeladener Player erhält **Join / Decline** → beim Join prüft der Hub die bestehende Authentifizierung und Session-ACL. Die Einladung authentifiziert keinen neuen User. Ein Invite gilt nur für diese konkrete Session und verfällt mit deren Ende; Offline-User können ihn erhalten, solange die Session noch läuft. Ein Session-Invite ist von einem Benutzer-Onboarding-Invite (Abschnitt 14) getrennt.

Keine Friends-Liste, öffentlichen Share-Links, Gastzugänge, Gastcodes, Cross-Hub-Invites oder Cross-Hub-Sessions im PoC.

## 5. Multiview

Der Client kombiniert die lokale Session mit einer empfangenen Remote-Session in mehreren Video-Surfaces. Im PoC sind eine zweite Session, **Picture-in-Picture (PiP)** und **Side-by-Side** vorgesehen.

Layout, Skalierung, Decoding und Rendering erfolgen vollständig auf dem Client. Der Server erzeugt kein zusammengesetztes Bild. Audio-Fokus beziehungsweise Mischung und genaue DS-Bildschirm-Anordnung sind noch zu definieren.

## 6. Erweiterbarkeit für weitere Emulatoren

FrameBeam wird nicht direkt an melonDS gekoppelt. Die gemeinsame Emulator-Schnittstelle trennt Anwendungslogik und UI vom jeweiligen Backend:

```text
FrameBeam → EmulatorBackend
              ├── LibretroBackend
              │      ├── melonDS DS  [PoC]
              │      ├── mGBA        [später möglich]
              │      └── Snes9x      [später möglich]
              └── StandaloneBackend [spätere Option]
```

Systeme und Cores werden datengetrieben über Manifeste beschrieben, statt konsolenspezifische Startlogik in der Anwendung zu verteilen. Ein NDS-Manifest enthält beispielsweise System-ID `nds`, Core-Zuordnung `melonds_ds`, Dateiendung `.nds`, BIOS-/Firmware-Angaben, Input-Profil `nds` und Display-Profil `dual_screen`.

Neue Libretro-Systeme sollen im Regelfall durch Core, Manifest und passende Profile hinzukommen. Zusätzliche Backend-Arbeit bleibt möglich, wenn ein Emulator besondere Anforderungen hat. Konkrete BIOS-/Firmware-Anforderungen werden je System/Core im Manifest beschrieben. Für den PoC wird melonDS DS mit dem Windows-Player ausgeliefert; automatische Core-Verteilung über den Hub folgt später.

### BIOS/Firmware über den Hub [PoC]

BIOS/Firmware wird zentral durch den **Admin** bereitgestellt und einem System/Core zugeordnet. FrameBeam liefert proprietäre BIOS-/Firmware-Dateien nicht automatisch mit und lädt sie nicht eigenständig aus dem Internet. Normale User verwalten diese Dateien nicht.

Der Hub validiert erwartete Metadaten und Hashes gemäß System-/Core-Anforderungen und liefert benötigte, bereitgestellte Dateien an autorisierte Player aus. Der Player validiert und cached sie lokal, getrennt von ROMs und Cores. Benötigt ein Core eine nicht verfügbare Datei, zeigt er klar **„Firmware required/missing“** und verhindert den betroffenen Start. Cores ohne BIOS-/Firmware-Bedarf funktionieren ohne diesen Pfad. Die konkreten Anforderungen des PoC-Cores und Manifest-/Dateiformate bleiben zu spezifizieren; der Bereitstellungs- und Fehlerpfad gehört zum PoC.

## 7. Klar abgegrenzter PoC-Scope

| Bereich | FrameBeam 0.1 / PoC |
|---|---|
| Client | Windows x86-64 |
| Server | Linux x86-64 und ARM64; Raspberry Pi 5 als vorgesehenes Server-Ziel |
| System/Core | Ausschließlich Nintendo DS mit melonDS DS über Libretro; Core im Windows-Player enthalten |
| Registry | Hub kennt System, Core-ID und erwartete Version; keine automatische Core-Verteilung |
| Oberflächen | Dark/Light Mode für Player und Hub; optionale Diagnostics |
| Einstellungen | Player-Seiten Emulation und Controllers; nur die für melonDS im PoC tatsächlich benötigten Optionen müssen vollständig funktionieren; generische Core Options und Hierarchie vorbereitet |
| Library | Zentrale ROM-Bibliothek mit technischen Library-Daten und einfacher Spielbezeichnung; keine externen Metadata-Provider oder Boxart-Beschaffung |
| ROM-Zugriff | Download und lokaler, hashbasierter Cache |
| Spielstände | Start-Sync, geänderte periodische Auto-Checkpoints, Final-Sync; Current Checkpoint getrennt von History; lokale pending sync; `base_version` auch für Checkpoints; Konfliktmodell und UI-Zustand |
| Emulation | Lokal auf dem Client |
| Eingabe | Controller; DS-Touch über Maus |
| Sessions | Veröffentlichen, entdecken und teilen; Private und Hub users funktionsfähig; Invite only mit ACL und Join/Decline-Flow im Datenmodell/PoC vorsehen; Zuschauer ohne Input-Rechte |
| Streaming | Direktes WebRTC/P2P mit Video und Audio |
| Multiview | Lokale plus eine Remote-Session; PiP und Side-by-Side |
| Hub-Verbindungen | Mehrere gespeicherte Hub-Profile, Adresse hinzufügen, Hub identifizieren, auswählen, wechseln und entfernen; genau ein aktiver Hub je verbundener Player-Instanz |
| Benutzerverwaltung | Admin/User; initialer Admin mit Username/Passwort; Admin-kontrollierte kurzlebige Onboarding-Invites; normale User passwordless; User und Devices getrennt |
| Gerätefreigabe | Player-Anfrage → Admin Allow/Deny als Standard; Trusted/Revoked und Revoke access; kurzlebiges Access Token plus langfristiges widerrufbares Device/Refresh Credential im OS-Credential-Store |
| Berechtigungen | Library-Verwaltung Admin-only; optionale Einstellung Allow users to upload games; zentrale Uploads mit `uploaded_by`, keine Verwaltung fremder Einträge |
| Firmware | Admin-Bereitstellung, System/Core-Zuordnung, Metadaten-/Hashprüfung, Auslieferung und lokaler Cache; Firmware required/missing |
| Handshake | Getrennte `protocol_version`, Produktversionen und Capability Negotiation; klare Kompatibilitätsfehler |
| Transport | HTTPS/WSS; eigenes initiales TLS-Zertifikat, eigenes cert/key oder Reverse Proxy; Player-TOFU/Pinning; HTTP/WS nur expliziter Dev-Modus bzw. localhost |
| Verbindungsstart | Start-/Connection-Screen; optional Auto-Connect zum zuletzt verwendeten Hub |

**Außerhalb des PoC:** Linux- und macOS-Clients, Windows-/macOS-Server-Builds, weitere Emulatoren, Hosted Emulation, automatische Core-Verteilung, vollständige Game-Override-UI und TURN-Fallback. Ebenfalls außerhalb: Metadata Service mit externen Providern, automatisches oder manuelles Provider-Matching, externe Basis-Metadaten und Artwork-/Metadata-Cache; gleichzeitige Multi-Hub-Nutzung, Hub-Federation, Cross-Hub Sessions, OAuth und zentrale Accounts. Ein Webclient ist nicht vorgesehen. Remote-Steuerung, Emulator-Netplay, Save States, Friends-Liste, öffentliche Session-Links, Gastzugänge, E-Mail-/Password-Recovery und ein eigener ACME-Client gehören nicht zum vereinbarten PoC.

Langfristig bleibt Windows/Linux/macOS das Plattformziel. Der PoC soll bereits die wiederverwendbare Basis aus Go-Server, C++-Client, Emulator-Abstraktion, ROM-/Save-Protokoll und WebRTC-Sessionmodell schaffen.

### Nachweis des PoC

Zwei Windows-Player an einem Linux-Hub starten eine NDS-ROM lokal aus der zentralen Library; ein erneuter Start nutzt den ROM-Cache. Ein geänderter Save wird periodisch als Current Checkpoint gesichert, beim Pause/Stop/sauberen Beenden sofort abgeglichen und auf dem anderen Gerät verfügbar. Auto-Checkpoints erzeugen nicht jedes Mal History; relevante Ereignisse tun dies. Bei Hub-Ausfall bleibt pending sync lokal dem ursprünglichen Hub zugeordnet. Ein Upload mit veralteter `base_version` zeigt den Konfliktzustand statt zu überschreiben.

Eine Session mit Bild und Ton erscheint per direktem P2P in PiP und Side-by-Side; der Hub emuliert und rendert nicht. Private verhindert fremden Join, Hub users erlaubt authentifizierten Usern desselben Hubs Join; Zuschauer senden keinen Input. Invite-only-ACL, Owner-Rechte, Join/Decline und Ablauf bei Session-Ende sind im Modell/Flow vorgesehen.

Der erste Hub-Start richtet einen Admin mit Username/Passwort ein. Ein Admin-Invite erstellt einen hub-lokalen User ohne dauerhaftes Passwort und kann dessen erstes Gerät autorisieren. Ein weiteres Gerät stellt nach Eingabe der Hub-Adresse eine Pending Request mit Gerätename, Plattform und Player-Version; der Admin demonstriert Allow und Deny. Nach Neustart erfolgt Anmeldung über das Device/Refresh Credential und ein neues kurzlebiges Access Token. Einzelnes Revoke entzieht diesem Gerät Zugriff. Library-Verwaltung ist für User gesperrt; bei aktivierter Upload-Option landen eigene Uploads mit `uploaded_by` in derselben Library, ohne Verwaltung fremder Einträge.

Handshake und Kompatibilitätsfehler werden geprüft. Der Firmware-Pfad demonstriert Admin-Bereitstellung, Validierung, Player-Cache und Firmware required/missing bei einem erforderlichen, fehlenden Artefakt. HTTPS/WSS und gespeicherter Zertifikats-Fingerprint funktionieren; eine spätere Abweichung erzeugt Warnung/Fehler.

Zwei Hub-Profile lassen sich speichern und adressbasiert identifizieren. Beim Wechsel bleibt genau der ausgewählte Hub aktiv; Library, Saves und Sessions sind diesem Hub zugeordnet. Das Entfernen eines Profils entfernt dessen lokale Credentials. Metadata-Provider und Boxart bleiben vollständig außerhalb dieses Nachweises.

## 8. Repository und offene Implementierungsentscheidungen

Client und Server sind zwei Build-Targets desselben FrameBeam-Monorepos und keine getrennten Projekte oder Repositories. Die zugehörigen Client- und Server-Artefakte werden im gemeinsamen FrameBeam-Release bereitgestellt.

Die verbindlichen Produktbegriffe in UI und Dokumentation sind **FrameBeam Player** für den Client und **FrameBeam Hub** für den Server. Intern bleiben die Bezeichnungen `client` und `server` bestehen.

Ein gemeinsames Monorepo hält Client, Server und Protokolländerungen zusammen:

```text
framebeam/
├── client/       # app, core, emulation, media, network, ui
├── server/
├── protocol/     # openapi, schemas
├── packaging/    # windows, linux, macos
└── docs/
```

Noch zu spezifizieren sind konkrete Encoder-/Decoder-Anbindung, API-Endpunkte und Nachrichtenformate, Protokoll-Kompatibilitätsregeln, Token-Format und exakte Access-/Refresh-Laufzeiten sowie technische Widerrufs-/Erneuerungsdetails innerhalb des festgelegten Modells. Offen bleiben außerdem konkrete TLS-Zertifikatsverwaltung einschließlich Erneuerung/Pin-Wechsel, Save-Retention und Details der Konfliktauflösung, Cache-Limits/Bereinigung, ICE-/STUN-Konfiguration und späterer TURN-Einsatz, Medienparameter sowie Core-/Firmware-Manifest- und Paketformate. Approval-Pairing, Rollen, passwordless User und TLS-Pflicht sind bereits entschieden. Diese offenen Details führen keine zusätzlichen PoC-Features ein.

## 9. Produktbegriffe, Navigation und Darstellung

In der Nutzeroberfläche heißt eine laufende oder geteilte Spielsitzung konsequent **Session**: „Session teilen“, „Session ansehen“ und „Zu Multiview hinzufügen“. **Stream** bezeichnet nur den technischen Übertragungspfad in Diagnostics. Technische Begriffe in Implementierung und Protokoll bleiben zulässig.

Player und Hub unterstützen jeweils eine wählbare **Dark-/Light-Darstellung** mit gemeinsamer Designsprache. Die Auswahl ist in den jeweiligen Settings verfügbar.

| FrameBeam Hub | FrameBeam Player |
|---|---|
| Library | Library |
| Saves einschließlich Historie und Konfliktzustand | Emulation |
| Systeme & Cores | Controllers |
| Clients (Pending Requests, Trusted/Revoked, Revoke access) | Settings |
| Benutzer (Admin-kontrollierte User und Onboarding-Invites) | — |
| Settings | Während des Spiels: laufende Session, Multiview, optional Diagnostics |

Vor der Hauptnavigation erhält der Player einen **Start-/Connection-Screen** mit gespeicherten Hubs, Verbindungsstatus, „Hub hinzufügen“ und „Verbinden“. Ohne erfolgreiche Verbindung bleibt dieser Screen erreichbar; optionales Auto-Connect zum zuletzt verwendeten Hub führt bei Erfolg direkt zur Library. Ein kompakter Hub-Switcher sowie **Settings → Hubs** bieten Wechsel, Entfernen und Auto-Connect-Einstellung. Es entsteht keine zusätzliche Hauptnavigationsseite. Beim Wechsel werden laufende Sessions zunächst beendet und ausstehende Save-Uploads gesichert beziehungsweise geklärt (siehe Abschnitt 14).

Die bestehenden Hub-Seiten bleiben erhalten; Benutzerverwaltung ergänzt sie. Admin verwaltet Library, Benutzer, Clients/Pairings, Systeme/Cores/Firmware und Hub-Einstellungen. Normale User nutzen Library, ihre Saves und Sessions; die sichtbaren Aktionen folgen ihren Berechtigungen. Pending Requests erscheinen auf Clients mit Device Name, Plattform, Player-Version und Allow/Deny. „Allow users to upload games“ liegt in Hub Settings. Firmware required/missing, Pairing-, Zertifikats- und Kompatibilitätsfehler sind handlungsrelevante Zustände und bleiben direkt sichtbar. Spätere Provider-Konfiguration liegt unter **Settings → Metadata**; Metadata-Aktionen liegen direkt im Library-Eintrag (siehe Abschnitt 15).

Der Hub erhält **keine Sessions-Verwaltungsseite**, kein Live-Sessions-Dashboard und keine Video-Vorschau. Seine Session-Metadaten bleiben intern für Presence, Sichtbarkeit/Berechtigungen und Signaling. Session-Entdeckung, Teilen, Ansehen und Multiview gehören in den Player. Das bestehende Session-Protokoll bleibt dafür erhalten.

FPS, Latenz, Encoder, Codec, Bitrate, WebRTC-/Streamingstatistiken und technische Debug-Daten erscheinen ausschließlich in einem optional einblendbaren oder einklappbaren Diagnostics-Bereich. Sie bilden nicht die primäre UX. Handlungsrelevante Zustände wie Downloadbedarf und Save-Konflikte bleiben dagegen unmittelbar sichtbar.

## 10. Systeme & Cores: Registry und spätere Paketbereitstellung

Die Hub-Seite **„Systeme & Cores“** bleibt erhalten. Sie verwaltet die System Registry und Core Registry: System-ID, zugeordnete beziehungsweise bevorzugte Cores, Core-ID, erwartete Version und unterstützte Plattformen. Der Hub führt Cores niemals aus.

Für FrameBeam 0.1 gilt: `nds → melonds_ds`; melonDS DS ist Bestandteil des Windows-Players. Der Player meldet Plattform und verfügbare Core-Versionen, damit die Kompatibilität geprüft werden kann. Eine fehlende oder unpassende Core-Version wird sichtbar gemeldet; der PoC enthält keinen automatischen Core-Paketmanager.

Später kommt ein **Core Package Cache** auf dem Hub hinzu. Registrierte Pakete enthalten Core-ID, Version, Plattform/Architektur, SHA-256 und Herkunft sowie die für die Verteilung erforderlichen Lizenzinformationen. Für die spätere automatische Core-Verteilung sind außerdem Paket-Signaturen beziehungsweise vertrauenswürdige Manifestquellen vorzusehen; SHA-256 allein bestätigt keine vertrauenswürdige Herkunft. Diese Vertrauensprüfung erweitert den PoC nicht. „Auf Hub gecacht“ bezeichnet ein gespeichertes Paket, keine dort installierte oder ausgeführte Emulation.

Der spätere Ablauf ist: Player fragt den vorgesehenen Core an → Hub liefert Paketmetadaten → Player prüft lokale Version, Plattform und Hash → Download nur bei Bedarf → Hash-/Versionsprüfung → Aufnahme in den lokalen Core-Cache → lokale Ausführung. Mehrere Versionen können parallel gecacht werden; eine spätere versionsgebundene Spielzuordnung bleibt möglich.

ROM-Cache und Core-Cache bleiben getrennt, beispielsweise:

```text
Player Data/cache/
├── roms/<sha256>.nds
└── cores/<core-id>/<version>/<platform>/
```

### Sichtbare Cache- und Bereitschaftszustände

Library und Emulation zeigen verständliche Zustände für die jeweils benötigte ROM beziehungsweise den Core. Verfügbarkeit, Transfer und Validierung werden getrennt erfasst, damit ein Paket zugleich auf dem Hub vorhanden und lokal fehlerhaft sein kann.

| Zustand | Bedeutung / UI-Aktion |
|---|---|
| Lokal gecacht / bereit | Lokale Datei ist vorhanden und validiert; im PoC kann der Core auch mitgeliefert sein |
| Nur auf Hub vorhanden | ROM oder späteres Core-Paket liegt auf dem Hub, lokal fehlt es |
| Download nötig | Benötigte lokale Datei fehlt; Download anbieten beziehungsweise beim Start auslösen |
| Download läuft / fehlgeschlagen | Fortschritt beziehungsweise verständlicher Fehler mit Wiederholungsmöglichkeit |
| Hash mismatch | Lokale Datei stimmt nicht mit dem erwarteten SHA-256 überein; nicht verwenden, erneut beziehen |
| Version mismatch | Core-Version entspricht nicht der vorgesehenen Version; passende Version erforderlich |
| Core nicht verfügbar / inkompatibel | Kein passender Core für die Player-Plattform; Start nicht möglich |

Der Status ist auf das jeweilige Gerät und Artefakt bezogen. Die Hub-Registry kann gemeldete Client-Kompatibilität anzeigen; sie suggeriert keine Core-Ausführung auf dem Hub. Vollständige Hashes und weitere technische Details können in einer Detailansicht liegen.

## 11. Emulation und Einstellungs-Hierarchie im Player

Der Player erhält eine eigene Seite **„Emulation“** mit Systemen, verfügbaren Cores, Version und Bereitschaftszustand sowie deren Konfiguration. Sie umfasst allgemeine, Grafik-, Audio- und core-spezifische Optionen, soweit der jeweilige Core sie unterstützt.

FrameBeam-eigene Optionen wie Vollbild, UI-Skalierung, Präsentation und Multiview-Layout bleiben von Emulator-Optionen wie interner Auflösung, Renderer und core-spezifischer Audioverarbeitung unterscheidbar.

Core-Einstellungen sollen möglichst **dynamisch aus Libretro Core Options** erzeugt werden. Gemeldete Kategorien, Beschreibungen, zulässige Werte und Defaults dienen als Grundlage. Enumerierte Optionen erscheinen als Auswahlfelder; Toggle, Zahlenfeld, Slider oder Dateiauswahl werden nur verwendet, wenn passende Typ-/Validierungsinformationen vorliegen. Libretro-Optionen liefern nicht automatisch für jede Option ein freies Zahlen- oder Pfadfeld. Zusätzliche FrameBeam-Metadaten oder Backend-Adapter können die Darstellung ergänzen. Nicht unterstützte Optionen werden nicht erfunden; nötige Neustarts beziehungsweise verzögerte Wirksamkeit sind kenntlich zu machen.

Die Konfiguration sieht folgende Hierarchie vor:

```text
Global → System/Core → Game Override
```

Eine spezifischere Ebene überschreibt nur explizit gesetzte Werte. Nicht gesetzte Werte werden geerbt; das Entfernen eines Overrides stellt die Vererbung wieder her. Das Datenmodell speichert deshalb partielle Overrides mit System-/Core- beziehungsweise Game-Zuordnung statt vollständiger Konfigurationskopien. Die UI soll Herkunft und wirksamen Wert erkennbar machen. Globale Vorgaben gelten nur, soweit der ausgewählte Core sie unterstützt; core-spezifische Optionsschlüssel bleiben dem Core zugeordnet.

Im PoC bleiben Emulations-Einstellungen lokal. Die Hierarchie wird konzeptionell und im Datenmodell vorbereitet; eine vollständige Game-Override-UI ist keine Voraussetzung für den PoC. Eine optionale spätere Synchronisierung von Emulations-Einstellungen über den Hub ist offen.

## 12. Controllers und lokale Input-Profile

Der Player erhält eine eigene Seite **„Controllers“**, getrennt von Emulation/Core Settings. Sie bietet lokale Controllerprofile, Geräte-/Spielerzuordnung, Remapping, Zurücksetzen und einen Input-Test. Gamepads und Tastatur können eigene Profile haben; DS-Touch über Maus bleibt Teil des PoC.

```text
Physischer Controller / Tastatur
              ↓
Lokales Controllerprofil / Remapping
              ↓
FrameBeam Input
              ↓
Systemprofil-Mapping, z. B. nds
              ↓
Libretro Input
```

Das Systemprofil übersetzt die einheitlichen FrameBeam-Eingaben in die Eingaben des emulierten Systems. Controllerprofile und gerätespezifische Zuordnungen bleiben **lokal im Player und werden nicht zentral synchronisiert**. Sie sind keine Hub-Verwaltungsfunktion.

## 13. Save-Konflikte: Datenmodell und UI

Save-Versionierung wird um einen ausdrücklich modellierten Konfliktzustand ergänzt. Ein neuer Upload verweist auf die beim Abgleich verwendete **Basisversion**. Hat sich die aktuelle Hub-Version inzwischen geändert, darf eine abweichende lokale Änderung sie nicht still überschreiben. Eine Offline-Änderung mit veralteter Basis kann so ebenso erkannt werden wie Änderungen auf zwei Geräten. Zeitstempel dienen der Anzeige, nicht als alleinige Konfliktentscheidung.

`base_version` bezeichnet auch bei Auto-Checkpoints die beim Abgleich gelesene Current-Checkpoint-Revision. Checkpoint-Revision und permanente History-Version sind getrennt; eine Konfliktauflösung sichert zuvor die konkurrierenden Inhalte in der History.

Vorgesehene Metadaten sind Save-Zuordnung (Spiel/Nutzer beziehungsweise bestehender Save-Slot), Versions-ID, Basisversions-ID, Inhalts-Hash, Herkunftsgerät und Zeitstempel. Ein Konfliktdatensatz referenziert die konkurrierenden Versionen beziehungsweise den gesicherten lokalen Upload, seinen Status und eine spätere Auflösungsentscheidung. Beide Inhalte bleiben erhalten, bis eine bewusste Auswahl erfolgt; Save-Dateien liegen weiterhin im Dateisystem.

Der Hub zeigt Konflikte auf der **Saves-Seite** neben der Versionshistorie. Der Player zeigt den Sync-/Konfliktzustand beim betroffenen Spiel und beim Start-, Checkpoint- und Final-Abgleich. Die Detailansicht macht Herkunft, Zeitpunkt und Versionen vergleichbar und sieht Aktionen wie „Hub-Version verwenden“, „Lokalen Save als neue aktuelle Version übernehmen“ und „Beide Versionen behalten / später entscheiden“ vor. Auch eine Auflösung darf keinen stillen Datenverlust erzeugen und muss gegen zwischenzeitliche Änderungen geprüft werden.

Die konkrete API, Auflösungslogik und Startentscheidung bei ungelöstem Konflikt bleiben zu spezifizieren. Für den PoC sind Datenmodell und UI-Zustand vorzusehen; automatisches Zusammenführen binärer Spielstände ist nicht vereinbart.

## 14. Hub-Verbindungen, Identität und Pairing [PoC]

### Rollen, Benutzeranlage und Identity

FrameBeam besitzt keine globale Account-Infrastruktur, keine öffentliche Registrierung und keine globale User-ID. Der Hub verwaltet **hub-lokale User-IDs**; derselbe Mensch kann auf verschiedenen Hubs unterschiedliche Identitäten besitzen.

Mindestens **Admin** und **User** sind vorgesehen. Admin verwaltet Library, Benutzer, Clients, Systeme/Cores einschließlich Firmware, Hub-Einstellungen und Pairings. Normale User nutzen Library, ihre Saves und Sessions gemäß Session-Berechtigungen. Library-Verwaltung ist standardmäßig Admin-only. Die optionale Hub-Einstellung **„Allow users to upload games“** erlaubt normalen Usern eigene ROM-Uploads in dieselbe zentrale Library. Einträge erhalten `uploaded_by` als hub-lokale User-ID. Diese Freigabe verleiht keine allgemeine Library-Verwaltung und erlaubt insbesondere kein Löschen oder Verwalten bestehender fremder Einträge.

Beim ersten Hub-Start wird ein Admin-Account mit **Username + Passwort** eingerichtet. Der Admin nutzt klassischen Web-Login; der Hub speichert einen sicheren Passworthash, beispielsweise Argon2id. Weitere User werden ausschließlich Admin-kontrolliert angelegt, bevorzugt durch kurzlebige Invite-Codes/Invite-Links. Der Nutzer löst den Invite im Player ein und wählt einen Display-Namen; der Hub erstellt den lokalen User und kann dabei das erste Gerät direkt autorisieren. Benutzer-Onboarding-Links sind keine Session-Share-Links. Invite-Laufzeiten und konkrete Einlöse-Endpunkte bleiben zu spezifizieren.

Normale User benötigen **kein dauerhaftes Passwort**. User und Device sind getrennte Entitäten; ein User kann mehrere Devices besitzen. Der Player erzeugt lokal Device-ID und Gerätename. Diese Identität sowie lokales Profil/Display Name dienen der UX und begründen keine Authentifizierung. Der Admin ordnet eine Pairing-Anfrage einem bestehenden hub-lokalen User zu; eine frei angegebene User-ID wird nicht blind übernommen. Kein E-Mail-/Password-Recovery-System im PoC.

### Hub-Profile und Geräte-Credentials

Mehrere Hub-Profile sind lokal speicherbar. Ein Profil enthält Hub-ID, Anzeigename, Adresse, Hub-User-ID, lokale Device-ID, Credential-Referenz, gepinnten Zertifikats-Fingerprint und Zeitpunkt der letzten Verbindung.

Jedes autorisierte Gerät besitzt eigene **hub-spezifische Credentials**: ein kurzlebiges Access Token und ein langfristiges, widerrufbares Device/Refresh Credential, gebunden an Hub, User und Device. Das langfristige Secret liegt ausschließlich im **OS-Credential-Store** (Windows Credential Manager im PoC, später macOS Keychain/Linux Secret Service), niemals in Profil-Dateien oder Logs. Der Hub speichert eine sichere Prüfrepräsentation statt Klartext. Exakte Laufzeiten und Erneuerungsregeln bleiben offen. Ein verlorenes Gerät kann einzeln widerrufen werden; Widerruf muss auch weiteren Zugriff mit dessen Access Tokens entziehen.

**Genau ein Hub ist pro verbundener Player-Instanz aktiv.** Vor einer Verbindung oder nach dem Trennen kann kein Hub aktiv sein. Library, Saves, Sessions und Core Registry stammen ausschließlich vom ausgewählten Hub; Profile erzeugen weder parallele Verbindungen noch eine zusammengeführte Library. Lokale Hub-Daten und pending sync bleiben nach Hub-ID und gegebenenfalls Hub-User-ID getrennt, auch bei identischen ROM-Hashes.

### Verbindung und Approval-Pairing [PoC]

1. Der Nutzer trägt im Connection-Screen die Hub-Adresse ein oder wählt ein Profil.
2. Der Player identifiziert den Hub über einen FrameBeam-Info-Endpunkt, beispielsweise `GET /.well-known/framebeam`, mit Hub-ID, Name, Hub-Version und Protokollinformationen; der konkrete Pfad bleibt offen. Dies ist adressbasierte Identifikation, keine notwendige automatische Netzwerksuche. TLS-Vertrauen wird gemäß dem folgenden Abschnitt geprüft; der Capability Handshake erfolgt beim Connect.
3. Ohne gültige Geräte-Credentials stellt der Player eine **Pairing-Anfrage**. Im Hub-Webinterface erscheint eine **Pending Request** mit Device Name, Plattform und Player-Version.
4. Der Admin ordnet den User zu und klickt **Allow / Deny**. Erst Allow registriert und autorisiert das Gerät. Anfragen/Versuche werden begrenzt; der Info-Endpunkt und eine Pending Request verleihen keine Rechte. Ein gültiger Admin-Onboarding-Invite kann das erste Gerät direkt autorisieren.
5. Nach erfolgreicher Freigabe erhält das Gerät Hub-ID, User-Zuordnung und eigene Access-/Device-Credentials. Weitere Verbindungen verwenden kurzlebige Access Tokens; das Device/Refresh Credential ermöglicht deren Erneuerung.
6. **Clients** zeigt **Trusted / Revoked** und **Revoke access**. Bei Widerruf oder ungültigen Credentials führt der Player zurück zur erforderlichen Freigabe.

Ein kurzlebiger Pairing-Code darf als Alternative vorgesehen werden; **Player-Anfrage → Admin Allow/Deny ist der bevorzugte Standard und verpflichtende PoC-Flow**. Ein Code-only-Flow ersetzt ihn nicht.

Optional verbindet sich der Player beim Start mit dem zuletzt verwendeten Hub. Ist dieser nicht erreichbar, erscheint die Verbindungsauswahl mit verständlichem Fehler und Wiederholungsmöglichkeit.

### Transport Security und Zertifikatsvertrauen [PoC]

Im produktiven Betrieb sind **HTTPS/WSS zwingend** für API, Signaling, Authentifizierung und Dateiübertragung. HTTP/WS ist ausschließlich explizit im Dev-Modus beziehungsweise auf localhost erlaubt.

Für einfache Self-hosted-/LAN-Nutzung kann der Hub beim ersten Start ein eigenes TLS-Zertifikat erzeugen. Eigenes cert/key und Betrieb hinter einem Reverse Proxy sind ebenfalls möglich. Der Player verwendet beim ersten Pairing **Trust-on-first-use / Certificate Pinning**: Zertifikats-Fingerprint anzeigen/bestätigen und im Hub-Profil speichern. Spätere Abweichungen erzeugen Warnung/Fehler und werden nicht still als neuer Trust übernommen. Konkrete Zertifikatsverwaltung einschließlich Erneuerung und bestätigtem Pin-Wechsel bleibt zu spezifizieren. Ein eigener ACME-Client liegt außerhalb des PoC. WebRTC ist unabhängig davon separat verschlüsselt.

### Wechseln und Entfernen

Ein Hub-Wechsel beendet zunächst die alte Verbindung einschließlich Presence und Signaling. Laufende lokale/geteilte Sessions werden vor dem Wechsel beendet; ausstehende Saves werden zum bisherigen Hub hochgeladen oder eindeutig für diesen Hub zur späteren Wiederholung gesichert. Scheitert der Abgleich, muss die UI eine bewusste Entscheidung ermöglichen. Ein Upload darf niemals an den neu gewählten Hub umgeleitet werden. Erst danach wird der neue Hub verbunden.

Beim Entfernen eines Profils löscht der Player die lokale Profilzuordnung und deren Credentials; offene Saves werden zuvor geklärt. Hub-Konto, ROMs und zentrale Saves werden dadurch nicht gelöscht. Geräte-Credentials können im Hub unter **Clients** widerrufen werden; das lokale Entfernen allein ersetzt keinen serverseitigen Widerruf.

Der PoC umfasst dieses Hub-Profil-/Pairing-Grundmodell. Gleichzeitige Multi-Hub-Nutzung, Federation, Cross-Hub Sessions, OAuth und zentrale FrameBeam-Accounts sind ausgeschlossen. Geteilte Sessions im PoC verbinden Benutzer desselben aktiven Hubs.

## 15. Zentrale Spielmetadaten und Artwork [Future – außerhalb des PoC]

Der spätere **Hub Metadata Service** beschafft und normalisiert Spielinformationen zentral. Eine **Metadata-Provider-Abstraktion** kapselt externe APIs, Matching und die Übersetzung der Ergebnisse. ScreenScraper und IGDB sind lediglich mögliche Provider-Beispiele; kein Anbieter ist fest verdrahtet oder Voraussetzung für FrameBeam. Auswahl und konkrete Integration erfolgen später anhand der verfügbaren Schnittstellen und Nutzungsbedingungen.

```text
Hub-Library / ROM-Analyse
          ↓
Hub Metadata Service
          ↓
Provider-Abstraktion → optionale externe Provider
          ↓
Normalisiertes FrameBeam-Modell + manuelle Overrides
          ↓
Hub: SQLite-Metadata-Cache + Artwork-Dateicache
          ↓
Player: Library / Spielansicht über Hub-API
```

### Internes Metadata-Modell und Matching

Das interne Modell ist unabhängig von Provider-Datenformaten und ergänzt die bestehenden technischen ROM-/Library-Daten. Der erste spätere Metadata-Ausbau umfasst:

| Feld | Inhalt |
|---|---|
| Display Title | Anzeigename des Spiels |
| Release Date / Year | Erscheinungsdatum oder nur Jahr entsprechend der bekannten Genauigkeit |
| Developer / Publisher | Entwickler und Publisher |
| Genre | Ein oder mehrere normalisierte Genres |
| Kurzbeschreibung | Sprachabhängige kurze Spielbeschreibung |
| Region | Region der zugeordneten Veröffentlichung beziehungsweise ROM-Ausgabe |
| Boxart | Referenz auf ein vom Hub bereitgestelltes Artwork-Asset |

Zusätzlich hält der Hub Provider-Referenzen, Herkunft, Sprache/Region, Matching-Status und Aktualisierungszeit fest. Fehlende Werte bleiben zulässig. Screenshot, Fanart, Logos, Ratings, Videos und weitere Felder sind keine Voraussetzung für diesen ersten Ausbau.

Matching erfolgt möglichst **hashbasiert** und berücksichtigt System sowie ROM-Ausgabe. SHA-256 bleibt der Integritäts-Hash von FrameBeam; zusätzliche Matching-Hashes wie CRC32, MD5 oder SHA-1 können bei der späteren ROM-Analyse nach Provider-Bedarf berechnet werden. Nicht jeder Provider muss Hash-Matching unterstützen. Hashlose oder erfolglose Zuordnung kann auf System-/Titel-/Regionssuche zurückfallen; mehrdeutige Treffer erfordern eine manuelle Auswahl statt einer stillen falschen Zuordnung.

Library-Einträge können Zustände wie „Matched“, „No match“ und „Multiple matches“ besitzen. Manuelle Auswahl und feldweise **Overrides** erlauben Korrekturen. Overrides haben Vorrang vor Provider-Daten und bleiben bei einem Refresh erhalten, bis sie bewusst entfernt werden. Technische ROM-Identität und Hashes werden durch Metadata-Änderungen nicht verändert.

### Cache, Präferenzen und Oberfläche

Normalisierte Metadaten werden im Hub gespeichert; Artwork liegt im Hub-Dateicache, beispielsweise unter `/var/lib/framebeam/metadata/artwork/`. Cache-/Refresh-Regeln berücksichtigen die jeweiligen Provider-Bedingungen. Bereits gecachte Inhalte können ohne erneuten externen Zugriff bereitgestellt werden, soweit diese Regeln es erlauben.

Der Player bezieht **Metadata und Artwork ausschließlich über seinen aktiven Hub** und kennt keine externen Provider-Credentials. Die Hub-API liefert das normalisierte Modell und Hub-eigene Asset-Referenzen statt externer Provider-URLs. Provider-Ausfälle verhindern keinen Start vorhandener ROMs; einfache Library-Bezeichnungen und Artwork-Platzhalter bleiben verfügbar.

Unter **Hub Settings → Metadata** liegen Provider-Konfiguration und Credentials, Verbindungstest sowie bevorzugte Sprache/Region und deren Fallbacks. Die tatsächliche ROM-/Veröffentlichungsregion bleibt von einer bevorzugten Darstellungsregion unterscheidbar. Verfügbare passende Varianten werden bevorzugt; fehlende Varianten fallen kontrolliert auf die konfigurierten Alternativen zurück.

Direkt im **Library-Eintrag** liegen Matching-Status und Aktionen wie „Refresh“, „Edit Metadata“ und „Change Match“. Es gibt keine neue Hub-Hauptseite für Metadata. Der Player zeigt später Boxart und Basis-Metadaten in Library und Spielansicht; diese Darstellung erweitert seine Hauptnavigation nicht.

**Der gesamte Metadata Service einschließlich Provider-Anbindung, Matching, Overrides, Präferenzen und Artwork-/Metadata-Cache liegt ausdrücklich außerhalb von FrameBeam 0.1 / PoC.** Im PoC genügen die bestehenden technischen Library-Daten, eine einfache Spielbezeichnung und Platzhalter. Es sind weder externe Provider-Zugriffe noch zusätzliche Matching-Hashes oder Metadata-Verwaltungsaktionen für den PoC erforderlich.
