#pragma once

#include <UI/DynamicWidget.h>
#include <UI/Stack.h>
#include <UI/StatefulWidget.h>

namespace rmlib {

struct OverlayEntry {
  std::function<DynamicWidget()> builder;
  bool maintainState = false;
};

class Navigator : public StatefulWidget<Navigator> {
private:
  class State : public StateBase<Navigator> {
  public:
    void init(AppContext&) { entries = getWidget().initElems; }

    auto build(AppContext& context, const BuildContext&) const {
      std::vector<DynamicWidget> widgets;
      widgets.reserve(entries.size());
      std::transform(entries.begin(),
                     entries.end(),
                     std::back_inserter(widgets),
                     [](const auto& entry) { return entry.builder(); });
      return Stack(std::move(widgets));
    }

    void push(OverlayEntry entry) const {
      setState([entry = std::move(entry)](auto& self) {
        self.entries.push_back(std::move(entry));
      });
    }

    void pop() const {
      setState([](auto& self) { self.entries.pop_back(); });
    }

  private:
    std::vector<OverlayEntry> entries;
  };

public:
  template<typename Widget>
  Navigator(Widget initWidget)
    : initElems({ OverlayEntry{
        [initWidget = std::move(initWidget)] { return initWidget; },
        true } }) {}

  Navigator(std::vector<OverlayEntry> initElems)
    : initElems(std::move(initElems)) {}

  State createState() const { return State(); }

  static const State& of(const BuildContext& buildCtx) {
    return StatefulWidget::getState(buildCtx);
  }

private:
  std::vector<OverlayEntry> initElems;
};

} // namespace rmlib
