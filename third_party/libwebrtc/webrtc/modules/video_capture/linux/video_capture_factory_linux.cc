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
#include "modules/video_capture/linux/video_capture_v4l2.h"

#include "rtc_base/refcountedobject.h"
#include "rtc_base/scoped_ref_ptr.h"

namespace webrtc {
namespace videocapturemodule {
rtc::scoped_refptr<VideoCaptureModule> VideoCaptureImpl::Create(
    const char* deviceUniqueId) {
  if (strcmp(deviceUniqueId, kPipewireDeviceUniqueId) == 0) {
    rtc::scoped_refptr<VideoCaptureModulePipewire> implementation(
        new rtc::RefCountedObject<VideoCaptureModulePipewire>());
    if (implementation->Init(deviceUniqueId) != 0) {
      return nullptr;
    }
    return implementation;
  } else {
    rtc::scoped_refptr<VideoCaptureModuleV4L2> implementation(
        new rtc::RefCountedObject<VideoCaptureModuleV4L2>());
    if (implementation->Init(deviceUniqueId) != 0) {
      return nullptr;
    }
    return implementation;
  }
}
}  // namespace videocapturemodule
}  // namespace webrtc
