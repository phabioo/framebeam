# fb-diag DataChannel

Between the Players of a Session (owner and each viewer) a negotiated WebRTC DataChannel `fb-diag` (id 0, same SDP flow, no Hub involvement) is opened. It brings up SCTP (RTT measurement) and carries small JSON text messages. Unknown message types and unknown fields are ignored by every reader, so new optional messages need no `protocol_version` bump.

| Message | Direction | Since | Schema |
|---|---|---|---|
| `{"t":"rx","loss":0..1,"kbps":n,"fps":n?,"dec":"h264"?}` | viewer to owner, once a second | 0.4 (`fps`, `dec`: 0.6) | ADR 0012 D5, ADR 0014 D7 |
| `{"t":"state","paused":bool,"by":"Name"?}` | owner to viewers | 0.14 | [schemas/dc-fb-diag-state.schema.json](schemas/dc-fb-diag-state.schema.json) |

## Host state (`state`, ADR 0026 D3)

The owner sends `state` when its game is paused or resumed, and once to every viewer whose channel opens while the game is paused. Viewers show "Paused by `<by>`" on the frozen tile and drop it on `paused: false`. `by` is the owner's display name; without it the viewer uses the Session owner's name. The message carries state only, never a picture or input.

Compatibility: a 0.13 viewer sets no message handler on the channel and a 0.13 owner never sends the message; a 0.13 owner reading a `state` message treats it like any non-`rx` message and ignores it. Viewers that connect to an older owner simply never see a paused state.
