#pragma once

#include <UI/Timer.h>

#include <vector>

namespace rmlib {

class AppContext {
public:
  TimerHandle addTimer(
    std::chrono::microseconds duration,
    Callback trigger,
    std::optional<std::chrono::microseconds> repeat = std::nullopt) {
    auto [timer, handle] =
      Timer::makeTimer(duration, std::move(trigger), repeat);
    timers.emplace(std::move(timer));
    return std::move(handle);
  }

  std::optional<std::chrono::microseconds> getNextDuration() const {
    if (timers.empty()) {
      return std::nullopt;
    }

    auto duration = timers.top()->getDuration();
    if (duration < std::chrono::microseconds(0)) {
      return std::chrono::microseconds(0);
    }
    return duration;
  }

  void checkTimers() {
    while (!timers.empty()) {
      const auto& top = timers.top();
      if (top->check()) {
        std::shared_ptr<Timer> test = top;
        timers.pop();

        if (top->repeats()) {
          test->reset();
          timers.emplace(std::move(test));
        }
      } else {
        break;
      }
    }
  }

  void stop() { mShouldStop = true; }

  bool shouldStop() const { return mShouldStop; }

private:
  TimerQueue timers;
  bool mShouldStop = false;
};

} // namespace rmlib
