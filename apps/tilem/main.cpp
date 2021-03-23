#include <FrameBuffer.h>
#include <Input.h>
#include <UI.h>

#include "scancodes.h"
#include "tilem.h"

#include <atomic>
#include <csignal>
#include <iostream>
#include <optional>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "skin.h"

using namespace rmlib;
using namespace rmlib::input;

namespace {

volatile std::atomic_bool shouldStop = false;

std::optional<rmlib::ImageCanvas> skin;

std::optional<rmlib::Rect> lcd_rect;

TilemCalc* globalCalc = nullptr;

constexpr auto calc_save_extension = ".sav";
constexpr auto calc_default_rom = "ti84p.rom";

bool
showCalculator(rmlib::fb::FrameBuffer& fb) {
  if (!skin.has_value()) {
    skin = rmlib::ImageCanvas::load(assets_skin_png, assets_skin_png_len);
    if (!skin.has_value()) {
      std::cerr << "Error loading image\n";
      return false;
    }
  }

  std::cout << "calc skin size: " << skin->canvas.width() << "x"
            << skin->canvas.height() << std::endl;

  rmlib::transform(
    fb.canvas,
    { 0, 0 },
    skin->canvas,
    skin->canvas.rect(),
    [](int x, int y, int val) { return ((val & 0xff) / 16) << 1; });

  fb.doUpdate(skin->canvas.rect(),
              rmlib::fb::Waveform::GC16,
              rmlib::fb::UpdateFlags::FullRefresh);
  return true;
}

void
updateRect(rmlib::Rect& rect, int x, int y) {
  rect.topLeft.x = std::min(rect.topLeft.x, x);
  rect.topLeft.y = std::min(rect.topLeft.y, y);
  rect.bottomRight.x = std::max(rect.bottomRight.x, x);
  rect.bottomRight.y = std::max(rect.bottomRight.y, y);
}

bool
findLcd() {
  // find first red pixel
  skin->canvas.forEach([](int x, int y, int val) {
    if ((val & 0x00ff00) == 0x00ff00) {
      if (lcd_rect.has_value()) {
        updateRect(*lcd_rect, x, y);
      } else {
        lcd_rect = { { x, y }, { x, y } };
      }
    }
  });

  if (lcd_rect.has_value()) {
    std::cout << "LCD rect: " << *lcd_rect << std::endl;
    return true;
  }

  return false;
}

bool
loadKeymap() {
  findLcd();

  return true;
}

int
get_scancode(int x, int y) {
  if (x >= skin->canvas.width() || y >= skin->canvas.height()) {
    return -1;
  }

  auto pixel = skin->canvas.getPixel(x, y);
  auto code = (pixel & 0x00ff00) >> 8;

  // TODO: move to better location.
  if (code == 0xfe) {
    shouldStop = true;
    return -1;
  }

  if (code == 0 || code > TILEM_KEY_DEL) {
    return -1;
  }
  return code;
}

void
printEvent(const PenEvent& ev) {
  std::cout << "Pen ";
  switch (ev.type) {
    case PenEvent::ToolClose:
      std::cout << "ToolClose";
      break;
    case PenEvent::ToolLeave:
      std::cout << "ToolLeave";
      break;
    case PenEvent::TouchDown:
      std::cout << "TouchDown";
      break;
    case PenEvent::TouchUp:
      std::cout << "TouchUp";
      break;
    case PenEvent::Move:
      std::cout << "Move";
      break;
  }
  std::cout << " at " << ev.location;
  std::cout << " dist " << ev.distance << " pres " << ev.pressure << std::endl;
}

void
handleEvent(rmlib::fb::FrameBuffer& fb, rmlib::input::Event ev) {
  static int scanCodes[10];
  if (std::holds_alternative<rmlib::input::KeyEvent>(ev)) {
    std::cout << "key ev " << std::get<rmlib::input::KeyEvent>(ev).keyCode
              << std::endl;
    return;
  }

  if (std::holds_alternative<rmlib::input::PenEvent>(ev)) {
    static int scancode = -1;
    auto penEv = std::get<rmlib::input::PenEvent>(ev);
    if (penEv.type != PenEvent::Move) {
      printEvent(penEv);
    }

    if (penEv.type == PenEvent::TouchDown) {

      if (scancode != -1) {
        tilem_keypad_release_key(globalCalc, scancode);
      }

      scancode = get_scancode(penEv.location.x, penEv.location.y);
      std::cout << "pen down " << scancode << std::endl;

      if (scancode != -1) {
        tilem_keypad_press_key(globalCalc, scancode);
      }
    } else if (penEv.type == PenEvent::TouchUp/*penDown == true &&
               (penEv.pressure == 0 ||
                penEv.type == rmlib::input::PenEvent::TouchUp ||
                penEv.type == rmlib::input::PenEvent::ToolLeave)*/) {
      std::cout << "pen up " << scancode << std::endl;
      if (scancode != -1) {
        tilem_keypad_release_key(globalCalc, scancode);
        scancode = -1;
      }
    }
    return;
  }

  auto touchEv = std::get<rmlib::input::TouchEvent>(ev);
  if (touchEv.type == rmlib::input::TouchEvent::Down) {
    auto scancode = get_scancode(touchEv.location.x, touchEv.location.y);
    std::cout << "touch down " << touchEv.location << scancode << std::endl;
    scanCodes[touchEv.slot] = scancode;
    if (scancode != -1) {
      tilem_keypad_press_key(globalCalc, scancode);
    } else if (lcd_rect->contains(touchEv.location)) {
      std::cout << "lcd redraw\n";
      showCalculator(fb);
    }
  } else if (touchEv.type == rmlib::input::TouchEvent::Up) {
    auto scancode = scanCodes[touchEv.slot];
    std::cout << "touch up " << touchEv.location << scancode << std::endl;
    if (scancode != -1) {
      tilem_keypad_release_key(globalCalc, scancode);
    }
  }
}

std::chrono::steady_clock::time_point
getTime() {
  return std::chrono::steady_clock::now();
}

const auto FPS = 100;
const auto TPS = std::chrono::milliseconds(1000) / FPS;
const auto frame_time = std::chrono::milliseconds(50); // 50 ms ->  20 fps

void
frameCallback(TilemCalc* calc, void* data) {
  auto* fb = reinterpret_cast<fb::FrameBuffer*>(data);
  static auto* lcd = [] {
    auto* lcd = tilem_lcd_buffer_new();
    if (lcd == nullptr) {
      perror("Error alloc lcd bufer");
      std::exit(-1);
    }
    return lcd;
  }();

  tilem_lcd_get_frame(calc, lcd);

  float scale_x = (float)lcd_rect->width() / lcd->width;
  float scale_y = (float)lcd_rect->height() / lcd->height;
  fb->canvas.transform(
    [&](int x, int y, int) {
      int subY = (y - lcd_rect->topLeft.y) / scale_y;
      int subX = (x - lcd_rect->topLeft.x) / scale_x;
      uint8_t data = lcd->data[subY * lcd->rowstride + subX];
      uint8_t pixel = lcd->contrast == 0 ? 0 : data ? 0 : 0xff;
      return (pixel / 16) << 1;
    },
    *lcd_rect);

  fb->doUpdate(
    *lcd_rect, rmlib::fb::Waveform::DU, rmlib::fb::UpdateFlags::None);
}

void
intHandler(int sig) {
  shouldStop = true;
}

struct Key {
  int scancode;
  std::string_view front;
  std::string_view shift = "";
  std::string_view alpha = "";
};

const std::vector<std::vector<Key>> keymap = {
  { { TILEM_KEY_YEQU, "Y=", "STAT PLOT", "F1" },
    { TILEM_KEY_WINDOW, "WINDOW", "TBLST", "F2" },
    { TILEM_KEY_ZOOM, "ZOOM", "FORMAT", "F3" },
    { TILEM_KEY_TRACE, "TRACE", "CALC", "F4" },
    { TILEM_KEY_GRAPH, "GRAPH", "TABLE", "F5" } },

  { { TILEM_KEY_2ND, "2ND", "", "" },
    { TILEM_KEY_MODE, "MODE", "QUIT", "" },
    { TILEM_KEY_DEL, "DEL", "INS", "" },
    { TILEM_KEY_DOWN, "v", "", "" },
    { TILEM_KEY_UP, "^", "", "" } },

  { { TILEM_KEY_ALPHA, "ALPHA", "A-LOCK", "" },
    { TILEM_KEY_GRAPHVAR, "X,T,o,n", "LINK", "" },
    { TILEM_KEY_STAT, "STAT", "LIST", "" },
    { TILEM_KEY_LEFT, "<", "", "" },
    { TILEM_KEY_RIGHT, ">", "", "" } },

  { { TILEM_KEY_MATH, "MATH", "TEST", "A" },
    { TILEM_KEY_MATRIX, "APPS", "ANGLE", "B" },
    { TILEM_KEY_PRGM, "PRGM", "DRAW", "C" },
    { TILEM_KEY_VARS, "VARS", "DISTR", "" },
    { TILEM_KEY_CLEAR, "CLEAR", "", "" } },

  { { TILEM_KEY_RECIP, "^-1", "MATRIX", "D" },
    { TILEM_KEY_SIN, "SIN", "SIN^-1", "E" },
    { TILEM_KEY_COS, "COS", "COS^-1", "F" },
    { TILEM_KEY_TAN, "TAN", "TAN^-1", "G" },
    { TILEM_KEY_POWER, "^", "pi", "H" } },

  { { TILEM_KEY_SQUARE, "^2", "sqrt", "I" },
    { TILEM_KEY_COMMA, ",", "EE", "J" },
    { TILEM_KEY_LPAREN, "(", "{", "K" },
    { TILEM_KEY_RPAREN, ")", "}", "L" },
    { TILEM_KEY_DIV, "/", "e", "M" } },

  { { TILEM_KEY_LOG, "LOG", "10^x", "N" },
    { TILEM_KEY_7, "7", "u", "O" },
    { TILEM_KEY_8, "8", "v", "P" },
    { TILEM_KEY_9, "9", "w", "Q" },
    { TILEM_KEY_MUL, "*", "[", "R" } },

  { { TILEM_KEY_LN, "LN", "e^x", "S" },
    { TILEM_KEY_4, "4", "L4", "T" },
    { TILEM_KEY_5, "5", "L5", "U" },
    { TILEM_KEY_6, "6", "L6", "V" },
    { TILEM_KEY_SUB, "-", "]", "W" } },

  { { TILEM_KEY_STORE, "STO>", "RCL", "X" },
    { TILEM_KEY_1, "1", "L1", "Y" },
    { TILEM_KEY_2, "2", "L2", "Z" },
    { TILEM_KEY_3, "3", "L3", "phi" },
    { TILEM_KEY_ADD, "+", "MEM", "\"" } },

  { { TILEM_KEY_ON, "ON", "OFF", "" },
    { TILEM_KEY_0, "0", "CATALOG", "_" },
    { TILEM_KEY_DECPNT, ".", "i", ":" },
    { TILEM_KEY_CHS, "(-)", "ANS", "?" },
    { TILEM_KEY_ENTER, "ENTER", "ENTRY", "SOLVE" } },

};

class KeypadRenderObject;
class Keypad : public Widget<KeypadRenderObject> {
public:
  Keypad(TilemCalc* calc) : calc(calc) {
    maxRowSize = std::max_element(keymap.begin(),
                                  keymap.end(),
                                  [](const auto& a, const auto& b) {
                                    return a.size() < b.size();
                                  })
                   ->size();
    numRows = keymap.size();
  }

  std::unique_ptr<RenderObject> createRenderObject() const;

private:
  friend class KeypadRenderObject;
  TilemCalc* calc = nullptr;
  size_t maxRowSize;
  size_t numRows;
};

class KeypadRenderObject : public RenderObject {
public:
  KeypadRenderObject(const Keypad& widget) : widget(&widget) {}

  void update(const Keypad& newWidget) { widget = &newWidget; }

protected:
  Size doLayout(const Constraints& constraints) final {
    const auto width = constraints.max.width;

    keyWidth = width / widget->maxRowSize;
    keyHeight = keyWidth / key_aspect;

    const auto height = std::clamp(
      int(width * widget->numRows / (widget->maxRowSize * key_aspect)),
      constraints.min.height,
      constraints.max.height);

    return { width, height };
  }

  void drawKey(Canvas& canvas, Point pos, const Key& key) {
    const auto frontLabelHeight = int(front_label_factor * keyHeight);
    const auto upperLabelHeight = keyHeight - frontLabelHeight;

    {
      const auto fontSize = std::min(
        frontLabelHeight, int(key_aspect * keyWidth / key.front.size()));
      const auto fontSizes = Canvas::getTextSize(key.front, fontSize);

      const auto xOffset = (keyWidth - fontSizes.x) / 2;
      const auto yOffset =
        upperLabelHeight + (frontLabelHeight - fontSizes.y) / 2;
      const auto position = pos + Point{ xOffset, yOffset };

      canvas.drawText(key.front, position, fontSize);
    }

    {
      const auto upperLength = key.alpha.size() + key.shift.size();
      const auto fontSize =
        std::min(upperLabelHeight, int(1.6 * keyWidth / upperLength));

      auto testStr = std::string(key.shift);
      if (!key.alpha.empty()) {
        testStr += " " + std::string(key.alpha);
      }

      const auto fontSizes = Canvas::getTextSize(testStr, fontSize);

      const auto xOffset = (keyWidth - fontSizes.x) / 2;
      const auto yOffset = (upperLabelHeight - fontSizes.y) / 2;

      const auto position = pos + Point{ xOffset, yOffset };

      canvas.drawText(key.shift, position, fontSize, 0x55);

      if (!key.alpha.empty()) {
        const auto spacing =
          Canvas::getTextSize(std::string(key.shift) + " ", fontSize);
        const auto positonA = pos + Point{ xOffset + spacing.x, yOffset };
        canvas.drawText(key.alpha, positonA, fontSize, 0xaa);
      }
    }

    canvas.drawRectangle(
      pos, pos + Point{ keyWidth - 1, keyHeight - 1 }, black);
  }

  UpdateRegion doDraw(Rect rect, Canvas& canvas) final {
    keyLocations.clear();
    canvas.set(rect, white);

    int y = rect.topLeft.y;
    for (const auto& row : keymap) {

      int x = rect.topLeft.x;
      for (const auto& key : row) {
        keyLocations.emplace_back(
          Rect{ { x, y }, { x + keyWidth - 1, y + keyHeight - 1 } }, &key);
        drawKey(canvas, { x, y }, key);

        x += keyWidth;
      }

      y += keyHeight;
    }

    return { rect };
  }

  void handleInput(const Event& ev) final {
    if (!std::holds_alternative<TouchEvent>(ev)) {
      return;
    }

    const auto& touchEv = std::get<TouchEvent>(ev);

    if (touchEv.type == TouchEvent::Move) {
      return;
    }

    if (touchEv.type == TouchEvent::Up) {
      auto it = keyPointers.find(touchEv.id);
      if (it == keyPointers.end()) {
        return;
      }

      tilem_keypad_release_key(widget->calc, it->second->scancode);
      keyPointers.erase(it);
      return;
    }

    for (const auto& [rect, keyPtr] : keyLocations) {
      if (rect.contains(touchEv.location)) {
        tilem_keypad_press_key(widget->calc, keyPtr->scancode);
        keyPointers.emplace(touchEv.id, keyPtr);
        break;
      }
    }
  }

private:
  constexpr static auto key_aspect = 1.5;
  constexpr static auto front_label_factor = 0.6;

  const Keypad* widget = nullptr;
  std::vector<std::pair<Rect, const Key*>> keyLocations;
  std::unordered_map<int, const Key*> keyPointers;
  int keyWidth;
  int keyHeight;
};

std::unique_ptr<RenderObject>
Keypad::createRenderObject() const {
  return std::make_unique<KeypadRenderObject>(*this);
}

class ScreenRenderObject;

class Screen : public Widget<ScreenRenderObject> {
public:
  Screen(TilemCalc* calc) : calc(calc) {}

  std::unique_ptr<RenderObject> createRenderObject() const;

private:
  friend class ScreenRenderObject;
  TilemCalc* calc = nullptr;
};

class ScreenRenderObject : public RenderObject {
public:
  ScreenRenderObject(const Screen& widget)
    : widget(&widget)
    , lcd(tilem_lcd_buffer_new())
    , oldLcd(tilem_lcd_buffer_new()) {

    assert(lcd != nullptr && oldLcd != nullptr);
    addTimer();
  }

  static void stateFrameCallback(TilemCalc* calc, void* selfPtr) {
    auto* self = reinterpret_cast<ScreenRenderObject*>(selfPtr);
    self->markNeedsDraw(/* full */ false);
  }

  void addTimer() {
    tilem_z80_add_timer(widget->calc,
                        std::chrono::microseconds(frame_time).count(),
                        std::chrono::microseconds(frame_time).count(),
                        /* real time */ 1,
                        &stateFrameCallback,
                        this);
  }

  void update(const Screen& newWidget) {
    if (newWidget.calc != widget->calc) {
      addTimer();
    }
    widget = &newWidget;
  }

protected:
  Size doLayout(const Constraints& constraints) final {
    return constraints.max;
  }

  UpdateRegion doDraw(Rect rect, Canvas& canvas) final {
    tilem_lcd_get_frame(widget->calc, lcd);
    if (isPartialDraw() && oldLcd->data != nullptr &&
        oldLcd->contrast == lcd->contrast &&
        std::memcmp(lcd->data, oldLcd->data, lcd->rowstride * lcd->height) ==
          0) {
      return {};
    }

    if (lcd->contrast == 0) {
      canvas.set(rect, black);
    } else {
      float scale_x = (float)rect.width() / lcd->width;
      float scale_y = (float)rect.height() / lcd->height;
      canvas.transform(
        [&](int x, int y, int) {
          int subY = (y - rect.topLeft.y) / scale_y;
          int subX = (x - rect.topLeft.x) / scale_x;
          uint8_t data = lcd->data[subY * lcd->rowstride + subX];
          uint8_t pixel = data ? 0 : 0xff;
          return (pixel / 16) << 1;
        },
        rect);
    }
    std::swap(lcd, oldLcd);

    return { rect, fb::Waveform::DU };
  }

private:
  const Screen* widget;
  TilemLCDBuffer* lcd = nullptr;
  TilemLCDBuffer* oldLcd = nullptr;
};

std::unique_ptr<RenderObject>
Screen::createRenderObject() const {
  return std::make_unique<ScreenRenderObject>(*this);
}

class Calculator;
class CalcState;

class Calculator : public StatefulWidget<Calculator> {
public:
  Calculator(std::string romPath)
    : romPath(romPath), savePath(romPath + calc_save_extension) {}

  CalcState createState() const;

private:
  friend class CalcState;

  std::string romPath;
  std::string savePath;
};

class CalcState : public StateBase<Calculator> {
public:
  void updateCalcState() {
    const auto time = getTime();
    auto diff = time - lastUpdateTime;

    // Skip frames if we were paused.
    if (diff > std::chrono::seconds(1)) {
      diff = TPS;
    }

    tilem_z80_run_time(
      mCalc,
      std::chrono::duration_cast<std::chrono::microseconds>(diff).count(),
      nullptr);

    lastUpdateTime = time;
  }

  void init(AppContext& context) {
    mCalc = tilem_calc_new(TILEM_CALC_TI84P);
    if (mCalc == nullptr) {
      std::cerr << "Error init mCalc\n";
      std::exit(EXIT_FAILURE);
    }

    FILE* rom = fopen(getWidget().romPath.c_str(), "r");
    if (rom == nullptr) {
      perror("Error opening rom file");
      std::exit(EXIT_FAILURE);
    }

    FILE* save = fopen(getWidget().savePath.c_str(), "r");
    if (save == nullptr) {
      perror("No save");
    }

    if (tilem_calc_load_state(mCalc, rom, save) != 0) {
      perror("Error loading rom or save");
      std::exit(EXIT_FAILURE);
    }

    fclose(rom);
    if (save != nullptr) {
      fclose(save);
    }

    std::cout << "loaded rom, entering mainloop\n";
    lastUpdateTime = std::chrono::steady_clock::now();
    updateTimer = context.addTimer(
      TPS, [this] { updateCalcState(); }, TPS);
  }

  auto build(AppContext& context) const {
    constexpr auto scale = 5;
    return Center(Border(
      Column(Row(Text("Tilem"), Button("X", [&context] { context.stop(); })),
             Sized(Screen(mCalc), scale * 96, scale * 64),
             Sized(Keypad(mCalc), scale * 96, std::nullopt)),
      1));
  }

  ~CalcState() {
    std::cout << "Saving state\n";
    auto* save = fopen(getWidget().savePath.c_str(), "w");
    if (save == nullptr) {
      perror("Error opening save file");
    } else {
      tilem_calc_save_state(mCalc, nullptr, save);
      fclose(save);
    }
  }

private:
  TilemCalc* mCalc = nullptr;

  TimerHandle updateTimer;

  std::chrono::steady_clock::time_point lastUpdateTime;
};

CalcState
Calculator::createState() const {
  return CalcState{};
}

} // namespace

int
main(int argc, char* argv[]) {
  const auto* calc_name = argc > 1 ? argv[1] : calc_default_rom;

  const auto err = runApp(Calculator(calc_name));

  if (err.isError()) {
    std::cerr << err.getError().msg << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int
main_old(int argc, char* argv[]) {
  auto fb = rmlib::fb::FrameBuffer::open();
  if (fb.isError()) {
    std::cerr << fb.getError().msg << std::endl;
    return -1;
  }

  rmlib::input::InputManager input;

  if (auto err = input.openAll(); err.isError()) {
    std::cerr << "Error opening input: " + err.getError().msg + "\n";
    return -1;
  }

  if (!showCalculator(*fb)) {
    return -1;
  }

  if (!loadKeymap()) {
    return -1;
  }

  globalCalc = tilem_calc_new(TILEM_CALC_TI84P);
  if (globalCalc == nullptr) {
    std::cerr << "Error init calc\n";
    return -1;
  }

  const auto* calc_name = argc > 1 ? argv[1] : calc_default_rom;

  FILE* rom = fopen(calc_name, "r");
  if (rom == nullptr) {
    perror("Error opening rom file");
    return -1;
  }

  const auto save_name = std::string(calc_name) + calc_save_extension;
  FILE* save = fopen(save_name.c_str(), "r");
  if (save == nullptr) {
    perror("No save file");
  }

  if (tilem_calc_load_state(globalCalc, rom, save) != 0) {
    perror("Error reading rom");
    return -1;
  }

  fclose(rom);
  if (save != nullptr) {
    fclose(save);
  }

  tilem_z80_add_timer(globalCalc,
                      std::chrono::microseconds(frame_time).count(),
                      std::chrono::microseconds(frame_time).count(),
                      /* real time */ 1,
                      frameCallback,
                      &*fb);

  std::cout << "loaded rom, entering mainloop\n";

  std::signal(SIGINT, intHandler);

  auto lastUpdateT = getTime();
  while (!shouldStop) {
    constexpr auto wait_time = std::chrono::milliseconds(10);
    auto eventsOrErr = input.waitForInput(wait_time);
    if (eventsOrErr.isError()) {
      std::cerr << eventsOrErr.getError().msg << std::endl;
    } else {
      for (const auto& event : *eventsOrErr) {
        handleEvent(*fb, event);
      }
    }

    const auto time = getTime();
    auto diff = time - lastUpdateT;
    if (diff > TPS) {
      // Skip frames if we were paused.
      if (diff > std::chrono::seconds(1)) {
        diff = TPS;
      }

      tilem_z80_run_time(
        globalCalc,
        std::chrono::duration_cast<std::chrono::microseconds>(diff).count(),
        nullptr);
      lastUpdateT = time;
    }
  }

  std::cout << "Saving state\n";
  save = fopen(save_name.c_str(), "w");
  if (save == nullptr) {
    perror("Error opening save file");
    return -1;
  }

  tilem_calc_save_state(globalCalc, nullptr, save);

  return 0;
}
