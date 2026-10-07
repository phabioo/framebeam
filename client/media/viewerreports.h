#pragma once
// ViewerReports: the host keeps the latest rx report (fb-diag, ADR 0012 D5) of every viewer, so the diagnostics can show
// loss, received bitrate, decoded fps and decoder per viewer (0.6, D7). Plain data, no threads of its own: the owner
// guards it like the rest of its stats.

#include <QHash>
#include <QList>
#include <QString>

#include "bitratecontroller.h"
#include "mediastats.h"

namespace framebeam {

class ViewerReports {
 public:
  // The newest report replaces the previous one of that viewer (fields a report does not carry are cleared, not kept).
  void set(const QString& viewerId, const RxReport& r) { reports_.insert(viewerId, r); }
  void remove(const QString& viewerId) { reports_.remove(viewerId); }
  void clear() { reports_.clear(); }
  bool has(const QString& viewerId) const { return reports_.contains(viewerId); }

  // Copies the report of l.viewerId (if any) into the link entry.
  void applyTo(ViewerLinkStats& l) const {
    const auto it = reports_.constFind(l.viewerId);
    if (it == reports_.constEnd()) {
      l.hasReport = false;
      l.reportLoss = 0.0;
      l.reportKbps = 0.0;
      l.reportFps.reset();
      l.reportDecoder.clear();
      return;
    }
    l.hasReport = true;
    l.reportLoss = it->loss;
    l.reportKbps = it->kbps;
    l.reportFps = it->fps;
    l.reportDecoder = it->decoder;
  }
  void applyTo(QList<ViewerLinkStats>& links) const {
    for (ViewerLinkStats& l : links) applyTo(l);
  }

 private:
  QHash<QString, RxReport> reports_;
};

}  // namespace framebeam
