#pragma once
// Frame layout of the FrameBeam test core (test_core.cpp), shared with the tests that read it back. Header-only, no Qt.
//
// Frame (software path): 256x384 XRGB8888, two NDS-like screens stacked without a gap.
//   Top screen (y 0..191): four quadrants red | green over blue | white, plus an 8x8 cyan square in rows 0..7
//     whose x moves with the frame number (so successive frames differ, deterministically).
//   Bottom screen (y 192..383): magenta backdrop with two input strips and a data row:
//     y 192..207: port 0 joypad, y 208..223: port 1 joypad. Cell i (x = 16*i .. 16*i+15) is black while
//     RETRO_DEVICE_ID_JOYPAD id i is pressed (B Y Select Start Up Down Left Right A X L R L2 R2 L3 R3), else magenta.
//     y 240: data row, pixel i = 0xFF000000 | value (24 bits), see DataIndex.
// OpenGL path (render mode "opengl"): the same quadrants and magenta, scaled by the resolution option, no strips.
#include <cstdint>

namespace framebeam::testcore {
constexpr int kWidth = 256;
constexpr int kHeight = 384;
constexpr int kStripY0 = 192;       // port 0; port 1 starts 16 rows lower
constexpr int kStripHeight = 16;
constexpr int kCellWidth = 16;
constexpr int kDataRow = 240;
constexpr uint32_t kMagenta = 0xFFFF00FF;
constexpr uint32_t kPressed = 0xFF000000;

enum DataIndex : int {
  DataFrame = 0,     // frames since retro_load_game
  DataPointerX,      // RETRO_DEVICE_ID_POINTER_X + 0x8000
  DataPointerY,      // RETRO_DEVICE_ID_POINTER_Y + 0x8000
  DataPointerDown,   // RETRO_DEVICE_ID_POINTER_PRESSED
  DataFastForward,   // GET_FASTFORWARDING (0/1)
  DataSram0,         // SAVE_RAM byte 0 (0 without SAVE_RAM)
  DataSram1,         // SAVE_RAM byte 1
  DataLoads,         // retro_load_game calls with a game since the library was loaded
  DataResets,        // retro_reset calls
  DataRomSize,       // size of the game file (low 24 bits)
  DataRomSum,        // byte sum of the game file (low 24 bits)
  DataOptionTag,     // (length << 8) | first character of the flavor's tag option (GET_VARIABLE)
  DataDirs,          // bit 0: GET_SYSTEM_DIRECTORY answered, bit 1: GET_SAVE_DIRECTORY answered
  DataBitmask,       // 1 when the joypad was read as one RETRO_DEVICE_ID_JOYPAD_MASK word, 0 per button
  DataVarUpdates,    // GET_VARIABLE_UPDATE answered true this many times
  DataCount
};

constexpr uint32_t pixel(uint32_t value24) { return 0xFF000000u | (value24 & 0xFFFFFFu); }
}  // namespace framebeam::testcore
