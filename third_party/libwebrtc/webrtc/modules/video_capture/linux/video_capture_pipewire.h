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

#include <gio/gio.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>


#include "common_types.h"  // NOLINT(build/include)
#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/video_capture/video_capture_impl.h"
#include "modules/video_capture/linux/pipewire_common.h"

namespace webrtc {
namespace videocapturemodule {

class VideoCaptureModulePipewire: public VideoCaptureImpl {
 public:
  VideoCaptureModulePipewire();
  virtual ~VideoCaptureModulePipewire();
  virtual int32_t Init(const char* deviceUniqueId);
  virtual int32_t StartCapture(const VideoCaptureCapability& capability);
  virtual int32_t StopCapture();
  virtual bool CaptureStarted();
  virtual int32_t CaptureSettings(VideoCaptureCapability& settings);

 private:
  void InitPipeWire();
  pw_stream* CreateReceivingStream();
  void HandleBuffer(pw_buffer* buffer);

  static void OnCoreError(void *data,
                          uint32_t id,
                          int seq,
                          int res,
                          const char *message);
  static void OnStreamParamChanged(void *data,
                                   uint32_t id,
                                   const struct spa_pod *format);
  static void OnStreamStateChanged(void* data,
                                   pw_stream_state old_state,
                                   pw_stream_state state,
                                   const char* error_message);
  static void OnStreamProcess(void* data);
  static void OnStreamAddBuffer(void* data,
                                struct pw_buffer* buffer);
  static void OnStreamRemoveBuffer(void* data,
                                   struct pw_buffer* buffer);

  int32_t _currentWidth = -1;
  int32_t _currentHeight = -1;
  int32_t _currentFrameRate = -1;
  bool _captureStarted = false;
  VideoType _captureVideoType = VideoType::kUnknown;

  GDBusProxy* _proxy = nullptr;
  pw_context* _pw_context = nullptr;
  pw_core* _pw_core = nullptr;
  pw_stream* _pw_stream = nullptr;
  pw_thread_loop* _pw_main_loop = nullptr;

  spa_hook _spa_core_listener = {};
  spa_hook _spa_stream_listener = {};

  pw_core_events _pw_core_events = {};
  pw_stream_events _pw_stream_events = {};

  struct spa_video_info_raw _spa_video_format;

  //guint32 _pw_stream_node_id = 0;
  gint32 _pw_fd = -1;

  DesktopSize _video_size;
  DesktopSize _desktop_size = {};

  rtc::CriticalSection _current_frame_lock;
  std::unique_ptr<uint8_t[]> _current_frame;
};

}  // namespace videocapturemodule
}  // namespace webrtc

#endif // MODULES_VIDEO_CAPTURE_MAIN_SOURCE_LINUX_VIDEO_CAPTURE_PIPEWIRE_H_
