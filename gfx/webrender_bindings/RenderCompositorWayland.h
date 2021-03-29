/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RENDERCOMPOSITOR_WAYLAND_H
#define MOZILLA_GFX_RENDERCOMPOSITOR_WAYLAND_H

#include <queue>

#include "GLTypes.h"
#include "mozilla/Maybe.h"
#include "mozilla/webrender/RenderCompositor.h"
#include "mozilla/webrender/RenderThread.h"

namespace mozilla {
namespace gl {
class GLLibraryEGL;
}  // namespace gl

namespace layers {
class NativeLayerRoot;
class NativeLayer;
class SurfacePoolHandle;
}  // namespace layers

namespace wr {

class RenderCompositorWayland : public RenderCompositor {
 public:
  static UniquePtr<RenderCompositor> Create(
      RefPtr<widget::CompositorWidget> aWidget, nsACString& aError);

  explicit RenderCompositorWayland(RefPtr<widget::CompositorWidget> aWidget,
                                   gl::GLContext* aGL = nullptr);
  virtual ~RenderCompositorWayland();
  bool Initialize(nsACString& aError);

  bool BeginFrame() override;
  RenderedFrameId EndFrame(const nsTArray<DeviceIntRect>& aDirtyRects) final;
  bool WaitForGPU() override;
  void Pause() override;
  bool Resume() override;
  void Update() override;

  gl::GLContext* gl() const override {
    return RenderThread::Get()->SingletonGL();
  }

  bool MakeCurrent() override;

  layers::WebRenderCompositor CompositorType() const override {
    return layers::WebRenderCompositor::WAYLAND;
  }

  LayoutDeviceIntSize GetBufferSize() override;

  GLenum IsContextLost(bool aForce) override;

  bool SurfaceOriginIsTopLeft() override { return false; }

  bool SupportAsyncScreenshot() override;

  bool ShouldUseNativeCompositor() override;
  uint32_t GetMaxUpdateRects() override;

  // Interface for wr::Compositor
  void CompositorBeginFrame() override;
  void CompositorEndFrame() override;
  void Bind(wr::NativeTileId aId, wr::DeviceIntPoint* aOffset, uint32_t* aFboId,
            wr::DeviceIntRect aDirtyRect,
            wr::DeviceIntRect aValidRect) override;
  void Unbind() override;
  void CreateSurface(wr::NativeSurfaceId aId, wr::DeviceIntPoint aVirtualOffset,
                     wr::DeviceIntSize aTileSize, bool aIsOpaque) override;
  void CreateExternalSurface(wr::NativeSurfaceId aId, bool aIsOpaque) override;
  void DestroySurface(NativeSurfaceId aId) override;
  void CreateTile(wr::NativeSurfaceId aId, int32_t aX, int32_t aY) override;
  void DestroyTile(wr::NativeSurfaceId aId, int32_t aX, int32_t aY) override;
  void AttachExternalImage(wr::NativeSurfaceId aId,
                           wr::ExternalImageId aExternalImage) override;
  void AddSurface(wr::NativeSurfaceId aId,
                  const wr::CompositorSurfaceTransform& aTransform,
                  wr::DeviceIntRect aClipRect,
                  wr::ImageRendering aImageRendering) override;
  void EnableNativeCompositor(bool aEnable) override;

  struct TileKey {
    TileKey(int32_t aX, int32_t aY) : mX(aX), mY(aY) {}
    TileKey() : mX(0), mY(0) {}

    int32_t mX;
    int32_t mY;
  };

 protected:
  void BindNativeLayer(wr::NativeTileId aId, const gfx::IntRect& aDirtyRect);
  void UnbindNativeLayer();

  bool UseCompositor();
  void InsertGraphicsCommandsFinishedWaitQuery(
      RenderedFrameId aRenderedFrameId);
  bool WaitForPreviousGraphicsCommandsFinishedQuery(bool aWaitAll = false);
  bool ResizeBufferIfNeeded();
  bool CreateEGLSurface();
  void DestroyEGLSurface();
  bool ShutdownEGLLibraryIfNecessary(nsACString& aError);
  void ReleaseNativeCompositorResources();

  struct Tile {
    wr::NativeSurfaceId mSurfaceId;
    TileKey mKey;

    /*struct wl_surface* surface;
    struct wl_subsurface* subsurface;
    struct wp_viewport* viewport;
    struct wl_egl_window* egl_window;*/
    EGLSurface egl_surface;
    bool is_visible;

    std::vector<EGLint> damage_rects;
  };

  struct TileKeyHashFn {
    std::size_t operator()(const TileKey& aId) const {
      return HashGeneric(aId.mX, aId.mY);
    }
  };

  struct Surface {
    gfx::IntSize TileSize() {
      return gfx::IntSize(mTileSize.width, mTileSize.height);
    }

    wr::NativeSurfaceId mId;
    wr::DeviceIntSize mTileSize;
    bool mIsExternal;
    bool mIsOpaque;
    std::unordered_map<TileKey, RefPtr<layers::NativeLayer>, TileKeyHashFn>
        mNativeLayers;
    // std::unordered_map<TileKey, Tile, TileKeyHashFn> mTiles;
  };

  struct SurfaceIdHashFn {
    std::size_t operator()(const wr::NativeSurfaceId& aId) const {
      return HashGeneric(wr::AsUint64(aId));
    }
  };

  EGLConfig mEGLConfig;
  EGLSurface mEGLSurface;

  RefPtr<layers::NativeLayerRoot> mNativeLayerRoot;
  RefPtr<layers::SurfacePoolHandle> mSurfacePoolHandle;
  RefPtr<layers::NativeLayer> mCurrentlyBoundNativeLayer;
  nsTArray<RefPtr<layers::NativeLayer>> mAddedLayers;
  std::unordered_map<wr::NativeSurfaceId, Surface, SurfaceIdHashFn> mSurfaces;
};

static inline bool operator==(const RenderCompositorWayland::TileKey& a0,
                              const RenderCompositorWayland::TileKey& a1) {
  return a0.mX == a1.mX && a0.mY == a1.mY;
}

}  // namespace wr
}  // namespace mozilla

#endif
