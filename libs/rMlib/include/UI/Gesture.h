#pragma once

#include <UI/Layout.h>
#include <UI/RenderObject.h>
#include <UI/Text.h>
#include <UI/Widget.h>

namespace rmlib {

using PosCallback = std::function<void(Point)>;

struct Gestures {
  Callback onTap;

  PosCallback onTouchMove;
  PosCallback onTouchDown;

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
    std::visit(
      [this](const auto& ev) {
        if constexpr (input::is_pointer_event<decltype(ev)>) {
          if (ev.isDown() && getRect().contains(ev.location) &&
              currentId == -1) {
            currentId = ev.id;
            if (widget->gestures.onTouchDown) {
              widget->gestures.onTouchDown(ev.location);
            }
          }

          if (ev.id != currentId) {
            return;
          }

          if (ev.isUp()) {
            currentId = -1;
            if (widget->gestures.onTap) {
              widget->gestures.onTap();
            }
          }

          if (ev.isMove() && widget->gestures.onTouchMove) {
            widget->gestures.onTouchMove(ev.location);
          }
        }
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
    Container(Text(text), Insets::all(2), Insets::all(2), Insets::all(1)),
    { .onTap = std::move(onTap) });
}
} // namespace rmlib
