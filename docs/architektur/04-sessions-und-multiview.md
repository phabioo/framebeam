# Architektur: Sessions und Multiview

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
