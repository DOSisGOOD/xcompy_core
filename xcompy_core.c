/*
MIT License

Copyright (c) 2025 gamedevjeff

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/                  

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#if defined(_WIN32) && !defined(_XBOX)
#include <windows.h>
#endif
#include "libretro.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>

#define DEFAULT_VIDEO_WIDTH 256
#define DEFAULT_VIDEO_HEIGHT 384

static uint8_t* frame_buf;
static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

static float last_aspect;
char retro_base_directory[4096];
char retro_game_path[4096];

static Display* dpy;
static int screen;
static Window root;
static Window* windows;
static int num_windows;
static int current_window_index;
static int video_width, video_height;
#define MIN(a, b) ((a) < (b) ? (a) : (b))
static void fallback_log(enum retro_log_level level, const char* fmt, ...) {
  (void)level;
  va_list va;
  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
}

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static Window* get_all_windows(Display* dpy, int screen, int* num_windows_ret) {
  Window root = RootWindow(dpy, screen);
  Window root_return, parent_return, *children_return;
  unsigned int nchildren_return;

  XQueryTree(dpy, root, &root_return, &parent_return, &children_return,
             &nchildren_return);

  *num_windows_ret = nchildren_return;
  return children_return;
}

static void update_window_list() {
  if(windows) {
    XFree(windows);
    windows = NULL;
  }

  windows = get_all_windows(dpy, screen, &num_windows);
  if(windows) {
    log_cb(RETRO_LOG_INFO, "Found %d windows.\n", num_windows);
  } else {
    log_cb(RETRO_LOG_INFO, "No windows found.\n");
    num_windows = 0;
  }
}
static int x11_error_handler(Display *display, XErrorEvent *error) {
  if (error->error_code == BadWindow) {
    log_cb(RETRO_LOG_INFO, "Window no longer exists?\n");
    return 0;
  }
  
  char error_text[256];
  XGetErrorText(display, error->error_code, error_text, sizeof(error_text));
  log_cb(RETRO_LOG_ERROR, "X11 Error: %s\n", error_text);
  return 0;
}

static void capture_window(Window window, uint8_t* buffer) {
  if(!dpy || !buffer) {
    log_cb(RETRO_LOG_ERROR, "Invalid display or buffer\n");
    return;
  }

  XLockDisplay(dpy);

  XWindowAttributes attr;
  Status status = XGetWindowAttributes(dpy, window, &attr);

  if(!status || attr.map_state != IsViewable) {
    log_cb(RETRO_LOG_INFO, "Window not accessible or viewable\n");
    XUnlockDisplay(dpy);
    return;
  }

  XImage* img = NULL;

  if(attr.width <= 0 || attr.height <= 0) {
      log_cb(RETRO_LOG_ERROR, "Invalid window dimensions: %dx%d\n", attr.width,
             attr.height);
      XUnlockDisplay(dpy);
      return;
    }

    img = XGetImage(dpy, window, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
    if(!img) {
    log_cb(RETRO_LOG_ERROR, "XGetImage failed\n");
    XUnlockDisplay(dpy);
    return;
    }

    if(!img->data) {
    log_cb(RETRO_LOG_ERROR, "Image has no data\n");
    XDestroyImage(img);
    XUnlockDisplay(dpy);
    return;
    }

    int dst_stride = video_width * sizeof(uint32_t);
    int src_stride = img->bytes_per_line;
    uint8_t *src = (uint8_t*)img->data;
    uint8_t *dst = buffer;

    int copy_width = MIN(attr.width * sizeof(uint32_t), video_width * sizeof(uint32_t));
    int copy_height = MIN(attr.height, video_height);

    for (int y = 0; y < copy_height; y++) {
    memcpy(dst + (y * dst_stride), src + (y * src_stride), copy_width);
    }

    XDestroyImage(img);
    XUnlockDisplay(dpy);
}

static bool resize_framebuffer(int width, int height) {
  if(width <= 0 || height <= 0) {
    log_cb(RETRO_LOG_ERROR, "Invalid dimensions for resize: %dx%d\n", width,
           height);
    return false;
  }

  size_t new_size = width * height * sizeof(uint32_t);
  uint8_t* new_buf = (uint8_t*)malloc(new_size);
  if(!new_buf) {
    log_cb(RETRO_LOG_ERROR, "Failed to allocate buffer for %dx%d\n", width,
           height);
    return false;
  }

  memset(new_buf, 0, new_size);
  if(frame_buf) {
    free(frame_buf);
  }

  frame_buf = new_buf;
  video_width = width;
  video_height = height;

  struct retro_system_av_info av_info;
  av_info.geometry.base_width = width;
  av_info.geometry.base_height = height;
  av_info.geometry.max_width = width;
  av_info.geometry.max_height = height;
  av_info.geometry.aspect_ratio = (float)width / (float)height;
  av_info.timing.fps = 60.0;
  av_info.timing.sample_rate = 30000.0;

  environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);
  log_cb(RETRO_LOG_INFO, "Resized buffer and geometry to %dx%d\n", width,
         height);
  return true;
}
static void update_input(void) {
  input_poll_cb();
  int mouse_wheel = 0;
  if(input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP))
    mouse_wheel = 1;
  if(input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
    mouse_wheel = -1;
  if(mouse_wheel != 0) {
    update_window_list();
    current_window_index = (current_window_index + mouse_wheel) % num_windows;

    if(current_window_index < 0)
      current_window_index = num_windows - 1;

    if(windows && current_window_index >= 0 &&
        current_window_index < num_windows) {
      XWindowAttributes attr;
      XLockDisplay(dpy);
      Status status =
          XGetWindowAttributes(dpy, windows[current_window_index], &attr);
      XUnlockDisplay(dpy);
      
      if(status && attr.map_state == IsViewable) {
        if(attr.width > 0 && attr.height > 0) {
          if(!resize_framebuffer(attr.width, attr.height)) {
            log_cb(RETRO_LOG_ERROR, "Failed to resize for window %dx%d\n",
                   attr.width, attr.height);
          }
        }
      } else {
        log_cb(RETRO_LOG_INFO, "Window not accessible or viewable\n");
      }
    }
  }
}

void retro_init(void) {
  video_width = DEFAULT_VIDEO_WIDTH;
  video_height = DEFAULT_VIDEO_HEIGHT;

  if(!resize_framebuffer(video_width, video_height)) {
    log_cb(RETRO_LOG_ERROR, "Failed to allocate initial frame buffer.\n");
    return;
  }

  const char* dir = NULL;
  if(environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir) {
    snprintf(retro_base_directory, sizeof(retro_base_directory), "%s", dir);
  }

  dpy = XOpenDisplay(NULL);
  if(!dpy) {
    log_cb(RETRO_LOG_ERROR, "Cannot open display.\n");
    return;
  }
  XSetErrorHandler(x11_error_handler);
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);

  int composite_event_base, composite_error_base;
  if(!XCompositeQueryExtension(dpy, &composite_event_base,
                                &composite_error_base)) {
    log_cb(RETRO_LOG_ERROR, "XComposite extension is not available.\n");
    XCloseDisplay(dpy);
    dpy = NULL;
    return;
  }

  update_window_list();
  current_window_index = 0;

  if(windows && num_windows > 0) {
    XWindowAttributes attr;
    XLockDisplay(dpy);
    if(XGetWindowAttributes(dpy, windows[0], &attr) &&
        attr.map_state == IsViewable && attr.width > 0 && attr.height > 0) {
      resize_framebuffer(attr.width, attr.height);
    }
    XUnlockDisplay(dpy);
  }
}

void retro_deinit(void) {
  if(frame_buf) {
    free(frame_buf);
    frame_buf = NULL;
  }

  if(dpy) {
    if(windows)
      XFree(windows);

    XCloseDisplay(dpy);
    dpy = NULL;
  }
}

unsigned retro_api_version(void) {
  return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
  log_cb(RETRO_LOG_INFO, "Plugging device %u into port %u.\n", device, port);
}

void retro_get_system_info(struct retro_system_info* info) {
  memset(info, 0, sizeof(*info));
  info->library_name = "XCompy core";
  info->library_version = "0.1";
  info->need_fullpath = false;
  info->valid_extensions = "";
  info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
  info->geometry.max_width = video_width;
  info->geometry.max_height = video_height;
  info->geometry.base_width = video_width;
  info->geometry.base_height = video_height;
  info->geometry.aspect_ratio = (float)video_width / (float)video_height;

  last_aspect = info->geometry.aspect_ratio;
}

void retro_set_environment(retro_environment_t cb) {
  environ_cb = cb;

  if(cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
    log_cb = logging.log;
  else
    log_cb = fallback_log;

  static const struct retro_controller_description controllers[] = {
      {"Mouse", RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0)},
      {"Keyboard", RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_KEYBOARD, 0)}};

  static const struct retro_controller_info ports[] = {
      {controllers, 2},
      {NULL, 0},
  };
  cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
  bool support_no_game = true;
  environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &support_no_game);
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
  audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
  audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb) {
  input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb) {
  input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
  video_cb = cb;
}

void retro_reset(void) {}

static void check_variables(void) {}

bool retro_load_game(const struct retro_game_info* info) {
  struct retro_input_descriptor desc[] = {
      {0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP,
       "Mouse Wheel Up"},
      {0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN,
       "Mouse Wheel Down"},
      {0, RETRO_DEVICE_KEYBOARD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left"},
      {0, RETRO_DEVICE_KEYBOARD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right"},
      {0, RETRO_DEVICE_KEYBOARD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up"},
      {0, RETRO_DEVICE_KEYBOARD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down"},
      {0},
  };

  environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if(!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
    log_cb(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
    return false;
  }

  check_variables();

  (void)info;
  return true;
}
void retro_run(void) {
  static int frame_counter = 0;
  bool updated = false;
  frame_counter++;

  update_input();
  if(dpy && frame_buf && num_windows > 0 && windows &&
      current_window_index >= 0 && current_window_index < num_windows) {
    capture_window(windows[current_window_index], frame_buf);
    video_cb(frame_buf, video_width, video_height,
             video_width * sizeof(uint32_t));
    updated = true;
  } else {
    if(frame_buf) {
      memset(frame_buf, 0, video_width * video_height * sizeof(uint32_t));
      video_cb(frame_buf, video_width, video_height,
               video_width * sizeof(uint32_t));
      updated = true;
    }
  }

  if(environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
    check_variables();
  }
}

void retro_unload_game(void) {}

unsigned retro_get_region(void) {
  return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type,
                             const struct retro_game_info* info,
                             size_t num) {
  return false;
}

size_t retro_serialize_size(void) {
  return 0;
}

bool retro_serialize(void* data_, size_t size) {
  return false;
}

bool retro_unserialize(const void* data_, size_t size) {
  return false;
}

void* retro_get_memory_data(unsigned id) {
  (void)id;
  return NULL;
}

size_t retro_get_memory_size(unsigned id) {
  (void)id;
  return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char* code) {
  (void)index;
  (void)enabled;
  (void)code;
}
