#pragma once

#include <UI/RenderObject.h>
#include <UI/Widget.h>

namespace rmlib {

template<typename Child>
class Wrap;

template<typename Child>
class WrapRenderObject : public MultiChildRenderObject {
public:
  WrapRenderObject(const Wrap<Child>& widget)
    : MultiChildRenderObject([&widget] {
      std::vector<std::unique_ptr<RenderObject>> children;
      children.reserve(widget.children.size());
      std::transform(
        widget.children.begin(),
        widget.children.end(),
        std::back_inserter(children),
        [](const auto& child) { return child.createRenderObject(); });
      return children;
    }())
    , widget(&widget) {}

  void update(const Wrap<Child>& newWidget) {
    if (newWidget.axis != widget->axis) {
      markNeedsLayout();
      markNeedsDraw();
    }

    auto updateEnd = children.size();

    if (newWidget.children.size() != children.size()) {

      // TODO: move the ROs out of tree and reuse?
      if (newWidget.children.size() < children.size()) {
        children.resize(newWidget.children.size());
        updateEnd = newWidget.children.size();
      } else {
        children.reserve(newWidget.children.size());
        std::transform(
          std::next(newWidget.children.begin(), updateEnd),
          newWidget.children.end(),
          std::back_inserter(children),
          [](const auto& child) { return child.createRenderObject(); });
      }

      markNeedsLayout();
      markNeedsDraw();
    }

    for (size_t i = 0; i < updateEnd; i++) {
      newWidget.children[i].update(*children[i]);
    }

    widget = &newWidget;
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    const auto childConstraints =
      isVertical()
        ? Constraints{ { 0, 0 },
                       { constraints.max.width, Constraints::unbound } }
        : Constraints{ { 0, 0 },
                       { Constraints::unbound, constraints.max.height } };

    runSizes.clear();
    Size result = { 0, 0 };
    Size rowSize = { 0, 0 };
    for (const auto& child : children) {
      const auto oldSize = child->getSize();
      const auto size = child->layout(childConstraints);
      if (oldSize != size) {
        markNeedsDraw();
      }

      if (isVertical()) {
        if (rowSize.height + size.height > constraints.max.height) {
          result.height = std::max(result.height, rowSize.height);
          result.width += rowSize.width;
          runSizes.push_back(rowSize.width);

          rowSize = size;
        } else {
          rowSize.height += size.height;
          rowSize.width = std::max(size.width, rowSize.width);
        }
      } else {
        if (rowSize.width + size.width > constraints.max.width) {
          result.width = std::max(result.width, rowSize.width);
          result.height += rowSize.height;
          runSizes.push_back(rowSize.height);

          rowSize = size;
        } else {
          rowSize.width += size.width;
          rowSize.height = std::max(size.height, rowSize.height);
        }
      }
    }

    if (isVertical()) {
      result.height = std::max(result.height, rowSize.height);
      result.width += rowSize.width;
      runSizes.push_back(rowSize.width);
    } else {
      result.width = std::max(result.width, rowSize.width);
      result.height += rowSize.height;
      runSizes.push_back(rowSize.height);
    }

    totalSize = result;

    // Align on each axis:
    if (result.height < constraints.min.height) {
      result.height = constraints.min.height;
    }

    if (result.width < constraints.min.width) {
      result.width = constraints.min.width;
    }

    return result;
  }

  UpdateRegion doDraw(Rect rect, Canvas& canvas) override {
    UpdateRegion result;

    const auto origin =
      ((rect.size() - totalSize) / 2).toPoint() + Point{ 1, 1 };
    auto offset = origin;

    int run = 0;
    for (const auto& child : children) {
      const auto size = child->getSize();

      if (isVertical()) {
        if (offset.y + size.height > rect.height()) {
          offset.y = origin.y;
          offset.x += runSizes.at(run++);
        }
      } else {
        if (offset.x + size.width > rect.width()) {
          offset.x = origin.x;
          offset.y += runSizes.at(run++);
        }
      }

      const auto subRect =
        Rect{ rect.topLeft + offset, rect.topLeft + offset + size.toPoint() };
      result |= child->draw(subRect, canvas);

      if (isVertical()) {
        offset.y += size.height;
      } else {
        offset.x += size.width;
      }
    }

    return result;
  }

private:
  bool isVertical() const { return widget->axis == Axis::Vertical; }

  const Wrap<Child>* widget;
  std::vector<int> runSizes;
  Size totalSize;
};

template<typename Child>
class Wrap : public Widget<WrapRenderObject<Child>> {
public:
  Wrap(std::vector<Child> children, Axis axis = Axis::Horizontal)
    : children(std::move(children)), axis(axis) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<WrapRenderObject<Child>>(*this);
  }

private:
  friend class WrapRenderObject<Child>;
  std::vector<Child> children;
  Axis axis;
};

} // namespace rmlib
