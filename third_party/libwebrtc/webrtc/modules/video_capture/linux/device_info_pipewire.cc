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

DeviceInfoPipewire::DeviceInfoPipewire() : DeviceInfoImpl()
    , _proxy(nullptr){}

int32_t DeviceInfoPipewire::Init() {
  _proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_NONE, nullptr,
      kDesktopBusName, kDesktopObjectPath, kCameraInterfaceName,
      nullptr, nullptr);

  return _proxy ? 0 : -1;
}

DeviceInfoPipewire::~DeviceInfoPipewire() {
  if (_proxy) {
    g_object_unref(_proxy);
    _proxy = nullptr;
  }
}

uint32_t DeviceInfoPipewire::NumberOfDevices() {
  RTC_LOG(LS_INFO) << __FUNCTION__;

  bool isCameraPresent = false;
	GVariant *cameraPresentVariant =
	    g_dbus_proxy_get_cached_property(_proxy, "IsCameraPresent");
	if (cameraPresentVariant) {
	  isCameraPresent = g_variant_get_boolean(cameraPresentVariant);
	  g_variant_unref(cameraPresentVariant);
	}

  return isCameraPresent ? 1 : 0;
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

  assert(deviceNumber == 0);
  assert(deviceNameSize > strlen(kPipewireDeviceName));
  assert(deviceUniqueIdUTF8Size > strlen(kPipewireDeviceUniqueId));

  memset(deviceNameUTF8, 0, deviceNameSize);
  memset(deviceUniqueIdUTF8, 0, deviceUniqueIdUTF8Size);
  memcpy(deviceNameUTF8, kPipewireDeviceName, strlen(kPipewireDeviceName));
  memcpy(deviceUniqueIdUTF8, kPipewireDeviceUniqueId,
         strlen(kPipewireDeviceUniqueId));

  return 0;
}

int32_t DeviceInfoPipewire::CreateCapabilityMap(const char* deviceUniqueIdUTF8) {
  _captureCapabilities.clear();

  VideoCaptureCapability cap;
  cap.width = 800;
  cap.height = 600;
  cap.videoType = VideoType::kI420;
  cap.maxFPS = 30;
  _captureCapabilities.push_back(cap);

  // Store the new used device name
  const int32_t deviceUniqueIdUTF8Length =
      (int32_t)strlen((char*)deviceUniqueIdUTF8);
  _lastUsedDeviceNameLength = deviceUniqueIdUTF8Length;
  _lastUsedDeviceName =
      (char*)realloc(_lastUsedDeviceName, _lastUsedDeviceNameLength + 1);
  memcpy(_lastUsedDeviceName, deviceUniqueIdUTF8,
         _lastUsedDeviceNameLength + 1);

  return _captureCapabilities.size();
}

}  // namespace videocapturemodule
}  // namespace webrtc
