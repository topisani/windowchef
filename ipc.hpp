/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for
 * full license information. */
#pragma once

#include <cstdint>
#include <utility>

#define ATOM_COMMAND "__WM_IPC_COMMAND"

#define IPC_MUL_PLUS 0
#define IPC_MUL_MINUS 1

namespace ipc {
  enum struct Command {
    WindowMove,
    WindowMoveAbsolute,
    WindowResize,
    WindowResizeAbsolute,
    WindowMaximize,
    WindowUnmaximize,
    WindowHorMaximize,
    WindowVerMaximize,
    WindowClose,
    WindowPutInGrid,
    WindowSnap,
    WindowCycle,
    WindowRevCycle,
    // WindowCycleInWorkspace,
    // WindowRevCycleInWorkspace,
    WindowCardinalFocus,
    WindowFocus,
    WindowFocusLast,
    WorkspaceAddWindow,
    // WorkspaceRemoveWindow,
    // WorkspaceRemoveAllWindows,
    WorkspaceGoto,
    WorkspaceSetBar,
    WMQuit,
    WMConfig,
    WindowConfig,
    Number
  };

  constexpr auto n_commands = static_cast<std::size_t>(Command::Number);

  enum struct Config {
    BorderWidth,
    ColorFocused,
    ColorUnfocused,
    GapWidth,
    GridGapWidth,
    CursorPosition,
    WorkspacesNr,
    EnableSloppyFocus,
    EnableResizeHints,
    StickyWindows,
    EnableBorders,
    EnableLastWindowFocusing,
    ApplySettings,
    ReplayClickOnFocus,
    PointerActions,
    PointerModifier,
    ClickToFocus,
    BarPadding,
    Number
  };

  constexpr auto n_configs = static_cast<std::size_t>(Config::Number);

  enum struct WinConfig { AllowOffscreen, Number };

  constexpr auto n_win_configs = static_cast<std::size_t>(WinConfig::Number);

  using Data = uint32_t*;

  template<Command Cmd>
  struct Tag {};

  template<Command Cmd>
  constexpr const Tag tag = Tag<Cmd>();

  void call_handler(Command cmd, Data d);
}; // namespace ipc
