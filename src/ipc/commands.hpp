#pragma once

#include <string>

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
    WindowCardinalMove,
    WindowCardinalGrow,
    WindowCardinalShrink,
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

}; // namespace ipc
