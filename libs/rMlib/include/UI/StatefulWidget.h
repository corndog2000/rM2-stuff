#pragma once

#include <UI/RenderObject.h>
#include <UI/Widget.h>

#include <memory>
#include <optional>

namespace rmlib {

template<typename Derived>
class StatefulRenderObject;

template<typename Derived>
class StatefulWidget;

namespace details {
template<typename DerivedSW>
struct SWTraits {
  static_assert(std::is_base_of_v<StatefulWidget<DerivedSW>, DerivedSW>,
                "The widget must derive StatefulWidget");

  using StateType =
    std::result_of_t<decltype (&DerivedSW::createState)(const DerivedSW)>;
  using WidgetType = std::result_of_t<decltype (
    &StateType::build)(const StateType, AppContext&, const BuildContext&)>;
};

} // namespace details

template<typename DerivedSW>
class StateBase {
public:
  void setRenderObject(StatefulRenderObject<DerivedSW>& renderObject) {
    mRenderObject = &renderObject;
  }

  void init(AppContext& ctx) {}

protected:
  template<typename Fn>
  void setState(Fn fn) const {
    using StateT = typename details::SWTraits<DerivedSW>::StateType;
    fn(*const_cast<StateT*>(static_cast<const StateT*>(this)));

    // mark dirty
    mRenderObject->markNeedsRebuild();
  }

  const DerivedSW& getWidget() const { return mRenderObject->getWidget(); }

private:
  StatefulRenderObject<DerivedSW>* mRenderObject;
};

template<typename Derived>
class StatefulRenderObject : public SingleChildRenderObject<Derived> {
private:
  using SWTraits = details::SWTraits<Derived>;
  using StateT = typename SWTraits::StateType;
  using WidgetT = typename SWTraits::WidgetType;

public:
  StatefulRenderObject(const Derived& widget)
    : SingleChildRenderObject<Derived>(widget, nullptr)
    , state(widget.createState()) {
    state.setRenderObject(*this);
    this->markNeedsRebuild();
  }

  void update(const Derived& newWidget) {
    // currentWidget()->update(*child);

    this->widget = &newWidget;
    this->markNeedsRebuild();
  }

  const Derived& getWidget() const { return *this->widget; }
  const StateT& getState() const { return state; }

  // Provide a desctructor that destroys the child before destroying the
  // widgets.
  virtual ~StatefulRenderObject() { this->child = nullptr; }

protected:
  void doRebuild(AppContext& context, const BuildContext& buildCtx) override {
    if (!hasInitedState) {
      state.init(context);
      hasInitedState = true;
    }

    otherWidget().emplace(std::move(constState().build(context, buildCtx)));
    if (this->child == nullptr) {
      this->child = otherWidget()->createRenderObject();
    } else {
      otherWidget()->update(*this->child);
    }
    swapWidgets();
  }

private:
  std::optional<WidgetT>& currentWidget() { return buildWidgets[currentIdx]; }
  std::optional<WidgetT>& otherWidget() { return buildWidgets[1 - currentIdx]; }
  void swapWidgets() { currentIdx = 1 - currentIdx; }

  const StateT& constState() const { return state; }

  std::array<std::optional<WidgetT>, 2> buildWidgets;
  StateT state;

  int currentIdx = 0;

  bool hasInitedState = false;
};

template<typename Derived>
class StatefulWidget : public Widget<StatefulRenderObject<Derived>> {
public:
  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<StatefulRenderObject<Derived>>(
      *static_cast<const Derived*>(this));
  }

  static const auto& getState(const BuildContext& buildCtx) {
    const auto* ro = buildCtx.getRenderObject<StatefulRenderObject<Derived>>();
    assert(ro != nullptr && "No such parent");
    return ro->getState();
  }

private:
  friend Derived;
  StatefulWidget() {}
};

} // namespace rmlib
