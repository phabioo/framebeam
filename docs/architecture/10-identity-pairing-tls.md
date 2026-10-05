# Architektur: Identität, Pairing, TLS

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
