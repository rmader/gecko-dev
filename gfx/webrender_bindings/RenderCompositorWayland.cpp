/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositorWayland.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/StackArray.h"
#include "mozilla/layers/NativeLayer.h"
#include "mozilla/layers/SurfacePool.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/CompositorWidget.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/Telemetry.h"
#include "nsPrintfCString.h"

namespace mozilla {
namespace wr {

/* static */
UniquePtr<RenderCompositor> RenderCompositorWayland::Create(
    RefPtr<widget::CompositorWidget> aWidget, nsACString& aError) {
  if (!widget::GdkIsWaylandDisplay()) {
    return nullptr;
  }

  const auto& gl = RenderThread::Get()->SingletonGL(aError);
  if (!gl) {
    if (aError.IsEmpty()) {
      aError.Assign("RcWayland(no shared GL)"_ns);
    } else {
      aError.Append("(Create)"_ns);
    }
    return nullptr;
  }

  UniquePtr<RenderCompositorWayland> compositor =
      MakeUnique<RenderCompositorWayland>(aWidget, gl);
  return compositor;
}

RenderCompositorWayland::RenderCompositorWayland(
    RefPtr<widget::CompositorWidget> aWidget, gl::GLContext* aGL)
    : RenderCompositor(std::move(aWidget)),
      mEGLConfig(nullptr),
      mEGLSurface(nullptr),
      mNativeLayerRoot(GetWidget()->GetNativeLayerRoot()),
      mSurfacePoolHandle(nullptr) {
  auto pool = RenderThread::Get()->SharedSurfacePool();
  if (pool) {
    mSurfacePoolHandle = pool->GetHandleForGL(aGL);
  }
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle);
}

RenderCompositorWayland::~RenderCompositorWayland() {
  DestroyEGLSurface();
  MOZ_ASSERT(!mEGLSurface);
}

bool RenderCompositorWayland::ShutdownEGLLibraryIfNecessary(
    nsACString& aError) {
  MOZ_ASSERT(false);

  return true;
}

bool RenderCompositorWayland::BeginFrame() { return true; }

RenderedFrameId RenderCompositorWayland::EndFrame(
    const nsTArray<DeviceIntRect>& aDirtyRects) {
  RenderedFrameId frameId = GetNextRenderFrameId();

  return frameId;
}

bool RenderCompositorWayland::WaitForGPU() { return false; }

bool RenderCompositorWayland::ResizeBufferIfNeeded() {
  MOZ_ASSERT(false);

  return true;
}

bool RenderCompositorWayland::CreateEGLSurface() {
  MOZ_ASSERT(false);
  MOZ_ASSERT(mEGLSurface == EGL_NO_SURFACE);

  return true;
}

void RenderCompositorWayland::DestroyEGLSurface() {}

void RenderCompositorWayland::Pause() { MOZ_ASSERT(false); }

bool RenderCompositorWayland::Resume() { return true; }

void RenderCompositorWayland::Update() {
  // Update compositor window's size if it exists.
  // It needs to be called here, since OS might update compositor
  // window's size at unexpected timing.
  // mWidget->AsWindows()->UpdateCompositorWndSizeIfNecessary();
}

bool RenderCompositorWayland::MakeCurrent() {
  // MOZ_ASSERT(false);
  // gl::GLContextEGL::Cast(gl())->SetEGLSurfaceOverride(mEGLSurface);
  return gl()->MakeCurrent();
}

LayoutDeviceIntSize RenderCompositorWayland::GetBufferSize() {
  return mWidget->GetClientSize();
}

GLenum RenderCompositorWayland::IsContextLost(bool aForce) {
  return LOCAL_GL_NO_ERROR;
}

bool RenderCompositorWayland::UseCompositor() { return true; }

bool RenderCompositorWayland::SupportAsyncScreenshot() {
  MOZ_ASSERT(false);
  return !UseCompositor();
}

bool RenderCompositorWayland::ShouldUseNativeCompositor() {
  return UseCompositor();
}

uint32_t RenderCompositorWayland::GetMaxUpdateRects() {
  if (UseCompositor() &&
      StaticPrefs::gfx_webrender_compositor_max_update_rects_AtStartup() > 0) {
    return 1;
  }
  return 0;
}

void RenderCompositorWayland::CompositorBeginFrame() {
  mAddedLayers.Clear();
  mSurfacePoolHandle->OnBeginFrame();
}

void RenderCompositorWayland::CompositorEndFrame() {
  mNativeLayerRoot->SetLayers(mAddedLayers);
  mNativeLayerRoot->CommitToScreen();
  mSurfacePoolHandle->OnEndFrame();
}

void RenderCompositorWayland::BindNativeLayer(wr::NativeTileId aId,
                                              const gfx::IntRect& aDirtyRect) {
  MOZ_RELEASE_ASSERT(!mCurrentlyBoundNativeLayer);

  auto surfaceCursor = mSurfaces.find(aId.surface_id);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  Surface& surface = surfaceCursor->second;

  auto layerCursor = surface.mNativeLayers.find(TileKey(aId.x, aId.y));
  MOZ_RELEASE_ASSERT(layerCursor != surface.mNativeLayers.end());
  RefPtr<layers::NativeLayer> layer = layerCursor->second;

  mCurrentlyBoundNativeLayer = layer;
}

void RenderCompositorWayland::UnbindNativeLayer() {
  MOZ_RELEASE_ASSERT(mCurrentlyBoundNativeLayer);

  mCurrentlyBoundNativeLayer->NotifySurfaceReady();
  mCurrentlyBoundNativeLayer = nullptr;
}

void RenderCompositorWayland::Bind(wr::NativeTileId aId,
                                   wr::DeviceIntPoint* aOffset,
                                   uint32_t* aFboId,
                                   wr::DeviceIntRect aDirtyRect,
                                   wr::DeviceIntRect aValidRect) {
  gfx::IntRect validRect(aValidRect.origin.x, aValidRect.origin.y,
                         aValidRect.size.width, aValidRect.size.height);
  gfx::IntRect dirtyRect(aDirtyRect.origin.x, aDirtyRect.origin.y,
                         aDirtyRect.size.width, aDirtyRect.size.height);

  BindNativeLayer(aId, dirtyRect);

  Maybe<GLuint> fbo = mCurrentlyBoundNativeLayer->NextSurfaceAsFramebuffer(
      validRect, dirtyRect, true);

  *aFboId = *fbo;
  *aOffset = wr::DeviceIntPoint{0, 0};
}

void RenderCompositorWayland::Unbind() { UnbindNativeLayer(); }

void RenderCompositorWayland::CreateSurface(wr::NativeSurfaceId aId,
                                            wr::DeviceIntPoint aVirtualOffset,
                                            wr::DeviceIntSize aTileSize,
                                            bool aIsOpaque) {
  MOZ_RELEASE_ASSERT(mSurfaces.find(aId) == mSurfaces.end());

  Surface surface;
  surface.mId = aId;
  surface.mTileSize = aTileSize;
  surface.mIsExternal = false;
  surface.mIsOpaque = aIsOpaque;

  mSurfaces.insert({aId, surface});
}

void RenderCompositorWayland::CreateExternalSurface(wr::NativeSurfaceId aId,
                                                    bool aIsOpaque) {
  // mDCLayerTree->CreateExternalSurface(aId, aIsOpaque);
  MOZ_ASSERT(false);
}

void RenderCompositorWayland::DestroySurface(NativeSurfaceId aId) {
  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());

  /*Surface& surface = surfaceCursor->second;
  if (!surface.mIsExternal) {
    for (const auto& iter : surface.mNativeLayers) {
      mTotalTilePixelCount -= gfx::IntRect({}, iter.second->GetSize()).Area();
    }
  }*/

  mSurfaces.erase(surfaceCursor);
}

void RenderCompositorWayland::CreateTile(wr::NativeSurfaceId aId, int aX,
                                         int aY) {
  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  Surface& surface = surfaceCursor->second;
  MOZ_RELEASE_ASSERT(!surface.mIsExternal);

  RefPtr<layers::NativeLayer> layer = mNativeLayerRoot->CreateLayer(
      surface.TileSize(), surface.mIsOpaque, mSurfacePoolHandle);

  surface.mNativeLayers.insert({TileKey(aX, aY), layer});
}

void RenderCompositorWayland::DestroyTile(wr::NativeSurfaceId aId, int aX,
                                          int aY) {
  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  Surface& surface = surfaceCursor->second;
  MOZ_RELEASE_ASSERT(!surface.mIsExternal);

  auto layerCursor = surface.mNativeLayers.find(TileKey(aX, aY));
  MOZ_RELEASE_ASSERT(layerCursor != surface.mNativeLayers.end());
  RefPtr<layers::NativeLayer> layer = std::move(layerCursor->second);
  surface.mNativeLayers.erase(layerCursor);

  // If the layer is currently present in mNativeLayerRoot, it will be destroyed
  // once CompositorEndFrame() replaces mNativeLayerRoot's layers and drops that
  // reference. So until that happens, the layer still needs to hold on to its
  // front buffer. However, we can tell it to drop its back buffers now, because
  // we know that we will never draw to it again.
  // Dropping the back buffers now puts them back in the surface pool, so those
  // surfaces can be immediately re-used for drawing in other layers in the
  // current frame.
  layer->DiscardBackbuffers();
}

void RenderCompositorWayland::AttachExternalImage(
    wr::NativeSurfaceId aId, wr::ExternalImageId aExternalImage) {
  MOZ_ASSERT(false);
  // mDCLayerTree->AttachExternalImage(aId, aExternalImage);
}

gfx::SamplingFilter ToSamplingFilter(wr::ImageRendering aImageRendering) {
  if (aImageRendering == wr::ImageRendering::Auto) {
    return gfx::SamplingFilter::LINEAR;
  }
  return gfx::SamplingFilter::POINT;
}

void RenderCompositorWayland::AddSurface(
    wr::NativeSurfaceId aId, const wr::CompositorSurfaceTransform& aTransform,
    wr::DeviceIntRect aClipRect, wr::ImageRendering aImageRendering) {
  MOZ_RELEASE_ASSERT(!mCurrentlyBoundNativeLayer);

  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  const Surface& surface = surfaceCursor->second;

  gfx::Matrix4x4 transform(
      aTransform.m11, aTransform.m12, aTransform.m13, aTransform.m14,
      aTransform.m21, aTransform.m22, aTransform.m23, aTransform.m24,
      aTransform.m31, aTransform.m32, aTransform.m33, aTransform.m34,
      aTransform.m41, aTransform.m42, aTransform.m43, aTransform.m44);

  for (auto it = surface.mNativeLayers.begin();
       it != surface.mNativeLayers.end(); ++it) {
    RefPtr<layers::NativeLayer> layer = it->second;
    gfx::IntPoint layerPosition(surface.mTileSize.width * it->first.mX,
                                surface.mTileSize.height * it->first.mY);
    layer->SetPosition(layerPosition);
    gfx::IntRect clipRect(aClipRect.origin.x, aClipRect.origin.y,
                          aClipRect.size.width, aClipRect.size.height);
    layer->SetClipRect(Some(clipRect));
    layer->SetTransform(transform);
    layer->SetSamplingFilter(ToSamplingFilter(aImageRendering));
    mAddedLayers.AppendElement(layer);
  }
}

void RenderCompositorWayland::EnableNativeCompositor(bool aEnable) {
  MOZ_ASSERT(false);
}

}  // namespace wr
}  // namespace mozilla
