#include "Input.h"

#include "Device.h"

#include <algorithm>
#include <cassert>

#include <fcntl.h>
#include <unistd.h>

#include <cstring>

#include <libevdev.h>
#include <linux/input.h>

#include <iostream>
#include <memory>

namespace rmlib::input {

namespace {

template<typename Device>
struct InputDevice : public InputDeviceBase {
  using InputDeviceBase::InputDeviceBase;

  virtual OptError<> readEvents(std::vector<Event>& out) final {
    Device* devThis = static_cast<Device*>(this);

    int rc = 0;
    do {

      auto event = input_event{};
      rc = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &event);

      if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
        auto evs = TRY(devThis->handleEvent(event));
        out.insert(out.end(), evs.begin(), evs.end());
      } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {

        while (rc == LIBEVDEV_READ_STATUS_SYNC) {
          auto evs = TRY(devThis->handleEvent(event));
          out.insert(out.end(), evs.begin(), evs.end());

          rc = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_SYNC, &event);
        }
      }
    } while (rc == LIBEVDEV_READ_STATUS_SYNC ||
             rc == LIBEVDEV_READ_STATUS_SUCCESS);

    return NoError{};
  }
};

struct TouchDevice : public InputDevice<TouchDevice> {
  TouchDevice(int fd, libevdev* evdev, Transform transform)
    : InputDevice(fd, evdev), transform(transform) {}
  ErrorOr<std::vector<TouchEvent>> handleEvent(input_event);
  TouchEvent& getSlot() { return slots.at(slot); }

  void flood() final {}

  Transform transform;
  int slot = 0;
  std::array<TouchEvent, max_num_slots> slots;
  std::unordered_set<int> changedSlots;
};

struct PenDevice : public InputDevice<PenDevice> {
  PenDevice(int fd, libevdev* evdev, Transform transform)
    : InputDevice(fd, evdev), transform(transform) {}
  ErrorOr<std::vector<PenEvent>> handleEvent(input_event);

  void flood() final {}

  Transform transform;
  PenEvent penEvent;
};

struct KeyDevice : public InputDevice<KeyDevice> {
  KeyDevice(int fd, libevdev* evdev) : InputDevice(fd, evdev) {}
  ErrorOr<std::vector<KeyEvent>> handleEvent(input_event);

  void flood() final {}

  KeyEvent keyEvent;
};

ErrorOr<std::vector<PenEvent>>
PenDevice::handleEvent(input_event event) {
  if (event.type == EV_SYN && event.code == SYN_REPORT) {
    auto ev = penEvent;
    ev.location = transform * penEvent.location;
    std::vector<PenEvent> events{ ev };

    penEvent.type = PenEvent::Move;
    return events;
  }

  if (event.type == EV_ABS) {
    if (event.code == ABS_X) {
      penEvent.location.x = event.value;
    } else if (event.code == ABS_Y) {
      penEvent.location.y = event.value;
    } else if (event.code == ABS_DISTANCE) {
      penEvent.distance = event.value;
    } else if (event.code == ABS_PRESSURE) {
      penEvent.pressure = event.value;
    }
  } else if (event.type == EV_KEY) {
    if (event.code == BTN_TOOL_PEN) {
      if (event.value == KeyEvent::Press) {
        penEvent.type = PenEvent::ToolClose;
        // setType = true;
      } else {
        penEvent.type = PenEvent::ToolLeave;
        // setType = true;
      }
    } else if (event.code == BTN_TOUCH) {
      if (event.value == KeyEvent::Press) {
        penEvent.type = PenEvent::TouchDown;
        // setType = true;
      } else {
        penEvent.type = PenEvent::TouchUp;
        // setType = true;
      }
    }
  }

  return std::vector<PenEvent>{};
}

ErrorOr<std::vector<TouchEvent>>
TouchDevice::handleEvent(input_event event) {
  if (event.type == EV_SYN && event.code == SYN_REPORT) {
    std::vector<TouchEvent> events;
    for (auto slotIdx : changedSlots) {
      auto slot = slots.at(slotIdx);
      slot.location = transform * slot.location;
      events.push_back(slot);

      slots.at(slotIdx).type = TouchEvent::Move;
    }
    changedSlots.clear();
    return events;
  }

  if (event.type == EV_ABS && event.code == ABS_MT_SLOT) {
    slot = event.value;
    getSlot().slot = event.value;
  }
  changedSlots.insert(slot);

  if (event.type == EV_ABS) {
    if (event.code == ABS_MT_TRACKING_ID) {
      auto& slot = getSlot();
      if (event.value == -1) {
        slot.type = TouchEvent::Up;
        // setType = true;
      } else {
        slot.type = TouchEvent::Down;
        slot.id = event.value;
        // setType = true;
      }
    } else if (event.code == ABS_MT_POSITION_X) {
      auto& slot = getSlot();
      slot.location.x = event.value;
    } else if (event.code == ABS_MT_POSITION_Y) {
      auto& slot = getSlot();
      slot.location.y = event.value;
    } else if (event.code == ABS_MT_PRESSURE) {
      auto& slot = getSlot();
      slot.pressure = event.value;
    }
  }

  return std::vector<TouchEvent>{};
}

ErrorOr<std::vector<KeyEvent>>
KeyDevice::handleEvent(input_event event) {
  if (event.type == EV_KEY) {
    keyEvent.type = static_cast<decltype(keyEvent.type)>(event.value);
    keyEvent.keyCode = event.code;
  } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
    return std::vector{ keyEvent };
  }
  return std::vector<KeyEvent>{};
}

std::unique_ptr<InputDeviceBase>
makeDevice(int fd, libevdev* evdev, Transform transform = {}) {
  if (libevdev_has_event_type(evdev, EV_ABS)) {
    // multi-touch screen -> ev_abs & abs_mt_slot
    if (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_SLOT)) {
      std::cout << "Got touch\n";
      return std::make_unique<TouchDevice>(fd, evdev, transform);
    }

    std::cout << "Got pen\n";
    // pen/single touch screen -> ev_abs
    return std::make_unique<PenDevice>(fd, evdev, transform);
  }

  std::cout << "Got key\n";
  // finally keyboard -> ev_key
  // TODO: support mouse?
  return std::make_unique<KeyDevice>(fd, evdev);
}

} // namespace

void
InputDeviceBase::grab() {
  libevdev_grab(evdev, LIBEVDEV_GRAB);
}

void
InputDeviceBase::ungrab() {
  libevdev_grab(evdev, LIBEVDEV_UNGRAB);
}

InputDeviceBase::~InputDeviceBase() {
  if (evdev != nullptr) {
    libevdev_free(evdev);
  }
  if (fd != -1) {
    close(fd);
  }
}

ErrorOr<InputDeviceBase*>
InputManager::open(std::string_view input, Transform inputTransform) {
  int fd = ::open(input.data(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    return Error{ "Couldn't open '" + std::string(input) + "'" };
  }

  libevdev* dev = nullptr;
  if (libevdev_new_from_fd(fd, &dev) < 0) {
    close(fd);
    return Error{ "Error initializing evdev for '" + std::string(input) + "'" };
  }

  devices.emplace_back(makeDevice(fd, dev, inputTransform));
  return &*devices.back();
}

ErrorOr<FileDescriptors>
InputManager::openAll() {
  auto type = TRY(device::getDeviceType());

  auto paths = device::getInputPaths(type);

  auto* touch = TRY(open(paths.touchPath, paths.touchTransform));
  auto* pen = TRY(open(paths.penPath, paths.penTransform));
  auto* key = TRY(open(paths.buttonPath));

  return FileDescriptors{ *pen, *touch, *key };
}

ErrorOr<std::vector<Event>>
InputManager::waitForInput(fd_set& fdSet,
                           int maxFd,
                           std::optional<std::chrono::microseconds> timeout) {
  for (auto& device : devices) {
    FD_SET(device->fd, &fdSet); // NOLINT
    maxFd = std::max(device->fd, maxFd);
  }

  auto tv = timeval{ 0, 0 };
  if (timeout.has_value()) {
    constexpr auto second_in_usec =
      std::chrono::microseconds(std::chrono::seconds(1)).count();
    tv.tv_sec =
      std::chrono::duration_cast<std::chrono::seconds>(*timeout).count();
    tv.tv_usec = timeout->count() - (tv.tv_sec * second_in_usec);
  }

  auto ret = select(
    maxFd + 1, &fdSet, nullptr, nullptr, !timeout.has_value() ? nullptr : &tv);
  if (ret < 0) {
    perror("Select on input failed");
    return Error{ "Select failed" };
  }

  if (ret == 0) {
    // timeout
    return std::vector<Event>{};
  }

  // Return the first device we see.
  std::vector<Event> result;
  for (auto& device : devices) {
    if (!FD_ISSET(device->fd, &fdSet)) { // NOLINT
      continue;
    }

    if (auto err = device->readEvents(result); err.isError()) {
      return err.getError();
    }
  }

  return result;
}
//
// std::vector<Event>
// InputManager::readEvents(int fd) {
//   auto it = devices.find(fd);
//   assert(it != devices.end());
//   return readEvents(it->second);
// }
//
// namespace {
// void
// handleEvent(InputManager::InputDevice& device, const input_event& event) {
//   if (event.type == EV_SYN && event.code == SYN_REPORT) {
//     for (auto slotIdx : device.changedSlots) {
//       auto& slot = device.slots.at(slotIdx);
//       device.events.push_back(slot);
//       std::visit(
//         [](auto& r) {
//           using Type = std::decay_t<decltype(r)>;
//           if constexpr (!std::is_same_v<Type, KeyEvent>) {
//             r.type = Type::Move;
//           }
//         },
//         slot);
//     }
//     device.changedSlots.clear();
//     return;
//   }
//
//   if (event.type == EV_ABS && event.code == ABS_MT_SLOT) {
//     device.slot = event.value;
//     device.getSlot<TouchEvent>().slot = event.value;
//   }
//   device.changedSlots.insert(device.slot);
//
//   if (event.type == EV_ABS) {
//     if (event.code == ABS_MT_TRACKING_ID) {
//       auto& slot = device.getSlot<TouchEvent>();
//       if (event.value == -1) {
//         slot.type = TouchEvent::Up;
//         // setType = true;
//       } else {
//         slot.type = TouchEvent::Down;
//         slot.id = event.value;
//         // setType = true;
//       }
//     } else if (event.code == ABS_MT_POSITION_X) {
//       auto& slot = device.getSlot<TouchEvent>();
//       slot.location.x = event.value;
//     } else if (event.code == ABS_MT_POSITION_Y) {
//       auto& slot = device.getSlot<TouchEvent>();
//       slot.location.y = event.value;
//     } else if (event.code == ABS_X) {
//       device.getSlot<PenEvent>().location.x = event.value;
//     } else if (event.code == ABS_Y) {
//       device.getSlot<PenEvent>().location.y = event.value;
//     } else if (event.code == ABS_DISTANCE) {
//       device.getSlot<PenEvent>().distance = event.value;
//     } else if (event.code == ABS_PRESSURE) {
//       device.getSlot<PenEvent>().pressure = event.value;
//     }
//   }
//
//   if (event.type == EV_KEY) {
//     if (event.code == BTN_TOOL_PEN) {
//       if (event.value == KeyEvent::Press) {
//         device.getSlot<PenEvent>().type = PenEvent::ToolClose;
//         // setType = true;
//       } else {
//         device.getSlot<PenEvent>().type = PenEvent::ToolLeave;
//         // setType = true;
//       }
//     } else if (event.code == BTN_TOUCH) {
//       if (event.value == KeyEvent::Press) {
//         device.getSlot<PenEvent>().type = PenEvent::TouchDown;
//         // setType = true;
//       } else {
//         device.getSlot<PenEvent>().type = PenEvent::TouchUp;
//         // setType = true;
//       }
//     } else {
//       auto& slot = device.getSlot<KeyEvent>();
//       slot.type = static_cast<decltype(slot.type)>(event.value);
//       slot.keyCode = event.code;
//     }
//   }
// }
// } // namespace
//
// std::vector<Event>
// InputManager::readEvents(InputDevice& device) {
//   int rc = 0;
//   do {
//     auto event = input_event{};
//     rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_NORMAL, &event);
//     if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
//       handleEvent(device, event);
//     } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
//       while (rc == LIBEVDEV_READ_STATUS_SYNC) {
//         handleEvent(device, event);
//         rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_SYNC,
//         &event);
//       }
//     }
//   } while (rc == LIBEVDEV_READ_STATUS_SYNC ||
//            rc == LIBEVDEV_READ_STATUS_SUCCESS);
//
//   auto results = std::move(device.events);
//   device.events.clear();
//
//   for (auto& result : results) {
//     // Transform result if needed
//     std::visit(
//       [&device](auto& r) {
//         using Type = std::decay_t<decltype(r)>;
//         if constexpr (!std::is_same_v<Type, KeyEvent>) {
//           r.location = device.transform * r.location;
//         }
//       },
//       result);
//   }
//
//   return results;
// }
//
// void
// InputManager::grab(int fd) {
//   auto device = devices.at(fd);
//   libevdev_grab(device.dev, libevdev_grab_mode::LIBEVDEV_GRAB);
// }
//
// void
// InputManager::ungrab(int fd) {
//   auto device = devices.at(fd);
//   libevdev_grab(device.dev, libevdev_grab_mode::LIBEVDEV_UNGRAB);
// }
//
// void
// InputManager::flood(int fd) {
//   constexpr auto size = 8 * 512 * 4;
//   static const auto* floodBuffer = [] {
//     // NOLINTNEXTLINE
//     static const auto ret = std::make_unique<input_event[]>(size);
//
//     constexpr auto mk_input_ev = [](int a, int b, int v) {
//       input_event r{};
//       r.type = a;
//       r.code = b;
//       r.value = v;
//       r.time = { 0, 0 };
//       return r;
//     };
//
//     for (int i = 0; i < size;) {
//       ret[i++] = mk_input_ev(EV_ABS, ABS_DISTANCE, 1);
//       ret[i++] = mk_input_ev(EV_SYN, 0, 0);
//       ret[i++] = mk_input_ev(EV_ABS, ABS_DISTANCE, 2);
//       ret[i++] = mk_input_ev(EV_SYN, 0, 0);
//     }
//
//     return ret.get();
//   }();
//
//   std::cout << "FLOODING" << std::endl;
//   auto device = devices.at(fd);
//   if (write(device.fd, floodBuffer, size * sizeof(input_event)) == -1) {
//     perror("Error writing");
//   }
// }

namespace {

SwipeGesture::Direction
getSwipeDirection(Point delta) {
  if (std::abs(delta.x) > std::abs(delta.y)) {
    return delta.x > 0 ? SwipeGesture::Right : SwipeGesture::Left;
  }

  return delta.y > 0 ? SwipeGesture::Down : SwipeGesture::Up;
}

PinchGesture::Direction
getPinchDirection() {
  // TODO
  return PinchGesture::In;
}

} // namespace

Gesture
GestureController::getGesture(Point currentDelta) {
  const auto [avgStart, delta] = [this] {
    std::vector<Point> delta;
    Point avgStart;
    int nActive = 0;

#ifndef NDEBUG
    std::cout << "- delta: ";
#endif

    for (const auto& slot : slots) {
      if (slot.active) {
        delta.push_back(slot.currentPos - slot.startPos);

#ifndef NDEBUG
        std::cout << delta.back() << " ";
#endif

        avgStart += slot.startPos;
        nActive++;
      }
    }

#ifndef NDEBUG
    std::cout << std::endl;
#endif

    avgStart /= nActive;
    return std::make_pair(avgStart, std::move(delta));
  }();

  const auto isSwipe =
    std::all_of(
      delta.cbegin(), delta.cend(), [](auto& p) { return p.x >= 0; }) ||
    std::all_of(
      delta.cbegin(), delta.cend(), [](auto& p) { return p.x <= 0; }) ||
    std::all_of(
      delta.cbegin(), delta.cend(), [](auto& p) { return p.y >= 0; }) ||
    std::all_of(delta.cbegin(), delta.cend(), [](auto& p) { return p.y <= 0; });

  if (isSwipe) {
    return SwipeGesture{ getSwipeDirection(currentDelta),
                         avgStart,
                         /* endPos */ {},
                         getCurrentFingers() };
  }

  return PinchGesture{ getPinchDirection(), avgStart, getCurrentFingers() };
}

void
GestureController::handleTouchDown(const TouchEvent& event) {
  auto& slot = slots.at(event.slot);
  slot.active = true;

  std::cerr << "Touch down, current fingers: " << getCurrentFingers()
            << std::endl;

  slot.currentPos = event.location;
  slot.startPos = event.location;
  slot.time = std::chrono::steady_clock::now();

  tapFingers = getCurrentFingers();
}

std::optional<Gesture>
GestureController::handleTouchUp(const TouchEvent& event) {
  std::optional<Gesture> result;

  auto& slot = slots.at(event.slot);
  slot.active = false;

  std::cerr << "Touch up, current fingers: " << getCurrentFingers()
            << std::endl;

  if (getCurrentFingers() == 0) {
    if (!started) {
      // TODO: do we need a time limit?
      // auto delta = std::chrono::steady_clock::now() -
      // slots[event.slot].time; if (delta < tap_time) {
      result = TapGesture{ tapFingers, slot.startPos };
      //}
    } else {
      if (std::holds_alternative<SwipeGesture>(gesture)) {
        std::get<SwipeGesture>(gesture).endPosition = event.location;
      }
      result = gesture;
    }

    reset();
  }

  return result;
}

void
GestureController::handleTouchMove(const TouchEvent& event) {
  auto& slot = slots.at(event.slot);
  slot.currentPos = event.location;
  auto delta = event.location - slot.startPos;

  if (!started) {
    if (getCurrentFingers() >= 2 && (std::abs(delta.x) >= start_threshold ||
                                     std::abs(delta.y) >= start_threshold)) {

      started = true;
      gesture = getGesture(delta);
    }

    return;
  }

  // Started, calc percentage (TODO)
}

std::pair<std::vector<Gesture>, std::vector<Event>>
GestureController::handleEvents(const std::vector<Event>& events) {
  std::vector<Gesture> result;
  std::vector<Event> unhandled;

  for (const auto& event : events) {
    if (!std::holds_alternative<TouchEvent>(event)) {
      unhandled.emplace_back(event);
      continue;
    }
    const auto& touchEv = std::get<TouchEvent>(event);
    switch (touchEv.type) {
      case TouchEvent::Down:
        handleTouchDown(touchEv);
        break;
      case TouchEvent::Move:
        handleTouchMove(touchEv);
        break;
      case TouchEvent::Up: {
        auto gesture = handleTouchUp(touchEv);
        if (gesture.has_value()) {
          result.emplace_back(*gesture);
        }
        break;
      }
    }
  }

  return { result, unhandled };
}

void
GestureController::sync(InputDeviceBase& device) {
  const auto maxSlots =
    std::min<int>(int(slots.size()), libevdev_get_num_slots(device.evdev));
  for (int i = 0; i < maxSlots; i++) {
    auto id = libevdev_get_slot_value(device.evdev, i, ABS_MT_TRACKING_ID);
    bool active = id != -1;
    if (active != slots[i].active) {
      std::cerr << "Desync for slot " << i << std::endl;
      if (!active) {
        slots[i].active = false;
        if (getCurrentFingers() == 0) {
          reset();
        }
      }
    }
  }
}

} // namespace rmlib::input
