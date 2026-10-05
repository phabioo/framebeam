# Architektur: Protokolle, Handshake, ROM-Cache

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
