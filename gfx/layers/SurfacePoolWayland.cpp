/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nullptr; c-basic-offset: 2
 * -*- This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/SurfacePoolWayland.h"

#include "GLContextEGL.h"

namespace mozilla {
namespace layers {

using gl::GLContext;
using gl::GLContextEGL;

/* static */ RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit) {
  return new SurfacePoolWayland(aPoolSizeLimit);
}

SurfacePoolWayland::SurfacePoolWayland(size_t aPoolSizeLimit)
    : mPoolSizeLimit(aPoolSizeLimit) {}

RefPtr<SurfacePoolHandle> SurfacePoolWayland::GetHandleForGL(GLContext* aGL) {
  return new SurfacePoolHandleWayland(this, aGL);
}

RefPtr<NativeSurfaceWayland> SurfacePoolWayland::ObtainSurfaceFromPool(
    const gfx::IntSize& aSize, GLContext* aGL) {
  RefPtr<NativeSurfaceWayland> surface = new NativeSurfaceWayland();

  wl_compositor* compositor = widget::WaylandDisplayGet()->GetCompositor();
  surface->mWlSurface = wl_compositor_create_surface(compositor);

  wl_region* region = wl_compositor_create_region(compositor);
  wl_surface_set_input_region(surface->mWlSurface, region);
  wl_region_destroy(region);

  wp_viewporter* viewporter = widget::WaylandDisplayGet()->GetViewporter();
  MOZ_ASSERT(viewporter);
  surface->mViewport =
      wp_viewporter_get_viewport(viewporter, surface->mWlSurface);

  surface->mEglWindow =
      wl_egl_window_create(surface->mWlSurface, aSize.width, aSize.height);

  GLContextEGL* egl = GLContextEGL::Cast(aGL);
  surface->mEglSurface =
      egl->mEgl->fCreateWindowSurface(egl->mConfig, surface->mEglWindow, NULL);
  MOZ_ASSERT(surface->mEglSurface != EGL_NO_SURFACE);

  return surface;
}

SurfacePoolHandleWayland::SurfacePoolHandleWayland(
    RefPtr<SurfacePoolWayland> aPool, GLContext* aGL)
    : mPool(aPool), mGL(aGL) {}

void SurfacePoolHandleWayland::OnBeginFrame() {}

void SurfacePoolHandleWayland::OnEndFrame() {}

RefPtr<NativeSurfaceWayland> SurfacePoolHandleWayland::ObtainSurfaceFromPool(
    const gfx::IntSize& aSize) {
  return mPool->ObtainSurfaceFromPool(aSize, mGL);
}

}  // namespace layers
}  // namespace mozilla
