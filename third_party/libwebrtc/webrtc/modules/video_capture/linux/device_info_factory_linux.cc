/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/linux/device_info_pipewire.h"
#include "modules/video_capture/linux/device_info_v4l2.h"

namespace webrtc {
namespace videocapturemodule {
VideoCaptureModule::DeviceInfo* VideoCaptureImpl::CreateDeviceInfo() {
  videocapturemodule::DeviceInfoPipewire *device_info = new videocapturemodule::DeviceInfoPipewire();
  if (device_info->Init() != -1 && device_info->NumberOfDevices() > 0) {
    return device_info;
  }
  delete device_info;

  return new videocapturemodule::DeviceInfoV4L2();
}
}  // namespace videocapturemodule
}  // namespace webrtc
