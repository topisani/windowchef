/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for
 * full license information. */
#pragma once

#define ATOM_COMMAND "__WM_IPC_COMMAND"

#define IPC_MUL_PLUS 0
#define IPC_MUL_MINUS 1

enum IPCCommand {
  IPCWindowMove,
  IPCWindowMoveAbsolute,
  IPCWindowResize,
  IPCWindowResizeAbsolute,
  IPCWindowMaximize,
  IPCWindowUnmaximize,
  IPCWindowHorMaximize,
  IPCWindowVerMaximize,
  IPCWindowMonocle,
  IPCWindowClose,
  IPCWindowPutInGrid,
  IPCWindowSnap,
  IPCWindowCycle,
  IPCWindowRevCycle,
  IPCWindowCycleInWorkspace,
  IPCWindowRevCycleInWorkspace,
  IPCWindowCardinalFocus,
  IPCWindowFocus,
  IPCWindowFocusLast,
  IPCWorkspaceAddWindow,
  IPCWorkspaceRemoveWindow,
  IPCWorkspaceRemoveAllWindows,
  IPCWorkspaceGoto,
  IPCWorkspaceSetBar,
  IPCWMQuit,
  IPCWMConfig,
  IPCWindowConfig,
  NR_IPC_COMMANDS
};

enum IPCConfig {
  IPCConfigBorderWidth,
  IPCConfigColorFocused,
  IPCConfigColorUnfocused,
  IPCConfigGapWidth,
  IPCConfigGridGapWidth,
  IPCConfigCursorPosition,
  IPCConfigWorkspacesNr,
  IPCConfigEnableSloppyFocus,
  IPCConfigEnableResizeHints,
  IPCConfigStickyWindows,
  IPCConfigEnableBorders,
  IPCConfigEnableLastWindowFocusing,
  IPCConfigApplySettings,
  IPCConfigReplayClickOnFocus,
  IPCConfigPointerActions,
  IPCConfigPointerModifier,
  IPCConfigClickToFocus,
  IPCConfigBarPadding,
  NR_IPC_CONFIGS
};

enum struct IPCWinConfig { AllowOffscreen, Number };
