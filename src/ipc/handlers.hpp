#include <array>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <err.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>

#include "server.hpp"
#include "parsers.hpp"

#include "../common.hpp"
#include "../types.hpp"
#include "../util.hpp"
#include "../wm.hpp"
#include "../xcb.hpp"

namespace ipc {

  void handler(For<Command::Number>, Args)
  {
    throw std::runtime_error("");
  }

  void handler(For<Command::WindowMove>, Args args)
  {
    auto [x, y] = args.parse<int, int>();

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (wm::is_maxed(focused)) {
      wm::unmaximize_window(focused);
      wm::set_focused(focused);
    }

    wm::focused_client()->geom.x += x;
    wm::focused_client()->geom.y += y;

    xcb::move_window(wm::focused_client()->window, x, y);
  }

  void handler(For<Command::WindowMoveAbsolute>, Args args)
  {
    auto [x, y] = args.parse<int, int>();

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (wm::is_maxed(focused)) {
      wm::unmaximize_window(focused);
      wm::set_focused(focused);
    }

    focused.geom.x = x;
    focused.geom.y = y;
    xcb::apply_client_geometry(focused);
  }

  void handler(For<Command::WindowResize>, Args args)
  {
    auto [w, h] = args.parse<int, int>();

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (wm::is_maxed(focused)) {
      wm::unmaximize_window(focused);
      wm::set_focused(focused);
    }

    wm::resize_window(focused, w, h);
  }

  void handler(For<Command::WindowResizeAbsolute>, Args args)
  {
    auto [w, h] = args.parse<int, int>();

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (wm::is_maxed(focused)) {
      wm::unmaximize_window(focused);
      wm::set_focused(focused);
    }

    // if (wm::focused_client()->min_width != 0 && w <
    // wm::focused_client()->min_width) w = wm::focused_client()->min_width;

    // if (wm::focused_client()->min_height != 0 && h <
    // wm::focused_client()->min_height) h = wm::focused_client()->min_height;

    focused.geom.width  = w;
    focused.geom.height = h;
    xcb::apply_client_geometry(focused);
  }

  void handler(For<Command::WindowMaximize>, Args args)
  {
    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (focused.hmaxed && focused.vmaxed) {
      wm::unmaximize_window(focused);
    } else {
      wm::maximize_window(focused);
    }

    wm::set_focused(focused);
    xcb::flush();
  }

  void handler(For<Command::WindowUnmaximize>, Args args)
  {
    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    wm::unmaximize_window(focused);

    wm::set_focused(focused);
    xcb::flush();
  }

  void handler(For<Command::WindowHorMaximize>, Args args)
  {
    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (focused.hmaxed) {
      wm::unmaximize_window(focused);
    } else {
      wm::hmaximize_window(focused);
    }

    wm::set_focused(focused);
    xcb::flush();
  }

  void handler(For<Command::WindowVerMaximize>, Args args)
  {
    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (focused.vmaxed) {
      wm::unmaximize_window(focused);
    } else {
      wm::vmaximize_window(focused);
    }

    wm::set_focused(focused);
    xcb::flush();
  }

  void handler(For<Command::WindowClose>, Args args)
  {
    if (auto* focused = wm::focused_client(); focused != nullptr) {
      xcb::close_window(*focused);
    }
  }

  void handler(For<Command::WindowPutInGrid>, Args args)
  {
    auto [grid_width, grid_height, grid_x, grid_y] =
      args.parse<int, int, int, int>();

    if (auto* focused = wm::focused_client();
        focused != nullptr && grid_x < grid_width && grid_y < grid_height) {
      wm::grid_window(*wm::focused_client(), grid_width, grid_height, grid_x,
                      grid_y);
    }
  }

  void handler(For<Command::WindowSnap>, Args args)
  {
    auto [pos] = args.parse<Position>();
    wm::snap_window(*wm::focused_client(), pos);
  }

  void handler(For<Command::WindowCycle>, Args args)
  {
    wm::cycle_window(*wm::focused_client());
  }

  void handler(For<Command::WindowRevCycle>, Args args)
  {
    wm::rcycle_window(*wm::focused_client());
  }

  void handler(For<Command::WindowCardinalFocus>, Args args)
  {
    auto [mode] = args.parse<direction>();
    wm::cardinal_focus(mode);
  }

  void handler(For<Command::WindowCardinalMove>, Args args)
  {
    auto [mode] = args.parse<direction>();
    if (auto focus = wm::focused_client()) wm::cardinal_move(*focus, mode);
  }

  void handler(For<Command::WindowCardinalGrow>, Args args)
  {
    auto [mode] = args.parse<direction>();
    if (auto focus = wm::focused_client())
      wm::cardinal_resize(*focus, mode, false);
  }

  void handler(For<Command::WindowCardinalShrink>, Args args)
  {
    auto [mode] = args.parse<direction>();
    if (auto focus = wm::focused_client())
      wm::cardinal_resize(*focus, mode, true);
  }

  //  void (Forhandler<Command::WindowCycleInWorkspace>, Args args)
  //  {
  //    (void) (d);
  //
  //    if (wm::focused_client() == nullptr) return;
  //
  //    cycle_window_in_workspace(wm::focused_client());
  //  }
  //  void (Forhandler<Command::WindowRevCycleInWorkspace>, Args args)
  //  {
  //    (void) (d);
  //
  //    rcycle_window_in_workspace(wm::focused_client());
  //  }

  void handler(For<Command::WindowFocus>, Args args)
  {
    Client* client = wm::find_client(args.parse<0, xcb_window_t>());

    if (client != nullptr) {
      wm::set_focused(*client);
    }
  }

  void handler(For<Command::WindowFocusLast>, Args args)
  {
    if (wm::focused_client() != nullptr) {
      wm::set_focused_last_best();
    }
  }

  void handler(For<Command::WorkspaceAddWindow>, Args args)
  {
    if (wm::focused_client() != nullptr) {
      wm::workspace_add_window(*wm::focused_client(),
                               wm::get_workspace(args.parse<0, int>() - 1));
    }
  }

  //  void (Forhandler<Command::WorkspaceRemoveWindow>, Args args)
  //  {
  //    (void) (d);
  //    if (wm::focused_client() != nullptr)
  //    workspace_remove_window(*wm::focused_client());
  //  }
  //
  //  void (Forhandler<Command::WorkspaceRemoveAllWindows>, Args args)
  //  {
  //    workspace_remove_all_windows(d[0] - 1);
  //  }

  void handler(For<Command::WorkspaceGoto>, Args args)
  {
    wm::workspace_goto(wm::get_workspace(args.parse<0, int>() - 1));
  }

  void handler(For<Command::WorkspaceSetBar>, Args args)
  {
    auto [ws, mode] = args.parse<int, int>();
    Workspace& workspace =
      ws == 0 ? wm::current_ws() : wm::get_workspace(ws - 1);
    workspace.bar_shown = (mode > 1 ? !workspace.bar_shown : (mode != 0));

    wm::update_bar_visibility();
    for (auto& win : wm::current_ws().windows) {
      wm::fit_on_screen(win);
    }
  }

  void handler(For<Command::WMQuit>, Args args)
  {
    wm::halt = false;
    for (auto& ws : wm::workspaces()) {
      for (auto& cl : ws.windows) {
        xcb::close_window(cl);
        wm::halt = false;
      }
    }
    auto code        = args.parse<0, int>();
    wm::should_close = true;
    if (code > 0) {
      wm::halt = true;
    }
    wm::exit_code = code;
  }

  void handler(For<Command::WMConfig>, Args args)
  {
    auto key = args.parse<0, Config>();
    DMSG("Setting config %s", args[0].c_str());

    switch (key) {
    case Config::BorderWidth:
      wm::conf.border_width = args.parse<1, int>();
      if (wm::conf.apply_settings) {
        wm::refresh_borders();
      }
      break;
    case Config::ColorFocused:
      wm::conf.focus_color = args.parse<1, unsigned>();
      if (wm::conf.apply_settings) {
        wm::refresh_borders();
      }
      break;
    case Config::ColorUnfocused:
      wm::conf.unfocus_color = args.parse<1, unsigned>();
      if (wm::conf.apply_settings) {
        wm::refresh_borders();
      }
      break;
    case Config::GapWidth:
      switch (args.parse<1, Position>()) {
      case LEFT: wm::conf.gap_left = args.parse<2, int>(); break;
      case BOTTOM: wm::conf.gap_down = args.parse<2, int>(); break;
      case TOP: wm::conf.gap_up = args.parse<2, int>(); break;
      case RIGHT: wm::conf.gap_right = args.parse<2, int>(); break;
      case ALL:
        // wm::conf.gap_left = conf.gap_down = conf.gap_up = conf.gap_right =
        // d[2];
      default: break;
      }
      break;
    case Config::GridGapWidth: wm::conf.grid_gap = args.parse<1, int>();
    case Config::CursorPosition:
      wm::conf.cursor_position = args.parse<1, Position>();
      break;
      //    case Config::WorkspacesNr: change_nr_of_workspaces(d[1]); break;
    case Config::EnableSloppyFocus:
      wm::conf.sloppy_focus = args.parse<1, bool>();
      break;
    case Config::EnableResizeHints:
      wm::conf.resize_hints = args.parse<1, bool>();
    case Config::StickyWindows:
      wm::conf.sticky_windows = args.parse<1, bool>();
      break;
    case Config::EnableBorders: wm::conf.borders = args.parse<1, bool>(); break;
    case Config::EnableLastWindowFocusing:
      wm::conf.last_window_focusing = args.parse<1, bool>();
      break;
    case Config::ApplySettings:
      wm::conf.apply_settings = args.parse<1, bool>();
      break;
    case Config::ReplayClickOnFocus:
      wm::conf.replay_click_on_focus = args.parse<1, bool>();
      break;
    case Config::PointerActions:
      for (int i = 0; i < underlying(Buttons::Count); i++) {
        wm::conf.pointer_actions[i] = args.parse<PointerAction>(i);
      }
      wm::ungrab_buttons();
      wm::grab_buttons();
      break;
    case Config::PointerModifier:
      wm::conf.pointer_modifier = args.parse<1, int>();
      wm::ungrab_buttons();
      wm::grab_buttons();
      break;
    case Config::ClickToFocus: {
      auto val = args.parse<1, int>();
      if (val == UINT32_MAX) {
        wm::conf.click_to_focus = -1;
      } else {
        wm::conf.click_to_focus = val;
      }
      wm::ungrab_buttons();
      wm::grab_buttons();
    } break;
    case Config::BarPadding:
      wm::conf.bar_padding[0] = args.parse<1, int>();
      wm::conf.bar_padding[1] = args.parse<2, int>();
      wm::conf.bar_padding[2] = args.parse<3, int>();
      // conf.bar_padding[3] = d[4];
      for (auto& win : wm::current_ws().windows) {
        wm::fit_on_screen(win);
      }
      break;
    default: DMSG("!!! unhandled config key %d\n", key); break;
    }
  }

  void handler(For<Command::WindowConfig>, Args args)
  {
    auto [key, win] = args.parse<WinConfig, xcb_window_t>();
    Client* cl_ptr = wm::find_client(win);

    DMSG("Window config %s for window %x", args[0].c_str(), win);
    if (cl_ptr == nullptr) {
      DMSG("Window config for nonexistant window %x", win);
      return;
    }
    args.shift(2);
    auto& client = *cl_ptr;
    switch (key) {
    case ipc::WinConfig::AllowOffscreen:
      client.allow_offscreen = args.parse<0, bool>();
      break;
    default: DMSG("!!! unhandled config key %d\n", key); break;
    }
  }

  std::string handler(For<Command::GetFocused>, Args args)
  {
    auto focused = wm::focused_client();
    if (focused == nullptr) return "";
    return std::to_string(focused->window);
  }

} // namespace ipc

