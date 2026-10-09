#pragma once
// Glyph sets of the Controllers page ("Button labels", design 3f-2/3f-3): which PadGlyph chip and which name a physical
// binding token gets in the Xbox, PlayStation, Generic (names of the emulated system) and Keyboard sets, and the cells
// of the controller-shaped input test. Pure functions, no SDL; the Generic names come from the built-in gamepad profile
// (frameBeamInputs(), builtinProfile), nothing per system is hard-coded here or in QML.

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace framebeam::ui {

// Set ids: "xbox" | "playstation" | "generic" | "keyboard".
QString labelSetTitle(const QString& set);  // "Xbox" | "PlayStation" | "Generic" | "Keyboard"

// One token of a profile: {glyph, name, group}. glyph = PadGlyph name or chip text ("" = none); group = "dpad" | "stick" | "".
QVariantMap tokenGlyph(const QString& set, const QString& token);

// Chips and name of a mapping field: {glyphs: [string], name: string}. Several tokens collapse into one entry per
// glyph; a D-pad direction together with the same direction of the left stick is "D-pad · or left stick".
// No tokens: no glyph, name "Unassigned".
QVariantMap describeBinding(const QString& set, const QStringList& tokens);

// Cells of the 7 x 5 input test: [{id, glyph, col, row, span, square, tokens}] (col/row 0-based). id = the FrameBeam
// input that the built-in profile binds to the position ("a", "up", ...; "lt"/"rt" for the triggers). tokens = physical
// tokens that light the cell. The keyboard uses the generic names.
QVariantList inputTestCells(const QString& set);

}  // namespace framebeam::ui
