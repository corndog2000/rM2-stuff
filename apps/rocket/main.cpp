#include "CommandSocket.h"
#include "Commands.h"
#include "Launcher.h"

#include <csignal>
#include <fstream>
#include <iostream>

#include <sys/wait.h>
#include <unistd.h>

#include <UI.h>

using namespace rmlib;

namespace {

#ifndef KEY_POWER
#define KEY_POWER 116
#endif

constexpr auto config_path_suffix = ".config/rocket/config";

std::vector<pid_t> stoppedChildren;
std::function<void()> stopCallback;

void
cleanup(int signal) {
  pid_t childPid = 0;
  while ((childPid = waitpid(static_cast<pid_t>(-1), nullptr, WNOHANG)) > 0) {
    std::cout << "Exited: " << childPid << std::endl;
    stoppedChildren.push_back(childPid);
  }

  if (stopCallback) {
    stopCallback();
  }
}

bool
parseConfig() {
  //   const auto* home = getenv("HOME");
  //   if (home == nullptr) {
  //     return false;
  //   }
  //
  //   auto configPath = std::string(home) + '/' + config_path_suffix;
  //
  //   std::ifstream ifs(configPath);
  //   if (!ifs.is_open()) {
  //     // TODO: default config
  //     std::cerr << "Error opening config path\n";
  //     return false;
  //   }
  //
  //   for (std::string line; std::getline(ifs, line);) {
  //     auto result = doCommand(launcher, line);
  //     if (result.isError()) {
  //       std::cerr << result.getError().msg << std::endl;
  //     } else {
  //       std::cout << *result << std::endl;
  //     }
  //   }

  return true;
}

int
runCommand(int argc, char* argv[]) {
  std::string command;
  for (int i = 1; i < argc; i++) {
    command.append(argv[i]);
    if (i != argc - 1) {
      command.push_back(' ');
    }
  }

  if (command.empty()) {
    std::cerr << "Rocket running, usage: " << argv[0] << " <command>\n";
    return EXIT_FAILURE;
  }

  if (!CommandSocket::send(command)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

template<typename Child>
class Hideable;

template<typename Child>
class HideableRenderObject : public SingleChildRenderObject {
public:
  HideableRenderObject(const Hideable<Child>& widget)
    : SingleChildRenderObject(
        widget.child.has_value() ? widget.child->createRenderObject() : nullptr)
    , widget(&widget) {}

  Size doLayout(const Constraints& constraints) override {
    if (!widget->child.has_value()) {
      return constraints.min;
    }
    return child->layout(constraints);
  }

  void update(const Hideable<Child>& newWidget) {
    auto wasVisible = widget->child.has_value();
    widget = &newWidget;
    if (widget->child.has_value()) {
      if (child == nullptr) {
        child = widget->child->createRenderObject();
      } else {
        widget->child->update(*child);
      }

      if (!wasVisible) {
        doRefresh = true;
        markNeedsDraw();
      }
    } else if (widget->background != nullptr && wasVisible) {
      // Don't mark the children!
      RenderObject::markNeedsDraw();
    }
  }

  void handleInput(const rmlib::input::Event& ev) override {
    if (widget->child.has_value()) {
      child->handleInput(ev);
    }
  }

protected:
  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    if (!widget->child.has_value()) {
      if (widget->background != nullptr) {
        const auto offset =
          (rect.size() - widget->background->rect().size()) / 2;
        copy(canvas,
             offset.toPoint(),
             *widget->background,
             widget->background->rect());
        return UpdateRegion{ rect };
      }

      return UpdateRegion{};
    }

    auto result = child->draw(rect, canvas);
    if (doRefresh) {
      doRefresh = false;
      result.waveform = fb::Waveform::GC16;
      result.flags = static_cast<fb::UpdateFlags>(fb::UpdateFlags::FullRefresh |
                                                  fb::UpdateFlags::Sync);
    }
    return result;
  }

private:
  const Hideable<Child>* widget;
  Size childSize;
  bool doRefresh = false;
};

template<typename Child>
class Hideable : public Widget<HideableRenderObject<Child>> {
private:
public:
  Hideable(std::optional<Child> child, const Canvas* background = nullptr)
    : child(std::move(child)), background(background) {}

  std::unique_ptr<RenderObject> createRenderObject() const {
    return std::make_unique<HideableRenderObject<Child>>(*this);
  }

private:
  friend class HideableRenderObject<Child>;
  std::optional<Child> child;
  const Canvas* background;
};

const auto missingImage = [] {
  auto mem = MemoryCanvas(128, 128, 2);
  mem.canvas.set(0xaa);
  return mem;
}();

class RunningAppWidget : public StatelessWidget<RunningAppWidget> {
public:
  RunningAppWidget(const App& app,
                   Callback onTap,
                   Callback onKill,
                   bool isCurrent)
    : app(app)
    , onTap(std::move(onTap))
    , onKill(std::move(onKill))
    , isCurrent(isCurrent) {}

  auto build(AppContext&) const {
    const Canvas& canvas =
      app.savedFb.has_value() ? app.savedFb->canvas : missingImage.canvas;

    return Container(
      Column(GestureDetector(Sized(Image(canvas), 234, 300),
                             Gestures{}.OnTap(onTap)),
             Row(Text(app.description.name), Button("X", onKill))),
      Insets::all(isCurrent ? 1 : 2),
      Insets::all(isCurrent ? 2 : 1),
      Insets::all(2));
  }

private:
  const App& app;
  Callback onTap;
  Callback onKill;
  bool isCurrent;
};

class AppWidget : public StatelessWidget<AppWidget> {
public:
  AppWidget(const App& app, Callback onLaunch)
    : app(app), onLaunch(std::move(onLaunch)) {}

  auto build(AppContext&) const {
    const Canvas& canvas = app.description.iconImage.has_value()
                             ? app.description.iconImage->canvas
                             : missingImage.canvas;
    return Container(GestureDetector(Column(Sized(Image(canvas), 128, 128),
                                            Text(app.description.name)),
                                     Gestures{}.OnTap(onLaunch)),
                     Insets::all(2),
                     Insets::all(1),
                     Insets::all(2));
  }

private:
  const App& app;
  Callback onLaunch;
};

class LauncherState;

class LauncherWidget : public StatefulWidget<LauncherWidget> {
public:
  LauncherState createState() const;
};

class LauncherState : public StateBase<LauncherWidget> {
public:
  ~LauncherState() {
    stopCallback = nullptr; // TODO: stop all apps
  }

  void init(AppContext& context) {
    context.getInputManager().getBaseDevices()->key.grab();

    fbCanvas = &context.getFbCanvas();
    touchDevice = &context.getInputManager().getBaseDevices()->touch;

    readApps(); // TODO: do this on turning visible

    assert(stopCallback == nullptr);
    stopCallback = [this, &context] {
      context.doLater(
        [this] { setState([](auto& self) { self.updateStoppedApps(); }); });
    };
  }

  auto header(AppContext& context) const {
    const auto text = [this]() -> std::string {
      switch (sleepCountdown) {
        case -1:
          return "Welcome";
        case 0:
          return "Sleeping";
        default:
          return "Sleeping in : " + std::to_string(sleepCountdown);
      }
    }();

    auto button = [this, &context] {
      if (sleepCountdown > 0) {
        return Button(
          "Stop", [this] { setState([](auto& self) { self.stopTimer(); }); });
      } else if (sleepCountdown == 0) {
        // TODO: make hideable?
        return Button("...", [] {});
      } else {
        return Button("Sleep", [this, &context] {
          setState([&context](auto& self) { self.startTimer(context, 0); });
        });
      }
    }();

    return Center(Padding(
      Column(Padding(Text(text, 2 * default_text_size), Insets::all(10)),
             button),
      Insets::all(50)));
  }

  auto runningApps() const {
    std::vector<RunningAppWidget> widgets;
    for (const auto& app : apps) {
      if (app.isRunning()) {
        widgets.emplace_back(
          app,
          [this, &app] {
            setState(
              [&app](auto& self) { self.switchApp(*const_cast<App*>(&app)); });
          },
          [this, &app] {
            setState([&app](auto& self) {
              std::cout << "stopping " << app.description.name << std::endl;
              const_cast<App*>(&app)->stop();
            });
          },
          app.description.path == currentAppPath);
      }
    }
    return Wrap(widgets);
  }

  auto appList() const {
    std::vector<AppWidget> widgets;
    for (const auto& app : apps) {
      if (!app.isRunning()) {
        widgets.emplace_back(app, [this, &app] {
          setState(
            [&app](auto& self) { self.switchApp(*const_cast<App*>(&app)); });
        });
      }
    }
    return Wrap(widgets);
  }

  auto launcher(AppContext& context) const {
    return Cleared(Column(header(context), runningApps(), appList()));
  }

  auto build(AppContext& context) const {
    const Canvas* background = nullptr;
    if (auto* currentApp = getCurrentApp(); currentApp != nullptr) {
      if (currentApp->savedFb.has_value()) {
        background = &currentApp->savedFb->canvas;
      } else if (currentApp->description.iconImage.has_value()) {
        background = &currentApp->description.iconImage->canvas;
      }
    }

    auto ui = Hideable(
      visible ? std::optional(launcher(context)) : std::nullopt, background);

    return GestureDetector(
      ui, Gestures{}.OnKeyDown([this, &context](auto keyCode) {
        if (keyCode == KEY_POWER) {
          setState([&context](auto& self) { self.toggle(context); });
        }
      }));
  }

private:
  void sleep() {
    system("/sbin/rmmod brcmfmac");
    system("echo \"mem\" > /sys/power/state");
    // TODO: sleep before reenable?
    std::cout << "RESUME\n";
    system("/sbin/modprobe brcmfmac");
  }

  void stopTimer() {
    sleepTimer.disable();
    sleepCountdown = -1;
  }

  void startTimer(AppContext& context, int time = 10) {
    sleepCountdown = time;
    sleepTimer = context.addTimer(
      std::chrono::seconds(time == 0 ? 0 : 1),
      [this] { tick(); },
      std::chrono::seconds(1));
  }

  void tick() const {
    setState([](auto& self) {
      self.sleepCountdown -= 1;

      if (self.sleepCountdown == -1) {
        self.sleep();
        self.sleepTimer.disable();
      }
    });
  }

  void toggle(AppContext& context) {
    if (visible) {
      hide();
    } else {
      startTimer(context);
      show();
    }
  }

  void show() {
    if (visible) {
      return;
    }

    if (auto* current = getCurrentApp(); current != nullptr) {
      current->pause(MemoryCanvas(*fbCanvas));
    }
    visible = true;
  }

  void hide() {
    if (!visible) {
      return;
    }

    if (auto* current = getCurrentApp(); current != nullptr) {
      switchApp(*current);
    }
  }

  App* getCurrentApp() {
    auto app = std::find_if(apps.begin(), apps.end(), [this](auto& app) {
      return app.description.path == currentAppPath;
    });

    if (app == apps.end()) {
      return nullptr;
    }

    return &*app;
  }

  const App* getCurrentApp() const {
    return const_cast<LauncherState*>(this)->getCurrentApp();
  }

  void switchApp(App& app) {
    app.lastActivated = std::chrono::steady_clock::now();

    visible = false;
    stopTimer();

    // Pause the current app.
    if (auto* currentApp = getCurrentApp(); currentApp != nullptr &&
                                            currentApp->isRunning() &&
                                            !currentApp->isPaused()) {
      currentApp->pause();
    }

    // resume or launch app
    if (app.isPaused()) {
      if (touchDevice) {
        touchDevice->flood();
      }
      app.resume();
    } else if (!app.isRunning()) {
      app.savedFb = std::nullopt;

      if (!app.launch()) {
        std::cerr << "Error launching " << app.description.command << std::endl;
        return;
      }
    }

    currentAppPath = app.description.path;
  }

  void updateStoppedApps() {
    std::vector<pid_t> currentStoppedApps;
    std::swap(currentStoppedApps, stoppedChildren);

    for (const auto pid : currentStoppedApps) {
      auto app = std::find_if(apps.begin(), apps.end(), [pid](auto& app) {
        return app.isRunning() && app.runInfo->pid == pid;
      });

      if (app == apps.end()) {
        continue;
      }

      const auto isCurrent = app->description.path == currentAppPath;

      if (app->runInfo->shouldRemove) {
        apps.erase(app);
      } else {
        app->runInfo = std::nullopt;
      }

      if (isCurrent) {
        visible = true;
        currentAppPath = "";
        // TODO: switch to last app
      }
    }
  }

  void readApps() {
#ifdef EMULATE
    constexpr auto apps_path = "/Users/timo/.config/draft";
#else
    constexpr auto apps_path = "/etc/draft";
#endif

    auto appDescriptions = readAppFiles(apps_path);

    // Update known apps.
    for (auto appIt = apps.begin(); appIt != apps.end();) {

      auto descIt = std::find_if(appDescriptions.begin(),
                                 appDescriptions.end(),
                                 [&app = *appIt](const auto& desc) {
                                   return desc.path == app.description.path;
                                 });

      // Remove old apps.
      if (descIt == appDescriptions.end()) {
        if (!appIt->isRunning()) {
          appIt = apps.erase(appIt);
          continue;
        } else {
          // Defer removing until exit.
          appIt->runInfo->shouldRemove = true;
        }
      } else {

        // Update existing apps.
        appIt->description = std::move(*descIt);
        appDescriptions.erase(descIt);
      }

      ++appIt;
    }

    // Any left over descriptions are new.
    for (auto& desc : appDescriptions) {
      apps.emplace_back(std::move(desc));
    }

    std::sort(apps.begin(), apps.end(), [](const auto& app1, const auto& app2) {
      return app1.description.path < app2.description.path;
    });
  }

  std::vector<App> apps;
  std::string currentAppPath;

  std::optional<MemoryCanvas> backupBuffer;

  TimerHandle sleepTimer;

  const Canvas* fbCanvas = nullptr;
  input::InputDeviceBase* touchDevice = nullptr;

  int sleepCountdown = -1;
  bool visible = true;
};

LauncherState
LauncherWidget::createState() const {
  return LauncherState{};
}

} // namespace

int
main(int argc, char* argv[]) {

  std::signal(SIGCHLD, cleanup);

  runApp(LauncherWidget());
  return EXIT_SUCCESS;
}

int
old_main(int argc, char* argv[]) {
  // if (CommandSocket::alreadyRunning()) {
  //   return runCommand(argc, argv);
  // }

  // if (argc != 1) {
  //   std::cerr << "Rocket socket not running\n";
  //   return EXIT_FAILURE;
  // }

  // std::signal(SIGCHLD, cleanup);
  // std::signal(SIGINT, stop);
  // std::signal(SIGTERM, stop);

  // if (auto err = launcher.init(); err.isError()) {
  //   std::cerr << "Error init: " << err.getError().msg << std::endl;
  //   return EXIT_FAILURE;
  // }

  // if (!parseConfig()) {
  //   return EXIT_FAILURE;
  // }

  // launcher.run();

  // launcher.closeLauncher();

  return EXIT_SUCCESS;
}
