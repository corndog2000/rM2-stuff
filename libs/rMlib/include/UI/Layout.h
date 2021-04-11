#pragma once

#include <UI/RenderObject.h>
#include <UI/Widget.h>

namespace rmlib {
template<typename Child>
class Center;

template<typename Child>
class CenterRenderObject : public SingleChildRenderObject {
public:
  CenterRenderObject(const Center<Child>& widget)
    : SingleChildRenderObject(widget.child.createRenderObject())
    , widget(&widget) {}

  Size doLayout(const Constraints& constraints) override {
    childSize = child->layout(Constraints{ { 0, 0 }, constraints.max });

    auto result = constraints.max;
    if (!constraints.hasBoundedWidth()) {
      result.width = childSize.width;
    }
    if (!constraints.hasBoundedHeight()) {
      result.height = childSize.height;
    }

    return result;
  }

  void update(const Center<Child>& newWidget) {
    widget = &newWidget;
    widget->child.update(*child);
  }

protected:
  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    const auto xOffset = (rect.width() - childSize.width) / 2;
    const auto yOffset = (rect.height() - childSize.height) / 2;

    const auto topLeft = rect.topLeft + rmlib::Point{ xOffset, yOffset };
    const auto bottomRight = topLeft + childSize.toPoint();
    return child->draw(rmlib::Rect{ topLeft, bottomRight }, canvas);
  }

private:
  const Center<Child>* widget;

  Size childSize;
};

template<typename Child>
class Center : public Widget<CenterRenderObject<Child>> {
private:
public:
  Center(Child child) : child(std::move(child)) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<CenterRenderObject<Child>>(*this);
  }

private:
  friend class CenterRenderObject<Child>;
  Child child;
};

template<class Child>
class Padding;

template<typename Child>
class PaddingRenderObject : public SingleChildRenderObject {
public:
  PaddingRenderObject(const Padding<Child>& widget)
    : SingleChildRenderObject(widget.child.createRenderObject())
    , widget(&widget) {}

  void update(const Padding<Child>& newWidget) {
    if (/*newWidget.insets != widget->insets*/ false) {
      markNeedsLayout();
      markNeedsDraw();
    }
    widget = &newWidget;
    widget->child.update(*child);
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    const auto childConstraints = constraints.inset(widget->insets);
    const auto childSize = child->layout(childConstraints);
    return constraints.expand(childSize, widget->insets);
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    const auto childRect = widget->insets.shrink(rect);
    auto childRegion = child->draw(childRect, canvas);

    return childRegion;
  }

private:
  const Padding<Child>* widget;
};

template<class Child>
class Padding : public Widget<PaddingRenderObject<Child>> {
private:
public:
  Padding(Child child, Insets insets)
    : child(std::move(child)), insets(insets) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<PaddingRenderObject<Child>>(*this);
  }

private:
  friend class PaddingRenderObject<Child>;
  Child child;
  Insets insets;
};

template<typename Child>
class Border;

template<typename Child>
class BorderRenderObject : public SingleChildRenderObject {
public:
  BorderRenderObject(const Border<Child>& widget)
    : SingleChildRenderObject(widget.child.createRenderObject())
    , widget(&widget) {}

  void update(const Border<Child>& newWidget) {
    if (widget->size != newWidget.size) {
      markNeedsLayout();
      markNeedsDraw();
    }

    widget = &newWidget;
    widget->child.update(*child);
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    const auto childConstraints = constraints.inset(widget->size);
    const auto childSize = child->layout(childConstraints);
    const auto newSize = constraints.expand(childSize, widget->size);

    if (newSize != getSize()) {
      markNeedsDraw();
    }

    return newSize;
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    auto result = child->draw(widget->size.shrink(rect), canvas);

    if (isFullDraw()) {
      const auto drawLine = [&canvas](Point a, Point b, Point dir, int size) {
        for (int i = 0; i < size; i++) {
          canvas.drawLine(a, b, black);
          a += dir;
          b += dir;
        }
      };

      drawLine(rect.topLeft,
               { rect.bottomRight.x, rect.topLeft.y },
               { 0, 1 },
               widget->size.top);
      drawLine(rect.topLeft,
               { rect.topLeft.x, rect.bottomRight.y },
               { 1, 0 },
               widget->size.left);
      drawLine({ rect.bottomRight.x, rect.topLeft.y },
               rect.bottomRight,
               { -1, 0 },
               widget->size.right);
      drawLine({ rect.topLeft.x, rect.bottomRight.y },
               rect.bottomRight,
               { 0, -1 },
               widget->size.bottom);

      result |= UpdateRegion{ rect, rmlib::fb::Waveform::DU };
    }

    return result;
  }

private:
  const Border<Child>* widget;
};

template<typename Child>
class Border : public Widget<BorderRenderObject<Child>> {
private:
public:
  Border(Child child, Insets size) : child(std::move(child)), size(size) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<BorderRenderObject<Child>>(*this);
  }

private:
  friend class BorderRenderObject<Child>;
  Child child;
  Insets size;
};

template<class Child>
class Sized;

template<typename Child>
class SizedRenderObject : public SingleChildRenderObject {
public:
  SizedRenderObject(const Sized<Child>& widget)
    : SingleChildRenderObject(widget.child.createRenderObject())
    , widget(&widget) {}

  void update(const Sized<Child>& newWidget) {
    if (newWidget.width != widget->width ||
        newWidget.height != widget->height) {
      markNeedsLayout();
      markNeedsDraw();
    }
    widget = &newWidget;
    widget->child.update(*child);
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    const auto& w = widget->width;
    const auto& h = widget->height;

    const auto childConstraints = Constraints{
      { w.has_value()
          ? std::clamp(*w, constraints.min.width, constraints.max.width)
          : constraints.min.width,
        h.has_value()
          ? std::clamp(*h, constraints.min.height, constraints.max.height)
          : constraints.min.height },
      { w.has_value()
          ? std::clamp(*w, constraints.min.width, constraints.max.width)
          : constraints.max.width,
        h.has_value()
          ? std::clamp(*h, constraints.min.height, constraints.max.height)
          : constraints.max.height }
    };

    const auto childSize = child->layout(childConstraints);
    return childSize;
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    return child->draw(rect, canvas);
  }

private:
  const Sized<Child>* widget;
};

template<class Child>
class Sized : public Widget<SizedRenderObject<Child>> {
private:
public:
  Sized(Child child, std::optional<int> width, std::optional<int> height)
    : child(std::move(child)), width(width), height(height) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<SizedRenderObject<Child>>(*this);
  }

private:
  friend class SizedRenderObject<Child>;
  Child child;
  std::optional<int> width;
  std::optional<int> height;
};

template<typename Child>
class ClearedRenderObject;

template<typename Child>
class Cleared : public Widget<ClearedRenderObject<Child>> {
private:
public:
  Cleared(Child child, int color = white)
    : child(std::move(child)), color(color) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<ClearedRenderObject<Child>>(*this);
  }

private:
  friend class ClearedRenderObject<Child>;
  Child child;
  int color;
};

template<typename Child>
class ClearedRenderObject : public SingleChildRenderObject {
public:
  ClearedRenderObject(const Cleared<Child>& widget)
    : SingleChildRenderObject(widget.child.createRenderObject())
    , widget(&widget) {}

  void update(const Cleared<Child>& newWidget) {
    if (newWidget.color != widget->color) {
      markNeedsDraw();
    }
    widget = &newWidget;
    widget->child.update(*child);
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    return child->layout(constraints);
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    auto region = UpdateRegion{};

    if (isFullDraw()) {
      canvas.set(rect, widget->color);
      region = UpdateRegion{ rect };
    }

    return region | child->draw(rect, canvas);
  }

private:
  const Cleared<Child>* widget;
};

template<typename Child>
class PositionedRenderObject;

template<typename Child>
class Positioned : public Widget<PositionedRenderObject<Child>> {
private:
public:
  Positioned(Child child, Point position)
    : child(std::move(child)), position(position) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<PositionedRenderObject<Child>>(*this);
  }

private:
  friend class PositionedRenderObject<Child>;
  Child child;
  Point position;
};

template<typename Child>
class PositionedRenderObject : public SingleChildRenderObject {
public:
  PositionedRenderObject(const Positioned<Child>& widget)
    : SingleChildRenderObject(widget.child.createRenderObject())
    , widget(&widget) {}

  void update(const Positioned<Child>& newWidget) {
    if (newWidget.position != widget->position) {
      markNeedsLayout();

      RenderObject::markNeedsDraw(/* full */ false);
      child->markNeedsDraw(true);
    }
    widget = &newWidget;
    widget->child.update(*child);
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    const auto newConstraints =
      Constraints{ { 0, 0 },
                   { constraints.max.width - widget->position.x,
                     constraints.max.height - widget->position.y } };

    childSize = child->layout(newConstraints);

    auto result = constraints.max;
    if (!constraints.hasBoundedWidth()) {
      result.width = childSize.width;
    }
    if (!constraints.hasBoundedHeight()) {
      result.height = childSize.height;
    }

    return result;
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    const auto topLeft = rect.topLeft + widget->position;
    const auto bottomRight = topLeft + childSize.toPoint();
    return child->draw({ topLeft, bottomRight }, canvas);
  }

private:
  const Positioned<Child>* widget;
  Size childSize;
};

template<typename Child>
auto
Container(Child child,
          Insets padding = {},
          Insets border = Insets::all(0),
          Insets margin = {}) {
  return Padding(Border(Padding(child, padding), border), margin);
}

} // namespace rmlib
