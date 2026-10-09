#include "controllersglyphs.h"

#include "controllerprofiles.h"

namespace framebeam::ui {

namespace {

struct Glyph {
  const char* token;
  const char* xboxGlyph;
  const char* xboxName;
  const char* psGlyph;
  const char* psName;
};

// SDL names follow the position of the buttons: a = south, b = east, x = west, y = north.
const Glyph kGlyphs[] = {
    {"a", "A", "A", "cross", "Cross"},
    {"b", "B", "B", "circle", "Circle"},
    {"x", "X", "X", "square", "Square"},
    {"y", "Y", "Y", "triangle", "Triangle"},
    {"leftshoulder", "LB", "Left bumper", "L1", "L1"},
    {"rightshoulder", "RB", "Right bumper", "R1", "R1"},
    {"lefttrigger", "LT", "Left trigger", "L2", "L2"},
    {"righttrigger", "RT", "Right trigger", "R2", "R2"},
    {"back", "View", "View", "Create", "Create"},
    {"start", "Menu", "Menu", "Options", "Options"},
    {"guide", "Guide", "Guide", "PS", "PS button"},
    {"leftstick", "LS", "Left stick (press)", "L3", "L3"},
    {"rightstick", "RS", "Right stick (press)", "R3", "R3"},
};

// Generic names for buttons the system profile does not use (the DS has no triggers, stick clicks or guide button).
const Glyph kGenericRest[] = {
    {"lefttrigger", "L2", "L2", "", ""},
    {"righttrigger", "R2", "R2", "", ""},
    {"guide", "Guide", "Guide", "", ""},
    {"leftstick", "L3", "L3", "", ""},
    {"rightstick", "R3", "R3", "", ""},
};

struct Direction {
  const char* token;
  const char* glyph;
  const char* name;
  const char* group;
};
const Direction kDirections[] = {
    {"dpup", "up", "D-pad", "dpad"},       {"dpdown", "down", "D-pad", "dpad"},
    {"dpleft", "left", "D-pad", "dpad"},   {"dpright", "right", "D-pad", "dpad"},
    {"lefty-", "up", "Left stick", "stick"},   {"lefty+", "down", "Left stick", "stick"},
    {"leftx-", "left", "Left stick", "stick"}, {"leftx+", "right", "Left stick", "stick"},
};

// Generic name of a physical button: the label of the FrameBeam input that the built-in profile binds first to it.
QString genericName(const QString& sdlName) {
  const ControllerProfile def = ControllerProfiles::builtinProfile(QStringLiteral("gamepad"));
  const QString token = padToken(sdlName);
  for (const InputDef& in : frameBeamInputs()) {
    const QStringList t = def.bindings.value(in.id);
    if (!t.isEmpty() && t.first() == token) return in.label;
  }
  for (const Glyph& g : kGenericRest) {
    if (sdlName == QLatin1String(g.token)) return QString::fromLatin1(g.xboxGlyph);
  }
  return {};
}

QVariantMap glyph(const QString& g, const QString& name, const QString& group = QString()) {
  return {{QStringLiteral("glyph"), g}, {QStringLiteral("name"), name}, {QStringLiteral("group"), group}};
}

}  // namespace

QString labelSetTitle(const QString& set) {
  if (set == QLatin1String("xbox")) return QStringLiteral("Xbox");
  if (set == QLatin1String("playstation")) return QStringLiteral("PlayStation");
  if (set == QLatin1String("keyboard")) return QStringLiteral("Keyboard");
  return QStringLiteral("Generic");
}

QVariantMap tokenGlyph(const QString& set, const QString& token) {
  if (!isPadToken(token)) {
    const QString label = tokenLabel(token);  // keyboard: key name
    return glyph(label, QString());
  }
  const QString n = token.mid(4);
  for (const Direction& d : kDirections) {
    if (n == QLatin1String(d.token)) return glyph(QString::fromLatin1(d.glyph), QString::fromLatin1(d.name), QString::fromLatin1(d.group));
  }
  if (set == QLatin1String("xbox") || set == QLatin1String("playstation")) {
    for (const Glyph& g : kGlyphs) {
      if (n == QLatin1String(g.token)) {
        return set == QLatin1String("xbox") ? glyph(QString::fromLatin1(g.xboxGlyph), QString::fromLatin1(g.xboxName))
                                            : glyph(QString::fromLatin1(g.psGlyph), QString::fromLatin1(g.psName));
      }
    }
  } else {
    const QString name = genericName(n);
    if (!name.isEmpty()) return glyph(name, name);
  }
  return glyph(tokenLabel(token), tokenLabel(token));
}

QVariantMap describeBinding(const QString& set, const QStringList& tokens) {
  QList<QVariantMap> entries;
  for (const QString& t : tokens) entries.append(tokenGlyph(set, t));
  const bool keyboard = set == QLatin1String("keyboard");
  QStringList glyphs, names;
  const auto has = [&](const char* group) {
    for (const QVariantMap& e : std::as_const(entries)) {
      if (e.value(QStringLiteral("group")).toString() == QLatin1String(group)) return true;
    }
    return false;
  };
  const bool dpadAndStick = has("dpad") && has("stick");
  for (const QVariantMap& e : std::as_const(entries)) {
    const QString g = e.value(QStringLiteral("glyph")).toString();
    QString name = e.value(QStringLiteral("name")).toString();
    const QString group = e.value(QStringLiteral("group")).toString();
    if (dpadAndStick) {
      if (group == QLatin1String("stick") && glyphs.contains(g)) continue;  // same direction as the D-pad chip
      if (group == QLatin1String("dpad")) name = QStringLiteral("D-pad · or left stick");
    }
    if (!g.isEmpty() && !glyphs.contains(g)) glyphs.append(g);
    if (!name.isEmpty() && !names.contains(name)) names.append(name);
  }
  QString name = keyboard ? QString() : names.join(QStringLiteral(" / "));
  if (tokens.isEmpty()) name = QStringLiteral("Unassigned");
  return {{QStringLiteral("glyphs"), glyphs}, {QStringLiteral("name"), name}};
}

QVariantList inputTestCells(const QString& setIn) {
  struct Pos {
    const char* sdl;
    const char* fallbackId;
    int col, row, span;
    bool square;
  };
  static const Pos kPositions[] = {
      {"lefttrigger", "lt", 0, 0, 2, false}, {"righttrigger", "rt", 5, 0, 2, false},
      {"leftshoulder", "", 0, 1, 2, false},  {"rightshoulder", "", 5, 1, 2, false},
      {"back", "", 2, 1, 1, false},          {"start", "", 4, 1, 1, false},
      {"y", "", 5, 2, 1, false},             {"x", "", 4, 3, 1, false},
      {"b", "", 6, 3, 1, false},             {"a", "", 5, 4, 1, false},
      {"dpup", "", 1, 2, 1, true},           {"dpleft", "", 0, 3, 1, true},
      {"dpright", "", 2, 3, 1, true},        {"dpdown", "", 1, 4, 1, true},
  };
  // Keyboard devices have no physical layout of their own: the grid shows the names of the emulated system.
  const QString set = setIn == QLatin1String("keyboard") ? QStringLiteral("generic") : setIn;
  const ControllerProfile def = ControllerProfiles::builtinProfile(QStringLiteral("gamepad"));
  QVariantList cells;
  for (const Pos& p : kPositions) {
    const QString sdl = QString::fromLatin1(p.sdl);
    const QString token = padToken(sdl);
    QString id = QString::fromLatin1(p.fallbackId);
    for (const InputDef& in : frameBeamInputs()) {
      const QStringList t = def.bindings.value(in.id);
      if (!t.isEmpty() && t.first() == token) id = in.id;
    }
    QString g;
    QStringList tokens{token};
    if (p.square) {
      for (const Direction& d : kDirections) {
        if (sdl == QLatin1String(d.token)) g = QString::fromLatin1(d.glyph);
      }
      // The left stick drives the D-pad inputs in the built-in profile: it lights the same cell.
      for (const Direction& d : kDirections) {
        if (QLatin1String(d.group) == QLatin1String("stick") && QLatin1String(d.glyph) == g) tokens.append(padToken(QString::fromLatin1(d.token)));
      }
    } else {
      g = tokenGlyph(set, token).value(QStringLiteral("glyph")).toString();
    }
    cells.append(QVariantMap{{QStringLiteral("id"), id},
                             {QStringLiteral("glyph"), g},
                             {QStringLiteral("col"), p.col},
                             {QStringLiteral("row"), p.row},
                             {QStringLiteral("span"), p.span},
                             {QStringLiteral("square"), p.square},
                             {QStringLiteral("tokens"), tokens}});
  }
  return cells;
}

}  // namespace framebeam::ui
