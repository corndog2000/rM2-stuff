#include <functional>
#include <iostream>
#include <type_traits>

#include <unistd.h>

#include <Canvas.h>
#include <FrameBuffer.h>
#include <Input.h>

#include <UI.h>

using namespace rmlib;

class LabledInt : public StatelessWidget<LabledInt> {
public:
  LabledInt(std::string label, int n) : label(std::move(label)), integer(n) {}

  auto build(AppContext& context) const {
    return Row(Text(label), Text(std::to_string(integer)));
  }

private:
  std::string label;
  int integer;
};

class ToggleTest : public StatefulWidget<ToggleTest> {
public:
  class State : public StateBase<ToggleTest> {
  public:
    auto build(AppContext& context) const {
      return GestureDetector(
        Border(Padding(Text(""), Insets::all(on ? 0 : 8)),
               Insets::all(on ? 10 : 2)),
        [this]() { setState([](auto& self) { self.on = !self.on; }); });
    }

  private:
    bool on = true;
  };

public:
  State createState() const { return State{}; }
};

class TimerTest : public StatefulWidget<TimerTest> {
public:
  class State : public StateBase<TimerTest> {
  public:
    void init(AppContext& context) {
      timer = context.addTimer(
        std::chrono::seconds(1), [this] { tick(); }, std::chrono::seconds(1));
    }

    auto build(AppContext& context) const {
      return Text(std::to_string(ticks));
    }

  private:
    void tick() const {
      setState([](auto& self) { self.ticks *= 2; });
    }

    int ticks = 1;
    TimerHandle timer;
  };

  State createState() const { return State{}; }
};

class CounterTest : public StatefulWidget<CounterTest> {
public:
  class State : public StateBase<CounterTest> {
  public:
    void init(AppContext& context) {}

    DynamicWidget build(AppContext& context) const {
      if (count < 5) {
        return Column(LabledInt("Counter: ", count),
                      Row(Button("-1", [this] { decrease(); }),
                          Button("+1", [this] { increase(); })),
                      TimerTest());
      } else {
        return Row(Button("reset", [this]() { reset(); }), ToggleTest());
      }
    }

  private:
    void reset() const {
      setState([](auto& self) { self.count = 0; });
    }
    void increase() const {
      setState([](auto& self) { self.count++; });
    }

    void decrease() const {
      setState([](auto& self) { self.count--; });
    }

    int count = 0;
    TimerHandle timer;
  };

public:
  State createState() const { return State{}; }
};

int
main() {
  // auto optErr = runApp(Center(Row(Text("Test:"), CounterTest())));
  auto optErr = runApp(Center(Column(Sized(Colored(0x00), std::nullopt, 50),
                                     Sized(Colored(0x88), 50, 50),
                                     Sized(Colored(0xee), 50, 50))));

  if (optErr.isError()) {
    std::cerr << optErr.getError().msg << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
