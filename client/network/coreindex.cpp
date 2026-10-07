#include "coreindex.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

#include "updatesig.h"

namespace framebeam {

namespace {
constexpr qint64 kMaxIndexBytes = 4 << 20;  // corepkg.MaxIndexBytes

CoreIndexCheck fail(const QString& why) {
  CoreIndexCheck c;
  c.error = why;
  return c;
}
}  // namespace

CoreIndexCheck verifyCorePackageAgainstIndex(const QByteArray& index, const QByteArray& sigFile, const QList<QByteArray>& trustedKeys,
                                             const CorePackageInfo& pkg) {
  if (index.isEmpty() || index.size() > kMaxIndexBytes) {
    return fail(QStringLiteral("core index has an invalid size"));
  }
  const update::SigCheck sig = update::verifyIndexSignature(index, sigFile, trustedKeys);
  if (!sig.ok) {
    return fail(QStringLiteral("core index signature: %1").arg(sig.error));
  }
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(index, &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    return fail(QStringLiteral("core index is not valid JSON"));
  }
  const QJsonObject root = doc.object();
  if (root.value(QStringLiteral("schema")).toInt(0) != 1) {
    return fail(QStringLiteral("unknown core index schema"));
  }
  QJsonObject entry;
  int found = 0;
  for (const QJsonValue& v : root.value(QStringLiteral("packages")).toArray()) {
    const QJsonObject p = v.toObject();
    if (p.value(QStringLiteral("core_id")).toString() == pkg.coreId && p.value(QStringLiteral("version")).toString() == pkg.version &&
        p.value(QStringLiteral("platform")).toString() == pkg.platform) {
      entry = p;
      ++found;
    }
  }
  if (found == 0) {
    return fail(QStringLiteral("package %1 %2 %3 is not in the signed core index").arg(pkg.coreId, pkg.version, pkg.platform));
  }
  if (found > 1) {
    return fail(QStringLiteral("package %1 %2 %3 is listed more than once in the core index").arg(pkg.coreId, pkg.version, pkg.platform));
  }
  struct IndexFile {
    QString role, sha256;
    qint64 size = -1;
  };
  QMap<QString, IndexFile> files;
  for (const QJsonValue& v : entry.value(QStringLiteral("files")).toArray()) {
    const QJsonObject f = v.toObject();
    IndexFile x;
    x.role = f.value(QStringLiteral("role")).toString();
    x.sha256 = f.value(QStringLiteral("sha256")).toString().toLower();
    x.size = f.value(QStringLiteral("size")).toVariant().toLongLong();
    files.insert(f.value(QStringLiteral("name")).toString(), x);
  }
  if (files.size() != pkg.files.size()) {
    return fail(QStringLiteral("file list of %1 %2 differs from the signed core index").arg(pkg.coreId, pkg.version));
  }
  for (const CorePackageFile& f : pkg.files) {
    const auto it = files.constFind(f.name);
    if (it == files.constEnd()) {
      return fail(QStringLiteral("file %1 is not in the signed core index").arg(f.name));
    }
    if (it->size != f.size) {
      return fail(QStringLiteral("size of %1 does not match the signed core index").arg(f.name));
    }
    if (it->sha256 != f.sha256.toLower()) {
      return fail(QStringLiteral("SHA-256 of %1 does not match the signed core index").arg(f.name));
    }
    if (it->role != f.role) {
      return fail(QStringLiteral("role of %1 does not match the signed core index").arg(f.name));
    }
  }
  CoreIndexCheck ok;
  ok.ok = true;
  return ok;
}

}  // namespace framebeam
