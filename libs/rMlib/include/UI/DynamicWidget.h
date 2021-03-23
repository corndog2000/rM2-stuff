#pragma once

#include <UI/RenderObject.h>

namespace rmlib {
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

class DynamicWidget {
private:
  struct DynamicRenderObject : public SingleChildRenderObject {
    DynamicRenderObject(std::unique_ptr<RenderObject> ro,
                        details::type_id_t typeID)
      : SingleChildRenderObject(std::move(ro)), typeID(typeID) {}

    details::type_id_t typeID;
    rmlib::Rect lastRect;
    bool justChanged = false;

    RenderObject& getChild() { return *child; }

    void setChild(std::unique_ptr<RenderObject> ro, details::type_id_t typeID) {
      child = std::move(ro);
      this->typeID = typeID;
      justChanged = true;
    }

    UpdateRegion cleanup(rmlib::Canvas& canvas) override {
      if (justChanged) {
        justChanged = false;
        canvas.set(lastRect, rmlib::white);
        return UpdateRegion{ lastRect, rmlib::fb::Waveform::DU };
      }

      return SingleChildRenderObject::cleanup(canvas);
    }

    UpdateRegion doDraw(rmlib::Rect rect, rmlib::Canvas& canvas) override {
      if (child->RenderObject::needsDraw()) {
        lastRect = rect;
      }

      return child->draw(rect, canvas);
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

} // namespace rmlib
