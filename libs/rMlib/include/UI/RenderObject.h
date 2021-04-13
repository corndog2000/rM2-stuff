#pragma once

#include <Input.h>

#include <UI/Util.h>

namespace rmlib {

class AppContext;

class RenderObject {
  static int roCount;

public:
  RenderObject() : mID(roCount++) {
#ifndef NDEBUG
    std::cout << "alloc RO: " << mID << "\n";
#endif
  }
  // RenderObject(RenderContext& context) : context(context) {}
  virtual ~RenderObject() {
#ifndef NDEBUG
    std::cout << "free RO: " << mID << "\n";
#endif
    roCount--;
  }

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

  virtual UpdateRegion cleanup(rmlib::Canvas& canvas) {
    if (isFullDraw()) {
      canvas.set(this->rect, rmlib::white);
      return UpdateRegion{ this->rect, rmlib::fb::Waveform::DU };
    }
    return {};
  }

  UpdateRegion draw(rmlib::Rect rect, rmlib::Canvas& canvas) {
    auto result = UpdateRegion{};

    // TODO: do we need to distinguish when cleanup is used?
    if (needsDraw()) {
      this->rect = rect;
      result |= doDraw(rect, canvas);

      mNeedsDraw = No;
    }

    return result;
  }

  virtual void handleInput(const rmlib::input::Event& ev) {}

  virtual void rebuild(AppContext& context) {
    if (mNeedsRebuild) {
      doRebuild(context);
      mNeedsRebuild = false;
    }
  }

  bool needsDraw() {
    return needsDrawCache.getOrSetTo([this] { return getNeedsDraw(); });
  }

  bool needsLayout() {
    return needsLayoutCache.getOrSetTo([this] { return getNeedsLayout(); });
  }

  const rmlib::Rect& getRect() const { return rect; }

  const Size& getSize() const { return lastSize; }

  virtual void markNeedsLayout() { mNeedsLayout = true; }
  virtual void markNeedsDraw(bool full = true) {
    mNeedsDraw = full ? Full : (mNeedsDraw == No ? Partial : mNeedsDraw);
  }
  void markNeedsRebuild() { mNeedsRebuild = true; }

  virtual void reset() {
    needsLayoutCache.reset();
    needsDrawCache.reset();
  }

protected:
  virtual Size doLayout(const Constraints& Constraints) = 0;
  virtual UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) = 0;
  virtual void doRebuild(AppContext& context) {}

  virtual bool getNeedsDraw() const { return mNeedsDraw != No; }
  virtual bool getNeedsLayout() const { return mNeedsLayout; }

  bool isPartialDraw() const { return mNeedsDraw == Partial; }
  bool isFullDraw() const { return mNeedsDraw == Full; }

private:
  int mID;
  rmlib::Rect rect = { { 0, 0 }, { 0, 0 } };

  Size lastSize = { 0, 0 };

  // TODO: are both needed?
  CachedBool needsLayoutCache;
  bool mNeedsLayout = true;

  CachedBool needsDrawCache;
  // bool mNeedsDraw = true;
  enum { No, Full, Partial } mNeedsDraw = Full;

  bool mNeedsRebuild = false;
  // RenderContext& context;
};

int RenderObject::roCount = 0;

class SingleChildRenderObject : public RenderObject {
public:
  SingleChildRenderObject() = default;
  SingleChildRenderObject(std::unique_ptr<RenderObject> child)
    : child(std::move(child)) {}

  void handleInput(const rmlib::input::Event& ev) override {
    child->handleInput(ev);
  }

  UpdateRegion cleanup(rmlib::Canvas& canvas) override {
    if (isFullDraw()) {
      return RenderObject::cleanup(canvas);
    }
    return child->cleanup(canvas);
  }

  void markNeedsLayout() override {
    RenderObject::markNeedsLayout();
    if (child) {
      child->markNeedsLayout();
    }
  }

  void markNeedsDraw(bool full = true) override {
    RenderObject::markNeedsDraw(full);
    if (child) {
      child->markNeedsDraw(full);
    }
  }

  void rebuild(AppContext& context) final {
    RenderObject::rebuild(context);
    child->rebuild(context);
  }

  void reset() override {
    RenderObject::reset();
    child->reset();
  }

protected:
  Size doLayout(const Constraints& constraints) override {
    return child->layout(constraints);
  }

  UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
    return child->draw(rect, canvas);
  }

  bool getNeedsDraw() const override {
    return RenderObject::getNeedsDraw() || child->needsDraw();
  }

  bool getNeedsLayout() const override {
    return RenderObject::getNeedsLayout() || child->needsLayout();
  }

  std::unique_ptr<RenderObject> child;
};

class MultiChildRenderObject : public RenderObject {
public:
  MultiChildRenderObject(std::vector<std::unique_ptr<RenderObject>> children)
    : children(std::move(children)) {}

  void handleInput(const rmlib::input::Event& ev) override {
    for (const auto& child : children) {
      child->handleInput(ev);
    }
  }

  UpdateRegion cleanup(rmlib::Canvas& canvas) override {
    if (isFullDraw()) {
      return RenderObject::cleanup(canvas);
    }

    auto result = UpdateRegion{};
    for (const auto& child : children) {
      result |= child->cleanup(canvas);
    }
    return result;
  }

  void markNeedsLayout() override {
    RenderObject::markNeedsLayout();
    for (auto& child : children) {
      child->markNeedsLayout();
    }
  }

  void markNeedsDraw(bool full = true) override {
    RenderObject::markNeedsDraw(full);
    for (auto& child : children) {
      child->markNeedsDraw(full);
    }
  }

  void rebuild(AppContext& context) final {
    RenderObject::rebuild(context);
    for (const auto& child : children) {
      child->rebuild(context);
    }
  }

  void reset() override {
    RenderObject::reset();
    for (const auto& child : children) {
      child->reset();
    }
  }

protected:
  bool getNeedsDraw() const override {
    return RenderObject::getNeedsDraw() ||
           std::any_of(children.begin(), children.end(), [](const auto& child) {
             return child->needsDraw();
           });
  }

  bool getNeedsLayout() const override {
    return RenderObject::getNeedsLayout() ||
           std::any_of(children.begin(), children.end(), [](const auto& child) {
             return child->needsLayout();
           });
  }

  std::vector<std::unique_ptr<RenderObject>> children;
};

} // namespace rmlib
