# Architektur: UI und Navigation

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
