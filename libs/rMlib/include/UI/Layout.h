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

    Size result = constraints.max;
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
    const auto insets = Insets::all(widget->size);
    const auto childConstraints = constraints.inset(insets);
    const auto childSize = child->layout(childConstraints);
    const auto newSize = constraints.expand(childSize, insets);

    if (newSize != getSize()) {
      markNeedsDraw();
    }

    return newSize;
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    const auto insets = Insets::all(widget->size);
    auto result = child->draw(insets.shrink(rect), canvas);

    if (isFullDraw()) {
      for (int i = 0; i < widget->size; i++) {
        canvas.drawRectangle(rect.topLeft + rmlib::Point{ i, i },
                             rect.bottomRight - rmlib::Point{ i, i },
                             rmlib::black);
      }
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
  Border(Child child, int size) : child(std::move(child)), size(size) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<BorderRenderObject<Child>>(*this);
  }

private:
  friend class BorderRenderObject<Child>;
  Child child;
  int size;
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

class ColoredRenderObject;

class Colored : public Widget<ColoredRenderObject> {
private:
public:
  Colored(int color) : color(color) {}

  std::unique_ptr<RenderObject> createRenderObject() const;

private:
  friend class ColoredRenderObject;
  int color;
};

class ColoredRenderObject : public RenderObject {
public:
  ColoredRenderObject(const Colored& widget) : widget(&widget) {}

  void update(const Colored& newWidget) {
    if (newWidget.color != widget->color) {
      markNeedsDraw();
    }
    widget = &newWidget;
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    auto result = constraints.max;

    if (result.height == Constraints::unbound) {
      result.height = constraints.min.height;
    }

    if (result.width == Constraints::unbound) {
      result.width = constraints.min.width;
    }

    return result;
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    canvas.set(rect, widget->color);
    return UpdateRegion{ rect };
  }

private:
  const Colored* widget;
};

inline std::unique_ptr<RenderObject>
Colored::createRenderObject() const {
  return std::make_unique<ColoredRenderObject>(*this);
}

template<typename Child>
auto
Container(Child child,
          Insets padding = {},
          int border = 0,
          Insets margin = {}) {
  return Padding(Border(Padding(child, padding), border), margin);
}

} // namespace rmlib
