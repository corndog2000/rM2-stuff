#pragma once

#include <UI/RenderObject.h>
#include <UI/Widget.h>

namespace rmlib {

template<typename Derived>
class StatelessWidget;

template<typename Derived>
class StatelessRenderObject : public SingleChildRenderObject {
  using WidgetT =
    std::result_of_t<decltype (&Derived::build)(const Derived, AppContext&)>;

public:
  StatelessRenderObject(const Derived& derived)
    : SingleChildRenderObject(), derived(&derived) {
    markNeedsRebuild();
  }

  void update(const StatelessWidget<Derived>& newWidget) {
    derived = static_cast<const Derived*>(&newWidget);
    markNeedsRebuild();

    // Don't update the current widget as we need to rebuild anyways.
    // currentWidget()->update(*child);
  }

protected:
  void doRebuild(AppContext& context) override {
    otherWidget().emplace(std::move(derived->build(context)));
    if (child == nullptr) {
      child = otherWidget()->createRenderObject();
    } else {
      otherWidget()->update(*child);
    }
    swapWidgets();
  }

private:
  std::optional<WidgetT>& currentWidget() { return buildWidgets[currentIdx]; }
  std::optional<WidgetT>& otherWidget() { return buildWidgets[1 - currentIdx]; }
  void swapWidgets() { currentIdx = 1 - currentIdx; }
  const Derived* derived;

  std::array<std::optional<WidgetT>, 2> buildWidgets;
  int currentIdx = 0;
};

template<typename Derived>
class StatelessWidget : public Widget<StatelessRenderObject<Derived>> {
public:
  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<StatelessRenderObject<Derived>>(
      *static_cast<const Derived*>(this));
  }
};

} // namespace rmlib
