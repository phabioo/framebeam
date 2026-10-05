# server – FrameBeam Hub

Go ohne großes Framework, SQLite, Dateispeicher unter `/var/lib/framebeam/`. Webinterface mit `html/template` + htmx, per `embed` im Binary (ADR 0001).
Verwaltet Library, Saves, Benutzer, Geräte, Registry, Presence und Signaling; emuliert, encodiert und rendert nie.
Architektur: `docs/architektur.md` Abschnitte 2, 3, 4, 9, 10, 13, 14.
