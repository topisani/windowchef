/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for
 * full license information. */
#pragma once
#include <array>
#include <optional>

#include <xcb/randr.h>
#include "util.hpp"

enum Position {
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

enum struct PointerAction {
  Nothing,
  Focus,
  Move,
  ResizeCorner,
  ResizeSide,
};

enum struct Buttons { //
  Left,
  Middle,
  Right,
  None,
  Any,
  Count
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

enum struct WindowType {
  Desktop,
  Dock,
  Toolbar,
  Menu,
  Utility,
  Splash,
  Dialog,
  Dropdown_menu,
  Popup_menu,
  Tooltip,
  Notification,
  Combo,
  Dnd,
  Normal,
};

struct Coordinates {
  int16_t x, y;
};

struct Dimensions {
  uint16_t width = 0, height = 0;
};

struct Geometry {
  int16_t x = 0, y = 0;
  uint16_t width = 0, height = 0;

  Geometry(int16_t x = 0, int16_t y = 0, uint16_t w = 0, uint16_t h = 0)
    : x(x), y(y), width(w), height(h)
  {}

  Geometry& operator=(Geometry const&) = default;
  ~Geometry()                          = default;

  Geometry& operator=(Dimensions d) noexcept
  {
    width  = d.width;
    height = d.height;
    return *this;
  }

  Geometry& operator=(Coordinates c) noexcept
  {
    x = c.x;
    y = c.y;
    return *this;
  }

  Coordinates position(Position corner) const noexcept;
  bool overlaps(Geometry b) const noexcept;
  float angle_to(Geometry b) const noexcept;
  float distance(Geometry b) const noexcept;
};

struct WindowGeom : Geometry {
  WindowGeom(int16_t x  = 0,
             int16_t y  = 0,
             uint16_t w = 0,
             uint16_t h = 0,
             bool sbu   = false)
    : Geometry{x, y, w, h}, set_by_user(sbu)
  {}
  using Geometry::operator=;
  using Geometry::Geometry;

  WindowGeom(Geometry const& b, bool sbu = false)
    : Geometry(b), set_by_user(sbu)
  {}

  bool set_by_user = false;
};

struct Monitor {
  xcb_randr_output_t monitor;
  char* name;
  Geometry geom;
};

struct Workspace;

struct Client {
  const xcb_window_t window;
  const WindowType window_type;
  WindowGeom geom;
  std::optional<WindowGeom> orig_geom;
  bool fullscreen = false;
  bool hmaxed     = false;
  bool vmaxed     = false;
  Monitor* monitor;
  uint16_t min_width, min_height;
  uint16_t max_width, max_height;
  uint16_t width_inc  = 1;
  uint16_t height_inc = 1;
  bool mapped;
  bool should_map      = true;
  bool user_set_map    = true;
  bool user_set_unmap  = true;
  bool allow_offscreen = false;
  Workspace* workspace;
  int border_width      = 0;
  uint32_t border_color = 0;

  operator xcb_window_t() const
  {
    return window;
  }

  static Client make(xcb_window_t window, WindowType type)
  {
    return Client(window, type);
  }

  Client(Client&)  = delete;
  Client(Client&&) = default;

private:
  Client(xcb_window_t window, WindowType type)
    : window(window), window_type(type)
  {}
};

struct Workspace {
  const uint32_t index;
  bool bar_shown = true;
  nomove_vector<Client> windows;

  static Workspace make(uint32_t index)
  {
    return Workspace(index);
  }

  Workspace(Workspace&&) = default;
  Workspace(Workspace&)  = delete;

private:
  Workspace(uint32_t index) : index(index) {}
};

inline bool operator==(const Workspace& rhs, const Workspace& lhs)
{
  return rhs.index == lhs.index;
}

inline bool operator!=(const Workspace& rhs, const Workspace& lhs)
{
  return rhs.index != lhs.index;
}

inline bool operator==(const Client& rhs, const Client& lhs)
{
  return rhs.window == lhs.window;
}

inline bool operator!=(const Client& rhs, const Client& lhs)
{
  return rhs.window != lhs.window;
}

inline bool operator==(const Client& rhs, xcb_window_t window)
{
  return rhs.window == window;
}

inline bool operator==(xcb_window_t window, const Client& lhs)
{
  return window == lhs.window;
}

inline bool operator!=(const Client& rhs, xcb_window_t window)
{
  return rhs.window != window;
}

inline bool operator!=(xcb_window_t window, const Client& lhs)
{
  return window != lhs.window;
}



inline bool operator==(const Monitor& rhs, const Monitor& lhs)
{
  return rhs.monitor == lhs.monitor;
}

inline bool operator!=(const Monitor& rhs, const Monitor& lhs)
{
  return rhs.monitor != lhs.monitor;
}

struct Conf {
  int8_t border_width, grid_gap;
  int8_t gap_left, gap_down, gap_up, gap_right;
  uint32_t focus_color, unfocus_color;
  enum Position cursor_position;
  uint32_t workspaces;
  bool sloppy_focus;
  bool resize_hints;
  bool sticky_windows;
  bool borders;
  bool last_window_focusing;
  bool apply_settings;
  bool replay_click_on_focus;
  bool bar_shown;
  std::array<uint32_t, 4> bar_padding;
  std::array<PointerAction, underlying(Buttons::Count)> pointer_actions;
  uint16_t pointer_modifier;
  int8_t click_to_focus;
};
