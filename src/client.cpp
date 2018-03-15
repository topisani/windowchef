/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for
 * full license information. */
#include "client.hpp"

#include <xcb/xcb.h>

#include <err.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.hpp"
#include "ipc.hpp"
#include "types.hpp"

xcb_connection_t* conn;
xcb_screen_t* scr;

int opterr = 0;

namespace client {

  void usage(char* /*name*/, int /*status*/);
  void version();

  Command c[] = {
    // Command name,           // Enum value,                      // Arg count, // Arg parser
    {"window_move",            ipc::Command::WindowMove,           2,            fn_offset},
    {"window_move_absolute",   ipc::Command::WindowMoveAbsolute,   2,            fn_offset},
    {"window_resize",          ipc::Command::WindowResize,         2,            fn_offset},
    {"window_resize_absolute", ipc::Command::WindowResizeAbsolute, 2,            fn_naturals},
    {"window_maximize",        ipc::Command::WindowMaximize,       0,            nullptr},
    {"window_unmaximize",      ipc::Command::WindowUnmaximize,     0,            nullptr},
    {"window_hor_maximize",    ipc::Command::WindowHorMaximize,    0,            nullptr},
    {"window_ver_maximize",    ipc::Command::WindowVerMaximize,    0,            nullptr},
    {"window_close",           ipc::Command::WindowClose,          0,            nullptr},
    {"window_put_in_grid",     ipc::Command::WindowPutInGrid,      4,            fn_naturals},
    {"window_snap",            ipc::Command::WindowSnap,           1,            fn_position},
    {"window_cycle",           ipc::Command::WindowCycle,          0,            nullptr},
    {"window_rev_cycle",       ipc::Command::WindowRevCycle,       0,            nullptr},
    {"window_cardinal_focus",  ipc::Command::WindowCardinalFocus,  1,            fn_direction},
    {"window_cardinal_move",   ipc::Command::WindowCardinalMove,   1,            fn_direction},
    {"window_cardinal_resize", ipc::Command::WindowCardinalResize, 1,            fn_direction},
    {"window_focus",           ipc::Command::WindowFocus,          1,            fn_hex},
    {"window_focus_last",      ipc::Command::WindowFocusLast,      0,            nullptr},
    {"workspace_add_window",   ipc::Command::WorkspaceAddWindow,   1,            fn_naturals},
    {"workspace_goto",         ipc::Command::WorkspaceGoto,        1,            fn_naturals},
    {"workspace_set_bar",      ipc::Command::WorkspaceSetBar,      2,            fn_naturals},
    {"wm_quit",                ipc::Command::WMQuit,               1,            fn_naturals},
    {"wm_config",              ipc::Command::WMConfig,             -1,           fn_config},
    {"win_config",             ipc::Command::WindowConfig,         -1,           fn_win_config},
  };

  ConfigEntry configs[] = {
    // Config name,                 // Enum value,              // Arg count, // Arg parser
    {"border_width",                ipc::Config::BorderWidth,              1, fn_naturals},
    {"color_focused",               ipc::Config::ColorFocused,             1, fn_hex},
    {"color_unfocused",             ipc::Config::ColorUnfocused,           1, fn_hex},
    {"gap_width",                   ipc::Config::GapWidth,                 2, fn_gap},
    {"grid_gap_width",              ipc::Config::GridGapWidth,             1, fn_naturals},
    {"cursor_position",             ipc::Config::CursorPosition,           1, fn_position},
    {"workspaces_nr",               ipc::Config::WorkspacesNr,             1, fn_naturals},
    {"enable_sloppy_focus",         ipc::Config::EnableSloppyFocus,        1, fn_bool},
    {"enable_resize_hints",         ipc::Config::EnableResizeHints,        1, fn_bool},
    {"sticky_windows",              ipc::Config::StickyWindows,            1, fn_bool},
    {"enable_borders",              ipc::Config::EnableBorders,            1, fn_bool},
    {"enable_last_window_focusing", ipc::Config::EnableLastWindowFocusing, 1, fn_bool},
    {"apply_settings",              ipc::Config::ApplySettings,            1, fn_bool},
    {"replay_click_on_focus",       ipc::Config::ReplayClickOnFocus,       1, fn_bool},
    {"pointer_actions",             ipc::Config::PointerActions,           3, fn_pac},
    {"pointer_modifier",            ipc::Config::PointerModifier,          1, fn_mod},
    {"click_to_focus",              ipc::Config::ClickToFocus,             1, fn_button},
    {"bar_padding",                 ipc::Config::BarPadding,               4, fn_naturals},
  };

  WinConfigEntry win_configs[] = {
    // Config name,     // Enum value,       // Arg count, // Arg parser
    {"allow_offscreen", ipc::WinConfig::AllowOffscreen, 1, fn_bool},
  };

  /*
   * An offset is a pair of two signed integers.
   *
   * data[0], data[1] - if 1, then the number in negative
   * data[2], data[3] - the actual numbers, unsigned
   */
  bool fn_offset(uint32_t* data, int argc, char** argv)
  {
    int i = 0;
    do {
      errno = 0;
      int c = strtol(argv[i], nullptr, 10);
      if (c >= 0) {
        data[i] = IPC_MUL_PLUS;
      } else {
        data[i] = IPC_MUL_MINUS;
      }
      data[i + 2] = abs(c);
      i++;
    } while (i < argc && errno == 0);

    if (errno != 0) {
      return false;
    } else {
      return true;
    }
  }

  bool fn_naturals(uint32_t* data, int argc, char** argv)
  {
    int i = 0;
    do {
      errno   = 0;
      data[i] = strtol(argv[i], nullptr, 10);
      i++;
    } while (i < argc && errno == 0);

    if (errno != 0) {
      return false;
    } else {
      return true;
    }
  }

  bool fn_bool(uint32_t* data, int argc, char** argv)
  {
    int i = 0;
    char* arg;
    do {
      arg = argv[i];
      if (strcasecmp(argv[i], "true") == 0 || strcasecmp(arg, "yes") == 0 ||
          strcasecmp(arg, "t") == 0 || strcasecmp(arg, "y") == 0 ||
          strcasecmp(arg, "1") == 0) {
        data[i] = 1u;
      } else {
        data[i] = 0u;
      }
      i++;
    } while (i < argc);

    return true;
  }

  bool fn_config(uint32_t* data, int argc, char** argv)
  {
    char *key, *value;
    bool status;
    int i;

    key   = argv[0];
    value = argv[1];

    i = 0;
    while (i < ipc::n_configs && strcmp(key, configs[i].key) != 0) {
      i++;
    }

    if (i < ipc::n_configs) {
      if (configs[i].argc != argc - 1) {
        errx(EXIT_FAILURE, "too many or not enough arguments. Want: %d",
             configs[i].argc);
      }
      data[0] = static_cast<uint32_t>(configs[i].config);
      status  = (configs[i].handler)(data + 1, argc - 1, argv + 1);

      if (!status) {
        errx(EXIT_FAILURE, "malformed input");
      }
    } else {
      errx(EXIT_FAILURE, "no such config key %s", key);
    }
    return true;
  }

  bool fn_win_config(uint32_t* data, int argc, char** argv)
  {
    char *key, *value;
    bool status;
    int i;

    key   = argv[0];
    value = argv[1];

    i = 0;
    while (i < ipc::n_win_configs &&
           strcmp(key, win_configs[i].key) != 0) {
      i++;
    }

    if (i < ipc::n_win_configs) {
      if (win_configs[i].argc != argc - 2) {
        errx(EXIT_FAILURE, "too many or not enough arguments. Want: %d",
             win_configs[i].argc + 1);
      }
      data[0] = (uint32_t) win_configs[i].config;
      status  = fn_hex(data + 1, argc - 1, argv + 1);
      status = status && (win_configs[i].handler)(data + 2, argc - 2, argv + 2);

      if (!status) {
        errx(EXIT_FAILURE, "malformed input");
      }
    } else {
      errx(EXIT_FAILURE, "no such config key %s", key);
    }
    return true;
  }

  bool fn_hex(uint32_t* data, int argc, char** argv)
  {
    int i = 0;
    do {
      errno   = 0;
      data[i] = strtol(argv[i], nullptr, 16);
      i++;
    } while (i < argc && errno == 0);

    if (errno != 0) {
      return false;
    } else {
      return true;
    }
  }

  bool fn_direction(uint32_t* data, int argc, char** argv)
  {
    char* pos = argv[0];
    enum direction dir_sel;

    if (strcasecmp(pos, "up") == 0 || strcasecmp(pos, "north") == 0) {
      dir_sel = NORTH;
    } else if (strcasecmp(pos, "down") == 0 || strcasecmp(pos, "south") == 0) {
      dir_sel = SOUTH;
    } else if (strcasecmp(pos, "left") == 0 || strcasecmp(pos, "west") == 0) {
      dir_sel = WEST;
    } else if (strcasecmp(pos, "right") == 0 || strcasecmp(pos, "east") == 0) {
      dir_sel = EAST;
    } else {
      return false;
    }

    (void) (argc);
    data[0] = dir_sel;

    return true;
  }

  bool fn_pac(uint32_t* data, int argc, char** argv)
  {
    for (int i = 0; i < argc; i++) {
      char* pac = argv[i];
      if (strcasecmp(pac, "nothing") == 0) {
        data[i] = underlying(PointerAction::Nothing);
      } else if (strcasecmp(pac, "focus") == 0) {
        data[i] = underlying(PointerAction::Focus);
      } else if (strcasecmp(pac, "move") == 0) {
        data[i] = underlying(PointerAction::Move);
      } else if (strcasecmp(pac, "resize_corner") == 0) {
        data[i] = underlying(PointerAction::ResizeCorner);
      } else if (strcasecmp(pac, "resize_side") == 0) {
        data[i] = underlying(PointerAction::ResizeSide);
      } else {
        return false;
      }
    }

    return true;
  }
  bool fn_mod(uint32_t* data, int argc, char** argv)
  {
    (void) (argc);
    if (strcasecmp(argv[0], "alt") == 0) {
      data[0] = XCB_MOD_MASK_1;
    } else if (strcasecmp(argv[0], "super") == 0) {
      data[0] = XCB_MOD_MASK_4;
    } else {
      return false;
    }

    return true;
  }
  bool fn_button(uint32_t* data, int argc, char** argv)
  {
    char* btn = argv[0];
    (void) (argc);

    if (strcasecmp(btn, "left") == 0) {
      data[0] = 1;
    } else if (strcasecmp(btn, "middle") == 0) {
      data[0] = 2;
    } else if (strcasecmp(btn, "right") == 0) {
      data[0] = 3;
    } else if (strcasecmp(btn, "none") == 0) {
      data[0] = UINT32_MAX;
    } else if (strcasecmp(btn, "any") == 0) {
      data[0] = 0;
    } else {
      return false;
    }

    return true;
  }

  bool fn_position(uint32_t* data, int argc, char** argv)
  {
    char* pos = argv[0];
    Position snap_pos;

    if (strcasecmp(pos, "topleft") == 0) {
      snap_pos = TOP_LEFT;
    } else if (strcasecmp(pos, "topright") == 0) {
      snap_pos = TOP_RIGHT;
    } else if (strcasecmp(pos, "bottomleft") == 0) {
      snap_pos = BOTTOM_LEFT;
    } else if (strcasecmp(pos, "bottomright") == 0) {
      snap_pos = BOTTOM_RIGHT;
    } else if (strcasecmp(pos, "middle") == 0) {
      snap_pos = CENTER;
    } else if (strcasecmp(pos, "left") == 0) {
      snap_pos = LEFT;
    } else if (strcasecmp(pos, "bottom") == 0) {
      snap_pos = BOTTOM;
    } else if (strcasecmp(pos, "top") == 0) {
      snap_pos = TOP;
    } else if (strcasecmp(pos, "right") == 0) {
      snap_pos = RIGHT;
    } else if (strcasecmp(pos, "all") == 0) {
      snap_pos = ALL;
    } else {
      return false;
    }

    (void) (argc);
    data[0] = snap_pos;

    return true;
  }

  bool fn_gap(uint32_t* data, int argc, char** argv)
  {
    (void) (argc);
    bool status = true;

    status = status && fn_position(data, 1, argv);
    status = status && fn_naturals(data + 1, 1, argv + 1);

    return status;
  }

  void init_xcb(xcb_connection_t** conn)
  {
    *conn = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(*conn) != 0) {
      errx(EXIT_FAILURE, "unable to connect to X server");
    }
    scr = xcb_setup_roots_iterator(xcb_get_setup(*conn)).data;
  }

  xcb_atom_t get_atom(const char* name)
  {
    xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(conn, 0, strlen(name), name);
    xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(conn, cookie, nullptr);

    if (reply == nullptr) {
      return XCB_ATOM_STRING;
    }

    return reply->atom;
  }

  void send_command(Command* c, int argc, char** argv)
  {
    DMSG("Sending command %s", c->string_command);
    xcb_client_message_event_t msg;
    xcb_client_message_data_t data;
    xcb_generic_error_t* err;
    xcb_void_cookie_t cookie;
    bool status = true;

    msg.response_type = XCB_CLIENT_MESSAGE;
    msg.type          = get_atom(ATOM_COMMAND);
    msg.format        = 32;
    data.data32[0]    = static_cast<uint32_t>(c->command);
    if (c->handler != nullptr) {
      status = (c->handler)(data.data32 + 1, argc, argv);
    }
    if (!status) {
      errx(EXIT_FAILURE, "malformed input");
    }

    msg.data = data;

    cookie = xcb_send_event_checked(
      conn, 0u, scr->root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (char*) &msg);

    err = xcb_request_check(conn, cookie);
    if (err != nullptr) {
      fprintf(stderr, "oops %d\n", err->error_code);
    }
    xcb_flush(conn);
  }

  void usage(char* name, int status)
  {
    fprintf(stderr, "Usage: %s [-h|-v] <command> [args...]\n", name);
    exit(status);
  }

  void version()
  {
    fprintf(stderr, "%s %s\n", __NAME_CLIENT__, __THIS_VERSION__);
    fprintf(stderr, "Copyright (c) 2016-2017 Tudor Ioan Roman\n");
    fprintf(stderr, "Released under the ISC License\n");

    exit(EXIT_SUCCESS);
  }

} // namespace

using namespace client;

int main(int argc, char** argv)
{
  int i;
  int command_argc;
  char** command_argv;

  if (argc == 1) {
    usage(argv[0], EXIT_FAILURE);
  } else if (argc > 1) {
    if (strcmp(argv[1], "-h") == 0) {
      usage(argv[0], EXIT_SUCCESS);
    } else if (strcmp(argv[1], "-v") == 0) {
      version();
    }
  }

  init_xcb(&conn);

  /* argc - program name - command to send */
  command_argc = argc - 2;
  command_argv = argv + 2;

  i = 0;
  while (i < ipc::n_commands && strcmp(argv[1], c[i].string_command) != 0) {
    i++;
  }

  if (i < ipc::n_commands) {
    if (c[i].argc != -1) {
      if (command_argc < c[i].argc) {
        errx(EXIT_FAILURE, "not enough arguments");
      } else if (command_argc > c[i].argc) {
        warnx("too many arguments");
      }
    }
    if (c[i].argc == -1 || command_argc == c[i].argc) {
      send_command(&c[i], command_argc, command_argv);
    }

  } else {
    errx(EXIT_FAILURE, "no such command");
  }

  if (conn != nullptr) {
    xcb_disconnect(conn);
  }

  return 0;
}
