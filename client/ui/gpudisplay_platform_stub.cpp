// Zero-copy display (ADR 0027) on platforms without an implementation: always "not available" (the readback path stays).
#include "gpudisplaybridge.h"

namespace framebeam::ui {

PlatformDisplay makePlatformDisplay(QString* reason) {
  if (reason) *reason = QStringLiteral("not Windows");
  return {};
}

}  // namespace framebeam::ui
