/* Copyright (c) 2016, 2017 Tudor Ioan Roman. All rights reserved. */
/* Licensed under the ISC License. See the LICENSE file in the project root for
 * full license information. */

#include <algorithm>
#include <iostream>
#include <vector>

#include <X11/keysym.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

#include <err.h>
#include <unistd.h>
#include <ctgmath>

#include <sys/wait.h>

#include "common.hpp"
#include "config.hpp"
#include "ipc.hpp"
#include "types.hpp"
#include "util.hpp"
#include "wm.hpp"

#define EVENT_MASK(ev) (((ev) & ~0x80))
/* XCB event with the biggest value */
#define LAST_XCB_EVENT XCB_GET_MODIFIER_MAPPING
#define PI 3.14159265

namespace wm {

  // Definitions

  /* connection to the X server */
  xcb_connection_t* conn;
  xcb_ewmh_connection_t* ewmh;
  xcb_screen_t* scr;
  Conf conf;
  /* number of the screen we're using */
  int scrno;
  /* base for checking randr events */
  int randr_base;
  bool halt;
  bool should_close;
  int exit_code;

  namespace {
    std::vector<Workspace> _workspaces;
    Workspace* _current_ws;

    /* atoms identifiers */
    enum { WM_DELETE_WINDOW, _IPC_ATOM_COMMAND, NR_ATOMS };

    /* keyboard modifiers (for mouse support) */
    uint16_t num_lock, caps_lock, scroll_lock;
    const xcb_button_index_t mouse_buttons[] = {
      XCB_BUTTON_INDEX_1,
      XCB_BUTTON_INDEX_2,
      XCB_BUTTON_INDEX_3,
    };
    /* list of all windows */

    std::vector<Monitor> mon_list;
    /* Bar windows */
    nomove_vector<Client> bar_list;
    /* Windows to keep on top */
    std::vector<xcb_window_t> on_top;

    const char* atom_names[NR_ATOMS] = {
      "WM_DELETE_WINDOW",
      ATOM_COMMAND,
    };

    xcb_atom_t ATOMS[NR_ATOMS];

    /* function handlers for events received from the X server */
    void (*events[LAST_XCB_EVENT + 1])(xcb_generic_event_t*);
  } // namespace

  std::vector<Workspace>& workspaces() noexcept
  {
    return _workspaces;
  }

  Workspace& get_workspace(int idx) {
    if (idx > _workspaces.size()) {
      DMSG("Attempt to access workspace %d. Only %d exist", idx, (int) _workspaces.size());
      throw std::runtime_error("Out of bounds");
    }
    return _workspaces[idx];
  }

  Workspace& current_ws() noexcept {
    return *_current_ws;
  }

  /*
   * Gracefully disconnect.
   */

  void cleanup()
  {
    xcb_set_input_focus(conn, XCB_NONE, XCB_INPUT_FOCUS_POINTER_ROOT,
                        XCB_CURRENT_TIME);
    ungrab_buttons();
    if (ewmh != nullptr) {
      xcb_ewmh_connection_wipe(ewmh);
    }
    if (conn != nullptr) {
      xcb_disconnect(conn);
    }
  }

  /*
   * Connect to the X server and initialize some things.
   */

  int setup()
  {
    /* init xcb and grab events */
    unsigned int values[1];
    int mask;

    conn = xcb_connect(nullptr, &scrno);
    if (xcb_connection_has_error(conn) != 0) {
      return -1;
    }

    /* get the first screen. hope it's the last one too */
    scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

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
    if (ewmh == nullptr) {
      warnx("couldn't set up ewmh connection");
    }
    xcb_intern_atom_cookie_t* cookie = xcb_ewmh_init_atoms(conn, ewmh);
    xcb_ewmh_init_atoms_replies(ewmh, cookie, nullptr);
    xcb_ewmh_set_wm_pid(ewmh, scr->root, getpid());
    xcb_ewmh_set_wm_name(ewmh, scr->root, strlen(__NAME__), __NAME__);
    xcb_ewmh_set_current_desktop(ewmh, 0, 0);
    xcb_ewmh_set_number_of_desktops(ewmh, 0, WORKSPACES);

    /* get various atoms for icccm and ewmh */
    for (int i = 0; i < NR_ATOMS; i++) {
      ATOMS[i] = get_atom(atom_names[i]);
    }

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
      ewmh->_NET_WM_DESKTOP,
      ewmh->_NET_SUPPORTING_WM_CHECK,
      ATOMS[WM_DELETE_WINDOW],
    };
    xcb_ewmh_set_supported(ewmh, scrno,
                           sizeof(supported_atoms) / sizeof(xcb_atom_t),
                           supported_atoms);

    xcb_ewmh_set_supporting_wm_check(ewmh, scr->root, scr->root);

    pointer_init();

    /* send requests */
    xcb_flush(conn);

    randr_base = setup_randr();

    // workspaces.reserve(conf.workspaces);
    for (uint32_t i = 0; i < conf.workspaces; i++) {
      _workspaces.push_back(Workspace::make(i));
    }
    _current_ws = &_workspaces[0];
    return 0;
  }

  /*
   * Tells the server we want to use randr.
   */

  int setup_randr()
  {
    int base;
    const xcb_query_extension_reply_t* r =
      xcb_get_extension_data(conn, &xcb_randr_id);

    if (r->present == 0u) {
      return -1;
    }
    {
      get_randr();
    }

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

  void get_randr()
  {
    int len;
    xcb_randr_get_screen_resources_current_cookie_t c =
      xcb_randr_get_screen_resources_current(conn, scr->root);
    xcb_randr_get_screen_resources_current_reply_t* r =
      xcb_randr_get_screen_resources_current_reply(conn, c, nullptr);

    if (r == nullptr) {
      return;
    }

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

    for (int i = 0; i < len; i++) {
      out_cookie[i] = xcb_randr_get_output_info(conn, outputs[i], timestamp);
    }

    for (int i = 0; i < len; i++) {
      output = xcb_randr_get_output_info_reply(conn, out_cookie[i], nullptr);
      if (output == nullptr) {
        continue;
      }

      name_len = xcb_randr_get_output_info_name_length(output);
      if (16 < name_len) {
        name_len = 16;
      }

      /* +1 for the null character */
      name = (char*) malloc(name_len + 1);
      /* make sure the name is at most name_len + 1 length
       * or we may run into problems. */
      snprintf(name, name_len + 1, "%.*s", name_len,
               xcb_randr_get_output_info_name(output));

      if (output->crtc != XCB_NONE) {
        info_c = xcb_randr_get_crtc_info(conn, output->crtc, timestamp);
        crtc   = xcb_randr_get_crtc_info_reply(conn, info_c, nullptr);

        if (crtc == nullptr) {
          break;
        }

        clonemon = find_clones(outputs[i], crtc->x, crtc->y);
        if (clonemon != nullptr) {
          continue;
        }

        mon = find_monitor(outputs[i]);
        if (mon == nullptr) {
          add_monitor(outputs[i], name, crtc->x, crtc->y, crtc->width,
                      crtc->height);
        } else {
          mon->x      = crtc->x;
          mon->y      = crtc->y;
          mon->width  = crtc->width;
          mon->height = crtc->height;

          arrange_by_monitor(*mon);
        }

        free(crtc);
      } else {
        /* Check if the monitor was used before
         * becoming disabled. */
        mon = find_monitor(outputs[i]);
        if (mon != nullptr) {
          for (auto&& client : current_ws().windows) {
            /* Move window from this monitor to
             * either the next one or the first one. */
            if (client.monitor == mon) {
              auto iter =
                std::find(mon_list.begin(), mon_list.end(), *client.monitor);
              if (iter != mon_list.end()) {
                iter++;
              }
              if (iter == mon_list.end()) {
                iter = mon_list.begin();
              }
              if (iter != mon_list.end()) {
                client.monitor = &*iter;
              }
              fit_on_screen(client);
            }
          }

          /* Monitor not active. Delete it. */
          free_monitor(*mon);
        }
      }

      if (output != nullptr) {
        free(output);
      }
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
    if (iter == mon_list.end()) {
      return nullptr;
    }
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
    if (iter == mon_list.end()) {
      return nullptr;
    }
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
    if (iter == mon_list.end()) {
      return nullptr;
    }
    return &*iter;
  }

  /*
   * Add a monitor to the global monitor list.
   */

  Monitor& add_monitor(xcb_randr_output_t mon,
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

    return mon_list.emplace_back(monitor);
  }

  /*
   * Free a monitor from the global monitor list.
   */

  void free_monitor(Monitor& mon)
  {
    mon_list.erase(std::remove(mon_list.begin(), mon_list.end(), mon),
                   mon_list.end());
  }

  /*
   * Get information about a certain monitor situated in a window: coordinates
   * and size.
   */

  void get_monitor_size(Client& client,
                        int16_t& mon_x,
                        int16_t& mon_y,
                        uint16_t& mon_width,
                        uint16_t& mon_height,
                        bool include_padding)
  {
    if (client.monitor == nullptr) {
      mon_x = mon_y = 0;
      mon_width     = scr->width_in_pixels;
      mon_height    = scr->height_in_pixels;
    } else {
      mon_x      = client.monitor->x;
      mon_y      = client.monitor->y;
      mon_width  = client.monitor->width;
      mon_height = client.monitor->height;
    }
    if (!include_padding) {
      return;
    }

    auto& workspace = *client.workspace;
    if (show_bar(workspace)) {
      mon_x += conf.bar_padding[0];
      mon_y += conf.bar_padding[1];

      mon_width -= conf.bar_padding[0] + conf.bar_padding[2];

      mon_height -= conf.bar_padding[1] + conf.bar_padding[3];
    }
  }

  /*
   * Arrange clients on a monitor.
   */

  void arrange_by_monitor(Monitor& mon)
  {
    for (auto& client : current_ws().windows) {
      if (client.monitor == &mon) {
        fit_on_screen(client);
      }
    }
  }

  /*
   * Wait for events and handle them.
   */

  void run()
  {
    xcb_generic_event_t* ev;

    halt         = false;
    should_close = false;
    exit_code    = EXIT_SUCCESS;
    while (!halt) {
      xcb_flush(conn);
      ev = xcb_wait_for_event(conn);
      if (ev != nullptr) {
        DMSG("X Event %d\n", ev->response_type & ~0x80);
        if (ev->response_type == randr_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
          get_randr();
          DMSG("Screen layout changed\n");
        }
        if (events[EVENT_MASK(ev->response_type)] != nullptr) {
          (events[EVENT_MASK(ev->response_type)])(ev);
        }
        free(ev);
      }
      if (should_close) {
        if (std::none_of(std::begin(_workspaces), std::end(_workspaces),
                         [](auto& ws) { return ws.windows.size() > 0; })) {
          halt = true;
        }
      }
    }
  }

  /*
   * Initialize a window for further work.
   */

  Client* setup_window(xcb_window_t win, bool require_type)
  {
    uint32_t values[2];
    xcb_ewmh_get_atoms_reply_t win_type;
    xcb_atom_t atom;
    xcb_size_hints_t hints;
    bool is_bar = false;
    bool map    = false;
    bool ignore = require_type;

    // std::clog << "Setting up window " << win << std::endl;

    if (xcb_ewmh_get_wm_window_type_reply(
          ewmh, xcb_ewmh_get_wm_window_type(ewmh, win), &win_type, nullptr) ==
        1) {
      unsigned int i = 0;
      /* if the window is a toolbar or a dock, map it and ignore it */
      while (i < win_type.atoms_len) {
        atom = win_type.atoms[i];
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR ||
            atom == ewmh->_NET_WM_WINDOW_TYPE_DOCK) {
          is_bar = true;
          ignore = false;
        }
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_DESKTOP) {
          map    = true;
          ignore = true;
        }
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_NOTIFICATION) {
          on_top.push_back(win);
          // std::clog << "Notification window: " << win << std::endl;
          map    = true;
          ignore = true;
        }
        i++;
      }
      if (map) {
        xcb_map_window(conn, win);
      }
      if (ignore) {
        xcb_ewmh_get_atoms_reply_wipe(&win_type);
        return nullptr;
      }
      if (is_bar) {
        Client client = Client::make(win);

        /* initialize variables */
        client.window = win;

        xcb_ewmh_get_atoms_reply_wipe(&win_type);
        bar_list.erase(client);
        bar_list.push_back(std::move(client));
        update_bar_visibility();
        return nullptr;
      }
    } else {
      if (require_type) {
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

    Client cl = Client::make(win);

    /* initialize variables */
    cl.monitor   = nullptr;
    cl.mapped    = false;
    cl.workspace = nullptr;

    get_geometry(cl.window, cl.geom.x, cl.geom.y, cl.geom.width,
                 cl.geom.height);

    xcb_icccm_get_wm_normal_hints_reply(
      conn, xcb_icccm_get_wm_normal_hints_unchecked(conn, win), &hints,
      nullptr);

    if ((hints.flags & XCB_ICCCM_SIZE_HINT_US_POSITION) != 0u) {
      cl.geom.set_by_user = true;
    }

    if ((hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) != 0u) {
      cl.min_width  = hints.min_width;
      cl.min_height = hints.min_height;
    }

    if ((hints.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) != 0u) {
      cl.width_inc  = hints.width_inc;
      cl.height_inc = hints.height_inc;
    }

    DMSG("new window was born 0x%08x\n", cl.window);

    current_ws().windows.erase(cl);
    Client& cli = current_ws().windows.push_back(std::move(cl));
    return &cli;
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

  /*
   * Set focus state to active or inactive without raising the window.
   */

  void set_focused_no_raise(Client& client)
  {
    long data[] = {
      XCB_ICCCM_WM_STATE_NORMAL,
      XCB_NONE,
    };
    /* focus the window */
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, client.window,
                        XCB_CURRENT_TIME);

    /* set ewmh property */
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, scr->root,
                        ewmh->_NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 32, 1,
                        &client.window);

    /* set window state */
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client.window,
                        ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, data);

    // move the window to the back of the vector
    current_ws().windows.rotate_to_back(std::find(
      current_ws().windows.begin(), current_ws().windows.end(), client));

    refresh_borders();

    window_grab_buttons(client.window);
  }

  /*
   * Focus and raise.
   */

  void set_focused(Client& client)
  {
    set_focused_no_raise(client);
    
    raise_window(client.window);
  }

  /*
   * Focus last best focus (in a valid workspace, mapped, etc)
   */

  void set_focused_last_best()
  {
    auto iter =
      std::find_if(current_ws().windows.rbegin(), current_ws().windows.rend(),
                   [](Client& cl) { return cl.mapped; });
    if (iter != current_ws().windows.rend()) {
      set_focused(*iter);
    }
  }

  /*
   * Put window at the top of the window stack.
   */

  void raise_window(xcb_window_t win)
  {
    uint32_t values[1] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
    for (auto ot_win : on_top) {
      xcb_configure_window(conn, ot_win, XCB_CONFIG_WINDOW_STACK_MODE, values);
    }
  }
  /*
   * Returns true if the client supports the given protocol atom (like
   * WM_DELETE_WINDOW)
   *
   */
  bool window_supports_protocol(xcb_window_t window, xcb_atom_t atom)
  {
    xcb_get_property_cookie_t cookie;
    xcb_icccm_get_wm_protocols_reply_t protocols;
    bool result = false;

    cookie = xcb_icccm_get_wm_protocols(conn, window, ewmh->WM_PROTOCOLS);
    if (xcb_icccm_get_wm_protocols_reply(conn, cookie, &protocols, nullptr) !=
        1) {
      return false;
    }

    /* Check if the client’s protocols have the requested atom set */
    for (uint32_t i = 0; i < protocols.atoms_len; i++) {
      if (protocols.atoms[i] == atom) {
        result = true;
      }
    }

    xcb_icccm_get_wm_protocols_reply_wipe(&protocols);

    return result;
  }

  /*
   * Ask window to close gracefully. If the window doesn't respond, kill it.
   */

  void close_window(Client& client)
  {
    xcb_window_t win = client.window;

    if (window_supports_protocol(win, ATOMS[WM_DELETE_WINDOW])) {
      DMSG("Deleting window %d", win);
      delete_window(win);
    } else {
      DMSG("Destroying window %d", win);
      xcb_destroy_window(conn, win);
    }
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

    if (win == scr->root || win == 0) {
      return;
    }

    xcb_configure_window(conn, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                         values);

    xcb_flush(conn);
  }

  /*
   * Moves the window by a certain amount.
   */

  void move_window(xcb_window_t win, int16_t x, int16_t y)
  {
    int16_t win_x = 0, win_y = 0;
    uint16_t win_w = 0, win_h = 0;

    if (!is_mapped(win) || win == scr->root) {
      return;
    }

    get_geometry(win, win_x, win_y, win_w, win_h);

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
    int32_t aw, ah;

    Client* client = find_client(win);
    if (client == nullptr) {
      return;
    }

    aw = client->geom.width;
    ah = client->geom.height;

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

    client->geom.width =
      aw - static_cast<int>(conf.resize_hints) * (aw % client->width_inc);
    client->geom.height =
      ah - static_cast<int>(conf.resize_hints) * (ah % client->height_inc);

    resize_window_absolute(win, client->geom.width, client->geom.height);
  }

  /*
   * Fit window on screen if too big.
   */

  void fit_on_screen(Client& client)
  {
    int16_t mon_x, mon_y;
    uint16_t mon_width, mon_height;
    bool will_resize, will_move;

    if (client.allow_offscreen) {
      return;
    }

    will_resize = will_move = false;
    if (is_maxed(client)) {
      refresh_maxed(client);
      return;
    }
    get_monitor_size(client, mon_x, mon_y, mon_width, mon_height);
    if (client.geom.width == mon_width && client.geom.height == mon_height) {
      client.geom.x = mon_x;
      client.geom.y = mon_y;
      client.geom.width -= 2 * conf.border_width;
      client.geom.height -= 2 * conf.border_width;
      maximize_window(client);
      return;
    }

    /* Is it outside the display? */
    if (client.geom.x > mon_x + mon_width ||
        client.geom.y > mon_y + mon_height || client.geom.x < mon_x ||
        client.geom.y < mon_y) {
      will_move = true;
      if (client.geom.x > mon_x + mon_width) {
        client.geom.x =
          mon_x + mon_width - client.geom.width - 2 * conf.border_width;
      } else if (client.geom.x < mon_x) {
        client.geom.x = mon_x;
      }
      if (client.geom.y > mon_y + mon_height) {
        client.geom.y =
          mon_y + mon_height - client.geom.height - 2 * conf.border_width;
      } else if (client.geom.y < mon_y) {
        client.geom.y = mon_y;
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
    if (client.geom.width + 2 * conf.border_width > mon_width) {
      client.geom.x     = mon_x;
      client.geom.width = mon_width - 2 * conf.border_width;
      will_move = will_resize = true;
    } else if (client.geom.x + client.geom.width + 2 * conf.border_width >
               mon_x + mon_width) {
      client.geom.x =
        mon_x + mon_width - client.geom.width - 2 * conf.border_width;
      will_move = true;
    }

    if (client.geom.height + 2 * conf.border_width > mon_height) {
      client.geom.y      = mon_y;
      client.geom.height = mon_height - 2 * conf.border_width;
      will_move = will_resize = true;
    } else if (client.geom.y + client.geom.height + 2 * conf.border_width >
               mon_y + mon_height) {
      client.geom.y =
        mon_y + mon_height - client.geom.height - 2 * conf.border_width;
      will_move = true;
    }

    if (will_move) {
      teleport_window(client.window, client.geom.x, client.geom.y);
    }
    if (will_resize) {
      resize_window_absolute(client.window, client.geom.width,
                             client.geom.height);
    }
  }

  void refresh_maxed(Client& client)
  {
    if (client.hmaxed) {
      hmaximize_window(client);
    }
    if (client.vmaxed) {
      vmaximize_window(client);
    }
    if (client.fullscreen) {
      fullscreen_window(client);
    }
  }

  void fullscreen_window(Client& client)
  {
    int16_t mon_x;
    int16_t mon_y;
    uint16_t mon_width;
    uint16_t mon_height;
    get_monitor_size(client, mon_x, mon_y, mon_width, mon_height, false);

    /* maximized windows don't have borders */
    if ((client.geom.width != mon_width || client.geom.height != mon_height) &&
        !is_maxed(client)) {
      save_original_size(client);
    }
    uint32_t values[1] = {0};
    xcb_configure_window(conn, client.window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);

    client.geom.x      = mon_x;
    client.geom.y      = mon_y;
    client.geom.width  = mon_width;
    client.geom.height = mon_height;

    teleport_window(client.window, client.geom.x, client.geom.y);
    resize_window_absolute(client.window, client.geom.width,
                           client.geom.height);
    client.fullscreen = true;
    set_focused_no_raise(client);

    update_ewmh_wm_state(client);
  }

  void maximize_window(Client& client)
  {
    int16_t mon_x;
    int16_t mon_y;
    uint16_t mon_width;
    uint16_t mon_height;
    get_monitor_size(client, mon_x, mon_y, mon_width, mon_height);

    if ((client.geom.width != mon_width || client.geom.height != mon_height) &&
        !is_maxed(client)) {
      save_original_size(client);
    }
    /* maximized windows don't have borders */

    uint32_t values[1] = {0};
    xcb_configure_window(conn, client.window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);

    client.geom.x      = mon_x;
    client.geom.y      = mon_y;
    client.geom.width  = mon_width;
    client.geom.height = mon_height;

    teleport_window(client.window, client.geom.x, client.geom.y);
    resize_window_absolute(client.window, client.geom.width,
                           client.geom.height);
    client.vmaxed = true;
    client.hmaxed = true;
    set_focused_no_raise(client);

    update_ewmh_wm_state(client);
  }

  void hmaximize_window(Client& client)
  {
    if (client.vmaxed) {
      maximize_window(client);
      return;
    }
    unmaximize_geometry(client);


    int16_t mon_x;
    int16_t mon_y;
    uint16_t mon_width;
    uint16_t mon_height;
    get_monitor_size(client, mon_x, mon_y, mon_width, mon_height);

    if (client.geom.width != mon_width) {
      save_original_size(client);
    }
    client.geom.x = mon_x + conf.gap_left;
    client.geom.width =
      mon_width - conf.gap_left - conf.gap_right - 2 * conf.border_width;

    teleport_window(client.window, client.geom.x, client.geom.y);
    resize_window_absolute(client.window, client.geom.width,
                           client.geom.height);
    client.hmaxed = true;

    uint32_t values[1] = {0};
    xcb_configure_window(conn, client.window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);
    update_ewmh_wm_state(client);
  }

  void vmaximize_window(Client& client)
  {
    if (client.hmaxed) {
      maximize_window(client);
      return;
    }
    unmaximize_geometry(client);


    int16_t mon_x;
    int16_t mon_y;
    uint16_t mon_width;
    uint16_t mon_height;
    get_monitor_size(client, mon_x, mon_y, mon_width, mon_height);

    if (client.geom.height != mon_height) {
      save_original_size(client);
    }

    client.geom.y = mon_y + conf.gap_up;
    client.geom.height =
      mon_height - conf.gap_up - conf.gap_down - 2 * conf.border_width;

    teleport_window(client.window, client.geom.x, client.geom.y);
    resize_window_absolute(client.window, client.geom.width,
                           client.geom.height);
    client.vmaxed = true;

    update_ewmh_wm_state(client);
  }

  void unmaximize_geometry(Client& client)
  {
    client.geom.x      = client.orig_geom.x;
    client.geom.y      = client.orig_geom.y;
    client.geom.width  = client.orig_geom.width;
    client.geom.height = client.orig_geom.height;
    client.fullscreen = client.hmaxed = client.vmaxed = false;
  }

  void unmaximize_window(Client& client)
  {
    xcb_atom_t state[] = {XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE};

    if (client.fullscreen) {
      client.fullscreen = false;
      refresh_maxed(client);
      return;
    }
    unmaximize_geometry(client);


    teleport_window(client.window, client.geom.x, client.geom.y);
    resize_window_absolute(client.window, client.geom.width,
                           client.geom.height);
    set_borders(client, conf.unfocus_color);

    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client.window,
                        ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, state);
  }

  bool is_maxed(Client& client)
  {
    return client.fullscreen || client.vmaxed || client.hmaxed;
  }

  void cycle_window(Client& client)
  {
    auto iter =
      std::find(current_ws().windows.begin(), current_ws().windows.end(), client);
    if (iter == current_ws().windows.end()) {
      return;
    }
    iter = std::find_if(iter, current_ws().windows.end(), [&client](Client& cl) {
      return cl.mapped && cl != client;
    });
    if (iter != current_ws().windows.end()) {
      set_focused(*iter);
      return;
    }
    iter = std::find_if(current_ws().windows.begin(), current_ws().windows.end(),
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

    WinPosition focus_win_pos = get_window_position(CENTER, focused);
    std::vector<Client*> valid_windows;
    for (Client& cl : current_ws().windows) {
      if (cl.window == focused.window) {
        continue;
      }
      if (!cl.mapped) {
        continue;
      }

      WinPosition win_pos = get_window_position(CENTER, cl);

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

      cur_distance = get_distance_between_windows(focused, *cl);
      cur_angle    = get_angle_between_windows(focused, *cl);

      if (is_in_valid_direction(dir, cur_angle, 10)) {
        if (is_overlapping(focused, *cl)) {
          cur_distance = cur_distance * 0.1;
        }
        cur_distance = cur_distance * 0.80;
      } else if (is_in_valid_direction(dir, cur_angle, 25)) {
        if (is_overlapping(focused, *cl)) {
          cur_distance = cur_distance * 0.1;
        }
        cur_distance = cur_distance * 0.85;
      } else if (is_in_valid_direction(dir, cur_angle, 35)) {
        if (is_overlapping(focused, *cl)) {
          cur_distance = cur_distance * 0.1;
        }
        cur_distance = cur_distance * 0.9;
      } else if (is_in_valid_direction(dir, cur_angle, 50)) {
        if (is_overlapping(focused, *cl)) {
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

  void cardinal_move(Client& client, direction dir)
  {
    WinPosition new_pos = {client.geom.x, client.geom.y};
    switch (dir) {
      case direction::NORTH: {
          new_pos.y = -2 * conf.border_width;
          int16_t max = get_window_position(TOP_LEFT, client).y - 2 * conf.border_width;
          for (Client& cl2 : current_ws().windows) {
            auto y2 = get_window_position(BOTTOM_LEFT, cl2).y;
            if (y2 < max) new_pos.y = std::max(y2, new_pos.y);
          }
          new_pos.y += 2 * conf.border_width;
        }
        break;
      case direction::SOUTH: {
          new_pos.y = client.monitor->height;
          int16_t min = get_window_position(BOTTOM_LEFT, client).y + 2 * conf.border_width;
          for (Client& cl2 : current_ws().windows) {
            auto y2 = get_window_position(TOP_LEFT, cl2).y;
            if (y2 > min) new_pos.y = std::min(y2, new_pos.y);
          }
          new_pos.y -= client.geom.height;
          new_pos.y -= 2 * conf.border_width;
        }
        break;
      case direction::WEST: {
          new_pos.x = -2 * conf.border_width;
          int16_t max = get_window_position(TOP_LEFT, client).x - 2 * conf.border_width;
          for (Client& cl2 : current_ws().windows) {
            auto x2 = get_window_position(TOP_RIGHT, cl2).x;
            if (x2 < max) new_pos.x = std::max(x2, new_pos.x);
          }
          new_pos.x += 2 * conf.border_width;
        }
        break;
      case direction::EAST: {
          new_pos.x = client.monitor->width;
          int16_t min = get_window_position(BOTTOM_RIGHT, client).x + 2 * conf.border_width;
          for (Client& cl2 : current_ws().windows) {
            auto x2 = get_window_position(BOTTOM_LEFT, cl2).x;
            if (x2 > min) new_pos.x = std::min(x2, new_pos.x);
          }
          new_pos.x -= client.geom.width;
          new_pos.x -= 2 * conf.border_width;
        }
        break;
    }
    client.geom.x = new_pos.x;
    client.geom.y = new_pos.y;
    teleport_window(client.window, new_pos.x, new_pos.y);
  }

  void cardinal_resize(Client& client, direction dir)
  {

  }

  WinPosition get_window_position(uint32_t mode, Client& client)
  {
    WinPosition pos;
    pos.x = 0;
    pos.y = 0;

    switch (mode) {
    case CENTER:
      pos.x = client.geom.x + (client.geom.width / 2);
      pos.y = client.geom.y + (client.geom.height / 2);
      break;
    case TOP_LEFT:
      pos.x = client.geom.x;
      pos.y = client.geom.y;
      break;
    case TOP_RIGHT:
      pos.x = client.geom.x + client.geom.width;
      pos.y = client.geom.y;
      break;
    case BOTTOM_RIGHT:
      pos.x = client.geom.x + client.geom.width;
      pos.y = client.geom.y + client.geom.height;
      break;
    case BOTTOM_LEFT:
      pos.x = client.geom.x;
      pos.y = client.geom.y + client.geom.height;
      break;
    }
    return pos;
  }

  bool is_in_cardinal_direction(uint32_t direction, Client& a, Client& b)
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
          window_direction <= (-180 + delta)) {
        return true;
      }
      break;
    case SOUTH:
      if (fabs(window_direction) <= (0 + delta)) {
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

  bool is_overlapping(Client& a, Client& b)
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

  float get_angle_between_windows(Client& a, Client& b)
  {
    WinPosition a_pos = get_window_position(CENTER, a);
    WinPosition b_pos = get_window_position(CENTER, b);

    auto dx = (float) (b_pos.x - a_pos.x);
    auto dy = (float) (b_pos.y - a_pos.y);

    if (dx == 0.0 && dy == 0.0) {
      return 0.0;
    }

    return atan2(dx, dy) * (180 / PI);
  }

  float get_distance_between_windows(Client& a, Client& b)
  {
    WinPosition a_pos = get_window_position(CENTER, a);
    WinPosition b_pos = get_window_position(CENTER, b);

    float distance =
      hypot((float) (b_pos.x - a_pos.x), (float) (b_pos.y - a_pos.y));
    return distance;
  }

  void save_original_size(Client& client)
  {
    client.orig_geom.x      = client.geom.x;
    client.orig_geom.y      = client.geom.y;
    client.orig_geom.width  = client.geom.width;
    client.orig_geom.height = client.geom.height;
  }

  /*
   * Get atom by name.
   */

  xcb_atom_t get_atom(const char* name)
  {
    xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(conn, 0u, strlen(name), name);
    xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(conn, cookie, nullptr);

    if (reply == nullptr) {
      return XCB_ATOM_STRING;
    }

    return reply->atom;
  }

  /*
   * Get the mouse pointer's coordinates.
   */

  bool get_pointer_location(const xcb_window_t* win, int16_t* x, int16_t* y)
  {
    xcb_query_pointer_reply_t* pointer;

    pointer =
      xcb_query_pointer_reply(conn, xcb_query_pointer(conn, *win), nullptr);

    *x = pointer->win_x;
    *y = pointer->win_y;

    free(pointer);

    return pointer != nullptr;
  }

  void center_pointer(Client& client)
  {
    int16_t cur_x, cur_y;

    cur_x = cur_y = 0;

    switch (conf.cursor_position) {
    case TOP_LEFT:
      cur_x = -conf.border_width;
      cur_y = -conf.border_width;
      break;
    case TOP_RIGHT:
      cur_x = client.geom.width + conf.border_width;
      cur_y = 0 - conf.border_width;
      break;
    case BOTTOM_LEFT:
      cur_x = 0 - conf.border_width;
      cur_y = client.geom.height + conf.border_width;
      break;
    case BOTTOM_RIGHT:
      cur_x = client.geom.width + conf.border_width;
      cur_y = client.geom.height + conf.border_width;
      break;
    case CENTER:
      cur_x = client.geom.width / 2;
      cur_y = client.geom.height / 2;
      break;
    default: break;
    }

    xcb_warp_pointer(conn, XCB_NONE, client.window, 0, 0, 0, 0, cur_x, cur_y);
    xcb_flush(conn);
  }

  /*
   * Get the client instance with a given window id.
   */

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

  /*
   * Get a window's geometry.
   */

  bool get_geometry(xcb_window_t& win,
                    int16_t& x,
                    int16_t& y,
                    uint16_t& width,
                    uint16_t& height)
  {
    xcb_get_geometry_reply_t* reply =
      xcb_get_geometry_reply(conn, xcb_get_geometry(conn, win), nullptr);

    if (reply == nullptr) {
      return false;
    }
    x      = reply->x;
    y      = reply->y;
    width  = reply->width;
    height = reply->height;

    free(reply);
    return true;
  }

  /*
   * Set the color of the border.
   */

  void set_borders(Client& client, uint32_t color)
  {
    uint32_t values[1];
    values[0] = (client.fullscreen) || (client.vmaxed && client.hmaxed)
                  ? 0
                  : conf.border_width;
    xcb_configure_window(conn, client.window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);
    if (conf.borders) {
      values[0] = color;
      xcb_change_window_attributes(conn, client.window, XCB_CW_BORDER_PIXEL,
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
    if (r == nullptr) {
      return false;
    }

    yes = r->map_state == XCB_MAP_STATE_VIEWABLE;
    free(r);

    return yes;
  }

  /*
   * Deletes and frees a client from the list.
   */

  void free_window(Client& cl)
  {
    DMSG("freeing 0x%08x\n", cl.window);
    current_ws().windows.erase(cl);
    refresh_borders();
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

  void update_client_list()
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
      client = find_client(children[i]);
      if (client != nullptr && client->mapped) {
        add_to_client_list(client->window);
      }
    }

    free(reply);
  }

  void update_wm_desktop(Client& client)
  {
    xcb_ewmh_set_wm_desktop(ewmh, client.window, client.workspace->index);
  }

  void workspace_add_window(Client& client, Workspace& workspace)
  {
    auto* old_ws     = client.workspace;
    client.workspace = &workspace;
    if (old_ws == nullptr) return;
    auto uptr = old_ws->windows.erase(client);
    if (uptr == nullptr) return;
    workspace.windows.push_back(std::move(uptr));
    update_wm_desktop(client);
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
        xcb_unmap_window(conn, win.window);
      }
    }


    Client* last_win = nullptr;
    for (auto& win : current_ws().windows) {
      if (win.should_map) {
        win.user_set_map = false;
        xcb_map_window(conn, win.window);
        refresh_maxed(win);
        last_win = &win;
      } else {
        win.user_set_unmap = false;
        xcb_unmap_window(conn, win.window);
      }
    }

    if (focused_client() == nullptr && last_win != nullptr) {
      set_focused(*last_win);
    }

    refresh_borders();

    update_client_list();
    xcb_ewmh_set_current_desktop(ewmh, 0, current_ws().index);
    update_bar_visibility();
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
        xcb_map_window(conn, win.window);
      }
    } else {
      for (auto& win : bar_list) {
        xcb_unmap_window(conn, win.window);
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
      return;
    }

    if (&client == focused) {
      set_borders(client, conf.focus_color);
    } else {
      set_borders(client, conf.unfocus_color);
    }
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

  void update_ewmh_wm_state(Client& client)
  {
    int i;
    uint32_t values[12];

#define HANDLE_WM_STATE(s)                                                     \
  values[i] = ewmh->_NET_WM_STATE_##s;                                         \
  i++;                                                                         \
  DMSG("ewmh net_wm_state %s present\n", #s);

    i = 0;
    if (client.fullscreen) {
      HANDLE_WM_STATE(FULLSCREEN);
    }
    if (client.vmaxed) {
      HANDLE_WM_STATE(MAXIMIZED_VERT);
    }
    if (client.hmaxed) {
      HANDLE_WM_STATE(MAXIMIZED_HORZ);
    }

    xcb_ewmh_set_wm_state(ewmh, client.window, i, values);
  }

  /*
   * Maximize / unmaximize windows based on ewmh requests.
   */

  void handle_wm_state(Client& client, xcb_atom_t state, unsigned int action)
  {
    int16_t mon_x, mon_y;
    uint16_t mon_w, mon_h;

    get_monitor_size(client, mon_x, mon_y, mon_w, mon_h);

    if (state == ewmh->_NET_WM_STATE_FULLSCREEN) {
      if (action == XCB_EWMH_WM_STATE_ADD) {
        fullscreen_window(client);
      } else if (action == XCB_EWMH_WM_STATE_REMOVE && client.fullscreen) {
        unmaximize_window(client);
        set_focused(client);
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        if (client.fullscreen) {
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
        if (client.vmaxed) {
          unmaximize_window(client);
        }
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        if (client.vmaxed) {
          unmaximize_window(client);
        } else {
          vmaximize_window(client);
        }
      }
    } else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_HORZ) {
      if (action == XCB_EWMH_WM_STATE_ADD) {
        hmaximize_window(client);
      } else if (action == XCB_EWMH_WM_STATE_REMOVE) {
        if (client.hmaxed) {
          unmaximize_window(client);
        }
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        if (client.hmaxed) {
          unmaximize_window(client);
        } else {
          hmaximize_window(client);
        }
      }
    }
  }

  /*
   * Snap window in corner.
   */

  void snap_window(Client& client, enum position pos)
  {
    int16_t mon_x, mon_y, win_x, win_y;
    uint16_t mon_w, mon_h, win_w, win_h;

    if (is_maxed(client)) {
      unmaximize_window(client);
      set_focused(client);
    }

    fit_on_screen(client);

    win_x = client.geom.x;
    win_y = client.geom.y;
    win_w = client.geom.width + 2 * conf.border_width;
    win_h = client.geom.height + 2 * conf.border_width;

    get_monitor_size(client, mon_x, mon_y, mon_w, mon_h);

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

    client.geom.x = win_x;
    client.geom.y = win_y;
    teleport_window(client.window, win_x, win_y);
    xcb_flush(conn);
  }

  /*
   * Put window in grid.
   */

  void grid_window(Client& client,
                   uint32_t grid_width,
                   uint32_t grid_height,
                   uint32_t grid_x,
                   uint32_t grid_y)
  {
    int16_t mon_x, mon_y;
    uint16_t new_w, new_h;
    uint16_t mon_w, mon_h;

    if (grid_x >= grid_width || grid_y >= grid_height) {
      return;
    }

    if (is_maxed(client)) {
      unmaximize_window(client);
      set_focused(client);
    }

    get_monitor_size(client, mon_x, mon_y, mon_w, mon_h);

    /* calculate new window size */
    new_w =
      (mon_w - conf.gap_left - conf.gap_right -
       (grid_width - 1) * conf.grid_gap - grid_width * 2 * conf.border_width) /
      grid_width;

    new_h =
      (mon_h - conf.gap_up - conf.gap_down - (grid_height - 1) * conf.grid_gap -
       grid_height * 2 * conf.border_width) /
      grid_height;

    client.geom.width  = new_w;
    client.geom.height = new_h;

    client.geom.x =
      mon_x + conf.gap_left +
      grid_x * (conf.border_width + new_w + conf.border_width + conf.grid_gap);
    client.geom.y =
      mon_y + conf.gap_up +
      grid_y * (conf.border_width + new_h + conf.border_width + conf.grid_gap);

    DMSG("w: %d\th: %d\n", new_w, new_h);

    teleport_window(client.window, client.geom.x, client.geom.y);
    resize_window_absolute(client.window, client.geom.width,
                           client.geom.height);

    xcb_flush(conn);
  }

  /*
   * Adds X event handlers to the array.
   */

  void register_event_handlers()
  {
    for (int i = 0; i <= LAST_XCB_EVENT; i++) {
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
        xcb_configure_window(conn, e->window, XCB_CONFIG_WINDOW_STACK_MODE,
                             values);
      }

      if (!client->fullscreen) {
        fit_on_screen(*client);
      }

      teleport_window(client->window, client->geom.x, client->geom.y);
      resize_window_absolute(client->window, client->geom.width,
                             client->geom.height);
      if (!client->fullscreen && !(client->hmaxed && client->vmaxed)) {
        set_borders(*client, conf.focus_color);
      }
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
      xcb_configure_window(conn, e->window, e->value_mask, values);
    }
  }

  /*
   * Window has been destroyed.
   */

  void event_destroy_notify(xcb_generic_event_t* ev)
  {
    Client* client;
    auto* e = (xcb_destroy_notify_event_t*) ev;
    DMSG("Destroy notify event: %d\n", e->window);

    on_top.erase(std::remove(on_top.begin(), on_top.end(), e->window),
                 on_top.end());
    client = find_client(e->window);

    if (client != nullptr) {
      free_window(*client);
    }

    // update_client_list();
    workspace_goto(current_ws());
  }

  /*
   * The mouse pointer has entered the window.
   */

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
      set_focused_no_raise(*client);
    }
  }

  /*
   * A window wants to show up on the screen.
   */

  void event_map_request(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_map_request_event_t*) ev;
    Client* client;

    DMSG("Map request event: %d\n", e->window);

    long data[] = {
      XCB_ICCCM_WM_STATE_NORMAL,
      XCB_NONE,
    };

    /* create window if new */
    client = find_client(e->window);
    if (client == nullptr) {
      client = setup_window(e->window);

      /* client is a dock or some kind of window that needs to be ignored */
      if (client == nullptr) {
        return;
      }

      if (!client->geom.set_by_user) {
        if (!get_pointer_location(&scr->root, &client->geom.x,
                                  &client->geom.y)) {
          client->geom.x = client->geom.y = 0;
        }

        client->geom.x -= client->geom.width / 2;
        client->geom.y -= client->geom.height / 2;
        teleport_window(client->window, client->geom.x, client->geom.y);
      }
      workspace_add_window(*client, current_ws());
    }
    client->should_map = true;

    if (client->workspace == &current_ws()) {
      xcb_map_window(conn, e->window);
    } else {
      workspace_add_window(*client, current_ws());
    }


    /* in case of fire, abort */
    if (client == nullptr) {
      return;
    }

    if (randr_base != -1) {
      client->monitor = find_monitor_by_coord(client->geom.x, client->geom.y);
      if (client->monitor == nullptr) {
        client->monitor = mon_list.data();
      }
    }

    fit_on_screen(*client);

    /* window is normal */
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, client->window,
                        ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2, data);

    update_client_list();

    if (!client->fullscreen && !(client->hmaxed && client->vmaxed)) {
      set_borders(*client, conf.focus_color);
    }
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

  /*
   * Window has been unmapped (became invisible).
   */

  void event_unmap_notify(xcb_generic_event_t* ev)
  {
    auto* e        = (xcb_map_request_event_t*) ev;
    Client* client = nullptr;
    DMSG("Unmap event: %d\n", e->window);

    on_top.erase(std::remove(on_top.begin(), on_top.end(), e->window),
                 on_top.end());
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

    // workspace_goto(current_ws());
    update_client_list();
  }

  /*
   * Window has been configured.
   */

  void event_configure_notify(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_configure_notify_event_t*) ev;

    DMSG("confgure notify event: %d\n", e->window);

    /* The root window changes its geometry when the
     * user adds/removes/tilts screens */
    if (e->window == scr->root) {
      if (e->width != scr->width_in_pixels ||
          e->height != scr->height_in_pixels) {
        scr->width_in_pixels  = e->width;
        scr->height_in_pixels = e->height;

        if (randr_base != -1) {
          get_randr();
          for (auto& win : current_ws().windows) {
            fit_on_screen(win);
          }
        }
      }
    } else {
      Client* client = find_client(e->window);
      if (client != nullptr) {
        client->monitor = find_monitor_by_coord(client->geom.x, client->geom.y);
      } else {
        setup_window(e->window, true);
      }
    }
  }

  /*
   * Window wants to change its position in the stacking order.
   */

  void event_circulate_request(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_circulate_request_event_t*) ev;

    DMSG("circulate request event: %d\n", e->window);

    xcb_circulate_window(conn, e->window, e->place);
  }

  /*
   * Received client message. Either ewmh/icccm thing or
   * message from the client.
   */

  void event_client_message(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_client_message_event_t*) ev;
    uint32_t ipc_command;
    uint32_t* data;
    Client* client;

    if (e->type == ATOMS[_IPC_ATOM_COMMAND] && e->format == 32) {
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
      if (e->type == ewmh->_NET_WM_STATE) {
        DMSG("got _NET_WM_STATE for 0x%08x\n", client->window);
        handle_wm_state(*client, e->data.data32[1], e->data.data32[0]);
        handle_wm_state(*client, e->data.data32[2], e->data.data32[0]);
      }
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
    xcb_get_input_focus_reply_t* focus =
      xcb_get_input_focus_reply(conn, xcb_get_input_focus(conn), nullptr);
    Client* client = nullptr;

    if (focused_client() != nullptr &&
        focus->focus == focused_client()->window) {
      return;
    }

    client = find_client(focus->focus);
    if (client != nullptr) {
      set_focused_no_raise(*client);
    }
  }

  void event_button_press(xcb_generic_event_t* ev)
  {
    auto* e     = (xcb_button_press_event_t*) ev;
    bool replay = false;

    for (int i = 0; i < NR_BUTTONS; i++) {
      if (e->detail != mouse_buttons[i]) {
        continue;
      }
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

  void pointer_init()
  {
    num_lock    = pointer_modfield_from_keysym(XK_Num_Lock);
    caps_lock   = pointer_modfield_from_keysym(XK_Caps_Lock);
    scroll_lock = pointer_modfield_from_keysym(XK_Scroll_Lock);

    if (caps_lock == XCB_NO_SYMBOL) {
      caps_lock = XCB_MOD_MASK_LOCK;
    }
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
              if (*k == mk) {
                modfield |= (1 << i);
              }
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
          conf.click_to_focus == (int8_t) mouse_buttons[i]) {
        window_grab_button(win, mouse_buttons[i], XCB_NONE);
      }
      if (conf.pointer_actions[i] != POINTER_ACTION_NOTHING) {
        window_grab_button(win, mouse_buttons[i], conf.pointer_modifier);
      }
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
        scroll_lock != XCB_NO_SYMBOL) {
      GRAB(button, modifier | num_lock | caps_lock | scroll_lock);
    }
    if (num_lock != XCB_NO_SYMBOL && caps_lock != XCB_NO_SYMBOL) {
      GRAB(button, modifier | num_lock | caps_lock);
    }
    if (caps_lock != XCB_NO_SYMBOL && scroll_lock != XCB_NO_SYMBOL) {
      GRAB(button, modifier | caps_lock | scroll_lock);
    }
    if (num_lock != XCB_NO_SYMBOL && scroll_lock != XCB_NO_SYMBOL) {
      GRAB(button, modifier | num_lock | scroll_lock);
    }
    if (num_lock != XCB_NO_SYMBOL) {
      GRAB(button, modifier | num_lock);
    }
    if (caps_lock != XCB_NO_SYMBOL) {
      GRAB(button, modifier | caps_lock);
    }
    if (scroll_lock != XCB_NO_SYMBOL) {
      GRAB(button, modifier | scroll_lock);
    }
#undef GRAB
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

    client = find_client(win);
    if (client == nullptr) {
      return true;
    }

    raise_window(client->window);
    if (pac == POINTER_ACTION_FOCUS) {
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

    track_pointer(*client, pac, pos);

    return true;
  }

  enum resize_handle get_handle(Client& client,
                                xcb_point_t pos,
                                enum pointer_action pac)
  {
    //    if (client == nullptr)
    //      return pac == POINTER_ACTION_RESIZE_SIDE ? HANDLE_LEFT :
    //      HANDLE_TOP_LEFT;

    enum resize_handle handle;
    WindowGeom geom = client.geom;

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
    } else if (pac == POINTER_ACTION_RESIZE_CORNER) {
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

  void track_pointer(Client& client, enum pointer_action pac, xcb_point_t pos)
  {
    enum resize_handle handle = get_handle(client, pos, pac);
    WindowGeom geom           = client.geom;

    xcb_generic_event_t* ev = nullptr;

    bool grabbing   = true;
    Client& grabbed = client;

    do {
      free(ev);
      while ((ev = xcb_wait_for_event(conn)) == nullptr) {
        xcb_flush(conn);
      }
      uint8_t resp = EVENT_MASK(ev->response_type);

      if (resp == XCB_MOTION_NOTIFY) {
        auto* e = (xcb_motion_notify_event_t*) ev;
        DMSG(
          "tracking window by mouse root_x = %d  root_y = %d  posx = %d  posy "
          "= %d\n",
          e->root_x, e->root_y, pos.x, pos.y);
        int16_t dx = e->root_x - pos.x;
        int16_t dy = e->root_y - pos.y;
        int32_t x = client.geom.x, y = client.geom.y, width = client.geom.width,
                height = client.geom.height;

        if (pac == POINTER_ACTION_MOVE) {
          client.geom.x = geom.x + dx;
          client.geom.y = geom.y + dy;
          fit_on_screen(client);
          teleport_window(client.window, client.geom.x, client.geom.y);
        } else if (pac == POINTER_ACTION_RESIZE_SIDE ||
                   pac == POINTER_ACTION_RESIZE_CORNER) {
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

          int16_t monx, mony;
          uint16_t monw, monh;
          get_monitor_size(client, monx, mony, monw, monh);
          if (x < monx) {
            x = client.geom.x;
          }
          if (y < mony) {
            y = client.geom.y;
          }
          if (x + width > monx + monw) {
            x     = client.geom.x;
            width = client.geom.width;
          }
          if (y + height > mony + monh) {
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
          resize_window_absolute(client.window, client.geom.width,
                                 client.geom.height);
          teleport_window(client.window, client.geom.x, client.geom.y);
          xcb_flush(conn);
        }
      } else if (resp == XCB_BUTTON_RELEASE) {
        grabbing = false;
      } else {
        if (events[resp] != nullptr) {
          (events[resp])(ev);
        }
      }
    } while (grabbing);
    free(ev);

    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
  }

  void grab_buttons()
  {
    for (auto& client : current_ws().windows) {
      window_grab_buttons(client.window);
    }
  }

  void ungrab_buttons()
  {
    for (auto& client : current_ws().windows) {
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

  void version()
  {
    fprintf(stderr, "%s %s\n", __NAME__, __THIS_VERSION__);
    fprintf(stderr, "Copyright (c) 2016-2017 Tudor Ioan Roman\n");
    fprintf(stderr, "Released under the ISC License\n");

    exit(EXIT_SUCCESS);
  }

  void load_defaults()
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

