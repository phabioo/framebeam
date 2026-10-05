# Architektur: PoC-Scope

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
