/* See the LICENSE file for copyright and license details. */

#ifndef _WM_IPC_H
#define _WM_IPC_H

#define ATOM_COMMAND "__WM_IPC_COMMAND"

#define IPC_MUL_PLUS 0
#define IPC_MUL_MINUS 1

enum IPCCommand {
	IPCWindowMove,
	IPCWindowMoveAbsolute,
	IPCWindowResize,
	IPCWindowResizeAbsolute,
	IPCWindowMaximize,
	IPCWindowHorMaximize,
	IPCWindowVerMaximize,
	IPCWindowClose,
	IPCWindowPutInGrid,
	IPCWindowSnap,
	IPCWindowCycle,
	IPCWindowRevCycle,
	IPCGroupAddWindow,
	IPCGroupRemoveWindow,
	IPCGroupActivate,
	IPCGroupDeactivate,
	IPCGroupToggle,
	IPCWMQuit,
	IPCWMChangeNrOfGroups,
	IPCWMConfig,
	NR_IPC_COMMANDS
};

enum IPCConfig {
	IPCConfigBorderWidth,
	IPCConfigColorFocused,
	IPCConfigColorUnfocused,
	IPCConfigGapWidth,
	IPCConfigCursorPosition,
	IPCConfigGroupsNr,
	IPCConfigEnableSloppyFocus,
	NR_IPC_CONFIGS
};

#endif
