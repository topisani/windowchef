#pragma once

#include "../types.hpp"
#include "../util.hpp"
#include "server.hpp"

namespace ipc {

  template<>
  auto parse<int>(std::string const& str) -> int
  {
    return std::stoi(str, nullptr, 0);
  }

  template<>
  auto parse<unsigned>(std::string const& str) -> unsigned
  {
    return std::stoul(str, nullptr, 0);
  }

  template<>
  auto parse<bool>(std::string const& str) -> bool
  {
    return (str == "true" || str == "yes" || str == "y" || str == "t" ||
            str == "1");
  }

  template<>
  auto parse<Command>(std::string const& str) -> Command
  {
    if (str == "window_move")            return ipc::Command::WindowMove;
    if (str == "window_move_absolute")   return ipc::Command::WindowMoveAbsolute;
    if (str == "window_resize")          return ipc::Command::WindowResize;
    if (str == "window_resize_absolute") return ipc::Command::WindowResizeAbsolute;
    if (str == "window_maximize")        return ipc::Command::WindowMaximize;
    if (str == "window_unmaximize")      return ipc::Command::WindowUnmaximize;
    if (str == "window_hor_maximize")    return ipc::Command::WindowHorMaximize;
    if (str == "window_ver_maximize")    return ipc::Command::WindowVerMaximize;
    if (str == "window_close")           return ipc::Command::WindowClose;
    if (str == "window_put_in_grid")     return ipc::Command::WindowPutInGrid;
    if (str == "window_snap")            return ipc::Command::WindowSnap;
    if (str == "window_cycle")           return ipc::Command::WindowCycle;
    if (str == "window_rev_cycle")       return ipc::Command::WindowRevCycle;
    if (str == "window_cardinal_focus")  return ipc::Command::WindowCardinalFocus;
    if (str == "window_cardinal_move")   return ipc::Command::WindowCardinalMove;
    if (str == "window_cardinal_grow")   return ipc::Command::WindowCardinalGrow;
    if (str == "window_cardinal_shrink") return ipc::Command::WindowCardinalShrink;
    if (str == "window_focus")           return ipc::Command::WindowFocus;
    if (str == "window_focus_last")      return ipc::Command::WindowFocusLast;
    if (str == "workspace_add_window")   return ipc::Command::WorkspaceAddWindow;
    if (str == "workspace_goto")         return ipc::Command::WorkspaceGoto;
    if (str == "workspace_set_bar")      return ipc::Command::WorkspaceSetBar;
    if (str == "wm_quit")                return ipc::Command::WMQuit;
    if (str == "wm_config")              return ipc::Command::WMConfig;
    if (str == "win_config")             return ipc::Command::WindowConfig;
    if (str == "get_focused")            return ipc::Command::GetFocused;
    throw std::runtime_error(str_join("No command matches '", str, "'"));
  }

  template<>
  auto parse<Config>(std::string const& str) -> Config
  {
    if (str == "border_width")                return ipc::Config::BorderWidth;
    if (str == "color_focused")               return ipc::Config::ColorFocused;
    if (str == "color_unfocused")             return ipc::Config::ColorUnfocused;
    if (str == "gap_width")                   return ipc::Config::GapWidth;
    if (str == "grid_gap_width")              return ipc::Config::GridGapWidth;
    if (str == "cursor_position")             return ipc::Config::CursorPosition;
    if (str == "workspaces_nr")               return ipc::Config::WorkspacesNr;
    if (str == "enable_sloppy_focus")         return ipc::Config::EnableSloppyFocus;
    if (str == "enable_resize_hints")         return ipc::Config::EnableResizeHints;
    if (str == "sticky_windows")              return ipc::Config::StickyWindows;
    if (str == "enable_borders")              return ipc::Config::EnableBorders;
    if (str == "enable_last_window_focusing") return ipc::Config::EnableLastWindowFocusing;
    if (str == "apply_settings")              return ipc::Config::ApplySettings;
    if (str == "replay_click_on_focus")       return ipc::Config::ReplayClickOnFocus;
    if (str == "pointer_actions")             return ipc::Config::PointerActions;
    if (str == "pointer_modifier")            return ipc::Config::PointerModifier;
    if (str == "click_to_focus")              return ipc::Config::ClickToFocus;
    if (str == "bar_padding")                 return ipc::Config::BarPadding;
    throw std::runtime_error(str_join("No config matches '", str, "'"));
  }

  template<>
  auto parse<WinConfig>(std::string const& str) -> WinConfig
  {
    if (str == "allow_offscreen") return WinConfig::AllowOffscreen;
    throw std::runtime_error(str_join("No window config matches '", str, "'"));
  }

  template<>
  auto parse<direction>(std::string const& str) -> direction
  {
    if (strcasecmp(str.c_str(), "up") == 0 ||
        strcasecmp(str.c_str(), "north") == 0)
      return direction::NORTH;
    if (strcasecmp(str.c_str(), "down") == 0 ||
        strcasecmp(str.c_str(), "south") == 0)
      return direction::SOUTH;
    if (strcasecmp(str.c_str(), "left") == 0 ||
        strcasecmp(str.c_str(), "west") == 0)
      return direction::WEST;
    if (strcasecmp(str.c_str(), "right") == 0 ||
        strcasecmp(str.c_str(), "east") == 0)
      return direction::EAST;

    throw std::runtime_error(
      str_join("'", str,
               "' could not be parsed as a direction "
               "(up|north|down|south|left|west|right|east)"));
  }

  template<>
  auto parse<PointerAction>(std::string const& str) -> PointerAction
  {
    if (strcasecmp(str.c_str(), "nothing") == 0) return PointerAction::Nothing;
    if (strcasecmp(str.c_str(), "focus") == 0) return PointerAction::Focus;
    if (strcasecmp(str.c_str(), "move") == 0) return PointerAction::Move;
    if (strcasecmp(str.c_str(), "resize_corner") == 0)
      return PointerAction::ResizeCorner;
    if (strcasecmp(str.c_str(), "resize_side") == 0)
      return PointerAction::ResizeSide;
    throw std::runtime_error(
      str_join("'", str,
               "' could not be parsed as a pointer action "
               "(nothing|focus|move|resize_corner|resize_side)"));
  }

  template<>
  auto parse<xcb_mod_mask_t>(std::string const& str) -> xcb_mod_mask_t
  {
    if (strcasecmp(str.c_str(), "alt") == 0) return XCB_MOD_MASK_1;
    if (strcasecmp(str.c_str(), "super") == 0) return XCB_MOD_MASK_4;
    throw std::runtime_error(
      str_join("'", str, "' could not be parsed as a modifier (alt|super)"));
  }

  template<>
  auto parse<Buttons>(std::string const& str) -> Buttons
  {
    if (strcasecmp(str.c_str(), "left") == 0) return Buttons::Left;
    if (strcasecmp(str.c_str(), "middle") == 0) return Buttons::Middle;
    if (strcasecmp(str.c_str(), "right") == 0) return Buttons::Right;
    if (strcasecmp(str.c_str(), "none") == 0) return Buttons::None;
    if (strcasecmp(str.c_str(), "any") == 0) return Buttons::Any;
    throw std::runtime_error(str_join(
      "'", str,
      "' could not be parsed as a button (left|middle|right|none|any)"));
  }

  template<>
  auto parse<Position>(std::string const& str) -> Position
  {
    if (strcasecmp(str.c_str(), "topleft") == 0) return Position::TOP_LEFT;
    if (strcasecmp(str.c_str(), "topright") == 0) return Position::TOP_RIGHT;
    if (strcasecmp(str.c_str(), "bottomleft") == 0)
      return Position::BOTTOM_LEFT;
    if (strcasecmp(str.c_str(), "bottomright") == 0)
      return Position::BOTTOM_RIGHT;
    if (strcasecmp(str.c_str(), "middle") == 0) return Position::CENTER;
    if (strcasecmp(str.c_str(), "left") == 0) return Position::LEFT;
    if (strcasecmp(str.c_str(), "bottom") == 0) return Position::BOTTOM;
    if (strcasecmp(str.c_str(), "top") == 0) return Position::TOP;
    if (strcasecmp(str.c_str(), "right") == 0) return Position::RIGHT;
    if (strcasecmp(str.c_str(), "all") == 0) return Position::ALL;
    throw std::runtime_error(
      str_join("'", str,
               "' could not be parsed as a position "
               "(topleft|topright|bottomleft|bottomright|middle|left|bottom|"
               "top|right|all)"));
  }



  // To String //


  auto to_string(std::string&& str) noexcept
  {
    return str;
  }

  auto to_string(std::string const& str) noexcept
  {
    return str;
  }
} // namespace ipc
