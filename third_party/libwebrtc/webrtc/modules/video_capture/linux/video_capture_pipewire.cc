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

#include <gio/gunixfdlist.h>

#include <sys/mman.h>

#include <cstring>

namespace webrtc {
namespace videocapturemodule {

// static
struct dma_buf_sync {
  uint64_t flags;
};
#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)
#define DMA_BUF_BASE           'b'
#define DMA_BUF_IOCTL_SYNC     _IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

static void SyncDmaBuf(int fd, uint64_t start_or_end) {
  struct dma_buf_sync sync = { 0 };

  sync.flags = start_or_end | DMA_BUF_SYNC_READ;

  while(true) {
    int ret;
    ret = ioctl (fd, DMA_BUF_IOCTL_SYNC, &sync);
    if (ret == -1 && errno == EINTR) {
      continue;
    } else if (ret == -1) {
      RTC_LOG(LS_ERROR) << "Failed to synchronize DMA buffer: " << g_strerror(errno);
      break;
    } else {
      break;
    }
  }
}

VideoCaptureModulePipewire::VideoCaptureModulePipewire()
    : VideoCaptureImpl(){}

int32_t VideoCaptureModulePipewire::Init(const char* deviceUniqueIdUTF8) {
  int len = strlen((const char*)deviceUniqueIdUTF8);
  _deviceUniqueId = new (std::nothrow) char[len + 1];
  if (_deviceUniqueId) {
    memcpy(_deviceUniqueId, deviceUniqueIdUTF8, len + 1);
  }
  return 0;
}

VideoCaptureModulePipewire::~VideoCaptureModulePipewire() {
  if (_pw_main_loop) {
    pw_thread_loop_stop(_pw_main_loop);
  }

  if (_pw_stream) {
    pw_stream_destroy(_pw_stream);
  }

  if (_pw_core) {
    pw_core_disconnect(_pw_core);
  }

  if (_pw_context) {
    pw_context_destroy(_pw_context);
  }

  if (_pw_main_loop) {
    pw_thread_loop_destroy(_pw_main_loop);
  }

  if (_proxy) {
    g_object_unref(_proxy);
    _proxy = nullptr;
  }

  if (_pw_fd != -1) {
    close (_pw_fd);
  }
}

int32_t VideoCaptureModulePipewire::StartCapture(
    const VideoCaptureCapability& capability) {
  GError* error = nullptr;

  _proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
      G_DBUS_PROXY_FLAGS_NONE, nullptr,
      kDesktopBusName, kDesktopObjectPath, kCameraInterfaceName,
      nullptr, &error);
  if (!_proxy) {
    g_error_free(error);
    return -1;
  }

  /*std::string uniqueName = g_dbus_connection_get_unique_name(g_dbus_proxy_get_connection(_proxy));
  uniqueName = uniqueName.substr(1);
  std::replace(uniqueName.begin(), uniqueName.end(), '.', '_');*/

  GVariantBuilder accessCameraOptions;
  //char *token = NULL;
  //token = g_strdup_printf ("portal%d", g_random_int_range (0, G_MAXINT));
  g_variant_builder_init(&accessCameraOptions, G_VARIANT_TYPE_VARDICT);
  //g_variant_builder_add (&accessCameraOptions, "{sv}", "handle_token", g_variant_new_string (token));
  GVariant *accessCameraVariant = g_dbus_proxy_call_sync(
      _proxy,
      "AccessCamera",
      g_variant_new ("(a{sv})", &accessCameraOptions),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      nullptr,
      &error);
  if (!accessCameraVariant) {
    g_error_free(error);
    return -1;
  }

  GUnixFDList *outlist = nullptr;
  GVariantBuilder openPipewireOptions;
  g_variant_builder_init(&openPipewireOptions, G_VARIANT_TYPE_VARDICT);
  GVariant *openPipewireVariant = g_dbus_proxy_call_with_unix_fd_list_sync(
      _proxy,
      "OpenPipeWireRemote",
      g_variant_new("(a{sv})", &openPipewireOptions),
      G_DBUS_CALL_FLAGS_NONE,
      -1,
      nullptr,
      &outlist,
      nullptr,
      &error);
  if (!openPipewireVariant) {
    g_error_free(error);
    return -1;
  }

  gint32 index;
  g_variant_get(openPipewireVariant, "(h)", &index);
  if ((_pw_fd = g_unix_fd_list_get(outlist, index, &error)) == -1) {
    g_error_free(error);
    return -1;
  }

  InitPipeWire();

  return 0;
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

void VideoCaptureModulePipewire::InitPipeWire() {
  pw_init(/*argc=*/nullptr, /*argc=*/nullptr);

  _pw_main_loop = pw_thread_loop_new("pipewire-main-loop", nullptr);
  _pw_context = pw_context_new(pw_thread_loop_get_loop(_pw_main_loop), nullptr, 0);
  if (!_pw_context) {
    RTC_LOG(LS_ERROR) << "Failed to create PipeWire context";
    return;
  }

  _pw_core = pw_context_connect_fd(_pw_context, _pw_fd, nullptr, 0);
  if (!_pw_core) {
    RTC_LOG(LS_ERROR) << "Failed to connect PipeWire context";
    return;
  }

  // Initialize event handlers, remote end and stream-related.
  _pw_core_events.version = PW_VERSION_CORE_EVENTS;
  _pw_core_events.error = &OnCoreError;

  _pw_stream_events.version = PW_VERSION_STREAM_EVENTS;
  _pw_stream_events.state_changed = &OnStreamStateChanged;
  _pw_stream_events.param_changed = &OnStreamParamChanged;
  _pw_stream_events.process = &OnStreamProcess;

  pw_core_add_listener(_pw_core, &_spa_core_listener, &_pw_core_events, this);

  _pw_stream = CreateReceivingStream();
  if (!_pw_stream) {
    RTC_LOG(LS_ERROR) << "Failed to create PipeWire stream";
    return;
  }

  if (pw_thread_loop_start(_pw_main_loop) < 0) {
    RTC_LOG(LS_ERROR) << "Failed to start main PipeWire loop";
  }
}

pw_stream* VideoCaptureModulePipewire::CreateReceivingStream() {
  spa_rectangle pwMinScreenBounds = spa_rectangle{1, 1};
  spa_rectangle pwDefaultScreenBounds = spa_rectangle{640, 480};
  spa_rectangle pwMaxScreenBounds = spa_rectangle{INT32_MAX, INT32_MAX};

  auto stream = pw_stream_new(_pw_core, "webrtc-pipewire-stream", nullptr);

  if (!stream) {
    RTC_LOG(LS_ERROR) << "Could not create receiving stream.";
    return nullptr;
  }

  uint8_t buffer[1024] = {};
  const spa_pod* params[2];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof (buffer));

  params[0] = reinterpret_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
              SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
              SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
              SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
              SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3,
                                                              SPA_VIDEO_FORMAT_I420,
                                                              SPA_VIDEO_FORMAT_YUY2,
                                                              SPA_VIDEO_FORMAT_UYVY),
              SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&pwMinScreenBounds,
                                                                    &pwDefaultScreenBounds,
                                                                    &pwMaxScreenBounds),
              0));
  pw_stream_add_listener(stream, &_spa_stream_listener, &_pw_stream_events, this);

  if (pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
      PW_STREAM_FLAG_AUTOCONNECT, params, 1) != 0) {
    RTC_LOG(LS_ERROR) << "Could not connect receiving stream.";
  }

  return stream;
}

// static
void VideoCaptureModulePipewire::OnCoreError(void *data,
                                       uint32_t id,
                                       int seq,
                                       int res,
                                       const char *message) {
  RTC_LOG(LS_ERROR) << "core error: " << message;
}

// static
void VideoCaptureModulePipewire::OnStreamStateChanged(void* data,
                                                pw_stream_state old_state,
                                                pw_stream_state state,
                                                const char* error_message) {
  switch (state) {
    case PW_STREAM_STATE_ERROR:
      RTC_LOG(LS_ERROR) << "PipeWire stream state error: " << error_message;
      break;
    case PW_STREAM_STATE_PAUSED:
    case PW_STREAM_STATE_STREAMING:
    case PW_STREAM_STATE_UNCONNECTED:
    case PW_STREAM_STATE_CONNECTING:
      break;
  }
}

static const int kBytesPerPixel = 4;

// static
void VideoCaptureModulePipewire::OnStreamParamChanged(void *data, uint32_t id,
                                                const struct spa_pod *format) {
  VideoCaptureModulePipewire* that = static_cast<VideoCaptureModulePipewire*>(data);
  RTC_DCHECK(that);

  RTC_LOG(LS_INFO) << "PipeWire stream param changed.";

  if (!format || id != SPA_PARAM_Format) {
    return;
  }

  spa_format_video_raw_parse(format, &that->_spa_video_format);

  auto width = that->_spa_video_format.size.width;
  auto height = that->_spa_video_format.size.height;
  that->_desktop_size = DesktopSize(width, height);

  uint8_t buffer[512] = {};
  auto builder = spa_pod_builder{buffer, sizeof(buffer)};

  // Setup buffers and meta header for new format.
  const struct spa_pod* params[3];
  params[0] = reinterpret_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
              SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
              SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1<<SPA_DATA_MemPtr) |
                                                                   (1<<SPA_DATA_MemFd) |
                                                                   (1<<SPA_DATA_DmaBuf)),
              SPA_PARAM_BUFFERS_blocks, SPA_POD_CHOICE_RANGE_Int(0, 1, INT32_MAX),
              SPA_PARAM_BUFFERS_size, SPA_POD_CHOICE_RANGE_Int(0, 0, INT32_MAX),
              SPA_PARAM_BUFFERS_stride, SPA_POD_CHOICE_RANGE_Int(0, 0, INT32_MAX),
              SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(8, 1, 32)));
  params[1] = reinterpret_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
              SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
              SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
              SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_header))));
  params[2] = reinterpret_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
              SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
              SPA_PARAM_META_type, SPA_POD_Id (SPA_META_VideoCrop),
              SPA_PARAM_META_size, SPA_POD_Int (sizeof(struct spa_meta_region))));
  pw_stream_update_params(that->_pw_stream, params, 3);
}

// static
void VideoCaptureModulePipewire::OnStreamProcess(void* data) {
  VideoCaptureModulePipewire* that = static_cast<VideoCaptureModulePipewire*>(data);
  RTC_DCHECK(that);

  struct pw_buffer *next_buffer;
  struct pw_buffer *buffer = nullptr;

  next_buffer = pw_stream_dequeue_buffer(that->_pw_stream);
  while (next_buffer) {
    buffer = next_buffer;
    next_buffer = pw_stream_dequeue_buffer(that->_pw_stream);

    if (next_buffer) {
      pw_stream_queue_buffer (that->_pw_stream, buffer);
    }
  }

  if (!buffer) {
    return;
  }

  that->HandleBuffer(buffer);

  pw_stream_queue_buffer(that->_pw_stream, buffer);
}

static void SpaBufferUnmap(unsigned char *map, int map_size, bool IsDMABuf, int fd) {
  if (map) {
    if (IsDMABuf) {
      SyncDmaBuf(fd, DMA_BUF_SYNC_END);
    }
    munmap(map, map_size);
  }
}

void VideoCaptureModulePipewire::HandleBuffer(pw_buffer* buffer) {
  spa_buffer* spaBuffer = buffer->buffer;
  uint8_t *map = nullptr;
  uint8_t* src = nullptr;

  if (spaBuffer->datas[0].chunk->size == 0) {
    RTC_LOG(LS_ERROR) << "Failed to get video stream: Zero size.";
    return;
  }

  switch (spaBuffer->datas[0].type) {
    case SPA_DATA_MemFd:
    case SPA_DATA_DmaBuf:
      map = static_cast<uint8_t*>(mmap(
          nullptr, spaBuffer->datas[0].maxsize + spaBuffer->datas[0].mapoffset,
          PROT_READ, MAP_PRIVATE, spaBuffer->datas[0].fd, 0));
      if (map == MAP_FAILED) {
        RTC_LOG(LS_ERROR) << "Failed to mmap memory: " << std::strerror(errno);
        return;
      }
      if (spaBuffer->datas[0].type == SPA_DATA_DmaBuf) {
        SyncDmaBuf(spaBuffer->datas[0].fd, DMA_BUF_SYNC_START);
      }
      src = SPA_MEMBER(map, spaBuffer->datas[0].mapoffset, uint8_t);
      break;
    case SPA_DATA_MemPtr:
      map = nullptr;
      src = static_cast<uint8_t*>(spaBuffer->datas[0].data);
      break;
    default:
      return;
  }

  if (!src) {
    RTC_LOG(LS_ERROR) << "Failed to get video stream: Wrong data after mmap()";
    SpaBufferUnmap(map,
      spaBuffer->datas[0].maxsize + spaBuffer->datas[0].mapoffset,
      spaBuffer->datas[0].type == SPA_DATA_DmaBuf, spaBuffer->datas[0].fd);
    return;
  }

  struct spa_meta_region* video_metadata =
    static_cast<struct spa_meta_region*>(
       spa_buffer_find_meta_data(spaBuffer, SPA_META_VideoCrop, sizeof(*video_metadata)));

  // Video size from metada is bigger than an actual video stream size.
  // The metadata are wrong or we should up-scale te video...in both cases
  // just quit now.
  if (video_metadata &&
      (video_metadata->region.size.width > (uint32_t)_desktop_size.width() ||
        video_metadata->region.size.height > (uint32_t)_desktop_size.height())) {
    RTC_LOG(LS_ERROR) << "Stream metadata sizes are wrong!";
    SpaBufferUnmap(map,
      spaBuffer->datas[0].maxsize + spaBuffer->datas[0].mapoffset,
      spaBuffer->datas[0].type == SPA_DATA_DmaBuf, spaBuffer->datas[0].fd);
    return;
  }

  DesktopSize video_size_prev = _video_size;
  _video_size = _desktop_size;

  const int32_t srcStride = spaBuffer->datas[0].chunk->stride;
  const int32_t bytesPerPixel = srcStride / _video_size.width();

  rtc::CritScope lock(&_current_frame_lock);
  if (!_current_frame || !_video_size.equals(video_size_prev)) {
    _current_frame =
      std::make_unique<uint8_t[]>
        (_video_size.width() * _video_size.height() * bytesPerPixel);
  }

  std::memcpy(_current_frame.get(), src,
              _video_size.width() * _video_size.height() * bytesPerPixel);

  VideoCaptureCapability frameInfo;
  frameInfo.width = _video_size.width();
  frameInfo.height = _video_size.height();
  
  switch (_spa_video_format.format) {
    case SPA_VIDEO_FORMAT_I420:
      frameInfo.videoType = VideoType::kI420;
      break;
    case SPA_VIDEO_FORMAT_UYVY:
      frameInfo.videoType = VideoType::kIYUV;
      break;
    case SPA_VIDEO_FORMAT_YUY2:
      frameInfo.videoType = VideoType::kYUY2;
      break;
    default:
      frameInfo.videoType = VideoType::kUnknown;
      break;
  }

  IncomingFrame((unsigned char*)_current_frame.get(),
                spaBuffer->datas[0].maxsize + spaBuffer->datas[0].mapoffset,
                frameInfo);

  SpaBufferUnmap(map,
    spaBuffer->datas[0].maxsize + spaBuffer->datas[0].mapoffset,
    spaBuffer->datas[0].type == SPA_DATA_DmaBuf, spaBuffer->datas[0].fd);
}

}  // namespace videocapturemodule
}  // namespace webrtc
