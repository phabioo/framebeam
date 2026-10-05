#pragma once

#include <QString>

namespace framebeam {

// Inhaltsadressierter, hubuebergreifender ROM-Cache: <dir>/<sha256>.<ext>.
// Download landet in <sha256>.<ext>.part; erst nach SHA-256-Pruefung wird atomar umbenannt.
// Ein Treffer gilt nur nach Validierung. Um grosse Dateien nicht jedes Mal zu hashen, haelt die
// Sidecar-Datei <sha256>.<ext>.ok (Groesse + mtime) den letzten erfolgreichen Check fest.
class RomCache {
 public:
  enum class CommitResult { Ok, HashMismatch, IoError };

  explicit RomCache(const QString& dir);

  static bool isValidSha256(const QString& sha256);  // 64 Hex, klein
  static QString extensionFromFilename(const QString& filename);  // [a-z0-9]{1,8}, sonst "bin"
  static bool sha256OfFile(const QString& path, QString* hexOut);

  const QString& dir() const { return dir_; }
  QString finalPath(const QString& sha256, const QString& ext) const;
  QString partPath(const QString& sha256, const QString& ext) const;

  // Schnelle Pruefung ohne Hashen (UI-Thread-tauglich). Eine Datei mit falscher Groesse wird entfernt.
  enum class Probe { Missing, Valid, Unverified };  // Valid: Sidecar passt; Unverified: Hash noetig (off-thread)
  Probe probe(const QString& sha256, const QString& ext, qint64 expectedSize) const;
  void markVerified(const QString& sha256, const QString& ext) const { writeSidecar(sha256, ext); }
  void dropFinal(const QString& sha256, const QString& ext) const;
  // .part wurde bereits (inkrementell) verifiziert: atomar umbenennen + Sidecar.
  bool commitVerified(const QString& sha256, const QString& ext) const;

  // Blockierender, validierter Treffer (hasht ggf. die ganze Datei; nicht im UI-Thread verwenden).
  // Validierter Treffer. Eine beschaedigte Cache-Datei (Groesse/Hash passt nicht) wird entfernt.
  bool lookup(const QString& sha256, const QString& ext, qint64 expectedSize, QString* pathOut = nullptr) const;

  qint64 partSize(const QString& sha256, const QString& ext) const;
  void discardPart(const QString& sha256, const QString& ext) const;
  // Prueft die .part-Datei; Ok: atomar umbenannt + Sidecar. HashMismatch: .part geloescht.
  CommitResult verifyAndCommit(const QString& sha256, const QString& ext) const;

 private:
  void writeSidecar(const QString& sha256, const QString& ext) const;
  bool sidecarMatches(const QString& sha256, const QString& ext) const;

  QString dir_;
};

}  // namespace framebeam
