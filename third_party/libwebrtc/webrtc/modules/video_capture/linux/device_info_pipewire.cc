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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rtc_base/logging.h"

namespace webrtc {
namespace videocapturemodule {

DeviceInfoPipewire::DeviceInfoPipewire() : DeviceInfoImpl(){}

int32_t DeviceInfoPipewire::Init() {
  return 0;
}

DeviceInfoPipewire::~DeviceInfoPipewire() {}

uint32_t DeviceInfoPipewire::NumberOfDevices() {
  RTC_LOG(LS_INFO) << __FUNCTION__;

  return 0;
}

int32_t DeviceInfoPipewire::GetDeviceName(uint32_t deviceNumber,
                                       char* deviceNameUTF8,
                                       uint32_t deviceNameSize,
                                       char* deviceUniqueIdUTF8,
                                       uint32_t deviceUniqueIdUTF8Size,
                                       char* /*productUniqueIdUTF8*/,
                                       uint32_t /*productUniqueIdUTF8Size*/,
                                       pid_t* /*pid*/) {
  RTC_LOG(LS_INFO) << __FUNCTION__;

  return -1;
}

int32_t DeviceInfoPipewire::CreateCapabilityMap(const char* deviceUniqueIdUTF8) {
  return -1;
}

}  // namespace videocapturemodule
}  // namespace webrtc
