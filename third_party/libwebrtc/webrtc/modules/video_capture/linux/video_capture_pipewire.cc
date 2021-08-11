/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/linux/video_capture_pipewire.h"

namespace webrtc {
namespace videocapturemodule {

VideoCaptureModulePipewire::VideoCaptureModulePipewire()
    : VideoCaptureImpl(),
      _currentWidth(-1),
      _currentHeight(-1),
      _currentFrameRate(-1),
      _captureStarted(false),
      _captureVideoType(VideoType::kUnknown) {}

int32_t VideoCaptureModulePipewire::Init(const char* deviceUniqueIdUTF8) {
  return -1;
}

VideoCaptureModulePipewire::~VideoCaptureModulePipewire() {}

int32_t VideoCaptureModulePipewire::StartCapture(
    const VideoCaptureCapability& capability) {
  return -1;
}

int32_t VideoCaptureModulePipewire::StopCapture() {
  return 0;
}

bool VideoCaptureModulePipewire::CaptureStarted() {
  return _captureStarted;
}

int32_t VideoCaptureModulePipewire::CaptureSettings(
    VideoCaptureCapability& settings) {
  settings.width = _currentWidth;
  settings.height = _currentHeight;
  settings.maxFPS = _currentFrameRate;
  settings.videoType = _captureVideoType;

  return 0;
}
}  // namespace videocapturemodule
}  // namespace webrtc
