#include "updateindex.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace framebeam::update {

namespace {

bool validUrl(const QString& s, bool allowFile) {
  const QUrl u(s, QUrl::StrictMode);
  if (!u.isValid() || u.isRelative()) return false;
  if (u.scheme() == QLatin1String("https")) return !u.host().isEmpty();
  return allowFile && u.scheme() == QLatin1String("file");
}

bool validName(const QString& n) {
  return !n.isEmpty() && n.size() <= 200 && !n.contains(QLatin1Char('/')) && !n.contains(QLatin1Char('\\')) &&
         n != QLatin1String(".") && n != QLatin1String("..") && !n.contains(QChar(0));
}

// Reads an integer field strictly (a JSON number without fraction).
bool readInt(const QJsonObject& o, const char* key, int* out) {
  const QJsonValue v = o.value(QLatin1String(key));
  if (!v.isDouble()) return false;
  const double d = v.toDouble();
  if (d != static_cast<int>(d) || d < 0 || d > 1000000) return false;
  *out = static_cast<int>(d);
  return true;
}

// Returns an empty string and fills `out` on success, otherwise a reason.
QString parseArtifact(const QJsonObject& o, bool allowFile, Artifact* out) {
  static const QRegularExpression sha(QStringLiteral("^[0-9a-f]{64}$"));
  out->platform = o.value(QLatin1String("platform")).toString();
  out->kind = o.value(QLatin1String("kind")).toString();
  out->name = o.value(QLatin1String("name")).toString();
  out->sha256 = o.value(QLatin1String("sha256")).toString();
  out->url = o.value(QLatin1String("url")).toString();
  const QJsonValue sz = o.value(QLatin1String("size"));
  if (out->platform.isEmpty() || out->kind.isEmpty()) return QStringLiteral("artifact without platform/kind");
  if (!validName(out->name)) return QStringLiteral("artifact name invalid");
  if (!sz.isDouble() || sz.toDouble() < 1 || sz.toDouble() != static_cast<double>(static_cast<qint64>(sz.toDouble()))) {
    return QStringLiteral("artifact size invalid");
  }
  out->size = static_cast<qint64>(sz.toDouble());
  if (!sha.match(out->sha256).hasMatch()) return QStringLiteral("artifact sha256 invalid");
  if (!validUrl(out->url, allowFile)) return QStringLiteral("artifact url must be https");
  return {};
}

QString parseRelease(const QJsonObject& o, bool allowFile, Release* r) {
  static const QRegularExpression commitRe(QStringLiteral("^[0-9a-f]{7,64}$"));
  r->product = o.value(QLatin1String("product")).toString();
  if (r->product != QLatin1String("hub") && r->product != QLatin1String("player")) {
    return QStringLiteral("unknown product");
  }
  r->channel = o.value(QLatin1String("channel")).toString();
  if (r->channel != QLatin1String("beta") && r->channel != QLatin1String("stable")) {
    return QStringLiteral("unknown channel");
  }
  r->version = o.value(QLatin1String("version")).toString();
  const auto sv = SemVer::parse(r->version);
  if (!sv) return QStringLiteral("version is not SemVer");
  r->semver = *sv;
  r->commit = o.value(QLatin1String("commit")).toString();
  if (!r->commit.isEmpty() && !commitRe.match(r->commit).hasMatch()) return QStringLiteral("commit invalid");
  r->publishedAt = o.value(QLatin1String("published_at")).toString();
  if (!r->publishedAt.isEmpty() && !QDateTime::fromString(r->publishedAt, Qt::ISODate).isValid()) {
    return QStringLiteral("published_at invalid");
  }
  r->notesUrl = o.value(QLatin1String("notes_url")).toString();
  if (!r->notesUrl.isEmpty() && !validUrl(r->notesUrl, false)) return QStringLiteral("notes_url must be https");
  if (!readInt(o, "protocol_version", &r->protocolVersion) || !readInt(o, "min_protocol_version", &r->minProtocolVersion) ||
      r->protocolVersion < 1 || r->minProtocolVersion < 1 || r->minProtocolVersion > r->protocolVersion) {
    return QStringLiteral("protocol versions invalid");
  }
  const QJsonValue arts = o.value(QLatin1String("artifacts"));
  if (!arts.isArray() || arts.toArray().isEmpty()) return QStringLiteral("no artifacts");
  QSet<QString> seen;
  for (const QJsonValue& av : arts.toArray()) {
    Artifact a;
    if (!av.isObject()) return QStringLiteral("artifact is not an object");
    const QString why = parseArtifact(av.toObject(), allowFile, &a);
    if (!why.isEmpty()) return why;
    if (seen.contains(a.platform + QLatin1Char('/') + a.kind)) return QStringLiteral("duplicate artifact platform/kind");
    seen.insert(a.platform + QLatin1Char('/') + a.kind);
    r->artifacts.append(a);
  }
  return {};
}

}  // namespace

std::optional<Artifact> Release::artifact(const QString& platform, const QString& kind) const {
  for (const Artifact& a : artifacts) {
    if (a.platform == platform && a.kind == kind) return a;
  }
  return std::nullopt;
}

QJsonObject Release::toJson() const {
  QJsonArray arts;
  for (const Artifact& a : artifacts) {
    arts.append(QJsonObject{{"platform", a.platform}, {"kind", a.kind}, {"name", a.name},
                            {"size", static_cast<double>(a.size)}, {"sha256", a.sha256}, {"url", a.url}});
  }
  return QJsonObject{{"product", product}, {"channel", channel}, {"version", version}, {"commit", commit},
                     {"published_at", publishedAt}, {"notes_url", notesUrl}, {"protocol_version", protocolVersion},
                     {"min_protocol_version", minProtocolVersion}, {"artifacts", arts}};
}

ParseResult parseIndex(const QByteArray& json, bool allowFileUrls) {
  ParseResult res;
  QJsonParseError perr;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
    res.error = QStringLiteral("update index is not valid JSON");
    return res;
  }
  const QJsonObject root = doc.object();
  const QJsonValue schema = root.value(QLatin1String("schema"));
  if (!schema.isDouble() || schema.toInt() != 1) {
    res.error = QStringLiteral("unsupported update index schema");
    return res;
  }
  const QJsonValue rels = root.value(QLatin1String("releases"));
  if (!rels.isArray()) {
    res.error = QStringLiteral("update index has no releases array");
    return res;
  }
  res.index.generatedAt = root.value(QLatin1String("generated_at")).toString();
  QSet<QString> keys;
  int pos = 0;
  for (const QJsonValue& rv : rels.toArray()) {
    ++pos;
    Release r;
    QString why = rv.isObject() ? parseRelease(rv.toObject(), allowFileUrls, &r) : QStringLiteral("not an object");
    if (!why.isEmpty()) {
      res.index.skipped.append(QStringLiteral("release #%1 skipped: %2").arg(pos).arg(why));
      continue;
    }
    const QString key = r.product + QLatin1Char('|') + r.channel + QLatin1Char('|') + r.semver.toString();
    if (keys.contains(key)) {
      res.error = QStringLiteral("duplicate release %1/%2 %3 in the update index").arg(r.product, r.channel, r.version);
      res.index = {};
      return res;
    }
    keys.insert(key);
    res.index.releases.append(r);
  }
  res.ok = true;
  return res;
}

QString channelName(Channel c) {
  switch (c) {
    case Channel::Stable: return QStringLiteral("stable");
    case Channel::Beta: return QStringLiteral("beta");
    case Channel::Off: break;
  }
  return QStringLiteral("off");
}

std::optional<Channel> parseChannel(const QString& name) {
  const QString n = name.trimmed().toLower();
  if (n == QLatin1String("stable")) return Channel::Stable;
  if (n == QLatin1String("beta") || n == QLatin1String("test")) return Channel::Beta;  // "test": old alias
  if (n == QLatin1String("off")) return Channel::Off;
  return std::nullopt;
}

Channel channelFromCompiled(const QString& compiled) {
  const auto c = parseChannel(compiled);
  return c ? *c : Channel::Off;  // "dev" and unknown values: no automatic checks
}

bool protocolCompatible(const Release& c, const std::optional<HubProtocol>& hub) {
  if (!hub || hub->protocolVersion <= 0) return true;  // no known Hub
  return c.protocolVersion >= hub->minProtocolVersion && hub->protocolVersion >= c.minProtocolVersion;
}

QString Selection::statusName(Status s) {
  switch (s) {
    case Status::Disabled: return QStringLiteral("disabled");
    case Status::UpToDate: return QStringLiteral("up_to_date");
    case Status::Available: return QStringLiteral("available");
    case Status::Incompatible: return QStringLiteral("incompatible");
  }
  return QStringLiteral("disabled");
}

Selection selectRelease(const Index& index, const SelectionInput& in) {
  Selection sel;
  const auto cur = SemVer::parse(in.currentVersion);
  if (!cur) {
    sel.reason = QStringLiteral("The running version is not a SemVer version");
    return sel;
  }
  if (in.channel == Channel::Off) {
    sel.reason = QStringLiteral("Updates are off");
    return sel;
  }
  sel.status = Selection::Status::UpToDate;
  const Release* bestOk = nullptr;
  const Release* bestIncompat = nullptr;
  Artifact artOk;
  Artifact artIncompat;
  for (const Release& r : index.releases) {
    if (r.product != in.product) continue;
    const bool channelOk = r.channel == QLatin1String("stable") ||
                           (in.channel == Channel::Beta && r.channel == QLatin1String("beta"));
    if (!channelOk) continue;
    const auto art = r.artifact(in.platform, in.kind);
    if (!art) continue;
    if (SemVer::compare(r.semver, *cur) <= 0) continue;  // never an automatic downgrade; equal = up to date
    if (protocolCompatible(r, in.hub)) {
      if (bestOk == nullptr || SemVer::compare(r.semver, bestOk->semver) > 0) {
        bestOk = &r;
        artOk = *art;
      }
    } else if (bestIncompat == nullptr || SemVer::compare(r.semver, bestIncompat->semver) > 0) {
      bestIncompat = &r;
      artIncompat = *art;
    }
  }
  if (bestOk != nullptr) {
    sel.status = Selection::Status::Available;
    sel.release = *bestOk;
    sel.artifact = artOk;
  } else if (bestIncompat != nullptr) {
    sel.status = Selection::Status::Incompatible;
    sel.release = *bestIncompat;
    sel.artifact = artIncompat;
  }
  return sel;
}

QJsonObject selectionToJson(const Selection& sel, const SelectionInput& in, const QStringList& skipped) {
  QJsonObject o{{"status", Selection::statusName(sel.status)},
                {"current_version", in.currentVersion},
                {"channel", channelName(in.channel)}};
  if (!sel.reason.isEmpty()) o.insert("reason", sel.reason);
  if (sel.status == Selection::Status::Available || sel.status == Selection::Status::Incompatible) {
    o.insert("release", sel.release.toJson());
    o.insert("artifact", QJsonObject{{"platform", sel.artifact.platform}, {"kind", sel.artifact.kind},
                                     {"name", sel.artifact.name}, {"size", static_cast<double>(sel.artifact.size)},
                                     {"sha256", sel.artifact.sha256}, {"url", sel.artifact.url}});
  }
  QJsonArray sk;
  for (const QString& s : skipped) sk.append(s);
  o.insert("skipped", sk);
  return o;
}

}  // namespace framebeam::update
