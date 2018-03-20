/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for
 * full license information. */

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "common.hpp"
#include "config.hpp"
#include "ipc.hpp"
#include "types.hpp"
#include "util.hpp"
#include "wm.hpp"
#include "xcb.hpp"

#include <err.h>
#include <sys/wait.h>
#include <unistd.h>

#define PI 3.14159265

namespace wm {

  // Definitions
  Conf conf;

  bool halt;
  bool should_close;
  int exit_code;

  namespace {
    std::vector<Workspace> _workspaces;
    Workspace* _current_ws;

    /* Bar windows */
    nomove_vector<Client> bar_list;
    /* Windows to keep on top */
    std::vector<xcb_window_t> _on_top;

    /* function handlers for events received from the X server */
    void (*events[xcb::last_xcb_event + 1])(xcb_generic_event_t*);
  } // namespace

  std::vector<Workspace>& workspaces() noexcept
  {
    return _workspaces;
  }

  std::vector<xcb_window_t>& on_top() noexcept
  {
    return _on_top;
  }

  Workspace& get_workspace(int idx)
  {
    if (idx > _workspaces.size()) {
      DMSG("Attempt to access workspace %d. Only %d exist", idx,
           (int) _workspaces.size());
      throw std::runtime_error("Out of bounds");
    }
    return _workspaces[idx];
  }

  Workspace& current_ws() noexcept
  {
    return *_current_ws;
  }

  /// Gracefully disconnect.
  void cleanup()
  {
    xcb_set_input_focus(xcb::conn(), XCB_NONE, XCB_INPUT_FOCUS_POINTER_ROOT,
                        XCB_CURRENT_TIME);
    ungrab_buttons();
    xcb::cleanup();
  }

  /// Connect to the X server and initialize some things.
  int setup()
  {
    if (auto errc = xcb::init(); errc != 0) return errc;

    xcb::set_number_of_desktops(conf.workspaces);
    // workspaces.reserve(conf.workspaces);
    for (uint32_t i = 0; i < conf.workspaces; i++) {
      _workspaces.push_back(Workspace::make(i));
    }
    _current_ws = &_workspaces[0];
    return 0;
  }

  /// Get information about a certain monitor situated in a window: coordinates
  /// and size.
  Geometry get_monitor_size(Client& client, bool include_padding)
  {
    Geometry res;
    if (client.monitor == nullptr) {
      res.x = res.y = 0;
      res           = xcb::get_screen_size();
    } else {
      res = client.monitor->geom;
    }
    if (!include_padding) {
      return res;
    }

    auto& workspace = *client.workspace;
    if (show_bar(workspace)) {
      res.x += conf.bar_padding[0];
      res.y += conf.bar_padding[1];

      res.width -= conf.bar_padding[0] + conf.bar_padding[2];

      res.height -= conf.bar_padding[1] + conf.bar_padding[3];
    }
    return res;
  }

  /// Arrange clients on a monitor.
  void arrange_by_monitor(Monitor& mon)
  {
    for (auto& client : current_ws().windows) {
      if (client.monitor == &mon) {
        fit_on_screen(client);
      }
    }
  }


  /// Wait for events and handle them.
  void run()
  {
    halt         = false;
    should_close = false;
    exit_code    = EXIT_SUCCESS;
    while (!halt) {
      xcb::flush();
      auto ev = xcb::wait_for_event();
      if (should_close) {
        if (std::none_of(std::begin(_workspaces), std::end(_workspaces),
                         [](auto& ws) { return ws.windows.size() > 0; })) {
          halt = true;
        }
      }
      if (ev != nullptr) {
        DMSG("X Event %d\n", ev->response_type & ~0x80);
        if (events[EVENT_MASK(ev->response_type)] != nullptr) {
          (events[EVENT_MASK(ev->response_type)])(ev.get());
        }
      }
    }
  }

  /// Initialize a window for further work.
  Client* setup_window(xcb_window_t win, bool require_type)
  {
    try {
      Client client = xcb::make_client(win, require_type);
      bool is_bar   = false;
      bool map      = false;
      bool ignore   = require_type;

      switch (client.window_type) {
      case WindowType::Toolbar:
      case WindowType::Dock:
        is_bar = true;
        ignore = false;
        break;
      case WindowType::Notification: _on_top.push_back(client); [[fallthrough]];
      case WindowType::Desktop:
        map    = true;
        ignore = true;
        break;
      default: break;
      }
      if (map) {
        xcb::map_window(win);
      }
      if (ignore) {
        return nullptr;
      }
      if (is_bar) {
        bar_list.erase(client);
        bar_list.push_back(std::move(client));
        update_bar_visibility();
        return nullptr;
      }
      current_ws().windows.erase(client);
      return &current_ws().windows.push_back(std::move(client));
    } catch (std::runtime_error) {
      return nullptr;
    }
  }

  Client* focused_client()
  {
    auto iter =
      std::find_if(current_ws().windows.rbegin(), current_ws().windows.rend(),
                   [](Client& cl) { return cl.mapped; });
    if (iter == current_ws().windows.rend()) {
      return nullptr;
    }
    return &*iter;
  }

  /// Focus and raise.
  void set_focused(Client& client, bool raise)
  {
    // move the window to the back of the vector
    current_ws().windows.rotate_to_back(std::find(
      current_ws().windows.begin(), current_ws().windows.end(), client));

    xcb::set_focused(client, conf.click_to_focus, conf.pointer_actions,
                     conf.pointer_modifier);

    refresh_borders();

    if (raise) xcb::raise_window(client);
  }

  /// Focus last best focus (in a valid workspace, mapped, etc)
  void set_focused_last_best()
  {
    auto iter =
      std::find_if(current_ws().windows.rbegin(), current_ws().windows.rend(),
                   [](Client& cl) { return cl.mapped; });
    if (iter != current_ws().windows.rend()) {
      set_focused(*iter);
    }
  }

  /// Resizes window by a certain amount.
  void resize_window(Client& client, int16_t w, int16_t h)
  {
    int32_t aw, ah;

    aw = client.geom.width;
    ah = client.geom.height;

    if (aw + w > 0) {
      aw += w;
    }
    if (ah + h > 0) {
      ah += h;
    }

    /* avoid weird stuff */
    if (aw < 0) {
      aw = 0;
    }
    if (ah < 0) {
      ah = 0;
    }

    // if (client.min_width != 0 && aw < client.min_width)
    // aw = client.min_width;
    //
    // if (client.min_height != 0 && ah < client.min_height)
    // ah = client.min_height;

    client.geom.width =
      aw - static_cast<int>(conf.resize_hints) * (aw % client.width_inc);
    client.geom.height =
      ah - static_cast<int>(conf.resize_hints) * (ah % client.height_inc);

    xcb::resize_window_absolute(client, client.geom.width, client.geom.height);
  }

  /// Fit window on screen if too big.
  void fit_on_screen(Client& client)
  {
    if (client.allow_offscreen) {
      xcb::apply_client_geometry(client);
      return;
    }

    if (is_maxed(client)) {
      refresh_maxed(client);
      return;
    }
    auto mon_geom = get_monitor_size(client);
    if (client.geom.width == mon_geom.width &&
        client.geom.height == mon_geom.height) {
      client.geom.x = mon_geom.x;
      client.geom.y = mon_geom.y;
      client.geom.width -= 2 * conf.border_width;
      client.geom.height -= 2 * conf.border_width;
      maximize_window(client);
      return;
    }

    /* Is it outside the display? */
    if (client.geom.x > mon_geom.x + mon_geom.width ||
        client.geom.y > mon_geom.y + mon_geom.height ||
        client.geom.x < mon_geom.x || client.geom.y < mon_geom.y) {
      if (client.geom.x > mon_geom.x + mon_geom.width) {
        client.geom.x = mon_geom.x + mon_geom.width - client.geom.width -
                        2 * conf.border_width;
      } else if (client.geom.x < mon_geom.x) {
        client.geom.x = mon_geom.x;
      }
      if (client.geom.y > mon_geom.y + mon_geom.height) {
        client.geom.y = mon_geom.y + mon_geom.height - client.geom.height -
                        2 * conf.border_width;
      } else if (client.geom.y < mon_geom.y) {
        client.geom.y = mon_geom.y;
      }
    }

    /* Is it smaller than it wants to be? */
    // if (client.min_width != 0 && client.geom.width < client.min_width) {
    // client.geom.width = client.min_width;
    // will_resize        = true;
    // }
    // if (client.min_height != 0 && client.geom.height < client.min_height) {
    // client.geom.height = client.min_height;
    //
    // will_resize = true;
    // }

    /* If the window is larger than the screen or is a bit in the outside,
     * move it to the corner and resize it accordingly. */
    if (client.geom.width + 2 * conf.border_width > mon_geom.width) {
      client.geom.x     = mon_geom.x;
      client.geom.width = mon_geom.width - 2 * conf.border_width;
    } else if (client.geom.x + client.geom.width + 2 * conf.border_width >
               mon_geom.x + mon_geom.width) {
      client.geom.x =
        mon_geom.x + mon_geom.width - client.geom.width - 2 * conf.border_width;
    }

    if (client.geom.height + 2 * conf.border_width > mon_geom.height) {
      client.geom.y      = mon_geom.y;
      client.geom.height = mon_geom.height - 2 * conf.border_width;
    } else if (client.geom.y + client.geom.height + 2 * conf.border_width >
               mon_geom.y + mon_geom.height) {
      client.geom.y = mon_geom.y + mon_geom.height - client.geom.height -
                      2 * conf.border_width;
    }

    xcb::apply_client_geometry(client);
  }

  void refresh_maxed(Client& client)
  {
    if (client.fullscreen || client.vmaxed || client.hmaxed) {
      if (client.fullscreen) {
        fullscreen_window(client);
        return;
      }
      if (client.hmaxed) {
        hmaximize_window(client);
      }
      if (client.vmaxed) {
        vmaximize_window(client);
      }
    } else {
      unmaximize_window(client);
    }
  }

  void fullscreen_window(Client& client)
  {
    auto mon_geom = get_monitor_size(client, false);

    if (client.geom.width != mon_geom.width ||
         client.geom.height != mon_geom.height) {
      save_original_size(client, false);
    }

    client.border_width = 0;
    client.fullscreen = true;
    client.geom = mon_geom;
    xcb::apply_borders(client);
    xcb::apply_state(client);
    xcb::apply_client_geometry(client);
  }

  void maximize_window(Client& client)
  {
    auto mon_geom = get_monitor_size(client);

    if (client.geom.width != mon_geom.width ||
         client.geom.height != mon_geom.height) {
      save_original_size(client, false);
    }

    client.border_width = 0;
    client.geom = mon_geom;
    client.vmaxed = true;
    client.hmaxed = true;

    xcb::apply_borders(client);
    xcb::apply_client_geometry(client);
    xcb::apply_state(client);
  }

  void hmaximize_window(Client& client)
  {
    if (client.vmaxed) {
      maximize_window(client);
      return;
    }
    unmaximize_geometry(client);

    auto mon_geom = get_monitor_size(client);

    if (client.geom.width != mon_geom.width) {
      save_original_size(client);
    }
    client.geom.x = mon_geom.x + conf.gap_left;
    client.geom.width =
      mon_geom.width - conf.gap_left - conf.gap_right - 2 * conf.border_width;
    client.hmaxed = true;
    xcb::apply_client_geometry(client);
    xcb::apply_state(client);
  }

  void vmaximize_window(Client& client)
  {
    if (client.hmaxed) {
      maximize_window(client);
      return;
    }
    unmaximize_geometry(client);

    auto mon_geom = get_monitor_size(client);

    if (client.geom.height != mon_geom.height) {
      save_original_size(client);
    }

    client.geom.y = mon_geom.y + conf.gap_up;
    client.geom.height =
      mon_geom.height - conf.gap_up - conf.gap_down - 2 * conf.border_width;

    client.vmaxed = true;
    xcb::apply_client_geometry(client);
    xcb::apply_state(client);
  }

  void save_original_size(Client& client, bool overwrite)
  {
    DMSG("Saving original geometry for 0x%08x\n", client.window);
    if (overwrite || !client.orig_geom.has_value())
      client.orig_geom      = client.geom;
  }

  void unmaximize_geometry(Client& client)
  {
    if (client.orig_geom.has_value()) {
      DMSG("Restoring original geometry for 0x%08x\n", client.window);
      client.geom       = client.orig_geom.value();
      client.orig_geom  = std::nullopt;
    }
    client.fullscreen = client.hmaxed = client.vmaxed = false;
  }

  void unmaximize_window(Client& client)
  {
    if (client.fullscreen) {
      client.fullscreen = false;
      refresh_maxed(client);
      return;
    }
    unmaximize_geometry(client);
    xcb::apply_client_geometry(client);
    xcb::apply_state(client);
    refresh_borders(client);
  }

  bool is_maxed(Client& client)
  {
    return client.fullscreen || client.vmaxed || client.hmaxed;
  }

  void cycle_window(Client& client)
  {
    auto iter = std::find(current_ws().windows.begin(),
                          current_ws().windows.end(), client);
    if (iter == current_ws().windows.end()) {
      return;
    }
    iter =
      std::find_if(iter, current_ws().windows.end(),
                   [&client](Client& cl) { return cl.mapped && cl != client; });
    if (iter != current_ws().windows.end()) {
      set_focused(*iter);
      return;
    }
    iter =
      std::find_if(current_ws().windows.begin(), current_ws().windows.end(),
                   [](Client& cl) { return cl.mapped; });
    if (iter != current_ws().windows.end()) {
      set_focused(*iter);
    }
  }

  void rcycle_window(Client& client)
  {
    auto iter = std::find(current_ws().windows.rbegin(),
                          current_ws().windows.rend(), client);
    if (iter == current_ws().windows.rend()) {
      return;
    }
    iter =
      std::find_if(iter, current_ws().windows.rend(),
                   [&client](Client& cl) { return cl.mapped && cl != client; });
    if (iter != current_ws().windows.rend()) {
      set_focused(*iter);
      return;
    }
    iter =
      std::find_if(current_ws().windows.rbegin(), current_ws().windows.rend(),
                   [](Client& cl) { return cl.mapped; });
    if (iter != current_ws().windows.rend()) {
      set_focused(*iter);
    }
  }

  void cardinal_focus(uint32_t dir)
  {
    /* Don't focus if we don't have a current focus! */
    if (focused_client() == nullptr) {
      return;
    }
    Client& focused = *focused_client();

    auto focus_win_pos = focused.geom.position(CENTER);
    std::vector<Client*> valid_windows;
    for (Client& cl : current_ws().windows) {
      if (cl == focused) {
        continue;
      }
      if (!cl.mapped) {
        continue;
      }

      auto win_pos = cl.geom.position(CENTER);

      switch (dir) {
      case NORTH:
        if (win_pos.y < focus_win_pos.y) {
          valid_windows.push_back(&cl);
        }
      case SOUTH:
        if (win_pos.y >= focus_win_pos.y) {
          valid_windows.push_back(&cl);
        }
        break;
      case WEST:
        if (win_pos.x < focus_win_pos.x) {
          valid_windows.push_back(&cl);
        }
        break;
      case EAST:
        if (win_pos.x >= focus_win_pos.x) {
          valid_windows.push_back(&cl);
        }
        break;
      }
    }

    float closest_distance = -1;
    float closest_angle;
    Client* desired_window = nullptr;
    for (Client* cl : valid_windows) {
      float cur_distance;
      float cur_angle;

      cur_distance = focused.geom.distance(cl->geom);
      cur_angle    = focused.geom.angle_to(cl->geom);

      if (is_in_valid_direction(dir, cur_angle, 10)) {
        if (focused.geom.overlaps(cl->geom)) {
          cur_distance = cur_distance * 0.1;
        }
        cur_distance = cur_distance * 0.80;
      } else if (is_in_valid_direction(dir, cur_angle, 25)) {
        if (focused.geom.overlaps(cl->geom)) {
          cur_distance = cur_distance * 0.1;
        }
        cur_distance = cur_distance * 0.85;
      } else if (is_in_valid_direction(dir, cur_angle, 35)) {
        if (focused.geom.overlaps(cl->geom)) {
          cur_distance = cur_distance * 0.1;
        }
        cur_distance = cur_distance * 0.9;
      } else if (is_in_valid_direction(dir, cur_angle, 50)) {
        if (focused.geom.overlaps(cl->geom)) {
          cur_distance = cur_distance * 0.1;
        }
        cur_distance = cur_distance * 3;
      } else {
        continue;
      }

      if (is_in_cardinal_direction(dir, focused, *cl)) {
        cur_distance = cur_distance * 0.9;
      }

      if (closest_distance == -1 || (cur_distance < closest_distance)) {
        closest_distance = cur_distance;
        closest_angle    = cur_angle;
        desired_window   = cl;
      }
    }

    if (desired_window != nullptr) {
      set_focused(*desired_window);
    }
  }

  /// Get the nearest opposing edge for a client in a direction
  ///
  /// \param invert Get the nearest edge from client's border in the opposite end but direction `dir`
  static Coordinates nearest_edge(Client& client, direction dir, bool invert = false)
  {
    auto mon_geom = get_monitor_size(client);
    auto top_left = client.geom.position(Position::TOP_LEFT);
    auto bottom_right = client.geom.position(Position::BOTTOM_RIGHT);
    if (invert) {
      switch (dir) {
      case direction::NORTH: dir = direction::SOUTH; break;
      case direction::SOUTH: dir = direction::NORTH; break;
      case direction::WEST: dir = direction::EAST; break;
      case direction::EAST: dir = direction::WEST; break;
      }
      std::swap(top_left, bottom_right);
    }
    auto res = client.geom.position(Position::TOP_LEFT);
    switch (dir) {
    case direction::NORTH: {
      res.y   = mon_geom.y -2 * conf.border_width;
      int16_t max = top_left.y - 2 * conf.border_width;
      for (Client& cl2 : current_ws().windows) {
        auto y2 = cl2.geom.position(Position::BOTTOM_RIGHT).y;
        if (y2 < max) res.y = std::max(y2, res.y);
      }
      res.y += 2 * conf.border_width;
    } break;
    case direction::SOUTH: {
      res.y   = mon_geom.y + mon_geom.height;
      int16_t min = bottom_right.y + 2 * conf.border_width;
      for (Client& cl2 : current_ws().windows) {
        auto y2 = cl2.geom.position(Position::TOP_LEFT).y;
        if (y2 > min) res.y = std::min(y2, res.y);
      }
      res.y -= 2 * conf.border_width;
    } break;
    case direction::WEST: {
      res.x   = mon_geom.x -2 * conf.border_width;
      int16_t max = top_left.x - 2 * conf.border_width;
      for (Client& cl2 : current_ws().windows) {
        auto x2 = cl2.geom.position(Position::BOTTOM_RIGHT).x;
        if (x2 < max) res.x = std::max(x2, res.x);
      }
      res.x += 2 * conf.border_width;
    } break;
    case direction::EAST: {
      res.x = mon_geom.x + mon_geom.width;
      int16_t min = bottom_right.x + 2 * conf.border_width;
      for (Client& cl2 : current_ws().windows) {
        auto x2 = cl2.geom.position(Position::TOP_LEFT).x;
        if (x2 > min) res.x = std::min(x2, res.x);
      }
      res.x -= 2 * conf.border_width;
    } break;
    }
    return res;
  }

  void cardinal_move(Client& client, direction dir)
  {
    client.geom = nearest_edge(client, dir);
    switch (dir) {
    case direction::NORTH: break;
    case direction::SOUTH:
      client.geom.y -= client.geom.height;
      break;
    case direction::WEST: break;
    case direction::EAST:
      client.geom.x -= client.geom.width;
      break;
    }
    xcb::apply_client_geometry(client);
  }

  void cardinal_resize(Client& client, direction dir, bool shrink)
  {
    auto tl = client.geom.position(Position::TOP_LEFT);
    auto br = client.geom.position(Position::BOTTOM_RIGHT);
    auto edge = nearest_edge(client, dir, shrink);
    switch (dir) {
    case direction::NORTH:
      client.geom.y = edge.y;
      client.geom.height += tl.y - edge.y;
      break;
    case direction::SOUTH:
      client.geom.height = edge.y - tl.y;
      break;
    case direction::WEST:
      client.geom.x = edge.x;
      client.geom.width += tl.x - edge.x;
      break;
    case direction::EAST:
      client.geom.width = edge.x - tl.x;
      break;
    }
    xcb::apply_client_geometry(client);
  }

  bool is_in_cardinal_direction(uint32_t direction, Client& a, Client& b)
  {
    auto pos_a_top_left  = a.geom.position(TOP_LEFT);
    auto pos_a_top_right = a.geom.position(TOP_RIGHT);
    auto pos_a_bot_left  = a.geom.position(BOTTOM_LEFT);

    auto pos_b_center = b.geom.position(CENTER);

    switch (direction) {
    case NORTH:
    case SOUTH:
      return pos_a_top_left.x <= pos_b_center.x &&
             pos_a_top_right.x >= pos_b_center.x;

    case WEST:
    case EAST:
      return pos_a_top_left.y <= pos_b_center.y &&
             pos_a_bot_left.y >= pos_b_center.y;
    }

    return false;
  }

  bool is_in_valid_direction(uint32_t direction,
                             float window_direction,
                             float delta)
  {
    switch ((uint32_t) direction) {
    case NORTH:
      if (window_direction >= (180 - delta) ||
          window_direction <= (-180 + delta)) {
        return true;
      }
      break;
    case SOUTH:
      if (std::abs(window_direction) <= (0 + delta)) {
        return true;
      }
      break;
    case EAST:
      if (window_direction <= (90 + delta) && window_direction > (90 - delta)) {
        return true;
      }
      break;
    case WEST:
      if (window_direction <= (-90 + delta) &&
          window_direction >= (-90 - delta)) {
        return true;
      }
      break;
    }

    return false;
  }

  void center_pointer(Client& client)
  {
    xcb::warp_pointer(client, client.geom.position(conf.cursor_position));
    xcb::flush();
  }

  /// Get the client instance with a given window id.
  Client* find_client(xcb_window_t& win)
  {
    auto iter =
      std::find_if(current_ws().windows.begin(), current_ws().windows.end(),
                   [win](auto& cl) { return cl.window == win; });
    if (iter != current_ws().windows.end()) {
      return &*iter;
    }
    return nullptr;
  }

  /// Deletes and frees a client from the list.
  void free_window(Client& cl)
  {
    DMSG("freeing 0x%08x\n", cl.window);
    current_ws().windows.erase(cl);
    refresh_borders();
  }

  /// Adds all windows to the ewmh client list.
  void update_client_list()
  {
    xcb::clear_client_list();
    for (auto& client : current_ws().windows) {
      if (client.mapped) {
        xcb::add_to_client_list(client);
      }
    }
  }

  void workspace_add_window(Client& client, Workspace& workspace)
  {
    auto* old_ws     = client.workspace;
    client.workspace = &workspace;
    if (old_ws == nullptr) return;
    auto uptr = old_ws->windows.erase(client);
    if (uptr == nullptr) return;
    workspace.windows.push_back(std::move(uptr));
    xcb::apply_workspace(client);
    workspace_goto(current_ws());
  }

  //  void workspace_remove_window(Client& client)
  //  {
  //      client.workspace = nullptr;
  //      update_wm_desktop(client);
  //    }
  //  }
  //
  //  void workspace_remove_all_windows(uint32_t workspace)
  //  {
  //    if (workspace >= conf.workspaces) return;
  //
  //    for (auto& cl : current_ws().windows) {
  //      if (cl.workspace == workspace) workspace_remove_window(&cl);
  //    }
  //  }

  void workspace_goto(Workspace& workspace)
  {
    _current_ws = &workspace;

    // TODO: Instead of this, refresh the clients manually
    for (auto& ws : _workspaces) {
      if (ws == current_ws()) continue;
      for (auto& win : ws.windows) {
        win.user_set_unmap = false;
        xcb::unmap_window(win);
      }
    }


    Client* last_win = nullptr;
    for (auto& win : current_ws().windows) {
      if (win.should_map) {
        win.user_set_map = false;
        xcb::map_window(win);
        refresh_maxed(win);
        last_win = &win;
      } else {
        win.user_set_unmap = false;
        xcb::unmap_window(win);
      }
    }

    if (focused_client() == nullptr && last_win != nullptr) {
      set_focused(*last_win);
    }

    refresh_borders();

    xcb::set_current_desktop(current_ws().index);
    update_bar_visibility();
    update_client_list();
  }

  bool show_bar(Workspace& ws)
  {
    return ws.bar_shown;
    // || std::none_of(current_ws().windows.begin(), current_ws().windows.end(),
    // [] (Client& cl) {
    //     return cl.mapped;
    //   });
  }

  void update_bar_visibility()
  {
    if (show_bar()) {
      for (auto& win : bar_list) {
        xcb::map_window(win);
      }
    } else {
      for (auto& win : bar_list) {
        xcb::unmap_window(win);
      }
    }
  }

  //  void change_nr_of_workspaces(uint32_t n_workspaces)
  //  {
  //    if (n_workspaces < conf.workspaces) {
  //      for (auto& win : current_ws().windows) {
  //        if (win.workspace >= n_workspaces) {
  //          workspace_remove_window(&win);
  //        }
  //      }
  //    }
  //
  //    workspaces.resize(n_workspaces);
  //    conf.workspaces = n_workspaces;
  //  }

  void refresh_borders(Client& client, Client* focused)
  {
    if (client.fullscreen || (client.hmaxed && client.vmaxed)) {
      client.border_width = 0;
    } else {
      client.border_width = conf.border_width;
    }

    if (&client == focused) {
      client.border_color = conf.focus_color;
    } else {
      client.border_color = conf.unfocus_color;
    }
    xcb::apply_borders(client);
  }

  void refresh_borders()
  {
    if (!conf.apply_settings) {
      return;
    }

    auto* focused = focused_client();

    for (auto& win : current_ws().windows) {
      refresh_borders(win, focused);
    }
  }

  /// Snap window in corner.
  void snap_window(Client& client, enum Position pos)
  {
    if (is_maxed(client)) {
      unmaximize_window(client);
      set_focused(client);
    }

    fit_on_screen(client);

    int16_t win_x  = client.geom.x;
    int16_t win_y  = client.geom.y;
    uint16_t win_w = client.geom.width + 2 * conf.border_width;
    uint16_t win_h = client.geom.height + 2 * conf.border_width;

    auto mon_geom = get_monitor_size(client);

    switch (pos) {
    case TOP_LEFT:
      win_x = mon_geom.x + conf.gap_left;
      win_y = mon_geom.y + conf.gap_up;
      break;

    case TOP_RIGHT:
      win_x = mon_geom.x + mon_geom.width - conf.gap_right - win_w;
      win_y = mon_geom.y + conf.gap_up;
      break;

    case BOTTOM_LEFT:
      win_x = mon_geom.x + conf.gap_left;
      win_y = mon_geom.y + mon_geom.height - conf.gap_down - win_h;
      break;

    case BOTTOM_RIGHT:
      win_x = mon_geom.x + mon_geom.width - conf.gap_right - win_w;
      win_y = mon_geom.y + mon_geom.height - conf.gap_down - win_h;
      break;

    case CENTER:
      win_x = mon_geom.x + (mon_geom.width - win_w) / 2;
      win_y = mon_geom.y + (mon_geom.height - win_h) / 2;
      break;

    default: return;
    }

    client.geom.x = win_x;
    client.geom.y = win_y;
    xcb::apply_client_geometry(client);
  }

  /// Put window in grid.
  void grid_window(Client& client,
                   uint32_t grid_width,
                   uint32_t grid_height,
                   uint32_t grid_x,
                   uint32_t grid_y)
  {
    if (grid_x >= grid_width || grid_y >= grid_height) {
      return;
    }

    if (is_maxed(client)) {
      unmaximize_window(client);
      set_focused(client);
    }

    auto mon_geom = get_monitor_size(client);

    /* calculate new window size */
    uint16_t new_w =
      (mon_geom.width - conf.gap_left - conf.gap_right -
       (grid_width - 1) * conf.grid_gap - grid_width * 2 * conf.border_width) /
      grid_width;

    uint16_t new_h = (mon_geom.height - conf.gap_up - conf.gap_down -
                      (grid_height - 1) * conf.grid_gap -
                      grid_height * 2 * conf.border_width) /
                     grid_height;

    client.geom.width  = new_w;
    client.geom.height = new_h;

    client.geom.x =
      mon_geom.x + conf.gap_left +
      grid_x * (conf.border_width + new_w + conf.border_width + conf.grid_gap);
    client.geom.y =
      mon_geom.y + conf.gap_up +
      grid_y * (conf.border_width + new_h + conf.border_width + conf.grid_gap);

    DMSG("w: %d\th: %d\n", new_w, new_h);

    xcb::apply_client_geometry(client);
  }

  /// Adds X event handlers to the array.
  void register_event_handlers()
  {
    for (int i = 0; i <= xcb::last_xcb_event; i++) {
      events[i] = nullptr;
    }

    events[XCB_CONFIGURE_REQUEST] = event_configure_request;
    events[XCB_DESTROY_NOTIFY]    = event_destroy_notify;
    events[XCB_ENTER_NOTIFY]      = event_enter_notify;
    events[XCB_MAP_REQUEST]       = event_map_request;
    events[XCB_MAP_NOTIFY]        = event_map_notify;
    events[XCB_UNMAP_NOTIFY]      = event_unmap_notify;
    events[XCB_CLIENT_MESSAGE]    = event_client_message;
    events[XCB_CONFIGURE_NOTIFY]  = event_configure_notify;
    events[XCB_FOCUS_IN]          = event_focus_in;
    events[XCB_FOCUS_OUT]         = event_focus_out;
    events[XCB_BUTTON_PRESS]      = event_button_press;
  }

  /// A window wants to be configured.
  void event_configure_request(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_configure_request_event_t*) ev;
    DMSG("Configure request event: %d\n", e->window);
    Client* client;
    uint32_t values[7];
    int i = 0;

    client = find_client(e->window);
    if (client != nullptr) {
      if (((e->value_mask & XCB_CONFIG_WINDOW_X) != 0) && !client->fullscreen &&
          !client->hmaxed) {
        client->geom.x = e->x;
      }

      if (((e->value_mask & XCB_CONFIG_WINDOW_Y) != 0) && !client->fullscreen &&
          !client->vmaxed) {
        client->geom.y = e->y;
      }

      if (((e->value_mask & XCB_CONFIG_WINDOW_WIDTH) != 0) &&
          !client->fullscreen && !client->hmaxed) {
        client->geom.width = e->width;
      }

      if (((e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) != 0) &&
          !client->fullscreen && !client->vmaxed) {
        client->geom.height = e->height;
      }

      if ((e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) != 0) {
        values[0] = e->stack_mode;
        xcb_configure_window(xcb::conn(), e->window,
                             XCB_CONFIG_WINDOW_STACK_MODE, values);
      }

      if (!client->fullscreen) {
        fit_on_screen(*client);
      }

      xcb::apply_client_geometry(*client);
      refresh_borders(*client);
    } else {
      if ((e->value_mask & XCB_CONFIG_WINDOW_X) != 0) {
        values[i] = e->x;
        i++;
      }

      if ((e->value_mask & XCB_CONFIG_WINDOW_Y) != 0) {
        values[i] = e->y;
        i++;
      }

      if ((e->value_mask & XCB_CONFIG_WINDOW_WIDTH) != 0) {
        values[i] = e->width;
        i++;
      }

      if ((e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) != 0) {
        values[i] = e->height;
        i++;
      }

      if ((e->value_mask & XCB_CONFIG_WINDOW_SIBLING) != 0) {
        values[i] = e->sibling;
        i++;
      }

      if ((e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) != 0) {
        values[i] = e->stack_mode;
        i++;
      }

      if (i == 0) {
        return;
      }
      xcb_configure_window(xcb::conn(), e->window, e->value_mask, values);
    }
  }

  /// Window has been destroyed.
  void event_destroy_notify(xcb_generic_event_t* ev)
  {
    Client* client;
    auto* e = (xcb_destroy_notify_event_t*) ev;
    DMSG("Destroy notify event: %d\n", e->window);

    _on_top.erase(std::remove(_on_top.begin(), _on_top.end(), e->window),
                 _on_top.end());
    client = find_client(e->window);

    if (client != nullptr) {
      free_window(*client);
    }

    update_client_list();
    workspace_goto(current_ws());
  }

  /// The mouse pointer has entered the window.
  void event_enter_notify(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_enter_notify_event_t*) ev;
    Client* client;

    DMSG("Enter notify event: %d\n", e->event);

    if (!conf.sloppy_focus) {
      return;
    }

    if (focused_client() != nullptr && e->event == focused_client()->window) {
      return;
    }

    client = find_client(e->event);

    if (client != nullptr) {
      set_focused(*client);
    }
  }

  /// A window wants to show up on the screen.
  void event_map_request(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_map_request_event_t*) ev;
    Client* client;

    DMSG("Map request event: %d\n", e->window);

    /* create window if new */
    client = find_client(e->window);
    if (client == nullptr) {
      client = setup_window(e->window);

      /* client is a dock or some kind of window that needs to be ignored */
      if (client == nullptr) {
        return;
      }

      if (!client->geom.set_by_user) {
        auto ptl     = xcb::get_pointer_location(xcb::root());
        client->geom = ptl.value_or(Coordinates{0, 0});

        client->geom.x -= client->geom.width / 2;
        client->geom.y -= client->geom.height / 2;
        xcb::apply_client_geometry(*client);
      }
      workspace_add_window(*client, current_ws());
    }
    client->should_map = true;

    if (client->workspace == &current_ws()) {
      xcb::map_window(e->window);
    } else {
      workspace_add_window(*client, current_ws());
    }


    /* in case of fire, abort */
    if (client == nullptr) {
      return;
    }

    xcb::assign_monitor(*client);

    fit_on_screen(*client);

    xcb::apply_state(*client);
    update_client_list();

    refresh_borders(*client);
  }

  void event_map_notify(xcb_generic_event_t* ev)
  {
    auto* e        = (xcb_map_notify_event_t*) ev;
    Client* client = find_client(e->window);
    DMSG("Map notify event: %d\n", e->window);

    if (client != nullptr) {
      client->mapped = true;

      if (client->user_set_map) {
        client->should_map = true;
      }
      client->user_set_map = true;
      set_focused(*client);
    }
  }

  /// Window has been unmapped (became invisible).
  void event_unmap_notify(xcb_generic_event_t* ev)
  {
    auto* e        = (xcb_map_request_event_t*) ev;
    Client* client = nullptr;
    DMSG("Unmap event: %d\n", e->window);

    _on_top.erase(std::remove(_on_top.begin(), _on_top.end(), e->window),
                 _on_top.end());
    client = find_client(e->window);
    if (client == nullptr) {
      return;
    }

    client->mapped = false;
    if (client->user_set_unmap) {
      DMSG("User set unmap\n");
      client->should_map = false;
    } else {
      DMSG("WM set unmap\n");
      client->user_set_unmap = true;
    }

    set_focused_last_best();

    update_client_list();
  }

  /// Window has been configured.
  void event_configure_notify(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_configure_notify_event_t*) ev;

    DMSG("confgure notify event: %d\n", e->window);

    /* The root window changes its geometry when the
     * user adds/removes/tilts screens */
    if (e->window == xcb::root()) {
      for (auto& win : current_ws().windows) {
        fit_on_screen(win);
      }
    } else {
      Client* client = find_client(e->window);
      if (client != nullptr) {
        client->monitor =
          xcb::find_monitor_by_coord(client->geom.x, client->geom.y);
      } else {
        setup_window(e->window, true);
      }
    }
  }

  /// Received client message. Either ewmh/icccm thing or
  /// message from the client.
  void event_client_message(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_client_message_event_t*) ev;
    uint32_t ipc_command;
    uint32_t* data;
    Client* client;

    if (e->type == xcb::ATOMS[xcb::_IPC_ATOM_COMMAND] && e->format == 32) {
      /* Message from the client */
      data        = e->data.data32;
      ipc_command = data[0];
      ipc::call_handler(static_cast<ipc::Command>(ipc_command), data + 1);
      DMSG("IPC Command %u with arguments %u %u %u\n", data[1], data[2],
           data[3], data[4]);
    } else {
      client = find_client(e->window);
      if (client == nullptr) {
        return;
      }
      xcb::handle_client_message(*client, e);
      wm::refresh_maxed(*client);
    }
  }

  void event_focus_in(xcb_generic_event_t* ev)
  {
    auto* e          = (xcb_focus_in_event_t*) ev;
    xcb_window_t win = e->event;
  }

  void event_focus_out(xcb_generic_event_t* ev)
  {
    (void) (ev);
    xcb_get_input_focus_reply_t* focus = xcb_get_input_focus_reply(
      xcb::conn(), xcb_get_input_focus(xcb::conn()), nullptr);
    Client* client = nullptr;

    if (focused_client() != nullptr &&
        focus->focus == focused_client()->window) {
      return;
    }

    client = find_client(focus->focus);
    if (client != nullptr) {
      set_focused(*client, false);
    }
  }

  void event_button_press(xcb_generic_event_t* ev)
  {
    auto* e     = (xcb_button_press_event_t*) ev;
    bool replay = false;

    for (int i = 0; i < underlying(Buttons::Count); i++) {
      if (e->detail != xcb::mouse_buttons[i]) {
        continue;
      }
      if ((conf.click_to_focus == (int8_t) XCB_BUTTON_INDEX_ANY ||
           conf.click_to_focus == (int8_t) xcb::mouse_buttons[i]) &&
          (e->state & ~(xcb::num_lock | xcb::scroll_lock | xcb::caps_lock)) ==
            XCB_NONE) {
        replay = !pointer_grab(PointerAction::Focus);
      } else {
        pointer_grab(conf.pointer_actions[i]);
      }
    }
    xcb_allow_events(xcb::conn(),
                     replay ? XCB_ALLOW_REPLAY_POINTER : XCB_ALLOW_SYNC_POINTER,
                     e->time);
    xcb::flush();
  }

  /// Returns true if pointer needs to be synced.
  bool pointer_grab(PointerAction pac)
  {
    xcb_window_t win = XCB_NONE;
    xcb_point_t pos  = (xcb_point_t){0, 0};
    Client* client;

    xcb_query_pointer_reply_t* qr = xcb_query_pointer_reply(
      xcb::conn(), xcb_query_pointer(xcb::conn(), xcb::root()), nullptr);

    if (qr == nullptr) {
      return false;
    }

    win = qr->child;
    pos = (xcb_point_t){qr->root_x, qr->root_y};
    free(qr);

    client = find_client(win);
    if (client == nullptr) {
      return true;
    }

    xcb::raise_window(client->window);
    if (pac == PointerAction::Focus) {
      DMSG("grabbing pointer to focus on 0x%08x\n", client->window);
      if (client != focused_client()) {
        set_focused(*client);
        if (!conf.replay_click_on_focus) {
          return true;
        }
      }
      return false;
    }

    if (is_maxed(*client)) {
      return true;
    }

    xcb_grab_pointer_reply_t* reply = xcb_grab_pointer_reply(
      xcb::conn(),
      xcb_grab_pointer(
        xcb::conn(), 0, xcb::root(),
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
        XCB_CURRENT_TIME),
      nullptr);

    if (reply == nullptr || reply->status != XCB_GRAB_STATUS_SUCCESS) {
      free(reply);
      return true;
    }
    free(reply);

    track_pointer(*client, pac, pos);

    return true;
  }

  enum resize_handle get_handle(Client& client,
                                xcb_point_t pos,
                                PointerAction pac)
  {
    //    if (client == nullptr)
    //      return pac == POINTER_ACTION_RESIZE_SIDE ? HANDLE_LEFT :
    //      HANDLE_TOP_LEFT;

    enum resize_handle handle;
    WindowGeom geom = client.geom;

    if (pac == PointerAction::ResizeSide) {
      /* coordinates relative to the window */
      int16_t x      = pos.x - geom.x;
      int16_t y      = pos.y - geom.y;
      bool left_of_a = (x * geom.height) < (geom.width * y);
      bool left_of_b = ((geom.width - x) * geom.height) > (geom.width * y);

      /* Problem is that the above algorithm works in a 2d system
         where the origin is in the bottom-left. */
      if (left_of_a) {
        if (left_of_b) {
          handle = HANDLE_LEFT;
        } else {
          handle = HANDLE_BOTTOM;
        }
      } else {
        if (left_of_b) {
          handle = HANDLE_TOP;
        } else {
          handle = HANDLE_RIGHT;
        }
      }
    } else if (pac == PointerAction::ResizeCorner) {
      int16_t mid_x = geom.x + geom.width / 2;
      int16_t mid_y = geom.y + geom.height / 2;

      if (pos.y < mid_y) {
        if (pos.x < mid_x) {
          handle = HANDLE_TOP_LEFT;
        } else {
          handle = HANDLE_TOP_RIGHT;
        }
      } else {
        if (pos.x < mid_x) {
          handle = HANDLE_BOTTOM_LEFT;
        } else {
          handle = HANDLE_BOTTOM_RIGHT;
        }
      }
    } else {
      handle = HANDLE_TOP_LEFT;
    }

    return handle;
  }

  void track_pointer(Client& client, PointerAction pac, xcb_point_t pos)
  {
    enum resize_handle handle = get_handle(client, pos, pac);
    WindowGeom geom           = client.geom;

    bool grabbing   = true;
    Client& grabbed = client;

    do {
      xcb::unique_ptr<xcb_generic_event_t> ev;
      while ((ev = xcb::wait_for_event(false)) == nullptr) {
        xcb::flush();
      }
      uint8_t resp = EVENT_MASK(ev->response_type);

      if (resp == XCB_MOTION_NOTIFY) {
        auto* e = (xcb_motion_notify_event_t*) ev.get();
        DMSG(
          "tracking window by mouse root_x = %d  root_y = %d  posx = %d  posy "
          "= %d\n",
          e->root_x, e->root_y, pos.x, pos.y);
        int16_t dx = e->root_x - pos.x;
        int16_t dy = e->root_y - pos.y;
        int32_t x = client.geom.x, y = client.geom.y, width = client.geom.width,
                height = client.geom.height;

        if (pac == PointerAction::Move) {
          client.geom.x = geom.x + dx;
          client.geom.y = geom.y + dy;
          fit_on_screen(client);
        } else if (pac == PointerAction::ResizeSide ||
                   pac == PointerAction::ResizeCorner) {
          DMSG("dx: %d\tdy: %d\n", dx, dy);
          if (conf.resize_hints) {
            dx /= client.width_inc;
            dx *= client.width_inc;

            dy /= client.width_inc;
            dy *= client.width_inc;
            DMSG("we have resize hints\tdx: %d\tdy: %d\n", dx, dy);
          }
          /* oh boy */
          switch (handle) {
          case HANDLE_LEFT:
            x     = geom.x + dx;
            width = geom.width - dx;
            break;
          case HANDLE_BOTTOM: height = geom.height + dy; break;
          case HANDLE_TOP:
            y      = geom.y + dy;
            height = geom.height - dy;
            break;
          case HANDLE_RIGHT: width = geom.width + dx; break;

          case HANDLE_TOP_LEFT:
            y      = geom.y + dy;
            height = geom.height - dy;
            x      = geom.x + dx;
            width  = geom.width - dx;
            break;
          case HANDLE_TOP_RIGHT:
            y      = geom.y + dy;
            height = geom.height - dy;
            width  = geom.width + dx;
            break;
          case HANDLE_BOTTOM_LEFT:
            x      = geom.x + dx;
            width  = geom.width - dx;
            height = geom.height + dy;
            break;
          case HANDLE_BOTTOM_RIGHT:
            width  = geom.width + dx;
            height = geom.height + dy;
            break;
          }

          /* check for overflow */
          // if (width < client.min_width) {
          // width = client.min_width;
          // x     = client.geom.x;
          // }
          //
          // if (height < client.min_height) {
          // height = client.min_height;
          // y      = client.geom.y;
          // }

          auto mon_geom = get_monitor_size(client);
          if (x < mon_geom.x) {
            x = client.geom.x;
          }
          if (y < mon_geom.y) {
            y = client.geom.y;
          }
          if (x + width > mon_geom.x + mon_geom.width) {
            x     = client.geom.x;
            width = client.geom.width;
          }
          if (y + height > mon_geom.y + mon_geom.height) {
            y      = client.geom.y;
            height = client.geom.height;
          }

          DMSG("moving by %d %d\n", x - geom.x, y - geom.y);
          DMSG("resizing by %d %d\n", width - geom.width, height - geom.height);
          client.geom.x      = x;
          client.geom.width  = width;
          client.geom.height = height;
          client.geom.y      = y;

          fit_on_screen(client);
          xcb::flush();
        }
      } else if (resp == XCB_BUTTON_RELEASE) {
        grabbing = false;
      } else {
        if (events[resp] != nullptr) {
          (events[resp])(ev.get());
        }
      }
    } while (grabbing);

    xcb_ungrab_pointer(xcb::conn(), XCB_CURRENT_TIME);
  }

  void grab_buttons()
  {
    for (auto& client : current_ws().windows) {
      xcb::window_grab_buttons(client.window, conf.click_to_focus,
                               conf.pointer_actions, conf.pointer_modifier);
    }
  }

  void ungrab_buttons()
  {
    for (auto& client : current_ws().windows) {
      xcb_ungrab_button(xcb::conn(), XCB_BUTTON_INDEX_ANY, client.window,
                        XCB_MOD_MASK_ANY);
      DMSG("ungrabbed buttons on 0x%08x\n", client.window);
    }
  }

  void usage(char* name)
  {
    fprintf(stderr, "Usage: %s [-h|-v|-c CONFIG_PATH]\n", name);

    exit(EXIT_SUCCESS);
  }

  void version()
  {
    fprintf(stderr, "%s %s\n", __NAME__, __THIS_VERSION__);
    fprintf(stderr, "Copyright (c) 2016-2017 Tudor Ioan Roman\n");
    fprintf(stderr, "Released under the ISC License\n");

    exit(EXIT_SUCCESS);
  }

  void load_defaults()
  {
    conf.border_width          = BORDER_WIDTH;
    conf.focus_color           = COLOR_FOCUS;
    conf.unfocus_color         = COLOR_UNFOCUS;
    conf.gap_left              = GAP;
    conf.gap_down              = GAP;
    conf.gap_up                = GAP;
    conf.gap_right             = GAP;
    conf.grid_gap              = GRID_GAP;
    conf.cursor_position       = CURSOR_POSITION;
    conf.workspaces            = WORKSPACES;
    conf.sloppy_focus          = SLOPPY_FOCUS;
    conf.resize_hints          = RESIZE_HINTS;
    conf.sticky_windows        = STICKY_WINDOWS;
    conf.borders               = BORDERS;
    conf.last_window_focusing  = LAST_WINDOW_FOCUSING;
    conf.apply_settings        = APPLY_SETTINGS;
    conf.replay_click_on_focus = REPLAY_CLICK_ON_FOCUS;
    conf.pointer_actions[underlying(Buttons::Left)] =
      DEFAULT_LEFT_BUTTON_ACTION;
    conf.pointer_actions[underlying(Buttons::Middle)] =
      DEFAULT_MIDDLE_BUTTON_ACTION;
    conf.pointer_actions[underlying(Buttons::Right)] =
      DEFAULT_RIGHT_BUTTON_ACTION;
    conf.bar_shown        = DEFAULT_BAR_SHOWN;
    conf.bar_padding[0]   = BAR_PADDING_LEFT;
    conf.bar_padding[1]   = BAR_PADDING_TOP;
    conf.bar_padding[2]   = BAR_PADDING_RIGHT;
    conf.bar_padding[3]   = BAR_PADDING_BOTTOM;
    conf.pointer_modifier = POINTER_MODIFIER;
    conf.click_to_focus   = CLICK_TO_FOCUS_BUTTON;
  }

  void load_config(char* config_path)
  {
    if (fork() == 0) {
      setsid();
      DMSG("loading %s\n", config_path);
      execl(config_path, config_path, nullptr);
      errx(EXIT_FAILURE, "couldn't load config file");
    }
  }

  void handle_child(int sig)
  {
    if (sig == SIGCHLD) {
      wait(nullptr);
    }
  }
} // namespace wm

using namespace wm;

int main(int argc, char* argv[])
{
  int opt;
  auto* config_path = (char*) malloc(MAXLEN * sizeof(char));
  config_path[0]    = '\0';
  while ((opt = getopt(argc, argv, "hvc:")) != -1) {
    switch (opt) {
    case 'h': usage(argv[0]); break;
    case 'c': snprintf(config_path, MAXLEN * sizeof(char), "%s", optarg); break;
    case 'v': version(); break;
    }
  }
  atexit(cleanup);

  register_event_handlers();
  load_defaults();

  if (setup() < 0) {
    errx(EXIT_FAILURE, "error connecting to X");
  }
  /* if not set, get path of the rc file */
  if (config_path[0] == '\0') {
    char* xdg_home = getenv("XDG_CONFIG_HOME");
    if (xdg_home != nullptr) {
      snprintf(config_path, MAXLEN * sizeof(char), "%s/%s/%s", xdg_home,
               __NAME__, __CONFIG_NAME__);
    } else {
      snprintf(config_path, MAXLEN * sizeof(char), "%s/%s/%s/%s",
               getenv("HOME"), ".config", __NAME__, __CONFIG_NAME__);
    }
  }

  signal(SIGCHLD, handle_child);

  /* execute config file */
  load_config(config_path);
  run();

  free(config_path);

  return exit_code;
}

Coordinates Geometry::position(Position corner) const noexcept
{
  Coordinates pos;
  pos.x = 0;
  pos.y = 0;

  switch (corner) {
  case Position::TOP_LEFT:
    pos.x = x;
    pos.y = y;
    break;
  case Position::TOP_RIGHT:
    pos.x = x + width;
    pos.y = y;
    break;
  case Position::BOTTOM_RIGHT:
    pos.x = x + width;
    pos.y = y + height;
    break;
  case Position::BOTTOM_LEFT:
    pos.x = x;
    pos.y = y + height;
    break;
  case Position::CENTER:
    pos.x = x + (width / 2);
    pos.y = y + (height / 2);
    break;
  case Position::TOP:
    pos.x = x + (width / 2);
    pos.y = y;
    break;
  case Position::BOTTOM:
    pos.x = x + (width / 2);
    pos.y = y + height;
    break;
  case Position::LEFT:
    pos.x = x;
    pos.y = y + (height / 2);
    break;
  case Position::RIGHT:
    pos.x = x + width;
    pos.y = y + (height / 2);
    break;
  case Position::ALL: break;
  }
  return pos;
}

bool Geometry::overlaps(Geometry b) const noexcept
{
  auto pos_a_top_left  = position(TOP_LEFT);
  auto pos_a_top_right = position(TOP_RIGHT);
  auto pos_a_bot_left  = position(BOTTOM_LEFT);

  auto pos_b_top_left  = b.position(TOP_LEFT);
  auto pos_b_top_right = b.position(TOP_RIGHT);
  auto pos_b_bot_left  = b.position(BOTTOM_LEFT);

  bool is_x_top_overlapped = pos_a_top_left.x <= pos_b_top_left.x &&
                             pos_a_top_right.x >= pos_b_top_left.x;
  bool is_x_bot_overlapped = pos_a_top_left.x <= pos_b_top_right.x &&
                             pos_a_top_right.x >= pos_b_top_right.x;

  bool is_y_top_overlapped = pos_a_top_left.y <= pos_b_top_left.y &&
                             pos_a_bot_left.y >= pos_b_top_left.y;
  bool is_y_bot_overlapped = pos_a_top_left.y <= pos_b_bot_left.y &&
                             pos_a_bot_left.y >= pos_b_bot_left.y;

  return (is_x_top_overlapped || is_x_bot_overlapped) &&
         (is_y_top_overlapped || is_y_bot_overlapped);
}

float Geometry::angle_to(Geometry b) const noexcept
{
  auto a_pos = position(CENTER);
  auto b_pos = b.position(CENTER);

  auto dx = (float) (b_pos.x - a_pos.x);
  auto dy = (float) (b_pos.y - a_pos.y);

  if (dx == 0.0 && dy == 0.0) {
    return 0.0;
  }

  return atan2(dx, dy) * (180 / PI);
}

float Geometry::distance(Geometry b) const noexcept
{
  auto a_pos = position(CENTER);
  auto b_pos = b.position(CENTER);

  float distance =
    hypot((float) (b_pos.x - a_pos.x), (float) (b_pos.y - a_pos.y));
  return distance;
}
