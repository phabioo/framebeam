#pragma once

#include <QString>
#include <functional>

#include "emulationsettings.h"
#include "hubprotocol.h"

namespace framebeam {

// Effective core of a game (ADR 0020 D6): game setting > system setting > the Hub's default core (> legacy preferred core
// of a Hub without cores_v2), restricted to the cores the Hub serves. A stored choice of a core the Hub does not serve
// (any more) is ignored and reported in `staleChoice` so the UI can show a notice.
struct CoreChoice {
  SystemCore core;          // coreId empty = the Hub serves no core for the system
  QString source;           // "game" | "system" | "hub" (default) | "none"
  QString staleChoice;      // first stored core id that is not served; empty = none
  QString staleLevel;       // "game" | "system" level of that stored choice
  bool valid() const { return !core.coreId.isEmpty(); }
};

// `canonical` (optional) maps legacy ids to canonical ones so that a stored "melonds_ds" still matches a served "melondsds".
CoreChoice chooseCore(const SystemInfo& system, const EmulationSettings& settings, const QString& gameId,
                      const std::function<QString(const QString&)>& canonical = {});

}  // namespace framebeam
