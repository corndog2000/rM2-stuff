#pragma once

#include <UI/Layout.h>
#include <UI/RenderObject.h>
#include <UI/Text.h>
#include <UI/Widget.h>

namespace rmlib {

using PosCallback = std::function<void(Point)>;
using KeyCallback = std::function<void(int)>;

struct Gestures {
  Callback onAny;
  Callback onTap;

  PosCallback onTouchMove;
  PosCallback onTouchDown;

  KeyCallback onKeyDown;
  KeyCallback onKeyUp;

  Gestures& OnTap(Callback cb) {
    onTap = std::move(cb);
    return *this;
  }

  Gestures& OnTouchMove(PosCallback cb) {
    onTouchMove = std::move(cb);
    return *this;
  }

  Gestures& OnTouchDown(PosCallback cb) {
    onTouchDown = std::move(cb);
    return *this;
  }

  Gestures& OnKeyDown(KeyCallback cb) {
    onKeyDown = std::move(cb);
    return *this;
  }

  Gestures& OnKeyUp(KeyCallback cb) {
    onKeyUp = std::move(cb);
    return *this;
  }

  Gestures& OnAny(Callback cb) {
    onAny = std::move(cb);
    return *this;
  }

  bool handlesTouch() const { return onTap || onTouchDown || onTouchMove; }
};

template<typename Child>
class GestureDetector;

template<typename Child>
class GestureRenderObject : public SingleChildRenderObject {
public:
  GestureRenderObject(const GestureDetector<Child>& widget)
    : SingleChildRenderObject(widget.child.createRenderObject())
    , widget(&widget) {}

  void handleInput(const rmlib::input::Event& ev) final {
    if (widget->gestures.onAny) {
      widget->gestures.onAny();
    }

    std::visit(
      [this](const auto& ev) {
        if constexpr (input::is_pointer_event<decltype(ev)>) {
          if (ev.isDown() && getRect().contains(ev.location) &&
              currentId == -1) {
            if (widget->gestures.handlesTouch()) {
              currentId = ev.id;
            }

            if (widget->gestures.onTouchDown) {
              widget->gestures.onTouchDown(ev.location);
              return;
            }
          }

          if (ev.id == currentId) {
            if (ev.isUp()) {
              currentId = -1;
              if (widget->gestures.onTap) {
                widget->gestures.onTap();
              }
              return;
            }

            if (ev.isMove() && widget->gestures.onTouchMove) {
              widget->gestures.onTouchMove(ev.location);
              return;
            }
          }
        } else {
          if (ev.type == input::KeyEvent::Press && widget->gestures.onKeyDown) {
            widget->gestures.onKeyDown(ev.keyCode);
            return;
          }

          if (ev.type == input::KeyEvent::Release && widget->gestures.onKeyUp) {
            widget->gestures.onKeyUp(ev.keyCode);
            return;
          }
        }

        // If we didn't return yet we didn't handle the event.
        // So let our child handle it.
        SingleChildRenderObject::handleInput(ev);
      },
      ev);
  }

  void update(const GestureDetector<Child>& newWidget) {
    widget = &newWidget;
    widget->child.update(*child);
  }

private:
  const GestureDetector<Child>* widget;
  int currentId = -1;
};

template<typename Child>
class GestureDetector : public Widget<GestureRenderObject<Child>> {
private:
public:
  GestureDetector(Child child, Gestures gestures)
    : child(std::move(child)), gestures(std::move(gestures)) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<GestureRenderObject<Child>>(*this);
  }

private:
  friend class GestureRenderObject<Child>;
  Child child;
  Gestures gestures;
};

auto
Button(std::string text, Callback onTap) {
  return GestureDetector(
    Container(
      Text(std::move(text)), Insets::all(2), Insets::all(2), Insets::all(1)),
    Gestures{}.OnTap(std::move(onTap)));
}
} // namespace rmlib
