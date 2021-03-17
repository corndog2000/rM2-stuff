#include <functional>
#include <iostream>
#include <type_traits>

#include <unistd.h>

#include <Canvas.h>
#include <FrameBuffer.h>
#include <Input.h>

/// Ideas: (most stolen from flutter)
// * Widgets are cheap to create, so have no real state.
// * StatefulWidget has state in seperate object, making it still cheap
// * The state is actually associated with the underlying render object in the
//    scene tree.

// TODO: move to math util.
struct Size {
  int width;
  int height;

  constexpr rmlib::Point toPoint() const { return { width - 1, height - 1 }; }

  friend bool operator==(const Size& lhs, const Size& rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
  }

  friend bool operator!=(const Size& lhs, const Size& rhs) {
    return !(lhs == rhs);
  }
};

struct Insets {
  int top = 0;
  int bottom = 0;
  int left = 0;
  int right = 0;

  constexpr static Insets all(int size) { return { size, size, size, size }; }

  constexpr int horizontal() const { return left + right; }
  constexpr int vertical() const { return top + bottom; }

  constexpr rmlib::Rect shrink(const rmlib::Rect& rect) const {
    return rmlib::Rect{ { rect.topLeft.x + left, rect.topLeft.y + top },
                        { rect.bottomRight.x - right,
                          rect.bottomRight.y - bottom } };
  }
};

struct Constraints {
  static constexpr auto unbound = std::numeric_limits<int>::max();

  Size min;
  Size max;

  constexpr bool hasBoundedWidth() const { return max.width != unbound; }
  constexpr bool hasBoundedHeight() const { return max.height != unbound; }
  constexpr bool hasFiniteWidth() const { return min.width != unbound; }
  constexpr bool hasFiniteHeight() const { return min.height != unbound; }

  constexpr bool contain(Size size) const {
    return min.width <= size.width && size.width <= max.width &&
           min.height <= size.height && size.height <= max.height;
  }

  constexpr Constraints inset(Insets insets) const {
    const auto minHorizontal = std::max(0, min.width - insets.horizontal());
    const auto minVertical = std::max(0, min.height - insets.vertical());

    const auto maxHorizontal =
      hasBoundedWidth()
        ? std::max(minHorizontal, max.width - insets.horizontal())
        : unbound;
    const auto maxVertical =
      hasBoundedHeight() ? std::max(minVertical, max.height - insets.vertical())
                         : unbound;

    return Constraints{ { minHorizontal, minVertical },
                        { maxHorizontal, maxVertical } };
  }

  constexpr Size expand(Size size, Insets insets) const {
    const auto newWidth = size.width + insets.horizontal();
    const auto newHeight = size.height + insets.vertical();

    return Size{ std::min(newWidth, max.width),
                 std::min(newHeight, max.height) };
  }
};

struct UpdateRegion {
  rmlib::Rect region = { { 0, 0 }, { 0, 0 } };
  rmlib::fb::Waveform waveform = rmlib::fb::Waveform::GC16Fast;
  rmlib::fb::UpdateFlags flags = rmlib::fb::UpdateFlags::None;

  constexpr UpdateRegion& operator|=(const UpdateRegion& other) {
    if (other.waveform == rmlib::fb::Waveform::GC16) {
      waveform = other.waveform;
    } else if (other.waveform == rmlib::fb::Waveform::GC16Fast &&
               waveform == rmlib::fb::Waveform::DU) {
      waveform = other.waveform;
    }

    if (region.empty()) {
      region = other.region;
    } else if (!other.region.empty()) {
      region |= other.region;
    }

    flags = static_cast<rmlib::fb::UpdateFlags>(flags | other.flags);

    return *this;
  }
};

constexpr UpdateRegion
operator|(UpdateRegion a, const UpdateRegion& b) {
  a |= b;
  return a;
}

class RenderObject {
public:
  virtual ~RenderObject(){};

  Size layout(const Constraints& constraints) {
    if (needsLayout()) {

      const auto result = doLayout(constraints);
      assert(result.width != Constraints::unbound &&
             result.height != Constraints::unbound);
      assert(constraints.contain(result));

      mNeedsLayout = false;

      lastSize = result;
      return result;
    }
    return lastSize;
  }

  UpdateRegion draw(rmlib::Rect rect, rmlib::Canvas& canvas) {
    auto result = UpdateRegion{};
    if (needsDraw()) {
      if (mNeedsDraw) {
        canvas.set(this->rect, rmlib::white);
        result |= UpdateRegion{ this->rect, rmlib::fb::Waveform::DU };
      }

      this->rect = rect;
      result |= doDraw(rect, canvas);

      mNeedsDraw = false;
    }

    return result;
  }

  virtual void handleInput(const rmlib::input::Event& ev) {}

  virtual bool needsDraw() const { return mNeedsDraw; }
  virtual bool needsLayout() const { return mNeedsLayout; }

  const rmlib::Rect& getRect() const { return rect; }

  const Size& getSize() const { return lastSize; }

  virtual void markNeedsLayout() { mNeedsLayout = true; }
  virtual void markNeedsDraw() { mNeedsDraw = true; }

protected:
  virtual Size doLayout(const Constraints& Constraints) = 0;
  virtual UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) = 0;

private:
  rmlib::Rect rect = { { 0, 0 }, { 0, 0 } };

  Size lastSize = { 0, 0 };

  bool mNeedsLayout = true;
  bool mNeedsDraw = true;
};

class SingleChildRenderObject : public RenderObject {
public:
  SingleChildRenderObject() = default;
  SingleChildRenderObject(std::unique_ptr<RenderObject> child)
    : child(std::move(child)) {}

  void handleInput(const rmlib::input::Event& ev) override {
    std::visit(
      [this](const auto& ev) {
        if constexpr (!std::is_same_v<decltype(ev),
                                      const rmlib::input::KeyEvent&>) {
          if (child->getRect().contains(ev.location)) {
            child->handleInput(ev);
          }
        } else {
          child->handleInput(ev);
        }
      },
      ev);
  }

  bool needsLayout() const override {
    return RenderObject::needsLayout() || child->needsLayout();
  }

  bool needsDraw() const override {
    return RenderObject::needsDraw() || child->needsDraw();
  }

  void markNeedsLayout() override {
    RenderObject::markNeedsLayout();
    child->markNeedsLayout();
  }

  void markNeedsDraw() override {
    RenderObject::markNeedsDraw();
    child->markNeedsDraw();
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    return child->layout(constraints);
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    return child->draw(rect, canvas);
  }

  std::unique_ptr<RenderObject> child;
};

class MultiChildRenderObject : public RenderObject {
public:
  MultiChildRenderObject(std::vector<std::unique_ptr<RenderObject>> children)
    : children(std::move(children)) {}

  void handleInput(const rmlib::input::Event& ev) override {
    std::visit(
      [this](const auto& ev) {
        if constexpr (!std::is_same_v<decltype(ev),
                                      const rmlib::input::KeyEvent&>) {

          for (const auto& child : children) {
            if (child->getRect().contains(ev.location)) {
              child->handleInput(ev);
            }
          }
        } else {
          for (const auto& child : children) {
            child->handleInput(ev);
          }
        }
      },
      ev);
  }

  bool needsLayout() const override {
    return RenderObject::needsLayout() ||
           std::any_of(children.begin(), children.end(), [](const auto& child) {
             return child->needsLayout();
           });
  }

  bool needsDraw() const override {
    return RenderObject::needsDraw() ||
           std::any_of(children.begin(), children.end(), [](const auto& child) {
             return child->needsDraw();
           });
  }

  void markNeedsLayout() override {
    RenderObject::markNeedsLayout();
    for (auto& child : children) {
      child->markNeedsLayout();
    }
  }

  void markNeedsDraw() override {
    RenderObject::markNeedsDraw();
    for (auto& child : children) {
      child->markNeedsDraw();
    }
  }

protected:
  std::vector<std::unique_ptr<RenderObject>> children;
};

template<typename AppWidget>
OptError<>
runApp(AppWidget widget) {
  auto fb = TRY(rmlib::fb::FrameBuffer::open());
  rmlib::input::InputManager inputMgr;
  TRY(inputMgr.openAll());

  auto rootRO = widget.createRenderObject();

  auto fbSize = Size{ fb.canvas.width(), fb.canvas.height() };
  auto constraint = Constraints{ fbSize, fbSize };

  while (true) {
    auto size = rootRO->layout(constraint);
    auto rect = rmlib::Rect{ { 0, 0 }, size.toPoint() };

    auto updateRegion = rootRO->draw(rect, fb.canvas);
    if (!updateRegion.region.empty()) {
      fb.doUpdate(
        updateRegion.region, updateRegion.waveform, updateRegion.flags);
    }

    auto evs = TRY(inputMgr.waitForInput(std::nullopt));
    for (const auto& ev : evs) {
      rootRO->handleInput(ev);
    }
  }
}

class Widget {
public:
  std::unique_ptr<RenderObject> createRenderObject() const {
    assert(false && "must implement");
    return nullptr;
  }

  // void update(RenderObject& ro){};
};

namespace details {
template<typename Fn>
struct ArgType;

template<typename R, typename Class, typename Arg>
struct ArgType<R (Class::*)(Arg)> {
  using Type = Arg;
};
} // namespace details

template<typename RO>
class WidgetTest : public Widget {
public:
  void update(RenderObject& ro) const {
    using ArgType = typename details::ArgType<decltype(&RO::update)>::Type;
    static_cast<RO*>(&ro)->update(static_cast<ArgType>(*this));
  }
};

class TextRenderObject : public RenderObject {
public:
  class Text : public WidgetTest<TextRenderObject> {
  public:
    Text(std::string text) : text(std::move(text)) {}

    std::unique_ptr<RenderObject> createRenderObject() const {
      return std::make_unique<TextRenderObject>(*this);
    }

  private:
    friend class TextRenderObject;
    std::string text;
  };

  TextRenderObject(const Text& widget) : widget(&widget) {}

  void update(const Text& newWidget) {
    if (newWidget.text.size() != widget->text.size()) {
      markNeedsLayout();
    }

    if (newWidget.text != widget->text) {
      markNeedsDraw();
    }

    widget = &newWidget;
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    const auto textSize =
      rmlib::Canvas::getTextSize(widget->text, rmlib::default_text_size);

    Size result;

    if (textSize.x > constraints.max.width) {
      result.width = constraints.max.width;
    } else if (textSize.x < constraints.min.width) {
      result.width = constraints.min.width;
    } else {
      result.width = textSize.x;
    }

    if (textSize.y > constraints.max.height) {
      result.height = constraints.max.height;
    } else if (textSize.y < constraints.min.height) {
      result.height = constraints.min.height;
    } else {
      result.height = textSize.y;
    }

    return result;
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    const auto textSize =
      rmlib::Canvas::getTextSize(widget->text, rmlib::default_text_size);
    const auto x = std::max(0, (rect.width() - textSize.x) / 2);
    const auto y = std::max(0, (rect.height() - textSize.y) / 2);

    const auto point = rect.topLeft + rmlib::Point{ x, y };
    // TODO: intersect with rect, fix size - 1:
    const auto drawRect = rmlib::Rect{ point, point + textSize };

    std::cout << "Drawing text: " << widget->text << "\n";
    canvas.set(drawRect, rmlib::white);
    canvas.drawText(widget->text, point, rmlib::default_text_size, rect);
    return UpdateRegion{ drawRect };
  }

private:
  const Text* widget;
};

using Text = TextRenderObject::Text;

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
class Center : public WidgetTest<CenterRenderObject<Child>> {
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
class Padding : public WidgetTest<PaddingRenderObject<Child>> {
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
      std::cout << "Border size changed, redrawing\n";
      markNeedsDraw();
    }

    return newSize;
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    const auto insets = Insets::all(widget->size);
    auto result = child->draw(insets.shrink(rect), canvas);

    if (RenderObject::needsDraw()) {
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
class Border : public WidgetTest<BorderRenderObject<Child>> {
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

enum class Axis { Horizontal, Vertical };

template<typename... Children>
class Flex;

template<typename... Children>
class FlexRenderObject : public MultiChildRenderObject {
public:
  FlexRenderObject(const Flex<Children...>& widget)
    : MultiChildRenderObject(std::apply(
        [](auto&... child) {
          auto array = std::array{ child.createRenderObject()... };
          std::vector<std::unique_ptr<RenderObject>> objs;
          objs.reserve(num_children);

          for (auto& elem : array) {
            objs.emplace_back(std::move(elem));
          }
          return objs;
        },
        widget.children))
    , widget(&widget) {}

  void update(const Flex<Children...>& newWidget) {
    if (newWidget.axis != widget->axis) {
      markNeedsLayout();
      markNeedsDraw();
    }

    std::apply(
      [this](const auto&... childWidget) {
        int i = 0;
        (childWidget.update(*children[i++]), ...);
      },
      newWidget.children);
    widget = &newWidget;
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    assert(isVertical() ? constraints.hasBoundedHeight()
                        : constraints.hasBoundedWidth());

    Size result = { 0, 0 };

    const auto childConstraints =
      isVertical()
        ? Constraints{ { constraints.min.width, 0 },
                       { constraints.max.width, Constraints::unbound } }
        : Constraints{ { 0, constraints.min.height },
                       { Constraints::unbound, constraints.max.height } };

    for (auto i = 0u; i < num_children; i++) {
      const auto newSize = children[i]->layout(childConstraints);
      if (isVertical() ? newSize.height != childSizes[i].height
                       : newSize.width != childSizes[i].width) {
        std::cout << "child changed size, redrawing\n";
        markNeedsDraw();
      }

      childSizes[i] = newSize;

      if (isVertical()) {
        result.height += childSizes[i].height;
        result.width = std::max(childSizes[i].width, result.width);
      } else {
        result.width += childSizes[i].width;
        result.height = std::max(childSizes[i].height, result.height);
      }
    }

    assert(result.height <= constraints.max.height && "Flex too large");
    assert(result.width <= constraints.max.width && "Flex too wide");

    totalSize = isVertical() ? result.height : result.width;

    // Align on each axis:
    if (result.height < constraints.min.height) {
      result.height = constraints.min.height;
    }

    if (result.width < constraints.min.width) {
      result.width = constraints.min.width;
    }

    return result;
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    UpdateRegion result;

    const auto maxSize = isVertical() ? rect.height() : rect.width();
    auto offset = (maxSize - totalSize) / 2;

    for (auto i = 0u; i < num_children; i++) {
      const auto& size = childSizes[i];
      const auto& child = children[i];

      const auto otherOffset = isVertical() ? (rect.width() - size.width) / 2
                                            : (rect.height() - size.height) / 2;
      const auto offsetPoint = isVertical()
                                 ? rmlib::Point{ otherOffset, offset }
                                 : rmlib::Point{ offset, otherOffset };

      const auto topLeft = rect.topLeft + offsetPoint;
      const auto bottomRight = topLeft + size.toPoint();
      const auto subRect = rmlib::Rect{ topLeft, bottomRight };

      if (i == 0) {
        result = child->draw(subRect, canvas);
      } else {
        result |= child->draw(subRect, canvas);
      }
      offset += isVertical() ? size.height : size.width;
    }

    return result;
  }

private:
  constexpr bool isVertical() const { return widget->axis == Axis::Vertical; }

  const Flex<Children...>* widget;
  constexpr static auto num_children = sizeof...(Children);

  std::array<Size, num_children> childSizes;
  int totalSize;
};

template<typename... Children>
class Flex : public WidgetTest<FlexRenderObject<Children...>> {
private:
public:
  Flex(Axis axis, Children... children)
    : children(std::move(children)...), axis(axis) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<FlexRenderObject<Children...>>(*this);
  }

private:
  friend class FlexRenderObject<Children...>;
  std::tuple<Children...> children;
  Axis axis;
};

template<typename... Children>
class Column : public Flex<Children...> {
public:
  Column(Children... children)
    : Flex<Children...>(Axis::Vertical, std::move(children)...) {}
};

template<typename... Children>
class Row : public Flex<Children...> {
public:
  Row(Children... children)
    : Flex<Children...>(Axis::Horizontal, std::move(children)...) {}
};

template<typename Child>
auto
Container(Child child,
          Insets padding = {},
          int border = 0,
          Insets margin = {}) {
  return Padding(Border(Padding(child, padding), border), margin);
}

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
class GestureDetector : public WidgetTest<GestureRenderObject<Child>> {
private:
public:
  GestureDetector(Child child, std::function<void()> onTap)
    : child(std::move(child)), onTap(std::move(onTap)) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<GestureRenderObject<Child>>(*this);
  }

private:
  friend class GestureRenderObject<Child>;
  Child child;
  std::function<void()> onTap;
};

auto
Button(std::string text, std::function<void()> onTap) {
  return GestureDetector(
    Container(Text(text), Insets::all(2), 2, Insets::all(1)), std::move(onTap));
}

template<typename Derived>
class StateBase {
public:
  void setStateChangeCb(std::function<void()> cb) {
    stateChangeCb = std::move(cb);
  }

protected:
  template<typename Fn>
  void setState(Fn fn) const {
    fn(*const_cast<Derived*>(static_cast<const Derived*>(this)));
    // mark dirty
    stateChangeCb();
  }

private:
  std::function<void()> stateChangeCb;
};

template<typename Derived>
class StatefulWidget : public Widget {
private:
  class StatefulRenderObject : public SingleChildRenderObject {
  public:
    using StateType = typename Derived::State;
    using WidgetType =
      std::result_of_t<decltype (&StateType::build)(const StateType)>;

    StatefulRenderObject(const Derived& widget)
      : state(widget.createState())
      , currentWidget(std::make_unique<WidgetType>(constState().build())) {
      child = currentWidget->createRenderObject();

      state.setStateChangeCb([this]() { rebuild(); });
    }

  private:
    void rebuild() {
      auto newWidget = std::make_unique<WidgetType>(constState().build());
      newWidget->update(*child);
      currentWidget = std::move(newWidget);
    }

    StateType state;
    const StateType& constState() const { return state; }

    std::unique_ptr<WidgetType> currentWidget; // TODO: do without allocations?
  };

public:
  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<StatefulRenderObject>(
      *static_cast<const Derived*>(this));
  }
};

namespace details {
class type_id_t {
  using sig = type_id_t();

  sig* id;
  type_id_t(sig* id) : id{ id } {}

public:
  template<typename T>
  friend type_id_t type_id();

  bool operator==(type_id_t o) const { return id == o.id; }
  bool operator!=(type_id_t o) const { return id != o.id; }
};

template<typename T>
type_id_t
type_id() {
  return &type_id<T>;
}
} // namespace details

class DynamicWidget : public Widget {
private:
  struct DynamicRenderObject : public SingleChildRenderObject {
    DynamicRenderObject(std::unique_ptr<RenderObject> ro,
                        details::type_id_t typeID)
      : SingleChildRenderObject(std::move(ro)), typeID(typeID) {}
    details::type_id_t typeID;
    RenderObject& getChild() { return *child; }

    void setChild(std::unique_ptr<RenderObject> ro, details::type_id_t typeID) {
      child = std::move(ro);
      this->typeID = typeID;
    }
  };

  struct DynamicWidgetBase {
    virtual std::unique_ptr<RenderObject> createRenderObject() const = 0;
    virtual void update(RenderObject& RO) const = 0;
    virtual ~DynamicWidgetBase() = default;
  };

  template<typename W>
  struct DynamicWidgetImpl : public DynamicWidgetBase {
    DynamicWidgetImpl(W w) : widget(std::move(w)) {}

    std::unique_ptr<RenderObject> createRenderObject() const final {
      return std::make_unique<DynamicRenderObject>(widget.createRenderObject(),
                                                   details::type_id<W>());
    }

    void update(RenderObject& ro) const final {
      auto& dynRo = *static_cast<DynamicRenderObject*>(&ro);
      if (dynRo.typeID != details::type_id<W>()) {
        dynRo.setChild(widget.createRenderObject(), details::type_id<W>());
      } else {
        widget.update(dynRo.getChild());
      }
    }

  private:
    W widget;
  };

public:
  template<typename W>
  DynamicWidget(W w)
    : mWidget(std::make_unique<DynamicWidgetImpl<W>>(std::move(w))) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return mWidget->createRenderObject();
  }

  void update(RenderObject& RO) const { mWidget->update(RO); }

private:
  std::unique_ptr<DynamicWidgetBase> mWidget;
};

class ToggleTest : public StatefulWidget<ToggleTest> {
public:
  class State : public StateBase<State> {
  public:
    auto build() const {
      return GestureDetector(
        Border(Padding(Text(""), Insets::all(on ? 0 : 8)), on ? 10 : 2),
        [this]() { setState([](auto& self) { self.on = !self.on; }); });
    }

  private:
    bool on = true;
  };

public:
  State createState() const { return State{}; }
};

class CounterTest : public StatefulWidget<CounterTest> {
public:
  class State : public StateBase<State> {
  public:
    auto build() const {
      // if (count < 3) {
      //   return Column(Row(Text("Counter: "), Text(std::to_string(count))),
      //                 Row(Button("-1", [this] { decrease(); }),
      //                     Button("+1", [this] { increase(); })));
      // } else {
      return ToggleTest();
      //}
    }

  private:
    void increase() const {
      setState([](auto& self) { self.count++; });
      std::cout << "new count: " << count << "\n";
    }

    void decrease() const {
      setState([](auto& self) { self.count--; });
      std::cout << "new count: " << count << "\n";
    }

    int count = 0;
  };

public:
  State createState() const { return State{}; }
};

int
main() {
  auto optErr = runApp(Center(ToggleTest()));

  if (optErr.isError()) {
    std::cerr << optErr.getError().msg << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
