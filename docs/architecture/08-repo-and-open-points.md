# Architektur: Repository und offene Punkte

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
