#pragma once
// EmulationController: data and actions of the Emulation page (3e). Local settings only
// (<data>/settings/emulation.json): partial overrides in the hierarchy Global -> System/Core -> Game Override.
// Core options are listed exactly as the core reports them (core_options.h); FrameBeam's own options come
// from a small fixed list ("framebeam.*"). Options that FrameBeam controls (manifest-locked, firmware) are not offered.
// Changes are applied at the next launch.

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "core_options.h"
#include "emulationsettings.h"
#include "system_manifest.h"

namespace framebeam::ui {

class EmulationController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")

  // [{id, name, coreName, coreVersion, readyText, readyTone, firmwareText, firmwareTone}]
  Q_PROPERTY(QVariantList systems READ systems NOTIFY systemsChanged)
  Q_PROPERTY(QString selectedSystem READ selectedSystem NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap system READ system NOTIFY selectionChanged)  // card of the selected system, empty if none
  Q_PROPERTY(QString level READ level WRITE setLevel NOTIFY levelChanged)  // "global" | "system"
  // [{id, title, subtitle, note, options:[{key, label, description, category, values:[{value,label}], value,
  //   valueLabel, isSet, origin, restart}]}]
  Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
  // Page state of 3e: "Defaults" is level "global" (defaultsSelected), a system is level "system".
  Q_PROPERTY(bool defaultsSelected READ defaultsSelected NOTIFY levelChanged)
  Q_PROPERTY(int defaultsChangedCount READ defaultsChangedCount NOTIFY systemsChanged)
  // Category chips (names present in the current scope), filter ("" = All) and search text; both narrow `groups`.
  Q_PROPERTY(QStringList categories READ categories NOTIFY groupsChanged)
  Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY filterChanged)
  Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
  Q_PROPERTY(int changedCount READ changedCount NOTIFY groupsChanged)    // options of the current scope that differ from their default
  Q_PROPERTY(bool restartHint READ restartHint NOTIFY groupsChanged)    // a changed option applies only after the next game start
  Q_PROPERTY(bool gameRunning READ gameRunning NOTIFY gameRunningChanged)
  Q_PROPERTY(int lockedCount READ lockedCount NOTIFY groupsChanged)  // options of the core that FrameBeam controls
  Q_PROPERTY(QString coreNote READ coreNote NOTIFY groupsChanged)    // why the core group is empty, else empty

 public:
  static constexpr const char* kFullscreenKey = "framebeam.fullscreen_on_start";
  static constexpr const char* kMultiviewKey = "framebeam.default_multiview";

  EmulationController(const QString& dataDir, const emu::ManifestRegistry* manifests, QObject* parent = nullptr);

  EmulationSettings* settings() { return &settings_; }
  QVariantList systems() const;  // cards plus "changedCount" per system
  QString selectedSystem() const { return selected_; }
  QVariantMap system() const;
  QString level() const { return level_; }
  void setLevel(const QString& level);
  QVariantList groups() const { return groups_; }
  bool defaultsSelected() const { return level_ == QLatin1String("global"); }
  int defaultsChangedCount() const;
  QStringList categories() const { return categories_; }
  QString categoryFilter() const { return categoryFilter_; }
  void setCategoryFilter(const QString& category);
  QString searchText() const { return searchText_; }
  void setSearchText(const QString& text);
  int changedCount() const { return changedCount_; }
  bool restartHint() const { return restartHint_; }
  bool gameRunning() const { return gameRunning_; }
  int lockedCount() const { return lockedCount_; }
  QString coreNote() const { return coreNote_; }

  // Filled by the PlayerController (it knows cores, versions and the firmware state).
  void setSystems(const QVariantList& cards);
  void setGameRunning(bool running);
  // Core options of a core (probe or cache). Writes the cache file for a successful probe.
  void setCoreProbe(const QString& coreId, const emu::CoreProbe& probe, bool writeCache);
  bool hasCoreProbe(const QString& coreId) const;
  const emu::CoreProbe* coreProbe(const QString& coreId) const;
  void loadCoreCache(const QString& coreId);  // when no probe is possible

  // Effective value of a FrameBeam option for a system (and game): game > system > global > default.
  QString frameBeamValue(const QString& key, const QString& systemId, const QString& gameId = QString()) const;
  // Explicit user values for the launch (merged game > system > global).
  QMap<QString, QString> launchOverrides(const QString& systemId, const QString& gameId) const;

  Q_INVOKABLE void selectSystem(const QString& id);
  Q_INVOKABLE void setOption(const QString& key, const QString& value);
  Q_INVOKABLE void resetOption(const QString& key);
  Q_INVOKABLE void resetAllChanged();  // "Reset N changed": every explicit value of the current scope

 signals:
  void systemsChanged();
  void selectionChanged();
  void levelChanged();
  void groupsChanged();
  void filterChanged();
  void gameRunningChanged();
  void frameBeamOptionsChanged();  // a "framebeam.*" value changed (the PlayerController applies it)

 private:
  struct FbOption {
    QString key;
    QString label;
    QString description;
    QList<emu::CoreOptionValue> values;
    QString defaultValue;
  };
  static const QList<FbOption>& frameBeamOptions();
  QString coreIdOf(const QString& systemId) const;
  QString cachePath(const QString& coreId) const;
  void rebuild();
  QVariantMap row(const QString& key, const QString& label, const QString& description, const QString& category,
                  const QList<emu::CoreOptionValue>& values, const QString& manifestDefault, const QString& coreDefault,
                  bool frameBeam) const;
  bool knownOption(const QString& key, QList<emu::CoreOptionValue>* values) const;
  EmulationSettings::Level levelEnum() const;
  QString defaultValueOf(const QString& key) const;  // default the current scope falls back to (without its own value)
  QString scope() const { return level_ == QLatin1String("global") ? QString() : selected_; }

  QString dataDir_;
  const emu::ManifestRegistry* manifests_;
  EmulationSettings settings_;
  QVariantList systems_;
  QString selected_;
  QString level_ = QStringLiteral("system");
  QVariantList groups_;
  QHash<QString, emu::CoreProbe> probes_;  // core id -> options
  bool gameRunning_ = false;
  QStringList categories_;
  QString categoryFilter_;
  QString searchText_;
  int changedCount_ = 0;
  bool restartHint_ = false;
  bool restartTouched_ = false;  // an option that applies on the next start was changed while a game runs
  int lockedCount_ = 0;
  QString coreNote_;
};

}  // namespace framebeam::ui
