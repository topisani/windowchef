/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for
 * full license information. */

#include <algorithm>
#include <vector>

#include <X11/keysym.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

#include <err.h>
#include <tgmath.h>
#include <unistd.h>

#include <sys/wait.h>

#include "common.hpp"
#include "config.hpp"
#include "ipc.hpp"
#include "types.hpp"

#define EVENT_MASK(ev) (((ev) & ~0x80))
/* XCB event with the biggest value */
#define LAST_XCB_EVENT XCB_GET_MODIFIER_MAPPING
#define PI 3.14159265

namespace {
  /* atoms identifiers */
  enum {
    WM_DELETE_WINDOW,
    _IPC_ATOM_COMMAND,
    NR_ATOMS
  };

  /* button identifiers */
  enum { BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT, NR_BUTTONS };

  /* connection to the X server */
  xcb_connection_t* conn;
  xcb_ewmh_connection_t* ewmh;
  xcb_screen_t* scr;
  Client* focused_win;
  Conf conf;
  /* number of the screen we're using */
  int scrno;
  /* base for checking randr events */
  int randr_base;
  bool halt;
  int exit_code;

  std::vector<Workspace> workspaces;
  int current_ws = 0;

  /* keyboard modifiers (for mouse support) */
  uint16_t num_lock, caps_lock, scroll_lock;
  const xcb_button_index_t mouse_buttons[] = {
    XCB_BUTTON_INDEX_1,
    XCB_BUTTON_INDEX_2,
    XCB_BUTTON_INDEX_3,
  };
  /* list of all windows */

  std::vector<Client> win_list;
  std::vector<Monitor> mon_list;
  /* Bar windows */
  std::vector<Client> bar_list;
  /* Windows to keep on top */
  std::vector<xcb_window_t> on_top;

  const char* atom_names[NR_ATOMS] = {
    "WM_DELETE_WINDOW",
    ATOM_COMMAND,
  };

  xcb_atom_t ATOMS[NR_ATOMS];
  /* function handlers for ipc commands */
  void (*ipc_handlers[NR_IPC_COMMANDS])(uint32_t*);
  /* function handlers for events received from the X server */
  void (*events[LAST_XCB_EVENT + 1])(xcb_generic_event_t*);

  void cleanup(void);
  int setup(void);
  int setup_randr(void);
  void get_randr(void);
  void get_outputs(xcb_randr_output_t*, int len, xcb_timestamp_t);
  Monitor* find_monitor(xcb_randr_output_t);
  Monitor* find_monitor_by_coord(int16_t, int16_t);
  Monitor* find_clones(xcb_randr_output_t, int16_t, int16_t);
  Monitor* add_monitor(xcb_randr_output_t,
                       char*,
                       int16_t,
                       int16_t,
                       uint16_t,
                       uint16_t);
  void free_monitor(Monitor*);
  void get_monitor_size(Client*,
                        int16_t*,
                        int16_t*,
                        uint16_t*,
                        uint16_t*,
                        bool include_padding = true);
  void arrange_by_monitor(Monitor*);
  Client* setup_window(xcb_window_t);
  void set_focused_no_raise(Client*&);
  void set_focused(Client*);
  void set_focused_last_best();
  void raise_window(xcb_window_t);
  void close_window(Client*);
  void delete_window(xcb_window_t);
  void teleport_window(xcb_window_t, int16_t, int16_t);
  void move_window(xcb_window_t, int16_t, int16_t);
  void resize_window_absolute(xcb_window_t, uint16_t, uint16_t);
  void resize_window(xcb_window_t, int16_t, int16_t);
  void fit_on_screen(Client*);
  void refresh_maxed(Client*);
  void fullscreen_window(Client*);
  void maximize_window(Client*);
  void hmaximize_window(Client*);
  void vmaximize_window(Client*);
  void unmaximize_geometry(Client*);
  void unmaximize_window(Client*);
  bool is_maxed(Client*);
  void cycle_window(Client*);
  void rcycle_window(Client*);
  void cycle_window_in_workspace(Client*);
  void rcycle_window_in_workspace(Client*);
  void cardinal_focus(uint32_t);
  float get_distance_between_windows(Client*, Client*);
  float get_angle_between_windows(Client*, Client*);
  WinPosition get_window_position(uint32_t, Client*);
  bool is_overlapping(Client*, Client*);
  bool is_in_valid_direction(uint32_t, float, float);
  bool is_in_cardinal_direction(uint32_t, Client*, Client*);
  void save_original_size(Client*);
  xcb_atom_t get_atom(const char*);
  bool get_pointer_location(xcb_window_t*, int16_t*, int16_t*);
  void center_pointer(Client*);
  Client* find_client(xcb_window_t*);
  bool get_geometry(xcb_window_t*, int16_t*, int16_t*, uint16_t*, uint16_t*);
  void set_borders(Client* client, uint32_t);
  bool is_mapped(xcb_window_t);
  void free_window(Client*);

  void add_to_client_list(xcb_window_t);
  void update_client_list(void);
  void update_wm_desktop(Client*);

  void workspace_add_window(Client*, uint32_t);
  void workspace_remove_window(Client*);
  void workspace_remove_all_windows(uint32_t);
  void workspace_goto(uint32_t);

  bool show_bar(uint32_t ws = current_ws);
  void update_bar_visibility();

  void change_nr_of_workspaces(uint32_t);
  void refresh_borders(void);
  void update_ewmh_wm_state(Client*);
  void handle_wm_state(Client*, xcb_atom_t, unsigned int);

  void snap_window(Client*, enum position);
  void grid_window(Client*, uint32_t, uint32_t, uint32_t, uint32_t);

  void register_event_handlers(void);
  void event_configure_request(xcb_generic_event_t*);
  void event_destroy_notify(xcb_generic_event_t*);
  void event_enter_notify(xcb_generic_event_t*);
  void event_map_request(xcb_generic_event_t*);
  void event_map_notify(xcb_generic_event_t*);
  void event_unmap_notify(xcb_generic_event_t*);
  void event_configure_notify(xcb_generic_event_t*);
  void event_circulate_request(xcb_generic_event_t*);
  void event_client_message(xcb_generic_event_t*);
  void event_focus_in(xcb_generic_event_t*);
  void event_focus_out(xcb_generic_event_t*);
  void event_button_press(xcb_generic_event_t*);

  void register_ipc_handlers(void);
  void ipc_window_move(uint32_t*);
  void ipc_window_move_absolute(uint32_t*);
  void ipc_window_resize(uint32_t*);
  void ipc_window_resize_absolute(uint32_t*);
  void ipc_window_maximize(uint32_t*);
  void ipc_window_unmaximize(uint32_t*);
  void ipc_window_hor_maximize(uint32_t*);
  void ipc_window_ver_maximize(uint32_t*);
  void ipc_window_close(uint32_t*);
  void ipc_window_put_in_grid(uint32_t*);
  void ipc_window_snap(uint32_t*);
  void ipc_window_cycle(uint32_t*);
  void ipc_window_rev_cycle(uint32_t*);
  void ipc_window_cycle_in_workspace(uint32_t*);
  void ipc_window_rev_cycle_in_workspace(uint32_t*);
  void ipc_window_cardinal_focus(uint32_t*);
  void ipc_window_focus(uint32_t*);
  void ipc_window_focus_last(uint32_t*);
  void ipc_workspace_add_window(uint32_t*);
  void ipc_workspace_remove_window(uint32_t*);
  void ipc_workspace_remove_all_windows(uint32_t*);
  void ipc_workspace_goto(uint32_t*);
  void ipc_workspace_set_bar(uint32_t*);
  void ipc_wm_quit(uint32_t*);
  void ipc_wm_config(uint32_t*);

  void pointer_init(void);
  int16_t pointer_modfield_from_keysym(xcb_keysym_t);
  void window_grab_buttons(xcb_window_t);
  void window_grab_button(xcb_window_t, uint8_t, uint16_t);
  bool pointer_grab(enum pointer_action);
  enum resize_handle get_handle(Client*, xcb_point_t, enum pointer_action);
  void track_pointer(Client*, enum pointer_action, xcb_point_t);
  void grab_buttons(void);
  void ungrab_buttons(void);

  void usage(char*);
  void version(void);
  void load_defaults(void);
  void load_config(char*);

  /*
   * Gracefully disconnect.
   */

  void cleanup(void)
  {
    xcb_set_input_focus(conn, XCB_NONE, XCB_INPUT_FOCUS_POINTER_ROOT,
                        XCB_CURRENT_TIME);
    ungrab_buttons();
    if (ewmh != nullptr) xcb_ewmh_connection_wipe(ewmh);
    if (conn != nullptr) xcb_disconnect(conn);
  }

  /*
   * Connect to the X server and initialize some things.
   */

  int setup(void)
  {
    /* init xcb and grab events */
    unsigned int values[1];
    int mask;

    conn = xcb_connect(nullptr, &scrno);
    if (xcb_connection_has_error(conn)) {
      return -1;
    }

    /* get the first screen. hope it's the last one too */
    scr         = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    focused_win = nullptr;

    mask = XCB_CW_EVENT_MASK;
    values[0] =
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
    xcb_generic_error_t* e = xcb_request_check(
      conn,
      xcb_change_window_attributes_checked(conn, scr->root, mask, values));
    if (e != nullptr) {
      free(e);
      errx(EXIT_FAILURE, "Another window manager is already running.");
    }

    /* initialize ewmh variables */
    ewmh = (xcb_ewmh_connection_t*) calloc(1, sizeof(xcb_ewmh_connection_t));
    if (!ewmh) warnx("couldn't set up ewmh connection");
    xcb_intern_atom_cookie_t* cookie = xcb_ewmh_init_atoms(conn, ewmh);
    xcb_ewmh_init_atoms_replies(ewmh, cookie, nullptr);
    xcb_ewmh_set_wm_pid(ewmh, scr->root, getpid());
    xcb_ewmh_set_wm_name(ewmh, scr->root, strlen(__NAME__), __NAME__);
    xcb_ewmh_set_current_desktop(ewmh, 0, 0);
    xcb_ewmh_set_number_of_desktops(ewmh, 0, WORKSPACES);

    xcb_atom_t supported_atoms[] = {
      ewmh->_NET_SUPPORTED,
      ewmh->_NET_WM_DESKTOP,
      ewmh->_NET_NUMBER_OF_DESKTOPS,
      ewmh->_NET_CURRENT_DESKTOP,
      ewmh->_NET_ACTIVE_WINDOW,
      ewmh->_NET_WM_STATE,
      ewmh->_NET_WM_STATE_FULLSCREEN,
      ewmh->_NET_WM_STATE_MAXIMIZED_VERT,
      ewmh->_NET_WM_STATE_MAXIMIZED_HORZ,
      ewmh->_NET_WM_NAME,
      ewmh->_NET_WM_ICON_NAME,
      ewmh->_NET_WM_WINDOW_TYPE,
      ewmh->_NET_WM_WINDOW_TYPE_DOCK,
      ewmh->_NET_WM_PID,
      ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR,
      ewmh->_NET_WM_WINDOW_TYPE_DESKTOP,
      ewmh->_NET_SUPPORTING_WM_CHECK,
    };
    xcb_ewmh_set_supported(ewmh, scrno,
                           sizeof(supported_atoms) / sizeof(xcb_atom_t),
                           supported_atoms);

    xcb_ewmh_set_supporting_wm_check(ewmh, scr->root, scr->root);

    pointer_init();

    /* send requests */
    xcb_flush(conn);

    /* get various atoms for icccm and ewmh */
    for (int i = 0; i < NR_ATOMS; i++) ATOMS[i] = get_atom(atom_names[i]);

    randr_base = setup_randr();

    workspaces.resize(conf.workspaces);
    for (uint32_t i = 0; i < conf.workspaces; i++) {
      workspaces[i].index     = i;
      workspaces[i].bar_shown = conf.bar_shown;
    }
    return 0;
  }

  /*
   * Tells the server we want to use randr.
   */

  int setup_randr(void)
  {
    int base;
    const xcb_query_extension_reply_t* r =
      xcb_get_extension_data(conn, &xcb_randr_id);

    if (!r->present)
      return -1;
    else
      get_randr();

    base = r->first_event;
    xcb_randr_select_input(conn, scr->root,
                           XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                             XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
                             XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                             XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);

    return base;
  }

  /*
   * Get information regarding randr.
   */

  void get_randr(void)
  {
    int len;
    xcb_randr_get_screen_resources_current_cookie_t c =
      xcb_randr_get_screen_resources_current(conn, scr->root);
    xcb_randr_get_screen_resources_current_reply_t* r =
      xcb_randr_get_screen_resources_current_reply(conn, c, nullptr);

    if (r == nullptr) return;

    xcb_timestamp_t timestamp = r->config_timestamp;
    len = xcb_randr_get_screen_resources_current_outputs_length(r);
    xcb_randr_output_t* outputs =
      xcb_randr_get_screen_resources_current_outputs(r);

    /* Request information for all outputs */
    get_outputs(outputs, len, timestamp);
    free(r);
  }

  /*
   * Gets information about connected outputs.
   */

  void get_outputs(xcb_randr_output_t* outputs,
                   int len,
                   xcb_timestamp_t timestamp)
  {
    int name_len;
    char* name;
    xcb_randr_get_crtc_info_cookie_t info_c;
    xcb_randr_get_crtc_info_reply_t* crtc;
    xcb_randr_get_output_info_reply_t* output;
    Monitor *mon, *clonemon;
    xcb_randr_get_output_info_cookie_t out_cookie[len];

    for (int i = 0; i < len; i++)
      out_cookie[i] = xcb_randr_get_output_info(conn, outputs[i], timestamp);

    for (int i = 0; i < len; i++) {
      output = xcb_randr_get_output_info_reply(conn, out_cookie[i], nullptr);
      if (output == nullptr) continue;

      name_len = xcb_randr_get_output_info_name_length(output);
      if (16 < name_len) name_len = 16;

      /* +1 for the null character */
      name = (char*) malloc(name_len + 1);
      /* make sure the name is at most name_len + 1 length
       * or we may run into problems. */
      snprintf(name, name_len + 1, "%.*s", name_len,
               xcb_randr_get_output_info_name(output));

      if (output->crtc != XCB_NONE) {
        info_c = xcb_randr_get_crtc_info(conn, output->crtc, timestamp);
        crtc   = xcb_randr_get_crtc_info_reply(conn, info_c, nullptr);

        if (crtc == nullptr) return;

        clonemon = find_clones(outputs[i], crtc->x, crtc->y);
        if (clonemon != nullptr) continue;

        mon = find_monitor(outputs[i]);
        if (mon == nullptr) {
          add_monitor(outputs[i], name, crtc->x, crtc->y, crtc->width,
                      crtc->height);
        } else {
          mon->x      = crtc->x;
          mon->y      = crtc->y;
          mon->width  = crtc->width;
          mon->height = crtc->height;

          arrange_by_monitor(mon);
        }

        free(crtc);
      } else {
        /* Check if the monitor was used before
         * becoming disabled. */
        mon = find_monitor(outputs[i]);
        if (mon) {
          for (auto& client : win_list) {
            /* Move window from this monitor to
             * either the next one or the first one. */
            if (client.monitor == mon) {
              auto iter =
                std::find(mon_list.begin(), mon_list.end(), *client.monitor);
              if (iter != mon_list.end()) iter++;
              if (iter == mon_list.end()) iter = mon_list.begin();
              if (iter != mon_list.end()) client.monitor = &*iter;
              fit_on_screen(&client);
            }
          }

          /* Monitor not active. Delete it. */
          free_monitor(mon);
        }
      }

      if (output != nullptr) free(output);
      free(name);
    }
  }

  /*
   * Finds a monitor in the list.
   */

  Monitor* find_monitor(xcb_randr_output_t mon)
  {
    auto iter = std::find_if(mon_list.begin(), mon_list.end(),
                             [mon](auto& el) { return el.monitor == mon; });
    if (iter == mon_list.end()) return nullptr;
    return &*iter;
  }

  /*
   * Find a monitor in the list by its coordinates.
   */

  Monitor* find_monitor_by_coord(int16_t x, int16_t y)
  {
    auto iter = std::find_if(mon_list.begin(), mon_list.end(), [x, y](auto& m) {
      return (x >= m.x && x <= m.x + m.width && y >= m.y &&
              y <= m.y + m.height);
    });
    if (iter == mon_list.end()) return nullptr;
    return &*iter;
  }

  /*
   * Find cloned (mirrored) outputs.
   */

  Monitor* find_clones(xcb_randr_output_t mon, int16_t x, int16_t y)
  {
    auto iter =
      std::find_if(mon_list.begin(), mon_list.end(), [mon, x, y](auto& m) {
        return (m.monitor != mon && m.x == x && m.y == y);
      });
    if (iter == mon_list.end()) return nullptr;
    return &*iter;
  }

  /*
   * Add a monitor to the global monitor list.
   */

  Monitor* add_monitor(xcb_randr_output_t mon,
                       char* name,
                       int16_t x,
                       int16_t y,
                       uint16_t width,
                       uint16_t height)
  {
    Monitor monitor;
    monitor.monitor = mon;
    monitor.name    = name;
    monitor.x       = x;
    monitor.y       = y;
    monitor.width   = width;
    monitor.height  = height;

    return &mon_list.emplace_back(monitor);
  }

  /*
   * Free a monitor from the global monitor list.
   */

  void free_monitor(Monitor* mon)
  {
    mon_list.erase(std::remove(mon_list.begin(), mon_list.end(), *mon), mon_list.end());
  }

  /*
   * Get information about a certain monitor situated in a window: coordinates
   * and size.
   */

  void get_monitor_size(Client* client,
                        int16_t* mon_x,
                        int16_t* mon_y,
                        uint16_t* mon_width,
                        uint16_t* mon_height,
                        bool include_padding)
  {
    if (client == nullptr || client->monitor == nullptr) {
      if (mon_x != nullptr && mon_y != nullptr) *mon_x = *mon_y = 0;
      if (mon_width != nullptr) *mon_width = scr->width_in_pixels;
      if (mon_height != nullptr) *mon_height = scr->height_in_pixels;
    } else {
      if (mon_x != nullptr) *mon_x = client->monitor->x;
      if (mon_y != nullptr) *mon_y = client->monitor->y;
      if (mon_width != nullptr) *mon_width = client->monitor->width;
      if (mon_height != nullptr) *mon_height = client->monitor->height;
    }
    if (!include_padding) return;

    uint32_t workspace = client == nullptr ? current_ws : client->workspace;
    if (workspace < conf.workspaces && show_bar(workspace)) {
      if (mon_x != nullptr) *mon_x += conf.bar_padding[0];
      if (mon_y != nullptr) *mon_y += conf.bar_padding[1];
      if (mon_width != nullptr)
        *mon_width -= conf.bar_padding[0] + conf.bar_padding[2];
      if (mon_height != nullptr)
        *mon_height -= conf.bar_padding[1] + conf.bar_padding[3];
    }
  }

  /*
   * Arrange clients on a monitor.
   */

  void arrange_by_monitor(Monitor* mon)
  {
    for (auto& client : win_list) {
      if (client.monitor == mon) fit_on_screen(&client);
    }
  }

  /*
   * Wait for events and handle them.
   */

  void run(void)
  {
    xcb_generic_event_t* ev;

    halt      = false;
    exit_code = EXIT_SUCCESS;
    while (!halt) {
      xcb_flush(conn);
      ev = xcb_wait_for_event(conn);
      if (ev) {
        DMSG("X Event %d\n", ev->response_type & ~0x80);
        if (ev->response_type == randr_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
          get_randr();
          DMSG("Screen layout changed\n");
        }
        if (events[EVENT_MASK(ev->response_type)] != nullptr)
          (events[EVENT_MASK(ev->response_type)])(ev);
        free(ev);
      }
    }
  }

  /*
   * Initialize a window for further work.
   */

  Client* setup_window(xcb_window_t win)
  {
    uint32_t values[2];
    xcb_ewmh_get_atoms_reply_t win_type;
    xcb_atom_t atom;
    xcb_size_hints_t hints;
    bool is_bar     = false;
    bool map_and_ignore = false;

    if (xcb_ewmh_get_wm_window_type_reply(
          ewmh, xcb_ewmh_get_wm_window_type(ewmh, win), &win_type, nullptr) ==
        1) {
      unsigned int i = 0;
      /* if the window is a toolbar or a dock, map it and ignore it */
      while (i < win_type.atoms_len) {
        if ((atom = win_type.atoms[i]) == ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR ||
            atom == ewmh->_NET_WM_WINDOW_TYPE_DOCK) {
          is_bar = true;
          break;
        }
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_DESKTOP) {
          map_and_ignore = true;
          break;
        }
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_NOTIFICATION) {
          on_top.push_back(win);
          map_and_ignore = true;
        }
        i++;
      }

      if (map_and_ignore) {
        xcb_ewmh_get_atoms_reply_wipe(&win_type);
        xcb_map_window(conn, win);
        return nullptr;
      }
      if (is_bar) {
        Client client;

        /* initialize variables */
        client.window = win;

        xcb_ewmh_get_atoms_reply_wipe(&win_type);
        bar_list.erase(std::remove(bar_list.begin(), bar_list.end(), client), bar_list.end());
        auto ptr = &bar_list.emplace_back(client);
        update_bar_visibility();
        return nullptr;
      }
    }

    /* subscribe to events */
    values[0] = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE;
    xcb_change_window_attributes(conn, win, XCB_CW_EVENT_MASK, values);

    /* in case of fire */
    xcb_change_save_set(conn, XCB_SET_MODE_INSERT, win);

    /* assign to the first workspace */
    xcb_ewmh_set_wm_desktop(ewmh, win, 0);

    Client cl;

    /* initialize variables */
    cl.window  = win;
    cl.monitor = nullptr;
    cl.mapped  = false;
    cl.workspace   = 0;

    get_geometry(&cl.window, &cl.geom.x, &cl.geom.y, &cl.geom.width,
                 &cl.geom.height);

    xcb_icccm_get_wm_normal_hints_reply(
      conn, xcb_icccm_get_wm_normal_hints_unchecked(conn, win), &hints,
      nullptr);

    if (hints.flags & XCB_ICCCM_SIZE_HINT_US_POSITION)
    cl.geom.set_by_user = true;

    if (hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
      cl.min_width  = hints.min_width;
      cl.min_height = hints.min_height;
    }

    if (hints.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
      cl.width_inc  = hints.width_inc;
      cl.height_inc = hints.height_inc;
    }

    DMSG("new window was born 0x%08x\n", cl.window);

    win_list.erase(std::remove(win_list.begin(), win_list.end(), cl), win_list.end());
    Client* ptr = &win_list.emplace_back(cl);
    return ptr;
  }

  /*
   * Set focus state to active or inactive without raising the window.
   */

  void set_focused_no_raise(Client*& client)
  {
    long data[] = {
      XCB_ICCCM_WM_STATE_NORMAL,
      XCB_NONE,
    };
    if (client == nullptr) return;

    set_borders(client, conf.focus_color);

    /* focus the window */
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, client->window,
                        XCB_CURRENT_TIME);

    /* set ewmh property */
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root,
                        ewmh->_NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 32, 1,
                        &client->window);

    /* set window state */
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
                        ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, data);

    /* set the focus state to inactive on the previously focused window */
    if (client != focused_win) {
      if (focused_win != nullptr)
        set_borders(focused_win, conf.unfocus_color);
    }

    // In reality, this moves the window to the back of the vector
    auto iter = std::find(win_list.begin(), win_list.end(), *client);
    if (iter != win_list.end()) client = &*std::rotate(iter, iter + 1, win_list.end());

    focused_win = client;

    window_grab_buttons(focused_win->window);
  }

  /*
   * Focus and raise.
   */

  void set_focused(Client* client)
  {
    set_focused_no_raise(client);
    raise_window(client->window);
  }

  /*
   * Focus last best focus (in a valid workspace, mapped, etc)
   */

  void set_focused_last_best()
  {
    auto iter = std::find_if(win_list.rbegin(), win_list.rend(),
      [] (Client& cl) {
        return cl.mapped;
      });
    if (iter != win_list.rend()) {
      set_focused(&*iter);
    }
  }

  /*
   * Put window at the top of the window stack.
   */

  void raise_window(xcb_window_t win)
  {
    uint32_t values[1] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
    for (auto win : on_top) {
      xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
    }
  }

  /*
   * Ask window to close gracefully. If the window doesn't respond, kill it.
   */

  void close_window(Client* client)
  {
    if (client == nullptr) return;

    if (conf.last_window_focusing && client != nullptr && client == focused_win)
      //set_focused_last_best();
      workspace_goto(current_ws);

    xcb_window_t win = client->window;
    xcb_get_property_cookie_t cookie =
      xcb_icccm_get_wm_protocols_unchecked(conn, win, ewmh->WM_PROTOCOLS);
    xcb_icccm_get_wm_protocols_reply_t reply;
    unsigned int i = 0;
    bool got       = false;

    if (xcb_icccm_get_wm_protocols_reply(conn, cookie, &reply, nullptr)) {
      for (i = 0; i < reply.atoms_len; i++) {
        got = (reply.atoms[i] = ATOMS[WM_DELETE_WINDOW]);
        if (got) break;
      }

      xcb_icccm_get_wm_protocols_reply_wipe(&reply);
    }

    if (got)
      delete_window(win);
    else
      xcb_kill_client(conn, win);
  }

  /*
   * Gracefully ask a window to close.
   */

  void delete_window(xcb_window_t win)
  {
    xcb_client_message_event_t ev;

    ev.response_type  = XCB_CLIENT_MESSAGE;
    ev.sequence       = 0;
    ev.format         = 32;
    ev.window         = win;
    ev.type           = ewmh->WM_PROTOCOLS;
    ev.data.data32[0] = ATOMS[WM_DELETE_WINDOW];
    ev.data.data32[1] = XCB_CURRENT_TIME;

    xcb_send_event(conn, 0, win, XCB_EVENT_MASK_NO_EVENT, (char*) &ev);
  }

  /*
   * Teleports window absolutely to the given coordinates.
   */

  void teleport_window(xcb_window_t win, int16_t x, int16_t y)
  {
    uint32_t values[2] = {(uint32_t) x, (uint32_t) y};

    if (win == scr->root || win == 0) return;

    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                         values);

    xcb_flush(conn);
  }

  /*
   * Moves the window by a certain amount.
   */

  void move_window(xcb_window_t win, int16_t x, int16_t y)
  {
    int16_t win_x, win_y;
    uint16_t win_w, win_h;

    if (!is_mapped(win) || win == scr->root) return;

    get_geometry(&win, &win_x, &win_y, &win_w, &win_h);

    win_x += x;
    win_y += y;

    teleport_window(win, win_x, win_y);
  }

  /*
   * Resizes window to the given size.
   */

  void resize_window_absolute(xcb_window_t win, uint16_t w, uint16_t h)
  {
    uint32_t val[2];
    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;

    val[0] = w;
    val[1] = h;

    xcb_configure_window(conn, win, mask, val);
  }

  /*
   * Resizes window by a certain amount.
   */

  void resize_window(xcb_window_t win, int16_t w, int16_t h)
  {
    Client* client;
    int32_t aw, ah;

    client = find_client(&win);
    if (client == nullptr) return;

    aw = client->geom.width;
    ah = client->geom.height;

    if (aw + w > 0) aw += w;
    if (ah + h > 0) ah += h;

    /* avoid weird stuff */
    if (aw < 0) aw = 0;
    if (ah < 0) ah = 0;

    // if (client->min_width != 0 && aw < client->min_width)
      // aw = client->min_width;
// 
    // if (client->min_height != 0 && ah < client->min_height)
      // ah = client->min_height;

    client->geom.width  = aw - conf.resize_hints * (aw % client->width_inc);
    client->geom.height = ah - conf.resize_hints * (ah % client->height_inc);

    resize_window_absolute(win, client->geom.width, client->geom.height);
  }

  /*
   * Fit window on screen if too big.
   */

  void fit_on_screen(Client* client)
  {
    int16_t mon_x, mon_y;
    uint16_t mon_width, mon_height;
    bool will_resize, will_move;

    will_resize = will_move = false;
    if (is_maxed(client)) {
      refresh_maxed(client);
      return;
    }
    get_monitor_size(client, &mon_x, &mon_y, &mon_width, &mon_height);
    if (client->geom.width == mon_width && client->geom.height == mon_height) {
      client->geom.x = mon_x;
      client->geom.y = mon_y;
      client->geom.width -= 2 * conf.border_width;
      client->geom.height -= 2 * conf.border_width;
      maximize_window(client);
      return;
    }

    /* Is it outside the display? */
    if (client->geom.x > mon_x + mon_width ||
        client->geom.y > mon_y + mon_height || client->geom.x < mon_x ||
        client->geom.y < mon_y) {
      will_move = true;
      if (client->geom.x > mon_x + mon_width)
        client->geom.x =
          mon_x + mon_width - client->geom.width - 2 * conf.border_width;
      else if (client->geom.x < mon_x)
        client->geom.x = mon_x;
      if (client->geom.y > mon_y + mon_height)
        client->geom.y =
          mon_y + mon_height - client->geom.height - 2 * conf.border_width;
      else if (client->geom.y < mon_y)
        client->geom.y = mon_y;
    }

    /* Is it smaller than it wants to be? */
    // if (client->min_width != 0 && client->geom.width < client->min_width) {
      // client->geom.width = client->min_width;
      // will_resize        = true;
    // }
    // if (client->min_height != 0 && client->geom.height < client->min_height) {
      // client->geom.height = client->min_height;
// 
      // will_resize = true;
    // }

    /* If the window is larger than the screen or is a bit in the outside,
     * move it to the corner and resize it accordingly. */
    if (client->geom.width + 2 * conf.border_width > mon_width) {
      client->geom.x     = mon_x;
      client->geom.width = mon_width - 2 * conf.border_width;
      will_move = will_resize = true;
    } else if (client->geom.x + client->geom.width + 2 * conf.border_width >
               mon_x + mon_width) {
      client->geom.x =
        mon_x + mon_width - client->geom.width - 2 * conf.border_width;
      will_move = true;
    }

    if (client->geom.height + 2 * conf.border_width > mon_height) {
      client->geom.y      = mon_y;
      client->geom.height = mon_height - 2 * conf.border_width;
      will_move = will_resize = true;
    } else if (client->geom.y + client->geom.height + 2 * conf.border_width >
               mon_y + mon_height) {
      client->geom.y =
        mon_y + mon_height - client->geom.height - 2 * conf.border_width;
      will_move = true;
    }

    if (will_move)
      teleport_window(client->window, client->geom.x, client->geom.y);
    if (will_resize)
      resize_window_absolute(client->window, client->geom.width,
                             client->geom.height);
  }

  void refresh_maxed(Client* client)
  {
    if (client == nullptr) return;

    if (client->hmaxed) {
      hmaximize_window(client);
    }
    if (client->vmaxed) {
      vmaximize_window(client);
    }
    if (client->fullscreen) {
      fullscreen_window(client);
    }
  }

  void fullscreen_window(Client* client)
  {
    if (client == nullptr) return;

    int16_t mon_x;
    int16_t mon_y;
    uint16_t mon_width;
    uint16_t mon_height;
    get_monitor_size(client, &mon_x, &mon_y, &mon_width, &mon_height, false);

    /* maximized windows don't have borders */
    if ((client->geom.width != mon_width || client->geom.height != mon_height) && !is_maxed(client))
      save_original_size(client);
    uint32_t values[1] = {0};
    xcb_configure_window(conn, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);

    client->geom.x      = mon_x;
    client->geom.y      = mon_y;
    client->geom.width  = mon_width;
    client->geom.height = mon_height;

    teleport_window(client->window, client->geom.x, client->geom.y);
    resize_window_absolute(client->window, client->geom.width,
                           client->geom.height);
    client->fullscreen = true;
    set_focused_no_raise(client);

    update_ewmh_wm_state(client);
  }

  void maximize_window(Client* client)
  {
    if (client == nullptr) return;

    int16_t mon_x;
    int16_t mon_y;
    uint16_t mon_width;
    uint16_t mon_height;
    get_monitor_size(client, &mon_x, &mon_y, &mon_width, &mon_height);

    if ((client->geom.width != mon_width || client->geom.height != mon_height) && !is_maxed(client))
      save_original_size(client);
    /* maximized windows don't have borders */

    uint32_t values[1] = {0};
    xcb_configure_window(conn, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);

    client->geom.x      = mon_x;
    client->geom.y      = mon_y;
    client->geom.width  = mon_width;
    client->geom.height = mon_height;

    teleport_window(client->window, client->geom.x, client->geom.y);
    resize_window_absolute(client->window, client->geom.width,
                           client->geom.height);
    client->vmaxed = true;
    client->hmaxed = true;
    set_focused_no_raise(client);

    update_ewmh_wm_state(client);
  }

  void hmaximize_window(Client* client)
  {
    if (client == nullptr) return;

    if (client->vmaxed) {
      maximize_window(client); 
      return;
    } else {
      unmaximize_geometry(client);
    }

    int16_t mon_x;
    int16_t mon_y;
    uint16_t mon_width;
    uint16_t mon_height;
    get_monitor_size(client, &mon_x, &mon_y, &mon_width, &mon_height);

    if (client->geom.width != mon_width) save_original_size(client);
    client->geom.x = mon_x + conf.gap_left;
    client->geom.width =
      mon_width - conf.gap_left - conf.gap_right - 2 * conf.border_width;

    teleport_window(client->window, client->geom.x, client->geom.y);
    resize_window_absolute(client->window, client->geom.width,
                           client->geom.height);
    client->hmaxed = true;

    uint32_t values[1] = {0};
    xcb_configure_window(conn, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);
    update_ewmh_wm_state(client);
  }

  void vmaximize_window(Client* client)
  {
    if (client == nullptr) return;

    if (client->hmaxed) {
      maximize_window(client); 
      return;
    } else {
      unmaximize_geometry(client);
    }

    int16_t mon_x;
    int16_t mon_y;
    uint16_t mon_width;
    uint16_t mon_height;
    get_monitor_size(client, &mon_x, &mon_y, &mon_width, &mon_height);

    if (client->geom.height != mon_height) save_original_size(client);

    client->geom.y = mon_y + conf.gap_up;
    client->geom.height =
      mon_height - conf.gap_up - conf.gap_down - 2 * conf.border_width;

    teleport_window(client->window, client->geom.x, client->geom.y);
    resize_window_absolute(client->window, client->geom.width,
                           client->geom.height);
    client->vmaxed = true;

    update_ewmh_wm_state(client);
  }

  void unmaximize_geometry(Client* client)
  {
    client->geom.x      = client->orig_geom.x;
    client->geom.y      = client->orig_geom.y;
    client->geom.width  = client->orig_geom.width;
    client->geom.height = client->orig_geom.height;
    client->fullscreen = client->hmaxed = client->vmaxed = false;
  }

  void unmaximize_window(Client* client)
  {
    xcb_atom_t state[] = {XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE};

    if (client->fullscreen) {
      client->fullscreen = false;
      refresh_maxed(client);
      return;
    } else {
      unmaximize_geometry(client);
    }

    teleport_window(client->window, client->geom.x, client->geom.y);
    resize_window_absolute(client->window, client->geom.width,
                           client->geom.height);
    set_borders(client, conf.unfocus_color);

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
                        ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, state);
  }

  bool is_maxed(Client* client)
  {
    if (client == nullptr) return false;

    return client->fullscreen || client->vmaxed || client->hmaxed;
  }

  void cycle_window(Client* client)
  {
    if (client == nullptr) return;
    auto iter = std::find(win_list.begin(), win_list.end(), *client);
    if (iter == win_list.end()) return;
    iter = std::find_if(iter, win_list.end(), [client](Client& cl) {
      return cl.mapped && cl != *client;
    });
    if (iter != win_list.end()) {
      set_focused(&*iter);
      return;
    }
    iter = std::find_if(win_list.begin(), win_list.end(),
                        [](Client& cl) { return cl.mapped; });
    if (iter != win_list.end()) {
      set_focused(&*iter);
    }
  }

  void rcycle_window(Client* client)
  {
    if (client == nullptr) return;
    auto iter = std::find(win_list.rbegin(), win_list.rend(), *client);
    if (iter == win_list.rend()) return;
    iter = std::find_if(iter, win_list.rend(), [client](Client& cl) {
      return cl.mapped && cl != *client;
    });
    if (iter != win_list.rend()) {
      set_focused(&*iter);
      return;
    }
    iter = std::find_if(win_list.rbegin(), win_list.rend(),
                        [](Client& cl) { return cl.mapped; });
    if (iter != win_list.rend()) {
      set_focused(&*iter);
    }
  }

  void cycle_window_in_workspace(Client* client)
  {
    if (client == nullptr) return;
    auto iter = std::find(win_list.begin(), win_list.end(), *client);
    if (iter == win_list.end()) return;
    iter = std::find_if(iter, win_list.end(), [client](Client& cl) {
      return cl.mapped && cl.workspace == client->workspace && cl != *client;
    });
    if (iter != win_list.end()) {
      set_focused(&*iter);
      return;
    }
    iter = std::find_if(win_list.begin(), win_list.end(), [client](Client& cl) {
      return cl.mapped && cl.workspace == client->workspace;
    });
    if (iter != win_list.end()) {
      set_focused(&*iter);
    }
  }

  void rcycle_window_in_workspace(Client* client)
  {
    if (client == nullptr) return;
    auto iter = std::find(win_list.rbegin(), win_list.rend(), *client);
    if (iter == win_list.rend()) return;
    iter = std::find_if(iter, win_list.rend(), [client](Client& cl) {
      return cl.mapped && cl.workspace == client->workspace && cl != *client;
    });
    if (iter != win_list.rend()) {
      set_focused(&*iter);
      return;
    }
    iter = std::find_if(
      win_list.rbegin(), win_list.rend(),
      [client](Client& cl) { return cl.mapped && cl.workspace == client->workspace; });
    if (iter != win_list.rend()) {
      set_focused(&*iter);
    }
  }

  void cardinal_focus(uint32_t dir)
  {
    /* Don't focus if we don't have a current focus! */
    if (focused_win == nullptr) return;

    WinPosition focus_win_pos = get_window_position(CENTER, focused_win);
    std::vector<Client*> valid_windows;
    for (Client& cl : win_list) {
      if (cl.window == focused_win->window) continue;
      if (!cl.mapped) continue;

      WinPosition win_pos = get_window_position(CENTER, &cl);

      switch (dir) {
      case NORTH:
        if (win_pos.y < focus_win_pos.y) valid_windows.push_back(&cl);
      case SOUTH:
        if (win_pos.y >= focus_win_pos.y) valid_windows.push_back(&cl);
        break;
      case WEST:
        if (win_pos.x < focus_win_pos.x) valid_windows.push_back(&cl);
        break;
      case EAST:
        if (win_pos.x >= focus_win_pos.x) valid_windows.push_back(&cl);
        break;
      }
    }

    float closest_distance;
    float closest_angle;
    Client* desired_window = nullptr;
    for (Client* cl : valid_windows) {
      float cur_distance;
      float cur_angle;

      cur_distance = get_distance_between_windows(focused_win, cl);
      cur_angle    = get_angle_between_windows(focused_win, cl);

      if (is_in_valid_direction(dir, cur_angle, 10)) {
        if (is_overlapping(focused_win, cl)) cur_distance = cur_distance * 0.1;
        cur_distance = cur_distance * 0.80;
      } else if (is_in_valid_direction(dir, cur_angle, 25)) {
        if (is_overlapping(focused_win, cl)) cur_distance = cur_distance * 0.1;
        cur_distance = cur_distance * 0.85;
      } else if (is_in_valid_direction(dir, cur_angle, 35)) {
        if (is_overlapping(focused_win, cl)) cur_distance = cur_distance * 0.1;
        cur_distance = cur_distance * 0.9;
      } else if (is_in_valid_direction(dir, cur_angle, 50)) {
        if (is_overlapping(focused_win, cl)) cur_distance = cur_distance * 0.1;
        cur_distance = cur_distance * 3;
      } else {
        continue;
      }

      if (is_in_cardinal_direction(dir, focused_win, cl))
        cur_distance = cur_distance * 0.9;

      if (closest_distance == -1 || (cur_distance < closest_distance)) {
        closest_distance = cur_distance;
        closest_angle    = cur_angle;
        desired_window   = cl;
      }
    }

    if (desired_window != nullptr) set_focused(desired_window);
  }

  WinPosition get_window_position(uint32_t mode, Client* win)
  {
    WinPosition pos;
    pos.x = 0;
    pos.y = 0;

    switch (mode) {
    case CENTER:
      pos.x = win->geom.x + (win->geom.width / 2);
      pos.y = win->geom.y + (win->geom.height / 2);
      break;
    case TOP_LEFT:
      pos.x = win->geom.x;
      pos.y = win->geom.y;
      break;
    case TOP_RIGHT:
      pos.x = win->geom.x + win->geom.width;
      pos.y = win->geom.y;
      break;
    case BOTTOM_RIGHT:
      pos.x = win->geom.x + win->geom.width;
      pos.y = win->geom.y + win->geom.height;
      break;
    case BOTTOM_LEFT:
      pos.x = win->geom.x;
      pos.y = win->geom.y + win->geom.height;
      break;
    }
    return pos;
  }

  bool is_in_cardinal_direction(uint32_t direction, Client* a, Client* b)
  {
    WinPosition pos_a_top_left  = get_window_position(TOP_LEFT, a);
    WinPosition pos_a_top_right = get_window_position(TOP_RIGHT, a);
    WinPosition pos_a_bot_left  = get_window_position(BOTTOM_LEFT, a);

    WinPosition pos_b_center = get_window_position(CENTER, b);

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
          window_direction <= (-180 + delta))
        return true;
      break;
    case SOUTH:
      if (fabs(window_direction) <= (0 + delta)) return true;
      break;
    case EAST:
      if (window_direction <= (90 + delta) && window_direction > (90 - delta))
        return true;
      break;
    case WEST:
      if (window_direction <= (-90 + delta) &&
          window_direction >= (-90 - delta))
        return true;
      break;
    }

    return false;
  }

  bool is_overlapping(Client* a, Client* b)
  {
    WinPosition pos_a_top_left  = get_window_position(TOP_LEFT, a);
    WinPosition pos_a_top_right = get_window_position(TOP_RIGHT, a);
    WinPosition pos_a_bot_left  = get_window_position(BOTTOM_LEFT, a);

    WinPosition pos_b_top_left  = get_window_position(TOP_LEFT, b);
    WinPosition pos_b_top_right = get_window_position(TOP_RIGHT, b);
    WinPosition pos_b_bot_left  = get_window_position(BOTTOM_LEFT, b);

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

  float get_angle_between_windows(Client* a, Client* b)
  {
    WinPosition a_pos = get_window_position(CENTER, a);
    WinPosition b_pos = get_window_position(CENTER, b);

    float dx = (float) (b_pos.x - a_pos.x);
    float dy = (float) (b_pos.y - a_pos.y);

    if (dx == 0.0 && dy == 0.0) return 0.0;

    return atan2(dx, dy) * (180 / PI);
  }

  float get_distance_between_windows(Client* a, Client* b)
  {
    WinPosition a_pos = get_window_position(CENTER, a);
    WinPosition b_pos = get_window_position(CENTER, b);

    float distance =
      hypot((float) (b_pos.x - a_pos.x), (float) (b_pos.y - a_pos.y));
    return distance;
  }

  void save_original_size(Client* client)
  {
    client->orig_geom.x      = client->geom.x;
    client->orig_geom.y      = client->geom.y;
    client->orig_geom.width  = client->geom.width;
    client->orig_geom.height = client->geom.height;
  }

  /*
   * Get atom by name.
   */

  xcb_atom_t get_atom(const char* name)
  {
    xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(conn, false, strlen(name), name);
    xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(conn, cookie, nullptr);

    if (!reply) return XCB_ATOM_STRING;

    return reply->atom;
  }

  /*
   * Get the mouse pointer's coordinates.
   */

  bool get_pointer_location(xcb_window_t* win, int16_t* x, int16_t* y)
  {
    xcb_query_pointer_reply_t* pointer;

    pointer = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, *win), 0);

    *x = pointer->win_x;
    *y = pointer->win_y;

    free(pointer);

    return pointer != nullptr;
  }

  void center_pointer(Client* client)
  {
    int16_t cur_x, cur_y;

    cur_x = cur_y = 0;

    switch (conf.cursor_position) {
    case TOP_LEFT:
      cur_x = -conf.border_width;
      cur_y = -conf.border_width;
      break;
    case TOP_RIGHT:
      cur_x = client->geom.width + conf.border_width;
      cur_y = 0 - conf.border_width;
      break;
    case BOTTOM_LEFT:
      cur_x = 0 - conf.border_width;
      cur_y = client->geom.height + conf.border_width;
      break;
    case BOTTOM_RIGHT:
      cur_x = client->geom.width + conf.border_width;
      cur_y = client->geom.height + conf.border_width;
      break;
    case CENTER:
      cur_x = client->geom.width / 2;
      cur_y = client->geom.height / 2;
      break;
    default: break;
    }

    xcb_warp_pointer(conn, XCB_NONE, client->window, 0, 0, 0, 0, cur_x, cur_y);
    xcb_flush(conn);
  }

  /*
   * Get the client instance with a given window id.
   */

  Client* find_client(xcb_window_t* win)
  {
    auto iter = std::find_if(win_list.begin(), win_list.end(),
                             [win](auto& cl) { return cl.window == *win; });
    if (iter != win_list.end()) return &*iter;
    return nullptr;
  }

  /*
   * Get a window's geometry.
   */

  bool get_geometry(xcb_window_t* win,
                    int16_t* x,
                    int16_t* y,
                    uint16_t* width,
                    uint16_t* height)
  {
    xcb_get_geometry_reply_t* reply =
      xcb_get_geometry_reply(conn, xcb_get_geometry(conn, *win), nullptr);

    if (reply == nullptr) return false;
    if (x != nullptr) *x = reply->x;
    if (y != nullptr) *y = reply->y;
    if (width != nullptr) *width = reply->width;
    if (height != nullptr) *height = reply->height;

    free(reply);
    return true;
  }

  /*
   * Set the color of the border.
   */

  void set_borders(Client* client, uint32_t color)
  {
    if (client == nullptr) return;
    uint32_t values[1];
    values[0] = (client->fullscreen) || (client->vmaxed && client->hmaxed) ? 0 : conf.border_width;
    xcb_configure_window(conn, client->window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);
    if (conf.borders == true) {
      values[0] = color;
      xcb_change_window_attributes(conn, client->window, XCB_CW_BORDER_PIXEL,
                                   values);
    }
  }

  /*
   * Returns true if window is mapped.
   */

  bool is_mapped(xcb_window_t win)
  {
    bool yes;
    xcb_get_window_attributes_reply_t* r = xcb_get_window_attributes_reply(
      conn, xcb_get_window_attributes(conn, win), nullptr);
    if (r == nullptr) return false;

    yes = r->map_state == XCB_MAP_STATE_VIEWABLE;
    free(r);

    return yes;
  }

  /*
   * Deletes and frees a client from the list.
   */

  void free_window(Client* cl)
  {
    if (cl == nullptr) return;
    DMSG("freeing 0x%08x\n", cl->window);
    win_list.erase(std::remove(win_list.begin(), win_list.end(), *cl), win_list.end());
    if (cl == focused_win) focused_win = nullptr;
  }

  /*
   * Add window to the ewmh client list.
   */

  void add_to_client_list(xcb_window_t win)
  {
    xcb_change_property(conn, XCB_PROP_MODE_APPEND, scr->root,
                        ewmh->_NET_CLIENT_LIST, XCB_ATOM_WINDOW, 32, 1, &win);
    xcb_change_property(conn, XCB_PROP_MODE_APPEND, scr->root,
                        ewmh->_NET_CLIENT_LIST_STACKING, XCB_ATOM_WINDOW, 32, 1,
                        &win);
  }

  /*
   * Adds all windows to the ewmh client list.
   */

  void update_client_list(void)
  {
    xcb_window_t* children;
    Client* client;
    uint32_t len;

    xcb_query_tree_reply_t* reply =
      xcb_query_tree_reply(conn, xcb_query_tree(conn, scr->root), nullptr);
    xcb_delete_property(conn, scr->root, ewmh->_NET_CLIENT_LIST);
    xcb_delete_property(conn, scr->root, ewmh->_NET_CLIENT_LIST_STACKING);

    if (reply == nullptr) {
      add_to_client_list(0);
      return;
    }

    len      = xcb_query_tree_children_length(reply);
    children = xcb_query_tree_children(reply);

    for (unsigned int i = 0; i < len; i++) {
      client = find_client(&children[i]);
      if (client != nullptr && client->mapped) add_to_client_list(client->window);
    }

    free(reply);
  }

  void update_wm_desktop(Client* client)
  {
    if (client != nullptr)
      xcb_ewmh_set_wm_desktop(ewmh, client->window, client->workspace);
  }

  void workspace_add_window(Client* client, uint32_t workspace)
  {
    if (client != nullptr && workspace < conf.workspaces) {
      client->workspace = workspace;
      update_wm_desktop(client);
    }
    workspace_goto(current_ws);
  }

  void workspace_remove_window(Client* client)
  {
    if (client != nullptr) {
      client->workspace = 0;
      update_wm_desktop(client);
    }
  }

  void workspace_remove_all_windows(uint32_t workspace)
  {
    if (workspace >= conf.workspaces) return;

    for (auto& cl : win_list) {
      if (cl.workspace == workspace) workspace_remove_window(&cl);
    }
  }

  void workspace_goto(uint32_t workspace)
  {
    if (workspace >= conf.workspaces) return;

    current_ws = workspace;

    for (auto& win : win_list) {
      if (win.workspace != workspace) {
        xcb_unmap_window(conn, win.window);
      }
    }

    Client* last_win = nullptr;
    for (auto& win : win_list) {
      if (win.workspace == workspace) {
        xcb_map_window(conn, win.window);
        refresh_maxed(&win);
        last_win = &win;
      }
    }
    if (focused_win == nullptr && last_win != nullptr)
      set_focused(last_win);

    update_client_list();
    xcb_ewmh_set_current_desktop(ewmh, 0, current_ws);
    update_bar_visibility();
  }

  bool show_bar(uint32_t ws)
  {
    return workspaces.at(ws).bar_shown;
    // || std::none_of(win_list.begin(), win_list.end(), [] (Client& cl) {
    //     return cl.mapped;
    //   });
  }

  void update_bar_visibility()
  {
    if (show_bar()) {
      for (auto& win : bar_list) xcb_map_window(conn, win.window);
    } else {
      for (auto& win : bar_list) xcb_unmap_window(conn, win.window);
    }
  }

  void change_nr_of_workspaces(uint32_t n_workspaces)
  {
    if (n_workspaces < conf.workspaces) {
      for (auto& win : win_list) {
        if (win.workspace >= n_workspaces) {
          workspace_remove_window(&win);
        }
      }
    }

    workspaces.resize(n_workspaces);
    conf.workspaces = n_workspaces;
  }

  void refresh_borders(void)
  {
    if (!conf.apply_settings) return;

    for (auto& win : win_list) {
      if (win.fullscreen || (win.hmaxed && win.vmaxed)) continue;

      if (&win == focused_win)
        set_borders(&win, conf.focus_color);
      else
        set_borders(&win, conf.unfocus_color);
    }
  }

  void update_ewmh_wm_state(Client* client)
  {
    int i;
    uint32_t values[12];

    if (client == nullptr) return;
#define HANDLE_WM_STATE(s)                                                     \
  values[i] = ewmh->_NET_WM_STATE_##s;                                         \
  i++;                                                                         \
  DMSG("ewmh net_wm_state %s present\n", #s);

    i = 0;
    if (client->fullscreen) {
      HANDLE_WM_STATE(FULLSCREEN);
    }
    if (client->vmaxed) {
      HANDLE_WM_STATE(MAXIMIZED_VERT);
    }
    if (client->hmaxed) {
      HANDLE_WM_STATE(MAXIMIZED_HORZ);
    }

    xcb_ewmh_set_wm_state(ewmh, client->window, i, values);
  }

  /*
   * Maximize / unmaximize windows based on ewmh requests.
   */

  void handle_wm_state(Client* client, xcb_atom_t state, unsigned int action)
  {
    int16_t mon_x, mon_y;
    uint16_t mon_w, mon_h;

    get_monitor_size(client, &mon_x, &mon_y, &mon_w, &mon_h);

    if (state == ewmh->_NET_WM_STATE_FULLSCREEN) {
      if (action == XCB_EWMH_WM_STATE_ADD) {
        fullscreen_window(client);
      } else if (action == XCB_EWMH_WM_STATE_REMOVE && client->fullscreen) {
        unmaximize_window(client);
        set_focused(client);
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        if (client->fullscreen) {
          unmaximize_window(client);
          set_focused(client);
        } else {
          fullscreen_window(client);
        }
      }
    } else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_VERT) {
      if (action == XCB_EWMH_WM_STATE_ADD) {
        vmaximize_window(client);
      } else if (action == XCB_EWMH_WM_STATE_REMOVE) {
        if (client->vmaxed) unmaximize_window(client);
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        if (client->vmaxed)
          unmaximize_window(client);
        else
          vmaximize_window(client);
      }
    } else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_HORZ) {
      if (action == XCB_EWMH_WM_STATE_ADD) {
        hmaximize_window(client);
      } else if (action == XCB_EWMH_WM_STATE_REMOVE) {
        if (client->hmaxed) unmaximize_window(client);
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        if (client->hmaxed)
          unmaximize_window(client);
        else
          hmaximize_window(client);
      }
    }
  }

  /*
   * Snap window in corner.
   */

  void snap_window(Client* client, enum position pos)
  {
    int16_t mon_x, mon_y, win_x, win_y;
    uint16_t mon_w, mon_h, win_w, win_h;

    if (client == nullptr) return;

    if (is_maxed(client)) {
      unmaximize_window(client);
      set_focused(client);
    }

    fit_on_screen(client);

    win_x = client->geom.x;
    win_y = client->geom.y;
    win_w = client->geom.width + 2 * conf.border_width;
    win_h = client->geom.height + 2 * conf.border_width;

    get_monitor_size(client, &mon_x, &mon_y, &mon_w, &mon_h);

    switch (pos) {
    case TOP_LEFT:
      win_x = mon_x + conf.gap_left;
      win_y = mon_y + conf.gap_up;
      break;

    case TOP_RIGHT:
      win_x = mon_x + mon_w - conf.gap_right - win_w;
      win_y = mon_y + conf.gap_up;
      break;

    case BOTTOM_LEFT:
      win_x = mon_x + conf.gap_left;
      win_y = mon_y + mon_h - conf.gap_down - win_h;
      break;

    case BOTTOM_RIGHT:
      win_x = mon_x + mon_w - conf.gap_right - win_w;
      win_y = mon_y + mon_h - conf.gap_down - win_h;
      break;

    case CENTER:
      win_x = mon_x + (mon_w - win_w) / 2;
      win_y = mon_y + (mon_h - win_h) / 2;
      break;

    default: return;
    }

    client->geom.x = win_x;
    client->geom.y = win_y;
    teleport_window(client->window, win_x, win_y);
    xcb_flush(conn);
  }

  /*
   * Put window in grid.
   */

  void grid_window(Client* client,
                   uint32_t grid_width,
                   uint32_t grid_height,
                   uint32_t grid_x,
                   uint32_t grid_y)
  {
    int16_t mon_x, mon_y;
    uint16_t new_w, new_h;
    uint16_t mon_w, mon_h;

    if (client == nullptr || grid_x >= grid_width || grid_y >= grid_height)
      return;

    if (is_maxed(client)) {
      unmaximize_window(client);
      set_focused(client);
    }

    get_monitor_size(client, &mon_x, &mon_y, &mon_w, &mon_h);

    /* calculate new window size */
    new_w =
      (mon_w - conf.gap_left - conf.gap_right -
       (grid_width - 1) * conf.grid_gap - grid_width * 2 * conf.border_width) /
      grid_width;

    new_h =
      (mon_h - conf.gap_up - conf.gap_down - (grid_height - 1) * conf.grid_gap -
       grid_height * 2 * conf.border_width) /
      grid_height;

    client->geom.width  = new_w;
    client->geom.height = new_h;

    client->geom.x =
      mon_x + conf.gap_left +
      grid_x * (conf.border_width + new_w + conf.border_width + conf.grid_gap);
    client->geom.y =
      mon_y + conf.gap_up +
      grid_y * (conf.border_width + new_h + conf.border_width + conf.grid_gap);

    DMSG("w: %d\th: %d\n", new_w, new_h);

    teleport_window(client->window, client->geom.x, client->geom.y);
    resize_window_absolute(client->window, client->geom.width,
                           client->geom.height);

    xcb_flush(conn);
  }

  /*
   * Adds X event handlers to the array.
   */

  void register_event_handlers(void)
  {
    for (int i = 0; i <= LAST_XCB_EVENT; i++) events[i] = nullptr;

    events[XCB_CONFIGURE_REQUEST] = event_configure_request;
    events[XCB_DESTROY_NOTIFY]    = event_destroy_notify;
    events[XCB_ENTER_NOTIFY]      = event_enter_notify;
    events[XCB_MAP_REQUEST]       = event_map_request;
    events[XCB_MAP_NOTIFY]        = event_map_notify;
    events[XCB_UNMAP_NOTIFY]      = event_unmap_notify;
    events[XCB_CLIENT_MESSAGE]    = event_client_message;
    events[XCB_CONFIGURE_NOTIFY]  = event_configure_notify;
    events[XCB_CIRCULATE_REQUEST] = event_circulate_request;
    events[XCB_FOCUS_IN]          = event_focus_in;
    events[XCB_FOCUS_OUT]         = event_focus_out;
    events[XCB_BUTTON_PRESS]      = event_button_press;
  }

  /*
   * A window wants to be configured.
   */

  void event_configure_request(xcb_generic_event_t* ev)
  {
    xcb_configure_request_event_t* e = (xcb_configure_request_event_t*) ev;
    Client* client;
    uint32_t values[7];
    int i = 0;

    client = find_client(&e->window);
    if (client != nullptr) {
      if (e->value_mask & XCB_CONFIG_WINDOW_X && !client->fullscreen &&
          !client->hmaxed)
        client->geom.x = e->x;

      if (e->value_mask & XCB_CONFIG_WINDOW_Y && !client->fullscreen &&
          !client->vmaxed)
        client->geom.y = e->y;

      if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH && !client->fullscreen &&
          !client->hmaxed)
        client->geom.width = e->width;

      if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT && !client->fullscreen &&
          !client->vmaxed)
        client->geom.height = e->height;

      if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
        values[0] = e->stack_mode;
        xcb_configure_window(conn, e->window, XCB_CONFIG_WINDOW_STACK_MODE,
                             values);
      }

      if (!client->fullscreen) {
        fit_on_screen(client);
      }

      teleport_window(client->window, client->geom.x, client->geom.y);
      resize_window_absolute(client->window, client->geom.width,
                             client->geom.height);
      if (!client->fullscreen && !(client->hmaxed && client->vmaxed))
        set_borders(client, conf.focus_color);
    } else {
      if (e->value_mask & XCB_CONFIG_WINDOW_X) {
        values[i] = e->x;
        i++;
      }

      if (e->value_mask & XCB_CONFIG_WINDOW_Y) {
        values[i] = e->y;
        i++;
      }

      if (e->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
        values[i] = e->width;
        i++;
      }

      if (e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
        values[i] = e->height;
        i++;
      }

      if (e->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
        values[i] = e->sibling;
        i++;
      }

      if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
        values[i] = e->stack_mode;
        i++;
      }

      if (i == 0) return;
      xcb_configure_window(conn, e->window, e->value_mask, values);
    }
  }

  /*
   * Window has been destroyed.
   */

  void event_destroy_notify(xcb_generic_event_t* ev)
  {
    Client* client;
    xcb_destroy_notify_event_t* e = (xcb_destroy_notify_event_t*) ev;

    on_top.erase(std::remove(on_top.begin(), on_top.end(), e->window));
    client = find_client(&e->window);
    if (conf.last_window_focusing && focused_win != nullptr &&
        focused_win == client) {
      focused_win = nullptr;
      //set_focused_last_best();
    }

    if (client != nullptr) {
      free_window(client);
    }

    // update_client_list();
    workspace_goto(current_ws);
  }

  /*
   * The mouse pointer has entered the window.
   */

  void event_enter_notify(xcb_generic_event_t* ev)
  {
    xcb_enter_notify_event_t* e = (xcb_enter_notify_event_t*) ev;
    Client* client;

    if (conf.sloppy_focus == false) return;

    if (focused_win != nullptr && e->event == focused_win->window) return;

    client = find_client(&e->event);

    if (client != nullptr) set_focused_no_raise(client);
  }

  /*
   * A window wants to show up on the screen.
   */

  void event_map_request(xcb_generic_event_t* ev)
  {
    xcb_map_request_event_t* e = (xcb_map_request_event_t*) ev;
    Client* client;
    long data[] = {
      XCB_ICCCM_WM_STATE_NORMAL,
      XCB_NONE,
    };

    /* create window if new */
    client = find_client(&e->window);
    if (client == nullptr) {
      client = setup_window(e->window);

      /* client is a dock or some kind of window that needs to be ignored */
      if (client == nullptr) return;

      if (!client->geom.set_by_user) {
        if (!get_pointer_location(&scr->root, &client->geom.x, &client->geom.y))
          client->geom.x = client->geom.y = 0;

        client->geom.x -= client->geom.width / 2;
        client->geom.y -= client->geom.height / 2;
        teleport_window(client->window, client->geom.x, client->geom.y);
      }
      workspace_add_window(client, current_ws);
    }

    if (client->workspace == current_ws) {
      xcb_map_window(conn, e->window);
    } else {
      workspace_add_window(client, current_ws);
    }


    /* in case of fire, abort */
    if (client == nullptr) return;

    if (randr_base != -1) {
      client->monitor = find_monitor_by_coord(client->geom.x, client->geom.y);
      if (client->monitor == nullptr) client->monitor = mon_list.data();
    }

    fit_on_screen(client);

    /* window is normal */
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
                        ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, data);

    update_client_list();

    if (!client->fullscreen && !(client->hmaxed && client->vmaxed)) set_borders(client, conf.focus_color);
  }

  void event_map_notify(xcb_generic_event_t* ev)
  {
    xcb_map_notify_event_t* e = (xcb_map_notify_event_t*) ev;
    Client* client            = find_client(&e->window);

    if (client != nullptr) {
      client->mapped = true;
      set_focused(client);
    }
  }

  /*
   * Window has been unmapped (became invisible).
   */

  void event_unmap_notify(xcb_generic_event_t* ev)
  {
    xcb_map_request_event_t* e = (xcb_map_request_event_t*) ev;
    Client* client             = nullptr;

    on_top.erase(std::remove(on_top.begin(), on_top.end(), e->window));
    client = find_client(&e->window);
    if (client == nullptr) return;

    client->mapped = false;

    if (conf.last_window_focusing && focused_win != nullptr &&
        client->window == focused_win->window) {
      focused_win = nullptr;
      set_focused_last_best();
    }

    // workspace_goto(current_ws);
    update_client_list();
  }

  /*
   * Window has been configured.
   */

  void event_configure_notify(xcb_generic_event_t* ev)
  {
    xcb_configure_notify_event_t* e = (xcb_configure_notify_event_t*) ev;

    /* The root window changes its geometry when the
     * user adds/removes/tilts screens */
    if (e->window == scr->root) {
      if (e->window != scr->width_in_pixels ||
          e->height != scr->height_in_pixels) {
        scr->width_in_pixels  = e->width;
        scr->height_in_pixels = e->height;

        if (randr_base != -1) {
          get_randr();
          for (auto& win : win_list) {
            fit_on_screen(&win);
          }
        }
      }
    } else {
      Client* client = find_client(&e->window);
      if (client != nullptr)
        client->monitor = find_monitor_by_coord(client->geom.x, client->geom.y);
    }
  }

  /*
   * Window wants to change its position in the stacking order.
   */

  void event_circulate_request(xcb_generic_event_t* ev)
  {
    xcb_circulate_request_event_t* e = (xcb_circulate_request_event_t*) ev;

    xcb_circulate_window(conn, e->window, e->place);
  }

  /*
   * Received client message. Either ewmh/icccm thing or
   * message from the client.
   */

  void event_client_message(xcb_generic_event_t* ev)
  {
    xcb_client_message_event_t* e = (xcb_client_message_event_t*) ev;
    uint32_t ipc_command;
    uint32_t* data;
    Client* client;

    if (e->type == ATOMS[_IPC_ATOM_COMMAND] && e->format == 32) {
      /* Message from the client */
      data        = e->data.data32;
      ipc_command = data[0];
      if (ipc_handlers[ipc_command] != nullptr)
        (ipc_handlers[ipc_command])(data + 1);
      DMSG("IPC Command %u with arguments %u %u %u\n", data[1], data[2],
           data[3], data[4]);
    } else {
      client = find_client(&e->window);
      if (client == nullptr) return;
      if (e->type == ewmh->_NET_WM_STATE) {
        DMSG("got _NET_WM_STATE for 0x%08x\n", client->window);
        handle_wm_state(client, e->data.data32[1], e->data.data32[0]);
        handle_wm_state(client, e->data.data32[2], e->data.data32[0]);
      }
    }
  }

  void event_focus_in(xcb_generic_event_t* ev)
  {
    xcb_focus_in_event_t* e = (xcb_focus_in_event_t*) ev;
    xcb_window_t win        = e->event;
  }

  void event_focus_out(xcb_generic_event_t* ev)
  {
    (void) (ev);
    xcb_get_input_focus_reply_t* focus =
      xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), nullptr);
    Client* client = nullptr;

    if (focused_win != nullptr && focus->focus == focused_win->window) return;

    if (focus->focus == scr->root) {
      focused_win = nullptr;
    } else {
      client = find_client(&focus->focus);
      if (client != nullptr) set_focused_no_raise(client);
    }
  }

  void event_button_press(xcb_generic_event_t* ev)
  {
    xcb_button_press_event_t* e = (xcb_button_press_event_t*) ev;
    bool replay                 = false;

    for (int i = 0; i < NR_BUTTONS; i++) {
      if (e->detail != mouse_buttons[i]) continue;
      if ((conf.click_to_focus == (int8_t) XCB_BUTTON_INDEX_ANY ||
           conf.click_to_focus == (int8_t) mouse_buttons[i]) &&
          (e->state & ~(num_lock | scroll_lock | caps_lock)) == XCB_NONE) {
        replay = !pointer_grab(POINTER_ACTION_FOCUS);
      } else {
        pointer_grab(conf.pointer_actions[i]);
      }
    }
    xcb_allow_events(conn,
                     replay ? XCB_ALLOW_REPLAY_POINTER : XCB_ALLOW_SYNC_POINTER,
                     e->time);
    xcb_flush(conn);
  }

  /*
   * Populates array with functions for handling IPC commands.
   */

  void register_ipc_handlers(void)
  {
    ipc_handlers[IPCWindowMove]             = ipc_window_move;
    ipc_handlers[IPCWindowMoveAbsolute]     = ipc_window_move_absolute;
    ipc_handlers[IPCWindowResize]           = ipc_window_resize;
    ipc_handlers[IPCWindowResizeAbsolute]   = ipc_window_resize_absolute;
    ipc_handlers[IPCWindowMaximize]         = ipc_window_maximize;
    ipc_handlers[IPCWindowUnmaximize]       = ipc_window_unmaximize;
    ipc_handlers[IPCWindowHorMaximize]      = ipc_window_hor_maximize;
    ipc_handlers[IPCWindowVerMaximize]      = ipc_window_ver_maximize;
    ipc_handlers[IPCWindowClose]            = ipc_window_close;
    ipc_handlers[IPCWindowPutInGrid]        = ipc_window_put_in_grid;
    ipc_handlers[IPCWindowSnap]             = ipc_window_snap;
    ipc_handlers[IPCWindowCycle]            = ipc_window_cycle;
    ipc_handlers[IPCWindowRevCycle]         = ipc_window_rev_cycle;
    ipc_handlers[IPCWindowCycleInWorkspace] = ipc_window_cycle_in_workspace;
    ipc_handlers[IPCWindowRevCycleInWorkspace] =
      ipc_window_rev_cycle_in_workspace;
    ipc_handlers[IPCWindowCardinalFocus]   = ipc_window_cardinal_focus;
    ipc_handlers[IPCWindowFocus]           = ipc_window_focus;
    ipc_handlers[IPCWindowFocusLast]       = ipc_window_focus_last;
    ipc_handlers[IPCWorkspaceAddWindow]    = ipc_workspace_add_window;
    ipc_handlers[IPCWorkspaceRemoveWindow] = ipc_workspace_remove_window;
    ipc_handlers[IPCWorkspaceRemoveAllWindows] =
      ipc_workspace_remove_all_windows;
    ipc_handlers[IPCWorkspaceGoto]   = ipc_workspace_goto;
    ipc_handlers[IPCWorkspaceSetBar] = ipc_workspace_set_bar;
    ipc_handlers[IPCWMQuit]          = ipc_wm_quit;
    ipc_handlers[IPCWMConfig]        = ipc_wm_config;
  }

  void ipc_window_move(uint32_t* d)
  {
    int16_t x, y;

    if (focused_win == nullptr) return;

    if (is_maxed(focused_win)) {
      unmaximize_window(focused_win);
      set_focused(focused_win);
    }

    x = d[2];
    y = d[3];
    if (d[0]) x = -x;
    if (d[1]) y = -y;

    focused_win->geom.x += x;
    focused_win->geom.y += y;

    move_window(focused_win->window, x, y);
  }

  void ipc_window_move_absolute(uint32_t* d)
  {
    int16_t x, y;

    if (focused_win == nullptr) return;

    if (is_maxed(focused_win)) {
      unmaximize_window(focused_win);
      set_focused(focused_win);
    }

    x = d[2];
    y = d[3];

    if (d[0] == IPC_MUL_MINUS) x = -x;
    if (d[1] == IPC_MUL_MINUS) y = -y;

    focused_win->geom.x = x;
    focused_win->geom.y = y;

    teleport_window(focused_win->window, x, y);
  }

  void ipc_window_resize(uint32_t* d)
  {
    int16_t w, h;

    if (focused_win == nullptr) return;

    if (is_maxed(focused_win)) {
      unmaximize_window(focused_win);
      set_focused(focused_win);
    }

    w = d[2];
    h = d[3];

    if (d[0] == IPC_MUL_MINUS) w = -w;
    if (d[1] == IPC_MUL_MINUS) h = -h;

    resize_window(focused_win->window, w, h);
  }

  void ipc_window_resize_absolute(uint32_t* d)
  {
    int16_t w, h;

    if (focused_win == nullptr) return;

    if (is_maxed(focused_win)) {
      unmaximize_window(focused_win);
      set_focused(focused_win);
    }

    w = d[0];
    h = d[1];

    // if (focused_win->min_width != 0 && w < focused_win->min_width)
      // w = focused_win->min_width;

    // if (focused_win->min_height != 0 && h < focused_win->min_height)
      // h = focused_win->min_height;

    focused_win->geom.width  = w;
    focused_win->geom.height = h;

    resize_window_absolute(focused_win->window, w, h);
  }

  void ipc_window_maximize(uint32_t* d)
  {
    (void) (d);

    if (focused_win == nullptr) return;

    if (focused_win->hmaxed && focused_win->vmaxed) {
      unmaximize_window(focused_win);
    } else {
      maximize_window(focused_win);
    }

    set_focused(focused_win);

    xcb_flush(conn);
  }

  void ipc_window_unmaximize(uint32_t* d)
  {
    (void) (d);

    if (focused_win == nullptr) return;

    unmaximize_window(focused_win);

    set_focused(focused_win);

    xcb_flush(conn);
  }

  void ipc_window_hor_maximize(uint32_t* d)
  {
    (void) (d);

    if (focused_win == nullptr) return;

    if (focused_win->hmaxed) {
      unmaximize_window(focused_win);
    } else {
      hmaximize_window(focused_win);
    }

    set_focused(focused_win);

    xcb_flush(conn);
  }

  void ipc_window_ver_maximize(uint32_t* d)
  {
    (void) (d);

    if (focused_win == nullptr) return;

    if (focused_win->vmaxed) {
      unmaximize_window(focused_win);
    } else {
      vmaximize_window(focused_win);
    }

    set_focused(focused_win);

    xcb_flush(conn);
  }

  void ipc_window_close(uint32_t* d)
  {
    (void) (d);
    close_window(focused_win);
  }

  void ipc_window_put_in_grid(uint32_t* d)
  {
    uint32_t grid_width, grid_height;
    uint32_t grid_x, grid_y;

    grid_width  = d[0];
    grid_height = d[1];
    grid_x      = d[2];
    grid_y      = d[3];

    if (focused_win == nullptr || grid_x >= grid_width || grid_y >= grid_height)
      return;

    grid_window(focused_win, grid_width, grid_height, grid_x, grid_y);
  }

  void ipc_window_snap(uint32_t* d)
  {
    enum position pos = (enum position) d[0];
    snap_window(focused_win, pos);
  }

  void ipc_window_cycle(uint32_t* d)
  {
    (void) (d);

    cycle_window(focused_win);
  }

  void ipc_window_rev_cycle(uint32_t* d)
  {
    (void) (d);

    rcycle_window(focused_win);
  }

  void ipc_window_cardinal_focus(uint32_t* d)
  {
    uint32_t mode = d[0];
    cardinal_focus(mode);
  }

  void ipc_window_cycle_in_workspace(uint32_t* d)
  {
    (void) (d);

    if (focused_win == nullptr) return;

    cycle_window_in_workspace(focused_win);
  }
  void ipc_window_rev_cycle_in_workspace(uint32_t* d)
  {
    (void) (d);

    rcycle_window_in_workspace(focused_win);
  }

  void ipc_window_focus(uint32_t* d)
  {
    Client* client = find_client(&d[0]);

    if (client != nullptr) set_focused(client);
  }

  void ipc_window_focus_last(uint32_t* d)
  {
    (void) (d);
    if (focused_win != nullptr) set_focused_last_best();
  }

  void ipc_workspace_add_window(uint32_t* d)
  {
    if (focused_win != nullptr) workspace_add_window(focused_win, d[0] - 1);
  }

  void ipc_workspace_remove_window(uint32_t* d)
  {
    (void) (d);
    if (focused_win != nullptr) workspace_remove_window(focused_win);
  }

  void ipc_workspace_remove_all_windows(uint32_t* d)
  {
    workspace_remove_all_windows(d[0] - 1);
  }

  void ipc_workspace_goto(uint32_t* d)
  {
    workspace_goto(d[0] - 1);
  }

  void ipc_workspace_set_bar(uint32_t* d)
  {
    Workspace& workspace    = workspaces.at(d[0] == 0 ? current_ws : d[0] - 1);
    workspace.bar_shown = d[1] > 1 ? !workspace.bar_shown : d[1];

    update_bar_visibility();
    for (auto& win : win_list) {
      fit_on_screen(&win);
    }
  }

  void ipc_wm_quit(uint32_t* d)
  {
    for (auto cl : win_list) {
      close_window(&cl);
    }
    uint32_t code = d[0];
    halt          = true;
    exit_code     = code;
  }

  void ipc_wm_config(uint32_t* d)
  {
    auto key = (enum IPCConfig) d[0];

    switch (key) {
    case IPCConfigBorderWidth:
      conf.border_width = d[1];
      if (conf.apply_settings) refresh_borders();
      break;
    case IPCConfigColorFocused:
      conf.focus_color = d[1];
      if (conf.apply_settings) refresh_borders();
      break;
    case IPCConfigColorUnfocused:
      conf.unfocus_color = d[1];
      if (conf.apply_settings) refresh_borders();
      break;
    case IPCConfigGapWidth:
      switch (d[1]) {
      case LEFT: conf.gap_left = d[2]; break;
      case BOTTOM: conf.gap_down = d[2]; break;
      case TOP: conf.gap_up = d[2]; break;
      case RIGHT: conf.gap_right = d[2]; break;
      case ALL:
        conf.gap_left = conf.gap_down = conf.gap_up = conf.gap_right = d[2];
      default: break;
      }
      break;
    case IPCConfigGridGapWidth: conf.grid_gap = d[1];
    case IPCConfigCursorPosition:
      conf.cursor_position = (enum position) d[1];
      break;
    case IPCConfigWorkspacesNr: change_nr_of_workspaces(d[1]); break;
    case IPCConfigEnableSloppyFocus: conf.sloppy_focus = d[1]; break;
    case IPCConfigEnableResizeHints: conf.resize_hints = d[1];
    case IPCConfigStickyWindows: conf.sticky_windows = d[1]; break;
    case IPCConfigEnableBorders: conf.borders = d[1]; break;
    case IPCConfigEnableLastWindowFocusing:
      conf.last_window_focusing = d[1];
      break;
    case IPCConfigApplySettings: conf.apply_settings = d[1]; break;
    case IPCConfigReplayClickOnFocus: conf.replay_click_on_focus = d[1]; break;
    case IPCConfigPointerActions:
      for (int i = 0; i < NR_BUTTONS; i++) {
        conf.pointer_actions[i] = (enum pointer_action) d[i + 1];
      }
      ungrab_buttons();
      grab_buttons();
      break;
    case IPCConfigPointerModifier:
      conf.pointer_modifier = d[1];
      ungrab_buttons();
      grab_buttons();
      break;
    case IPCConfigClickToFocus:
      if (d[1] == UINT32_MAX)
        conf.click_to_focus = -1;
      else
        conf.click_to_focus = d[1];
      ungrab_buttons();
      grab_buttons();
      break;
    case IPCConfigBarPadding:
      conf.bar_padding[0] = d[1];
      conf.bar_padding[1] = d[2];
      conf.bar_padding[2] = d[3];
      //conf.bar_padding[3] = d[4];
      for (auto& win : win_list) {
        fit_on_screen(&win);
      }
      break;
    default: DMSG("!!! unhandled config key %d\n", key); break;
    }
  }

  void pointer_init(void)
  {
    num_lock    = pointer_modfield_from_keysym(XK_Num_Lock);
    caps_lock   = pointer_modfield_from_keysym(XK_Caps_Lock);
    scroll_lock = pointer_modfield_from_keysym(XK_Scroll_Lock);

    if (caps_lock == XCB_NO_SYMBOL) caps_lock = XCB_MOD_MASK_LOCK;
  }

  int16_t pointer_modfield_from_keysym(xcb_keysym_t keysym)
  {
    uint16_t modfield       = 0;
    xcb_keycode_t *keycodes = nullptr, *mod_keycodes = nullptr;
    xcb_get_modifier_mapping_reply_t* reply = nullptr;
    xcb_key_symbols_t* symbols              = xcb_key_symbols_alloc(conn);

    /* wrapped all of them in an ugly if to prevent getting values when
       we don't need them */
    if (!((keycodes = xcb_key_symbols_get_keycode(symbols, keysym)) ==
            nullptr ||
          (reply = xcb_get_modifier_mapping_reply(
             conn, xcb_get_modifier_mapping(conn), nullptr)) == nullptr ||
          reply->keycodes_per_modifier < 1 ||
          (mod_keycodes = xcb_get_modifier_mapping_keycodes(reply)) ==
            nullptr)) {
      int num_mod = xcb_get_modifier_mapping_keycodes_length(reply) /
                    reply->keycodes_per_modifier;

      for (int i = 0; i < num_mod; i++) {
        for (int j = 0; j < reply->keycodes_per_modifier; j++) {
          xcb_keycode_t mk = mod_keycodes[i * reply->keycodes_per_modifier + j];
          if (mk != XCB_NO_SYMBOL) {
            for (xcb_keycode_t* k = keycodes; *k != XCB_NO_SYMBOL; k++) {
              if (*k == mk) modfield |= (1 << i);
            }
          }
        }
      }
    }

    xcb_key_symbols_free(symbols);
    free(keycodes);
    free(reply);
    return modfield;
  }

  void window_grab_buttons(xcb_window_t win)
  {
    for (int i = 0; i < NR_BUTTONS; i++) {
      if (conf.click_to_focus == (int8_t) XCB_BUTTON_INDEX_ANY ||
          conf.click_to_focus == (int8_t) mouse_buttons[i])
        window_grab_button(win, mouse_buttons[i], XCB_NONE);
      if (conf.pointer_actions[i] != POINTER_ACTION_NOTHING)
        window_grab_button(win, mouse_buttons[i], conf.pointer_modifier);
    }
    DMSG("grabbed buttons on 0x%08x\n", win);
  }

  void window_grab_button(xcb_window_t win, uint8_t button, uint16_t modifier)
  {
#define GRAB(b, m)                                                             \
  xcb_grab_button(conn, false, win, XCB_EVENT_MASK_BUTTON_PRESS,               \
                  XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, \
                  b, m)

    GRAB(button, modifier);
    if (num_lock != XCB_NO_SYMBOL && caps_lock != XCB_NO_SYMBOL &&
        scroll_lock != XCB_NO_SYMBOL)
      GRAB(button, modifier | num_lock | caps_lock | scroll_lock);
    if (num_lock != XCB_NO_SYMBOL && caps_lock != XCB_NO_SYMBOL)
      GRAB(button, modifier | num_lock | caps_lock);
    if (caps_lock != XCB_NO_SYMBOL && scroll_lock != XCB_NO_SYMBOL)
      GRAB(button, modifier | caps_lock | scroll_lock);
    if (num_lock != XCB_NO_SYMBOL && scroll_lock != XCB_NO_SYMBOL)
      GRAB(button, modifier | num_lock | scroll_lock);
    if (num_lock != XCB_NO_SYMBOL) GRAB(button, modifier | num_lock);
    if (caps_lock != XCB_NO_SYMBOL) GRAB(button, modifier | caps_lock);
    if (scroll_lock != XCB_NO_SYMBOL) GRAB(button, modifier | scroll_lock);
  }

  /*
   * Returns true if pointer needs to be synced.
   */
  bool pointer_grab(enum pointer_action pac)
  {
    xcb_window_t win = XCB_NONE;
    xcb_point_t pos  = (xcb_point_t){0, 0};
    Client* client;

    xcb_query_pointer_reply_t* qr = xcb_query_pointer_reply(
      conn, xcb_query_pointer(conn, scr->root), nullptr);

    if (qr == nullptr) {
      return false;
    }

    win = qr->child;
    pos = (xcb_point_t){qr->root_x, qr->root_y};
    free(qr);

    client = find_client(&win);
    if (client == nullptr) return true;

    raise_window(client->window);
    if (pac == POINTER_ACTION_FOCUS) {
      DMSG("grabbing pointer to focus on 0x%08x\n", client->window);
      if (client != focused_win) {
        set_focused(client);
        if (!conf.replay_click_on_focus) return true;
      }
      return false;
    }

    if (is_maxed(client)) {
      return true;
    }

    xcb_grab_pointer_reply_t* reply = xcb_grab_pointer_reply(
      conn,
      xcb_grab_pointer(
        conn, 0, scr->root,
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
        XCB_CURRENT_TIME),
      nullptr);

    if (reply == nullptr || reply->status != XCB_GRAB_STATUS_SUCCESS) {
      free(reply);
      return true;
    }
    free(reply);

    track_pointer(client, pac, pos);

    return true;
  }

  enum resize_handle get_handle(Client* client,
                                xcb_point_t pos,
                                enum pointer_action pac)
  {
    if (client == nullptr)
      return pac == POINTER_ACTION_RESIZE_SIDE ? HANDLE_LEFT : HANDLE_TOP_LEFT;

    enum resize_handle handle;
    WindowGeom geom = client->geom;

    if (pac == POINTER_ACTION_RESIZE_SIDE) {
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
        } else
          handle = HANDLE_BOTTOM;
      } else {
        if (left_of_b)
          handle = HANDLE_TOP;
        else
          handle = HANDLE_RIGHT;
      }
    } else if (pac == POINTER_ACTION_RESIZE_CORNER) {
      int16_t mid_x = geom.x + geom.width / 2;
      int16_t mid_y = geom.y + geom.height / 2;

      if (pos.y < mid_y) {
        if (pos.x < mid_x)
          handle = HANDLE_TOP_LEFT;
        else
          handle = HANDLE_TOP_RIGHT;
      } else {
        if (pos.x < mid_x)
          handle = HANDLE_BOTTOM_LEFT;
        else
          handle = HANDLE_BOTTOM_RIGHT;
      }
    } else {
      handle = HANDLE_TOP_LEFT;
    }

    return handle;
  }

  void track_pointer(Client* client, enum pointer_action pac, xcb_point_t pos)
  {
    enum resize_handle handle = get_handle(client, pos, pac);
    WindowGeom geom           = client->geom;

    xcb_generic_event_t* ev = nullptr;

    bool grabbing   = true;
    Client* grabbed = client;

    if (client == nullptr) return;

    do {
      free(ev);
      while ((ev = xcb_wait_for_event(conn)) == nullptr) xcb_flush(conn);
      uint8_t resp = EVENT_MASK(ev->response_type);

      if (resp == XCB_MOTION_NOTIFY) {
        xcb_motion_notify_event_t* e = (xcb_motion_notify_event_t*) ev;
        DMSG(
          "tracking window by mouse root_x = %d  root_y = %d  posx = %d  posy "
          "= %d\n",
          e->root_x, e->root_y, pos.x, pos.y);
        int16_t dx = e->root_x - pos.x;
        int16_t dy = e->root_y - pos.y;
        int32_t x = client->geom.x, y = client->geom.y,
                width = client->geom.width, height = client->geom.height;

        if (pac == POINTER_ACTION_MOVE) {
          client->geom.x = geom.x + dx;
          client->geom.y = geom.y + dy;
          fit_on_screen(client);
          teleport_window(client->window, client->geom.x, client->geom.y);
        } else if (pac == POINTER_ACTION_RESIZE_SIDE ||
                   pac == POINTER_ACTION_RESIZE_CORNER) {
          DMSG("dx: %d\tdy: %d\n", dx, dy);
          if (conf.resize_hints) {
            dx /= client->width_inc;
            dx *= client->width_inc;

            dy /= client->width_inc;
            dy *= client->width_inc;
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
          // if (width < client->min_width) {
            // width = client->min_width;
            // x     = client->geom.x;
          // }
// 
          // if (height < client->min_height) {
            // height = client->min_height;
            // y      = client->geom.y;
          // }

          int16_t monx, mony;
          uint16_t monw, monh;
          get_monitor_size(client, &monx, &mony, &monw, &monh);
          if (x < monx) x = client->geom.x;
          if (y < mony) y = client->geom.y;
          if (x + width > monx + monw) {
            x     = client->geom.x;
            width = client->geom.width;
          }
          if (y + height > mony + monh) {
            y      = client->geom.y;
            height = client->geom.height;
          }

          DMSG("moving by %d %d\n", x - geom.x, y - geom.y);
          DMSG("resizing by %d %d\n", width - geom.width, height - geom.height);
          client->geom.x      = x;
          client->geom.width  = width;
          client->geom.height = height;
          client->geom.y      = y;

          fit_on_screen(client);
          resize_window_absolute(client->window, client->geom.width,
                                 client->geom.height);
          teleport_window(client->window, client->geom.x, client->geom.y);
          xcb_flush(conn);
        }
      } else if (resp == XCB_BUTTON_RELEASE) {
        grabbing = false;
      } else {
        if (events[resp] != nullptr) (events[resp])(ev);
      }
    } while (grabbing && grabbed != nullptr);
    free(ev);

    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
  }

  void grab_buttons(void)
  {
    for (auto& client : win_list) {
      window_grab_buttons(client.window);
    }
  }

  void ungrab_buttons(void)
  {
    for (auto& client : win_list) {
      xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, client.window,
                        XCB_MOD_MASK_ANY);
      DMSG("ungrabbed buttons on 0x%08x\n", client.window);
    }
  }

  void usage(char* name)
  {
    fprintf(stderr, "Usage: %s [-h|-v|-c CONFIG_PATH]\n", name);

    exit(EXIT_SUCCESS);
  }

  void version(void)
  {
    fprintf(stderr, "%s %s\n", __NAME__, __THIS_VERSION__);
    fprintf(stderr, "Copyright (c) 2016-2017 Tudor Ioan Roman\n");
    fprintf(stderr, "Released under the ISC License\n");

    exit(EXIT_SUCCESS);
  }

  void load_defaults(void)
  {
    conf.border_width                   = BORDER_WIDTH;
    conf.focus_color                    = COLOR_FOCUS;
    conf.unfocus_color                  = COLOR_UNFOCUS;
    conf.gap_left                       = GAP;
    conf.gap_down                       = GAP;
    conf.gap_up                         = GAP;
    conf.gap_right                      = GAP;
    conf.grid_gap                       = GRID_GAP;
    conf.cursor_position                = CURSOR_POSITION;
    conf.workspaces                     = WORKSPACES;
    conf.sloppy_focus                   = SLOPPY_FOCUS;
    conf.resize_hints                   = RESIZE_HINTS;
    conf.sticky_windows                 = STICKY_WINDOWS;
    conf.borders                        = BORDERS;
    conf.last_window_focusing           = LAST_WINDOW_FOCUSING;
    conf.apply_settings                 = APPLY_SETTINGS;
    conf.replay_click_on_focus          = REPLAY_CLICK_ON_FOCUS;
    conf.pointer_actions[BUTTON_LEFT]   = DEFAULT_LEFT_BUTTON_ACTION;
    conf.pointer_actions[BUTTON_MIDDLE] = DEFAULT_MIDDLE_BUTTON_ACTION;
    conf.pointer_actions[BUTTON_RIGHT]  = DEFAULT_RIGHT_BUTTON_ACTION;
    conf.bar_shown                      = DEFAULT_BAR_SHOWN;
    conf.bar_padding[0]                 = BAR_PADDING_LEFT;
    conf.bar_padding[1]                 = BAR_PADDING_TOP;
    conf.bar_padding[2]                 = BAR_PADDING_RIGHT;
    conf.bar_padding[3]                 = BAR_PADDING_BOTTOM;
    conf.pointer_modifier               = POINTER_MODIFIER;
    conf.click_to_focus                 = CLICK_TO_FOCUS_BUTTON;
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

} // namespace

int main(int argc, char* argv[])
{
  int opt;
  char* config_path = (char*) malloc(MAXLEN * sizeof(char));
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
  register_ipc_handlers();
  load_defaults();

  if (setup() < 0) errx(EXIT_FAILURE, "error connecting to X");
  /* if not set, get path of the rc file */
  if (config_path[0] == '\0') {
    char* xdg_home = getenv("XDG_CONFIG_HOME");
    if (xdg_home != nullptr)
      snprintf(config_path, MAXLEN * sizeof(char), "%s/%s/%s", xdg_home,
               __NAME__, __CONFIG_NAME__);
    else
      snprintf(config_path, MAXLEN * sizeof(char), "%s/%s/%s/%s",
               getenv("HOME"), ".config", __NAME__, __CONFIG_NAME__);
  }

  signal(SIGCHLD, handle_child);

  /* execute config file */
  load_config(config_path);
  run();

  free(config_path);

  return exit_code;
}
