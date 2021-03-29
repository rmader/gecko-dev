/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/NativeLayerWayland.h"

#include <utility>
#include <algorithm>

#include "gfxUtils.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "MozFramebuffer.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/layers/ScreenshotGrabber.h"
#include "mozilla/layers/SurfacePoolWayland.h"
#include "mozilla/webrender/RenderThread.h"
#include "ScopedGLHelpers.h"

namespace mozilla {
namespace layers {

using gfx::DataSourceSurface;
using gfx::IntPoint;
using gfx::IntRect;
using gfx::IntRegion;
using gfx::IntSize;
using gfx::Matrix4x4;
using gfx::SurfaceFormat;
using gl::GLContext;
using gl::GLContextEGL;

/* static */
already_AddRefed<NativeLayerRootWayland>
NativeLayerRootWayland::CreateForMozContainer(MozContainer* aContainer) {
  RefPtr<NativeLayerRootWayland> layerRoot =
      new NativeLayerRootWayland(aContainer);
  return layerRoot.forget();
}

NativeLayerRootWayland::NativeLayerRootWayland(MozContainer* aContainer)
    : mMutex("NativeLayerRootWayland"), mContainer(aContainer) {
  EGLNativeWindowType eglWindow =
      moz_container_wayland_get_egl_window(mContainer, 1);
  if (eglWindow) {
    GLContextEGL* gl =
        GLContextEGL::Cast(wr::RenderThread::Get()->SingletonGL());
    auto egl = gl->mEgl;
    MOZ_ASSERT(egl);

    mEglSurface = egl->fCreateWindowSurface(gl->mConfig, eglWindow, 0);
    MOZ_ASSERT(mEglSurface != EGL_NO_SURFACE);

    gl->SetEGLSurfaceOverride(mEglSurface);
    gl->MakeCurrent();

    gl->fClearColor(0.0, 0.0, 0.0, 0.0);
    gl->fClear(LOCAL_GL_COLOR_BUFFER_BIT);

    gl->SetEGLSurfaceOverride(nullptr);
    gl->MakeCurrent();
  }
}

NativeLayerRootWayland::~NativeLayerRootWayland() {}

already_AddRefed<NativeLayer> NativeLayerRootWayland::CreateLayer(
    const IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandle* aSurfacePoolHandle) {
  RefPtr<NativeLayer> layer = new NativeLayerWayland(
      aSize, aIsOpaque, aSurfacePoolHandle->AsSurfacePoolHandleWayland());
  return layer.forget();
}

already_AddRefed<NativeLayer>
NativeLayerRootWayland::CreateLayerForExternalTexture(bool aIsOpaque) {
  RefPtr<NativeLayer> layer = new NativeLayerWayland(aIsOpaque);
  return layer.forget();
}

void NativeLayerRootWayland::AppendLayer(NativeLayer* aLayer) {
  MOZ_ASSERT(false);
  MutexAutoLock lock(mMutex);

  RefPtr<NativeLayerWayland> layerWayland = aLayer->AsNativeLayerWayland();
  MOZ_RELEASE_ASSERT(layerWayland);

  mSublayers.AppendElement(layerWayland);
  layerWayland->SetBackingScale(mBackingScale);
}

void NativeLayerRootWayland::RemoveLayer(NativeLayer* aLayer) {
  MOZ_ASSERT(false);
  MutexAutoLock lock(mMutex);

  RefPtr<NativeLayerWayland> layerWayland = aLayer->AsNativeLayerWayland();
  MOZ_RELEASE_ASSERT(layerWayland);

  mSublayers.RemoveElement(layerWayland);
}

void NativeLayerRootWayland::ShowLayer(RefPtr<NativeLayerWayland> aLayer) {
  if (aLayer->mIsShown) {
    return;
  }

  RefPtr<NativeSurfaceWayland> nativeSurface = aLayer->mNativeSurface;
  if (!nativeSurface->mWlSubsurface) {
    wl_surface* wlSurface = moz_container_wayland_surface_lock(mContainer);
    wl_subcompositor* subcompositor =
        widget::WaylandDisplayGet()->GetSubcompositor();

    nativeSurface->mWlSubsurface = wl_subcompositor_get_subsurface(
        subcompositor, nativeSurface->mWlSurface, wlSurface);

    moz_container_wayland_surface_unlock(mContainer, &wlSurface);
  }
}

void NativeLayerRootWayland::HideLayer(RefPtr<NativeLayerWayland> aLayer) {
  if (!aLayer->mIsShown) {
    return;
  }

  RefPtr<NativeSurfaceWayland> nativeSurface = aLayer->mNativeSurface;

  wl_subsurface_set_position(nativeSurface->mWlSubsurface, 0, 0);
  wp_viewport_set_source(nativeSurface->mViewport, wl_fixed_from_int(0),
                         wl_fixed_from_int(0), wl_fixed_from_int(1),
                         wl_fixed_from_int(1));
  wl_surface_commit(nativeSurface->mWlSurface);
  wl_surface* wlSurface = moz_container_wayland_surface_lock(mContainer);
  wl_subsurface_place_below(nativeSurface->mWlSubsurface, wlSurface);
  moz_container_wayland_surface_unlock(mContainer, &wlSurface);
}

void NativeLayerRootWayland::SetLayers(
    const nsTArray<RefPtr<NativeLayer>>& aLayers) {
  MutexAutoLock lock(mMutex);

  // Ideally, we'd just be able to do mSublayers = std::move(aLayers).
  // However, aLayers has a different type: it carries NativeLayer objects,
  // whereas mSublayers carries NativeLayerWayland objects, so we have to
  // downcast all the elements first. There's one other reason to look at all
  // the elements in aLayers first: We need to make sure any new layers know
  // about our current backing scale.

  nsTArray<RefPtr<NativeLayerWayland>> newSublayers(aLayers.Length());
  for (auto& layer : aLayers) {
    RefPtr<NativeLayerWayland> layerWayland = layer->AsNativeLayerWayland();
    MOZ_RELEASE_ASSERT(layerWayland);
    layerWayland->SetBackingScale(mBackingScale);
    newSublayers.AppendElement(std::move(layerWayland));
  }

  if (newSublayers != mSublayers) {
    for (RefPtr<NativeLayerWayland> layer : mSublayers) {
      if (!newSublayers.Contains(layer)) {
        HideLayer(layer);
      }
    }
    mSublayers = std::move(newSublayers);
  }

  for (RefPtr<NativeLayerWayland> layer : mSublayers) {
    RefPtr<NativeSurfaceWayland> nativeSurface = layer->mNativeSurface;

    float clipX, clipY, clipW, clipH;
    if (layer->mClipRect) {
      clipX = layer->mClipRect.value().x;
      clipY = layer->mClipRect.value().y;
      clipW = layer->mClipRect.value().width;
      clipH = layer->mClipRect.value().height;
    } else {
      clipX = clipY = clipW = clipH = 0.f;
    }

    Point transform = layer->mTransform.TransformPoint(Point(0, 0));

    float posX = std::max(layer->mPosition.x + transform.x, clipX);
    float posY = std::max(layer->mPosition.y + transform.y, clipY);

    float viewX = std::max((clipX - transform.x) - layer->mPosition.x, 0.f);
    float viewY = std::max((clipY - transform.y) - layer->mPosition.y, 0.f);

    float viewW = std::min(layer->mSize.width - viewX, (clipX + clipW) - posX);
    float viewH = std::min(layer->mSize.height - viewY, (clipY + clipH) - posY);

    if (viewW > 0 && viewH > 0) {
      ShowLayer(layer);
      wp_viewport_set_source(
          nativeSurface->mViewport, wl_fixed_from_double(viewX),
          wl_fixed_from_double(viewY), wl_fixed_from_double(viewW),
          wl_fixed_from_double(viewH));

      wl_subsurface_set_position(nativeSurface->mWlSubsurface,
                                 int(roundf(posX)), int(roundf(posY)));
    } else {
      HideLayer(layer);
    }
  }

  wl_surface* previousSurface = nullptr;
  for (RefPtr<NativeLayerWayland> layer : mSublayers) {
    if (!layer->mIsShown) {
      continue;
    }

    RefPtr<NativeSurfaceWayland> nativeSurface = layer->mNativeSurface;
    if (previousSurface) {
      wl_subsurface_place_above(nativeSurface->mWlSubsurface, previousSurface);
      previousSurface = nativeSurface->mWlSurface;
    } else {
      wl_surface* wlSurface = moz_container_wayland_surface_lock(mContainer);
      wl_subsurface_place_above(nativeSurface->mWlSubsurface, wlSurface);
      moz_container_wayland_surface_unlock(mContainer, &wlSurface);
      previousSurface = nativeSurface->mWlSurface;
    }

    if (layer->mIsOpaque) {
      wl_compositor* compositor = widget::WaylandDisplayGet()->GetCompositor();

      struct wl_region* region = wl_compositor_create_region(compositor);
      wl_region_add(region, 0, 0, INT32_MAX, INT32_MAX);
      wl_surface_set_opaque_region(layer->mNativeSurface->mWlSurface, region);
      wl_region_destroy(region);
    }
  }
}

void NativeLayerRootWayland::SetBackingScale(float aBackingScale) {
  MutexAutoLock lock(mMutex);

  mBackingScale = aBackingScale;
  for (auto layer : mSublayers) {
    layer->SetBackingScale(aBackingScale);
  }
}

float NativeLayerRootWayland::BackingScale() {
  MutexAutoLock lock(mMutex);
  return mBackingScale;
}

bool NativeLayerRootWayland::CommitToScreen() {
  MutexAutoLock lock(mMutex);

  wl_surface* wlSurface = moz_container_wayland_surface_lock(mContainer);
  for (RefPtr<NativeLayerWayland> layer : mSublayers) {
    RefPtr<NativeSurfaceWayland> nativeSurface = layer->mNativeSurface;
    if (layer->mHasDamage) {
      GLContextEGL* gl = GLContextEGL::Cast(layer->mSurfacePoolHandle->gl());
      auto egl = gl->mEgl;

      gl->SetEGLSurfaceOverride(nativeSurface->mEglSurface);
      gl->MakeCurrent();

      egl->fSwapInterval(0);
      egl->fSwapBuffers(nativeSurface->mEglSurface);

      gl->SetEGLSurfaceOverride(nullptr);
      gl->MakeCurrent();

      layer->mHasDamage = false;
    } else {
      wl_surface_commit(nativeSurface->mWlSurface);
    }
  }

  if (!mInitializedBuffer) {
    GLContextEGL* gl =
        GLContextEGL::Cast(wr::RenderThread::Get()->SingletonGL());
    auto egl = gl->mEgl;

    gl->SetEGLSurfaceOverride(mEglSurface);
    gl->MakeCurrent();

    egl->fSwapInterval(0);
    egl->fSwapBuffers(mEglSurface);

    gl->SetEGLSurfaceOverride(nullptr);
    gl->MakeCurrent();

    mInitializedBuffer = true;
  } else {
    wl_surface_commit(wlSurface);
  }

  moz_container_wayland_surface_unlock(mContainer, &wlSurface);

  return true;
}

UniquePtr<NativeLayerRootSnapshotter>
NativeLayerRootWayland::CreateSnapshotter() {
  MutexAutoLock lock(mMutex);
  return nullptr;
}

NativeLayerWayland::NativeLayerWayland(
    const IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandleWayland* aSurfacePoolHandle)
    : mMutex("NativeLayerWayland"),
      mSurfacePoolHandle(aSurfacePoolHandle),
      mSize(aSize),
      mIsOpaque(aIsOpaque),
      mNativeSurface(nullptr) {
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle,
                     "Need a non-null surface pool handle.");
}

NativeLayerWayland::NativeLayerWayland(bool aIsOpaque)
    : mMutex("NativeLayerWayland"),
      mSurfacePoolHandle(nullptr),
      mIsOpaque(aIsOpaque) {
  MOZ_ASSERT(false);  // external image
}

NativeLayerWayland::~NativeLayerWayland() {}

void NativeLayerWayland::AttachExternalImage(
    wr::RenderTextureHost* aExternalImage) {
  MOZ_ASSERT(false);
}

void NativeLayerWayland::SetSurfaceIsFlipped(bool aIsFlipped) {
  MutexAutoLock lock(mMutex);

  if (aIsFlipped != mSurfaceIsFlipped) {
    mSurfaceIsFlipped = aIsFlipped;
  }
}

bool NativeLayerWayland::SurfaceIsFlipped() {
  MutexAutoLock lock(mMutex);

  return mSurfaceIsFlipped;
}

IntSize NativeLayerWayland::GetSize() {
  MutexAutoLock lock(mMutex);
  return mSize;
}

void NativeLayerWayland::SetPosition(const IntPoint& aPosition) {
  MutexAutoLock lock(mMutex);

  if (aPosition != mPosition) {
    mPosition = aPosition;
  }
}

IntPoint NativeLayerWayland::GetPosition() {
  MutexAutoLock lock(mMutex);
  return mPosition;
}

void NativeLayerWayland::SetTransform(const Matrix4x4& aTransform) {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(aTransform.IsRectilinear());

  if (aTransform != mTransform) {
    mTransform = aTransform;
  }
}

void NativeLayerWayland::SetSamplingFilter(
    gfx::SamplingFilter aSamplingFilter) {
  MutexAutoLock lock(mMutex);

  if (aSamplingFilter != mSamplingFilter) {
    mSamplingFilter = aSamplingFilter;
  }
}

Matrix4x4 NativeLayerWayland::GetTransform() {
  MutexAutoLock lock(mMutex);
  return mTransform;
}

IntRect NativeLayerWayland::GetRect() {
  MutexAutoLock lock(mMutex);
  return IntRect(mPosition, mSize);
}

void NativeLayerWayland::SetBackingScale(float aBackingScale) {
  MutexAutoLock lock(mMutex);

  if (aBackingScale != mBackingScale) {
    mBackingScale = aBackingScale;
  }
}

bool NativeLayerWayland::IsOpaque() {
  MutexAutoLock lock(mMutex);
  return mIsOpaque;
}

void NativeLayerWayland::SetClipRect(const Maybe<gfx::IntRect>& aClipRect) {
  MutexAutoLock lock(mMutex);

  if (aClipRect != mClipRect) {
    mClipRect = aClipRect;
  }
}

Maybe<gfx::IntRect> NativeLayerWayland::ClipRect() {
  MutexAutoLock lock(mMutex);
  return mClipRect;
}

gfx::IntRect NativeLayerWayland::CurrentSurfaceDisplayRect() {
  MutexAutoLock lock(mMutex);
  return mDisplayRect;
}

RefPtr<gfx::DrawTarget> NativeLayerWayland::NextSurfaceAsDrawTarget(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    gfx::BackendType aBackendType) {
  MOZ_ASSERT(false);
  return nullptr;
}

Maybe<GLuint> NativeLayerWayland::NextSurfaceAsFramebuffer(
    const IntRect& aDisplayRect, const IntRegion& aUpdateRegion,
    bool aNeedsDepth) {
  MutexAutoLock lock(mMutex);

  MOZ_ASSERT(!mSize.IsEmpty());
  if (!mNativeSurface) {
    mNativeSurface = mSurfacePoolHandle->ObtainSurfaceFromPool(mSize);
  }
  GLContextEGL* gl = GLContextEGL::Cast(mSurfacePoolHandle->gl());

  gl->SetEGLSurfaceOverride(mNativeSurface->mEglSurface);
  gl->MakeCurrent();

  mHasDamage = true;

  return Some(0);
}

void NativeLayerWayland::NotifySurfaceReady() {
  MutexAutoLock lock(mMutex);

  GLContextEGL* gl = GLContextEGL::Cast(mSurfacePoolHandle->gl());
  gl->SetEGLSurfaceOverride(nullptr);
  gl->MakeCurrent();
}

void NativeLayerWayland::DiscardBackbuffers() {
  /*
  MutexAutoLock lock(mMutex);

  for (const auto& surf : mSurfaces) {
    mSurfacePoolHandle->ReturnSurfaceToPool(surf.mEntry.mSurface);
  }
  mSurfaces.clear();
  */
}

}  // namespace layers
}  // namespace mozilla
