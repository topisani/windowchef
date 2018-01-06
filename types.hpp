/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for
 * full license information. */

#ifndef _TYPES_H
#define _TYPES_H

#include <stdbool.h>
#include <xcb/randr.h>

enum position {
  BOTTOM_LEFT,
  BOTTOM_RIGHT,
  TOP_LEFT,
  TOP_RIGHT,
  CENTER,
  LEFT,
  BOTTOM,
  TOP,
  RIGHT,
  ALL,
};

enum direction {
  NORTH,
  SOUTH,
  EAST,
  WEST,
};

enum mouse_mode {
  MOUSE_NONE,
  MOUSE_MOVE,
  MOUSE_RESIZE,
};

enum pointer_action {
  POINTER_ACTION_NOTHING,
  POINTER_ACTION_FOCUS,
  POINTER_ACTION_MOVE,
  POINTER_ACTION_RESIZE_CORNER,
  POINTER_ACTION_RESIZE_SIDE,
};

enum resize_handle {
  HANDLE_LEFT,
  HANDLE_BOTTOM,
  HANDLE_TOP,
  HANDLE_RIGHT,

  HANDLE_TOP_LEFT,
  HANDLE_TOP_RIGHT,
  HANDLE_BOTTOM_LEFT,
  HANDLE_BOTTOM_RIGHT,
};

struct WinPosition {
  int16_t x, y;
};

struct WindowGeom {
  int16_t x = 0, y = 0;
  uint16_t width = 0, height = 0;
  bool set_by_user = false;
};

struct Workspace {
  uint32_t index;
  bool bar_shown;
};

struct Monitor {
  xcb_randr_output_t monitor;
  char* name;
  int16_t x, y;
  uint16_t width, height;
};

struct Client {
  xcb_window_t window;
  WindowGeom geom;
  WindowGeom orig_geom;
  bool fullscreen = false;
  bool hmaxed = false;
  bool vmaxed = false;
  Monitor* monitor;
  uint16_t min_width, min_height;
  uint16_t max_width, max_height;
  uint16_t width_inc = 1;
  uint16_t height_inc = 1;
  bool mapped;
  uint32_t workspace;
};

bool operator==(const Client& rhs, const Client& lhs) {
  return rhs.window == lhs.window;
}

bool operator!=(const Client& rhs, const Client& lhs) {
  return rhs.window != lhs.window;
}

bool operator==(const Monitor& rhs, const Monitor& lhs) {
  return rhs.monitor == lhs.monitor;
}

bool operator!=(const Monitor& rhs, const Monitor& lhs) {
  return rhs.monitor != lhs.monitor;
}

struct Conf {
  int8_t border_width, grid_gap;
  int8_t gap_left, gap_down, gap_up, gap_right;
  uint32_t focus_color, unfocus_color;
  enum position cursor_position;
  uint32_t workspaces;
  bool sloppy_focus;
  bool resize_hints;
  bool sticky_windows;
  bool borders;
  bool last_window_focusing;
  bool apply_settings;
  bool replay_click_on_focus;
  bool bar_shown;
  uint32_t bar_padding[4];
  enum pointer_action pointer_actions[3];
  uint16_t pointer_modifier;
  int8_t click_to_focus;
};

#endif
