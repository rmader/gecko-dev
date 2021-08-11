/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CAPTURE_MAIN_SOURCE_LINUX_VIDEO_CAPTURE_PIPEWIRE_H_
#define MODULES_VIDEO_CAPTURE_MAIN_SOURCE_LINUX_VIDEO_CAPTURE_PIPEWIRE_H_

#include "common_types.h"  // NOLINT(build/include)
#include "modules/video_capture/video_capture_impl.h"
#include "modules/video_capture/linux/pipewire_common.h"

namespace webrtc
{
namespace videocapturemodule
{
class VideoCaptureModulePipewire: public VideoCaptureImpl
{
public:
    VideoCaptureModulePipewire();
    virtual ~VideoCaptureModulePipewire();
    virtual int32_t Init(const char* deviceUniqueId);
    virtual int32_t StartCapture(const VideoCaptureCapability& capability);
    virtual int32_t StopCapture();
    virtual bool CaptureStarted();
    virtual int32_t CaptureSettings(VideoCaptureCapability& settings);

private:
    int32_t _currentWidth;
    int32_t _currentHeight;
    int32_t _currentFrameRate;
    bool _captureStarted;
    VideoType _captureVideoType;
};
}  // namespace videocapturemodule
}  // namespace webrtc

#endif // MODULES_VIDEO_CAPTURE_MAIN_SOURCE_LINUX_VIDEO_CAPTURE_PIPEWIRE_H_
