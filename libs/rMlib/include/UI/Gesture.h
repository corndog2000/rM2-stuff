#pragma once

#include <UI/Layout.h>
#include <UI/RenderObject.h>
#include <UI/Text.h>
#include <UI/Widget.h>

namespace rmlib {

template<typename Child>
class GestureDetector;

template<typename Child>
class GestureRenderObject : public SingleChildRenderObject {
public:
  GestureRenderObject(const GestureDetector<Child>& widget)
    : SingleChildRenderObject(widget.child.createRenderObject())
    , widget(&widget) {}

  void handleInput(const rmlib::input::Event& ev) final {
    if (!std::holds_alternative<rmlib::input::TouchEvent>(ev)) {
      return;
    }

    const auto& touchEv = std::get<rmlib::input::TouchEvent>(ev);
    if (touchEv.type != rmlib::input::TouchEvent::Up) {
      return;
    }

    if (getRect().contains(touchEv.location)) {
      widget->onTap();
    }
  }

  void update(const GestureDetector<Child>& newWidget) {
    widget = &newWidget;
    widget->child.update(*child);
  }

private:
  const GestureDetector<Child>* widget;
};

template<typename Child>
class GestureDetector : public Widget<GestureRenderObject<Child>> {
private:
public:
  GestureDetector(Child child, Callback onTap)
    : child(std::move(child)), onTap(std::move(onTap)) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<GestureRenderObject<Child>>(*this);
  }

private:
  friend class GestureRenderObject<Child>;
  Child child;
  Callback onTap;
};

auto
Button(std::string text, Callback onTap) {
  return GestureDetector(
    Container(Text(text), Insets::all(2), 2, Insets::all(1)), std::move(onTap));
}
} // namespace rmlib
