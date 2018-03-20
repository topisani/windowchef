#include <array>
#include <type_traits>
#include <utility>
#include <cerrno>
#include <cstdio>
#include "common.hpp"
#include "ipc.hpp"
#include "types.hpp"
#include "util.hpp"
#include "wm.hpp"
#include "xcb.hpp"

namespace ipc {

  template<Command cmd>
  void handler(Data d);

  template<>
  void handler<Command::Number>(Data)
  {
    throw std::runtime_error("");
  }

  template<>
  void handler<Command::WindowMove>(Data d)
  {
    int16_t x, y;

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (wm::is_maxed(focused)) {
      wm::unmaximize_window(focused);
      wm::set_focused(focused);
    }

    x = d[2];
    y = d[3];
    if (d[0] != 0u) {
      x = -x;
    }
    if (d[1] != 0u) {
      y = -y;
    }

    wm::focused_client()->geom.x += x;
    wm::focused_client()->geom.y += y;

    xcb::move_window(wm::focused_client()->window, x, y);
  }

  template<>
  void handler<Command::WindowMoveAbsolute>(Data d)
  {
    int16_t x, y;

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (wm::is_maxed(focused)) {
      wm::unmaximize_window(focused);
      wm::set_focused(focused);
    }

    x = d[2];
    y = d[3];

    if (d[0] == IPC_MUL_MINUS) {
      x = -x;
    }
    if (d[1] == IPC_MUL_MINUS) {
      y = -y;
    }

    focused.geom.x = x;
    focused.geom.y = y;
    xcb::apply_client_geometry(focused);
  }

  template<>
  void handler<Command::WindowResize>(Data d)
  {
    int16_t w, h;

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (wm::is_maxed(focused)) {
      wm::unmaximize_window(focused);
      wm::set_focused(focused);
    }

    w = d[2];
    h = d[3];

    if (d[0] == IPC_MUL_MINUS) {
      w = -w;
    }
    if (d[1] == IPC_MUL_MINUS) {
      h = -h;
    }

    wm::resize_window(focused, w, h);
  }

  template<>
  void handler<Command::WindowResizeAbsolute>(Data d)
  {
    int16_t w, h;

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    if (wm::is_maxed(focused)) {
      wm::unmaximize_window(focused);
      wm::set_focused(focused);
    }

    w = d[0];
    h = d[1];

    // if (wm::focused_client()->min_width != 0 && w < wm::focused_client()->min_width)
    // w = wm::focused_client()->min_width;

    // if (wm::focused_client()->min_height != 0 && h <
    // wm::focused_client()->min_height) h = wm::focused_client()->min_height;

    focused.geom.width  = w;
    focused.geom.height = h;
    xcb::apply_client_geometry(focused);
  }

  template<>
  void handler<Command::WindowMaximize>(Data d)
  {
    (void) (d);

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

  template<>
  void handler<Command::WindowUnmaximize>(Data d)
  {
    (void) (d);

    if (wm::focused_client() == nullptr) {
      return;
    }
    Client& focused = *wm::focused_client();

    wm::unmaximize_window(focused);

    wm::set_focused(focused);
    xcb::flush();
  }

  template<>
  void handler<Command::WindowHorMaximize>(Data d)
  {
    (void) (d);

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

  template<>
  void handler<Command::WindowVerMaximize>(Data d)
  {
    (void) (d);

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

  template<>
  void handler<Command::WindowClose>(Data d)
  {
    (void) (d);
    if (auto* focused = wm::focused_client(); 
        focused != nullptr) {
      xcb::close_window(*focused);
    }
  }

  template<>
  void handler<Command::WindowPutInGrid>(Data d)
  {
    uint32_t grid_width, grid_height;
    uint32_t grid_x, grid_y;

    grid_width  = d[0];
    grid_height = d[1];
    grid_x      = d[2];
    grid_y      = d[3];

    if (auto* focused = wm::focused_client(); 
        focused != nullptr && grid_x < grid_width &&
        grid_y < grid_height) {
      wm::grid_window(*wm::focused_client(), grid_width, grid_height, grid_x, grid_y);
    }
  }

  template<>
  void handler<Command::WindowSnap>(Data d)
  {
    auto pos = (Position) d[0];
    wm::snap_window(*wm::focused_client(), pos);
  }

  template<>
  void handler<Command::WindowCycle>(Data d)
  {
    (void) (d);

    wm::cycle_window(*wm::focused_client());
  }

  template<>
  void handler<Command::WindowRevCycle>(Data d)
  {
    (void) (d);

    wm::rcycle_window(*wm::focused_client());
  }

  template<>
  void handler<Command::WindowCardinalFocus>(Data d)
  {
    uint32_t mode = d[0];
    wm::cardinal_focus(mode);
  }

  template<>
  void handler<Command::WindowCardinalMove>(Data d)
  {
    direction mode = static_cast<direction>(d[0]);
    if (auto focus = wm::focused_client())
      wm::cardinal_move(*focus, mode);
  }

  template<>
  void handler<Command::WindowCardinalGrow>(Data d)
  {
    direction mode = static_cast<direction>(d[0]);
    if (auto focus = wm::focused_client())
      wm::cardinal_resize(*focus, mode, false);
  }

  template<>
  void handler<Command::WindowCardinalShrink>(Data d)
  {
    direction mode = static_cast<direction>(d[0]);
    if (auto focus = wm::focused_client())
      wm::cardinal_resize(*focus, mode, true);
  }
  // template<>
  //  void handler<Command::WindowCycleInWorkspace>(Data d)
  //  {
  //    (void) (d);
  //
  //    if (wm::focused_client() == nullptr) return;
  //
  //    cycle_window_in_workspace(wm::focused_client());
  //  }
  //   template<>
  //  void handler<Command::WindowRevCycleInWorkspace>(Data d)
  //  {
  //    (void) (d);
  //
  //    rcycle_window_in_workspace(wm::focused_client());
  //  }

  template<>
  void handler<Command::WindowFocus>(Data d)
  {
    Client* client = wm::find_client(d[0]);

    if (client != nullptr) {
      wm::set_focused(*client);
    }
  }

  template<>
  void handler<Command::WindowFocusLast>(Data d)
  {
    (void) (d);
    if (wm::focused_client() != nullptr) {
      wm::set_focused_last_best();
    }
  }

  template<>
  void handler<Command::WorkspaceAddWindow>(Data d)
  {
    if (wm::focused_client() != nullptr) {
      wm::workspace_add_window(*wm::focused_client(), wm::get_workspace(d[0] - 1));
    }
  }

  // template<>
  //  void handler<Command::WorkspaceRemoveWindow>(Data d)
  //  {
  //    (void) (d);
  //    if (wm::focused_client() != nullptr)
  //    workspace_remove_window(*wm::focused_client());
  //  }
  //
  // template<>
  //  void handler<Command::WorkspaceRemoveAllWindows>(Data d)
  //  {
  //    workspace_remove_all_windows(d[0] - 1);
  //  }

  template<>
  void handler<Command::WorkspaceGoto>(Data d)
  {
    wm::workspace_goto(wm::get_workspace(d[0] - 1));
  }

  template<>
  void handler<Command::WorkspaceSetBar>(Data d)
  {
    Workspace& workspace = d[0] == 0 ? wm::current_ws() : wm::get_workspace(d[0] - 1);
    workspace.bar_shown  = (d[1] > 1 ? !workspace.bar_shown : (d[1] != 0u));

    wm::update_bar_visibility();
    for (auto& win : wm::current_ws().windows) {
      wm::fit_on_screen(win);
    }
  }

  template<>
  void handler<Command::WMQuit>(Data d)
  {
    wm::halt = false;
    for (auto& ws : wm::workspaces()) {
      for (auto& cl : ws.windows) {
        xcb::close_window(cl);
        wm::halt = false;
      }
    }
    uint32_t code = d[0];
    wm::should_close  = true;
    if (code > 0) {
      wm::halt = true;
    }
    wm::exit_code = code;
  }

  template<>
  void handler<Command::WMConfig>(Data d)
  {
    auto key = static_cast<Config>(d[0]);

    switch (key) {
    case Config::BorderWidth:
      wm::conf.border_width = d[1];
      if (wm::conf.apply_settings) {
        wm::refresh_borders();
      }
      break;
    case Config::ColorFocused:
      wm::conf.focus_color = d[1];
      if (wm::conf.apply_settings) {
        wm::refresh_borders();
      }
      break;
    case Config::ColorUnfocused:
      wm::conf.unfocus_color = d[1];
      if (wm::conf.apply_settings) {
        wm::refresh_borders();
      }
      break;
    case Config::GapWidth:
      switch (d[1]) {
      case LEFT: wm::conf.gap_left = d[2]; break;
      case BOTTOM: wm::conf.gap_down = d[2]; break;
      case TOP: wm::conf.gap_up = d[2]; break;
      case RIGHT: wm::conf.gap_right = d[2]; break;
      case ALL:
        // wm::conf.gap_left = conf.gap_down = conf.gap_up = conf.gap_right = d[2];
      default: break;
      }
      break;
    case Config::GridGapWidth: wm::conf.grid_gap = d[1];
    case Config::CursorPosition:
      wm::conf.cursor_position = (Position) d[1];
      break;
      //    case Config::WorkspacesNr: change_nr_of_workspaces(d[1]); break;
    case Config::EnableSloppyFocus: wm::conf.sloppy_focus = (d[1] != 0u); break;
    case Config::EnableResizeHints: wm::conf.resize_hints = (d[1] != 0u);
    case Config::StickyWindows: wm::conf.sticky_windows = (d[1] != 0u); break;
    case Config::EnableBorders: wm::conf.borders = (d[1] != 0u); break;
    case Config::EnableLastWindowFocusing:
      wm::conf.last_window_focusing = (d[1] != 0u);
      break;
    case Config::ApplySettings: wm::conf.apply_settings = (d[1] != 0u); break;
    case Config::ReplayClickOnFocus:
      wm::conf.replay_click_on_focus = (d[1] != 0u);
      break;
    case Config::PointerActions:
      for (int i = 0; i < underlying(Buttons::Count); i++) {
        wm::conf.pointer_actions[i] = (PointerAction) d[i + 1];
      }
      wm::ungrab_buttons();
      wm::grab_buttons();
      break;
    case Config::PointerModifier:
      wm::conf.pointer_modifier = d[1];
      wm::ungrab_buttons();
      wm::grab_buttons();
      break;
    case Config::ClickToFocus:
      if (d[1] == UINT32_MAX) {
        wm::conf.click_to_focus = -1;
      } else {
        wm::conf.click_to_focus = d[1];
      }
      wm::ungrab_buttons();
      wm::grab_buttons();
      break;
    case Config::BarPadding:
      wm::conf.bar_padding[0] = d[1];
      wm::conf.bar_padding[1] = d[2];
      wm::conf.bar_padding[2] = d[3];
      // conf.bar_padding[3] = d[4];
      for (auto& win : wm::current_ws().windows) {
        wm::fit_on_screen(win);
      }
      break;
    default: DMSG("!!! unhandled config key %d\n", key); break;
    }
  }

  template<>
  void handler<Command::WindowConfig>(Data d)
  {
    auto key       = (ipc::WinConfig) d[0];
    Client* cl_ptr = wm::find_client(d[1]);

    DMSG("Window config nr %d for window %x", d[0], d[1]);
    if (cl_ptr == nullptr) {
      DMSG("Window config for nonexistant window %x", d[1]);
      return;
    }
    d            = d + 2;
    auto& client = *cl_ptr;
    switch (key) {
    case ipc::WinConfig::AllowOffscreen:
      client.allow_offscreen = (d[0] != 0u);
      break;
    default: DMSG("!!! unhandled config key %d\n", key); break;
    }
  }

  /// Automatically construct array of handlers from enum.
  namespace detail {
    template<Command cmd>
    constexpr auto get_handler() -> function_ptr<void, Data>
    {
      return &handler<cmd>;
    }

    template<std::size_t... idxs>
    constexpr auto get_handlers(std::index_sequence<idxs...>)
    {
      std::array<function_ptr<void, Data>, n_commands> arr = {};
      return std::array<function_ptr<void, Data>, n_commands>{
        get_handler<static_cast<Command>(idxs)>()...};
    }

    constexpr auto get_handlers()
    {
      return get_handlers(std::make_index_sequence<n_commands>());
    }

  } // namespace detail


  /// Call the handler for a command.
  ///
  /// \throws `std::runtime_error` if no handler was found
  void call_handler(Command cmd, Data d) 
  {
    static constexpr auto handlers = detail::get_handlers();
    handlers.at(static_cast<std::size_t>(cmd))(d);
  }


} // namespace ipc
