#pragma once

#include <UI/RenderObject.h>
#include <UI/Widget.h>

#include <UI/DynamicWidget.h>
#include <UI/StatefulWidget.h>
#include <UI/StatelessWidget.h>

#include <UI/Flex.h>
#include <UI/Gesture.h>
#include <UI/Layout.h>
#include <UI/Text.h>

#include <UI/AppContext.h>
#include <UI/Util.h>

/// Ideas: (most stolen from flutter)
// * Widgets are cheap to create, so have no real state.
// * StatefulWidget has state in seperate object, making it still cheap
// * The state is actually associated with the underlying render object in the
//    scene tree.
namespace rmlib {

template<typename AppWidget>
OptError<>
runApp(AppWidget widget) {
  auto fb = TRY(rmlib::fb::FrameBuffer::open());
  rmlib::input::InputManager inputMgr;
  TRY(inputMgr.openAll());

  auto rootRO = widget.createRenderObject();

  const auto fbSize = Size{ fb.canvas.width(), fb.canvas.height() };
  const auto constraints = Constraints{ fbSize, fbSize };

  AppContext context;

  while (!context.shouldStop()) {
    rootRO->rebuild(context);

    const auto size = rootRO->layout(constraints);
    const auto rect = rmlib::Rect{ { 0, 0 }, size.toPoint() };

    auto updateRegion = rootRO->cleanup(fb.canvas);
    updateRegion |= rootRO->draw(rect, fb.canvas);

    if (!updateRegion.region.empty()) {
      fb.doUpdate(
        updateRegion.region, updateRegion.waveform, updateRegion.flags);
    }

    const auto duration = context.getNextDuration();
    const auto evs = TRY(inputMgr.waitForInput(duration));
    context.checkTimers();

    for (const auto& ev : evs) {
      rootRO->handleInput(ev);
    }

    rootRO->reset();
  }

  return NoError{};
}

} // namespace rmlib
