#pragma once

// Host state signal of a Session (ADR 0026 D3): the owner tells its viewers over the negotiated fb-diag DataChannel
// that its game is paused or running again, so viewers show "Paused by <name>" on the frozen picture.
//   {"t":"state","paused":true,"by":"Max"}
// State only: no picture, no input. Optional and backwards compatible (protocol_version stays 1): Players that do not know
// the message type ignore it (the host ignores every message that is not an rx report, a viewer sets no handler for
// messages), and a host without the feature never sends it. Documented in protocol/datachannel.md.

#include <QByteArray>
#include <QString>
#include <optional>

namespace uzume {

struct HostState {
  bool paused = false;
  QString by;  // display name of the user who paused; empty = unknown (the viewer falls back to the Session owner)
};

QByteArray makeHostState(bool paused, const QString& by);
// nullopt for anything that is not a well-formed state message (other types, bad JSON, "paused" not a bool).
// "by" is dropped when it is not a string, empty or longer than 64 characters (control characters are removed).
std::optional<HostState> parseHostState(const QByteArray& message);

}  // namespace uzume
