/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_SurfacePoolWayland_h
#define mozilla_layers_SurfacePoolWayland_h

#include <wayland-egl.h>

#include "mozilla/layers/SurfacePool.h"
#include "mozilla/widget/nsWaylandDisplay.h"

namespace mozilla {

namespace layers {

class NativeSurfaceWayland {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NativeSurfaceWayland);

  struct wl_surface* mWlSurface = nullptr;
  struct wl_subsurface* mWlSubsurface = nullptr;
  struct wp_viewport* mViewport = nullptr;
  struct wl_egl_window* mEglWindow = nullptr;
  EGLSurface mEglSurface = nullptr;

 private:
  friend class SurfacePoolWayland;
  NativeSurfaceWayland(){};
  ~NativeSurfaceWayland(){};
};

class SurfacePoolWayland final : public SurfacePool {
 public:
  // Get a handle for a new window. aGL can be nullptr.
  RefPtr<SurfacePoolHandle> GetHandleForGL(gl::GLContext* aGL) override;

  // Destroy all GL resources associated with aGL managed by this pool.
  void DestroyGLResourcesForContext(gl::GLContext* aGL) override{};

  RefPtr<NativeSurfaceWayland> ObtainSurfaceFromPool(const gfx::IntSize& aSize,
                                                     gl::GLContext* aGL);

 private:
  friend RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit);

  explicit SurfacePoolWayland(size_t aPoolSizeLimit);

  size_t mPoolSizeLimit;
};

// A surface pool handle that is stored on NativeLayerWayland and keeps the
// SurfacePool alive.
class SurfacePoolHandleWayland final : public SurfacePoolHandle {
 public:
  SurfacePoolHandleWayland* AsSurfacePoolHandleWayland() override {
    return this;
  }

  RefPtr<NativeSurfaceWayland> ObtainSurfaceFromPool(const gfx::IntSize& aSize);
  const auto& gl() { return mGL; }

  RefPtr<SurfacePool> Pool() override { return mPool; }
  void OnBeginFrame() override;
  void OnEndFrame() override;

 private:
  friend class SurfacePoolWayland;
  SurfacePoolHandleWayland(RefPtr<SurfacePoolWayland> aPool,
                           gl::GLContext* aGL);

  const RefPtr<SurfacePoolWayland> mPool;
  const RefPtr<gl::GLContext> mGL;
};

}  // namespace layers
}  // namespace mozilla

#endif
