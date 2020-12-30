#pragma once

#include <optional>

#include "Canvas.h"

namespace rmlib::fb {

enum class Waveform { DU = 1, GC16 = 2, GC16Fast = 3 };

enum UpdateFlags { None = 0, Sync = 1, FullRefresh = 2, Unknown = 4 };

struct FrameBuffer {
  enum Type { rM1, Shim, rM2fb, Swtcon };

  /// Opens the framebuffer.
  static std::optional<FrameBuffer> open();

  FrameBuffer(FrameBuffer&& other)
    : type(other.type), fd(other.fd), canvas(other.canvas) {
    other.fd = -1;
    other.canvas.memory = nullptr;
  }

  FrameBuffer(const FrameBuffer&) = delete;
  FrameBuffer& operator=(const FrameBuffer&) = delete;
  FrameBuffer& operator=(FrameBuffer&& other) {
    close();
    std::swap(other, *this);
    return *this;
  }

  /// Closes the framebuffer and unmaps the memory.
  ~FrameBuffer();

  void doUpdate(Rect region, Waveform waveform, UpdateFlags flags);

  Type type;
  int fd = -1;
  Canvas canvas;

private:
  FrameBuffer(Type type, int fd, Canvas canvas)
    : type(type), fd(fd), canvas(std::move(canvas)) {}

  void close();
};

} // namespace rmlib::fb