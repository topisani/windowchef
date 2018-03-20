#include "xcb.hpp"

#include <array>

#include "common.hpp"
#include "ipc.hpp"
#include "wm.hpp"

#include <X11/keysym.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

#include <cstdio>

#include <err.h>
#include <unistd.h>

namespace xcb {

  /* keyboard modifiers (for mouse support) */
  uint16_t num_lock, caps_lock, scroll_lock;
  xcb_atom_t ATOMS[NR_ATOMS];

  namespace {
    /* number of the screen we're using */
    int scrno;
    /* base for checking randr events */
    int randr_base;
    /* connection to the X server */
    xcb_connection_t* _conn;
    xcb_ewmh_connection_t* ewmh;
    xcb_screen_t* scr;

    /// This file also manages monitors
    std::vector<Monitor> mon_list;

    /* function handlers for events received from the X server */
    void (*events[xcb::last_xcb_event + 1])(xcb_generic_event_t*);
  } // namespace

  /// Get a pointer to the current xcb connection.
  ///
  /// This exists between `init` and `cleanup`
  xcb_connection_t* conn() noexcept
  {
    return _conn;
  }

  /// Get root window
  xcb_window_t root() noexcept
  {
    return scr->root;
  }

  int16_t pointer_modfield_from_keysym(xcb_keysym_t keysym)
  {
    uint16_t modfield       = 0;
    xcb_keycode_t *keycodes = nullptr, *mod_keycodes = nullptr;
    xcb_get_modifier_mapping_reply_t* reply = nullptr;
    xcb_key_symbols_t* symbols              = xcb_key_symbols_alloc(_conn);

    /* wrapped all of them in an ugly if to prevent getting values when
       we don't need them */
    if (!((keycodes = xcb_key_symbols_get_keycode(symbols, keysym)) ==
            nullptr ||
          (reply = xcb_get_modifier_mapping_reply(
             _conn, xcb_get_modifier_mapping(_conn), nullptr)) == nullptr ||
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

  static void pointer_init()
  {
    num_lock    = pointer_modfield_from_keysym(XK_Num_Lock);
    caps_lock   = pointer_modfield_from_keysym(XK_Caps_Lock);
    scroll_lock = pointer_modfield_from_keysym(XK_Scroll_Lock);

    if (caps_lock == XCB_NO_SYMBOL) {
      caps_lock = XCB_MOD_MASK_LOCK;
    }
  }

  int init()
  {
    register_event_handlers();
    /* init xcb and grab events */
    unsigned int values[1];
    int mask;

    _conn = xcb_connect(nullptr, &scrno);
    if (xcb_connection_has_error(_conn) != 0) {
      return -1;
    }

    /* get the first screen. hope it's the last one too */
    scr = xcb_setup_roots_iterator(xcb_get_setup(_conn)).data;

    mask = XCB_CW_EVENT_MASK;
    values[0] =
      XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;
    xcb_generic_error_t* e = xcb_request_check(
      _conn,
      xcb_change_window_attributes_checked(_conn, scr->root, mask, values));
    if (e != nullptr) {
      free(e);
      errx(EXIT_FAILURE, "Another window manager is already running.");
    }

    /* initialize ewmh variables */
    ewmh = (xcb_ewmh_connection_t*) calloc(1, sizeof(xcb_ewmh_connection_t));
    if (ewmh == nullptr) {
      warnx("couldn't set up ewmh _connection");
    }
    xcb_intern_atom_cookie_t* cookie = xcb_ewmh_init_atoms(_conn, ewmh);
    xcb_ewmh_init_atoms_replies(ewmh, cookie, nullptr);
    xcb_ewmh_set_wm_pid(ewmh, scr->root, getpid());
    xcb_ewmh_set_wm_name(ewmh, scr->root, strlen(__NAME__), __NAME__);
    xcb_ewmh_set_current_desktop(ewmh, 0, 0);

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
    xcb_flush(_conn);

    randr_base = setup_randr();
    return 0;
  }

  void cleanup()
  {
    if (ewmh != nullptr) {
      xcb_ewmh_connection_wipe(ewmh);
    }
    if (_conn != nullptr) {
      xcb_disconnect(_conn);
   }
 }

  int setup_randr()
  {
    int base;
    const xcb_query_extension_reply_t* r =
      xcb_get_extension_data(_conn, &xcb_randr_id);

    if (r->present == 0u) {
      return -1;
    }
    {
      get_randr();
    }

    base = r->first_event;
    xcb_randr_select_input(_conn, scr->root,
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
      xcb_randr_get_screen_resources_current(_conn, scr->root);
    xcb_randr_get_screen_resources_current_reply_t* r =
      xcb_randr_get_screen_resources_current_reply(_conn, c, nullptr);

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
      out_cookie[i] = xcb_randr_get_output_info(_conn, outputs[i], timestamp);
    }

    for (int i = 0; i < len; i++) {
      output = xcb_randr_get_output_info_reply(_conn, out_cookie[i], nullptr);
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
        info_c = xcb_randr_get_crtc_info(_conn, output->crtc, timestamp);
        crtc   = xcb_randr_get_crtc_info_reply(_conn, info_c, nullptr);

        if (crtc == nullptr) {
          break;
        }

        clonemon = find_clones(outputs[i], crtc->x, crtc->y);
        if (clonemon != nullptr) {
          continue;
        }

        mon = find_monitor(outputs[i]);
        if (mon == nullptr) {
          add_monitor(outputs[i], name, {crtc->x, crtc->y, crtc->width,
                      crtc->height});
        } else {
          mon->geom = {
          crtc->x,
          crtc->y,
          crtc->width,
          crtc->height
          };

          wm::arrange_by_monitor(*mon);
        }

        free(crtc);
      } else {
        /* Check if the monitor was used before
         * becoming disabled. */
        mon = find_monitor(outputs[i]);
        if (mon != nullptr) {
          for (auto&& client : wm::current_ws().windows) {
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
              wm::fit_on_screen(client);
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

  /// Get atom by name.
  xcb_atom_t get_atom(const char* name)
  {
    xcb_intern_atom_cookie_t cookie =
      xcb_intern_atom(_conn, 0u, strlen(name), name);
    xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(_conn, cookie, nullptr);

    if (reply == nullptr) {
      return XCB_ATOM_STRING;
    }

    return reply->atom;
  }

  /// Finds a monitor in the list.
  Monitor* find_monitor(xcb_randr_output_t mon)
  {
    auto iter = std::find_if(mon_list.begin(), mon_list.end(),
                             [mon](auto& el) { return el.monitor == mon; });
    if (iter == mon_list.end()) {
      return nullptr;
    }
    return &*iter;
  }

  /// Find a monitor in the list by its coordinates.
  Monitor* find_monitor_by_coord(int16_t x, int16_t y)
  {
    auto iter = std::find_if(mon_list.begin(), mon_list.end(), [x, y](auto& m) {
      return (x >= m.geom.x && x <= m.geom.x + m.geom.width && y >= m.geom.y &&
              y <= m.geom.y + m.geom.height);
    });
    if (iter == mon_list.end()) {
      return nullptr;
    }
    return &*iter;
  }

  /// Find cloned (mirrored) outputs.
  Monitor* find_clones(xcb_randr_output_t mon, int16_t x, int16_t y)
  {
    auto iter =
      std::find_if(mon_list.begin(), mon_list.end(), [mon, x, y](auto& m) {
        return (m.monitor != mon && m.geom.x == x && m.geom.y == y);
      });
    if (iter == mon_list.end()) {
      return nullptr;
    }
    return &*iter;
  }

  /// Add a monitor to the global monitor list.
  Monitor& add_monitor(xcb_randr_output_t mon, char* name, Geometry geom)
  {
    Monitor monitor;
    monitor.monitor = mon;
    monitor.name    = name;
    monitor.geom    = geom;

    return mon_list.emplace_back(monitor);
  }

  /// Free a monitor from the global monitor list.
  void free_monitor(Monitor& mon)
  {
    mon_list.erase(std::remove(mon_list.begin(), mon_list.end(), mon),
                   mon_list.end());
  }

  /// Assign the appropriate monitor to `client`
  void assign_monitor(Client& client)
  {
    if (randr_base != -1) {
      client.monitor = find_monitor_by_coord(client.geom.x, client.geom.y);
      if (client.monitor == nullptr) {
        client.monitor = mon_list.data();
      }
    }
  }

  /// Size of randr screen object in pixels
  Dimensions get_screen_size() noexcept
  {
    return {scr->width_in_pixels, scr->height_in_pixels};
  }

  /// Apply client.geom to the window
  void apply_client_geometry(Client& cl)
  {
    if (cl.window == scr->root || cl.window == 0) {
      return;
    }

    uint32_t values[2] = {(uint32_t) cl.geom.x, (uint32_t) cl.geom.y};
    uint32_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
    xcb_configure_window(_conn, cl.window, mask, values);

    uint32_t values2[2] = {(uint32_t) cl.geom.width, (uint32_t) cl.geom.height};
    mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    xcb_configure_window(_conn, cl.window, mask, values2);

    xcb_flush(_conn);
  }

  /// Apply client.border_color and border_width.
  void apply_borders(Client& client)
  {
    uint32_t values[1];
    values[0] = client.border_width;
    xcb_configure_window(_conn, client.window, XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);
    if (client.border_width > 0) {
      values[0] = client.border_color;
      xcb_change_window_attributes(_conn, client.window, XCB_CW_BORDER_PIXEL,
                                   values);
    }

    xcb_flush(_conn);
  }


  /// Put window at the top of the window stack.
  void raise_window(xcb_window_t win)
  {
    uint32_t values[1] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(_conn, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
    for (auto ot_win : wm::on_top()) {
      xcb_configure_window(_conn, ot_win, XCB_CONFIG_WINDOW_STACK_MODE,
        values);
    }
  }

  /// Returns true if the client supports the given protocol atom (like
  /// WM_DELETE_WINDOW)
  bool window_supports_protocol(xcb_window_t window, xcb_atom_t atom)
  {
    xcb_get_property_cookie_t cookie;
    xcb_icccm_get_wm_protocols_reply_t protocols;
    bool result = false;

    cookie = xcb_icccm_get_wm_protocols(_conn, window, ewmh->WM_PROTOCOLS);
    if (xcb_icccm_get_wm_protocols_reply(_conn, cookie, &protocols, nullptr) !=
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

  /// Ask window to close gracefully. If the window doesn't respond, kill it.
  void close_window(Client& client)
  {
    xcb_window_t win = client.window;

    if (window_supports_protocol(win, ATOMS[WM_DELETE_WINDOW])) {
      DMSG("Deleting window %d", win);
      delete_window(win);
    } else {
      DMSG("Destroying window %d", win);
      xcb_destroy_window(_conn, win);
    }
  }

  /// Gracefully ask a window to close.
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

    xcb_send_event(_conn, 0, win, XCB_EVENT_MASK_NO_EVENT, (char*) &ev);
  }

  /// Teleports window absolutely to the given coordinates.
  void teleport_window(xcb_window_t win, int16_t x, int16_t y)
  {
    uint32_t values[2] = {(uint32_t) x, (uint32_t) y};

    if (win == scr->root || win == 0) {
      return;
    }

    xcb_configure_window(_conn, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                         values);

    xcb_flush(_conn);
  }

  /// Moves the window by a certain amount.
  void move_window(xcb_window_t win, int16_t x, int16_t y)
  {
    if (!is_mapped(win) || win == scr->root) {
      return;
    }

    auto geom = get_geometry(win).value();

    geom.x += x;
    geom.y += y;

    teleport_window(win, geom.x, geom.y);
  }

  /// Resizes window to the given size.
  void resize_window_absolute(xcb_window_t win, uint16_t w, uint16_t h)
  {
    uint32_t val[2];
    uint32_t mask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;

    val[0] = w;
    val[1] = h;

    xcb_configure_window(_conn, win, mask, val);
  }

  /// Get a window's geometry.
  std::optional<Geometry> get_geometry(xcb_window_t win)
  {
    xcb_get_geometry_reply_t* reply =
      xcb_get_geometry_reply(_conn, xcb_get_geometry(_conn, win), nullptr);

    if (reply == nullptr) {
      return std::nullopt;
    }
    Geometry res;
    res.x      = reply->x;
    res.y      = reply->y;
    res.width  = reply->width;
    res.height = reply->height;

    free(reply);
    return res;
  }

  /// Returns true if window is mapped.
  bool is_mapped(xcb_window_t win)
  {
    bool yes;
    xcb_get_window_attributes_reply_t* r = xcb_get_window_attributes_reply(
      _conn, xcb_get_window_attributes(_conn, win), nullptr);
    if (r == nullptr) {
      return false;
    }

    yes = r->map_state == XCB_MAP_STATE_VIEWABLE;
    free(r);

    return yes;
  }

  static void window_grab_button(xcb_window_t win,
                                 uint8_t button,
                                 uint16_t modifier)
  {
#define GRAB(b, m)                                                             \
  xcb_grab_button(_conn, false, win, XCB_EVENT_MASK_BUTTON_PRESS,              \
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

  void window_grab_buttons(
    xcb_window_t win,
    int8_t click_to_focus,
    std::array<PointerAction, underlying(Buttons::Count)> pointer_actions,
    uint16_t pointer_modifier)
  {
    for (int i = 0; i < underlying(Buttons::Count); i++) {
      if (click_to_focus == (int8_t) XCB_BUTTON_INDEX_ANY ||
          click_to_focus == (int8_t) mouse_buttons[i]) {
        window_grab_button(win, mouse_buttons[i], XCB_NONE);
      }
      if (pointer_actions[i] != PointerAction::Nothing) {
        window_grab_button(win, mouse_buttons[i], pointer_modifier);
      }
    }
    DMSG("grabbed buttons on 0x%08x\n", win);
  }

  /// Map a window
  void map_window(xcb_window_t win) noexcept
  {
    xcb_map_window(_conn, win);
  }

  /// Unmap a window
  void unmap_window(xcb_window_t win) noexcept
  {
    xcb_unmap_window(_conn, win);
  }

  /// Set focus state to active or inactive without raising the window.
  void set_focused(
    xcb_window_t win,
    int8_t click_to_focus,
    std::array<PointerAction, underlying(Buttons::Count)> pointer_actions,
    uint16_t pointer_modifier) noexcept
  {
    long data[] = {
      XCB_ICCCM_WM_STATE_NORMAL,
      XCB_NONE,
    };
    /* focus the window */
    xcb_set_input_focus(_conn, XCB_INPUT_FOCUS_POINTER_ROOT, win,
                        XCB_CURRENT_TIME);

    /* set ewmh property */
    xcb_change_property(_conn, XCB_PROP_MODE_REPLACE, scr->root,
                        ewmh->_NET_ACTIVE_WINDOW, XCB_ATOM_WINDOW, 32, 1, &win);

    /* set window state */
    xcb_change_property(_conn, XCB_PROP_MODE_REPLACE, win, ewmh->_NET_WM_STATE,
                        ewmh->_NET_WM_STATE, 32, 2, data);

    window_grab_buttons(win, click_to_focus, pointer_actions, pointer_modifier);
  }

  /// Initialize a window for further work.
  Client make_client(xcb_window_t win, bool require_type)
  {
    uint32_t values[2];
    xcb_ewmh_get_atoms_reply_t win_type;
    xcb_atom_t atom;
    xcb_size_hints_t hints;
    WindowType type = WindowType::Normal;
    // std::clog << "Setting up window " << win << std::endl;

    if (xcb_ewmh_get_wm_window_type_reply(
          ewmh, xcb_ewmh_get_wm_window_type(ewmh, win), &win_type, nullptr) ==
        1) {
      for (int i = 0; i < win_type.atoms_len; ++i) {
        atom = win_type.atoms[i];
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_DESKTOP)
          type = WindowType::Desktop;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_DOCK) type = WindowType::Dock;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_TOOLBAR)
          type = WindowType::Toolbar;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_MENU) type = WindowType::Menu;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_UTILITY)
          type = WindowType::Utility;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_SPLASH) type = WindowType::Splash;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_DIALOG) type = WindowType::Dialog;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_DROPDOWN_MENU)
          type = WindowType::Dropdown_menu;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_POPUP_MENU)
          type = WindowType::Popup_menu;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_TOOLTIP)
          type = WindowType::Tooltip;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_NOTIFICATION)
          type = WindowType::Notification;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_COMBO) type = WindowType::Combo;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_DND) type = WindowType::Dnd;
        if (atom == ewmh->_NET_WM_WINDOW_TYPE_NORMAL) type = WindowType::Normal;
      }
    } else {
      if (require_type) {
        throw std::runtime_error("Type required, client has no type");
      }
    }

    /* subscribe to events */
    values[0] = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE;
    xcb_change_window_attributes(_conn, win, XCB_CW_EVENT_MASK, values);

    /* in case of fire */
    xcb_change_save_set(_conn, XCB_SET_MODE_INSERT, win);

    /* assign to the first workspace */
    xcb_ewmh_set_wm_desktop(ewmh, win, 0);

    Client cl = Client::make(win, type);

    /* initialize variables */
    cl.monitor   = nullptr;
    cl.mapped    = false;
    cl.workspace = nullptr;

    cl.geom = get_geometry(cl).value_or(Geometry{});

    xcb_icccm_get_wm_normal_hints_reply(
      _conn, xcb_icccm_get_wm_normal_hints_unchecked(_conn, win), &hints,
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
    return cl;
  }

  /// Add window to the ewmh client list.
  void add_to_client_list(xcb_window_t win) noexcept
  {
    xcb_change_property(_conn, XCB_PROP_MODE_APPEND, scr->root,
                        ewmh->_NET_CLIENT_LIST, XCB_ATOM_WINDOW, 32, 1, &win);
    xcb_change_property(_conn, XCB_PROP_MODE_APPEND, scr->root,
                        ewmh->_NET_CLIENT_LIST_STACKING, XCB_ATOM_WINDOW, 32, 1,
                        &win);
  }

  /// Clear the ewhm client list and return a vector of windows before clearing
  std::vector<xcb_window_t> clear_client_list() noexcept
  {
    xcb_query_tree_reply_t* reply =
      xcb_query_tree_reply(_conn, xcb_query_tree(_conn, scr->root), nullptr);
    xcb_delete_property(_conn, scr->root, ewmh->_NET_CLIENT_LIST);
    xcb_delete_property(_conn, scr->root, ewmh->_NET_CLIENT_LIST_STACKING);

    if (reply == nullptr) {
      add_to_client_list(0);
      return {};
    }

    uint32_t len           = xcb_query_tree_children_length(reply);
    xcb_window_t* children = xcb_query_tree_children(reply);

    std::vector<xcb_window_t> res = {children, children + len};
    free(reply);
    return res;
  }

  /// Set ewmh number of desktops
  void set_number_of_desktops(int n)
  {
    xcb_ewmh_set_number_of_desktops(ewmh, 0, n);
    xcb_flush(_conn);
  }

  /// Set ewmh current desktop
  void set_current_desktop(int idx) noexcept
  {
    xcb_ewmh_set_current_desktop(ewmh, 0, idx);
  }

  /// Set window desktop
  void update_wm_desktop(xcb_window_t window, uint32_t ws_idx) noexcept
  {
    xcb_ewmh_set_wm_desktop(ewmh, window, ws_idx);
  }

  /// Apply client workspace setting
  void apply_workspace(Client& client) noexcept
  {
    if (client.workspace != nullptr)
      update_wm_desktop(client.window, client.workspace->index);
  }

  /// Apply window state (maximization, fullscreen, etc.)
  void apply_state(Client& client) noexcept
  {
    int i;
    uint32_t values[12];

#define HANDLE_WM_STATE(s)                                                     \
  values[i] = ewmh->_NET_WM_STATE_##s;                                         \
  i++;                                                                         \
  DMSG("ewmh net_wm_state %s present\n", #s);

    if (client.fullscreen || client.vmaxed || client.hmaxed) {
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
    } else {
      xcb_atom_t state[] = {XCB_ICCCM_WM_STATE_NORMAL, XCB_NONE};
      xcb_change_property(_conn, XCB_PROP_MODE_REPLACE, client.window,
                          ewmh->_NET_WM_STATE, ewmh->_NET_WM_STATE, 32, 2,
                          state);
    }
#undef HANDLE_WM_STATE
  }

  /// Get the mouse pointer's coordinates.
  std::optional<Coordinates> get_pointer_location(xcb_window_t win) noexcept
  {
    auto* pointer =
      xcb_query_pointer_reply(_conn, xcb_query_pointer(_conn, win), nullptr);

    if (pointer == nullptr) return std::nullopt;
    Coordinates res = {pointer->win_x, pointer->win_y};

    free(pointer);
    return res;
  }

  /// Set the mouse pointer's position relative to `win`
  void warp_pointer(xcb_window_t win, Coordinates location) noexcept
  {
    xcb_warp_pointer(_conn, XCB_NONE, win, 0, 0, 0, 0, location.x, location.y);
  }

  /// Wait for an xcb event
  unique_ptr<xcb_generic_event_t> wait_for_event(bool handle) noexcept
  {
    auto ev = unique_ptr<xcb_generic_event_t>(xcb_wait_for_event(_conn));
    if (ev == nullptr || !handle) return ev;

    if (ev->response_type == randr_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
      get_randr();
      DMSG("Screen layout changed\n");
    }
    if (ev != nullptr) {
      DMSG("X Event %d\n", ev->response_type & ~0x80);
      if (events[EVENT_MASK(ev->response_type)] != nullptr) {
        (events[EVENT_MASK(ev->response_type)])(ev.get());
      }
    }
    return ev;
  }

  /// Flush the xcb connection
  void flush() noexcept
  {
    xcb_flush(_conn);
  }

  /// Window has been configured.
  static void event_configure_notify(xcb_generic_event_t* ev)
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
        }
      }
    }
  }

  /// Window wants to change its position in the stacking order.
  static void event_circulate_request(xcb_generic_event_t* ev)
  {
    auto* e = (xcb_circulate_request_event_t*) ev;

    DMSG("circulate request event: %d\n", e->window);

    xcb_circulate_window(_conn, e->window, e->place);
  }

  void register_event_handlers()
  {
    for (int i = 0; i <= xcb::last_xcb_event; i++) {
      events[i] = nullptr;
    }

    events[XCB_CONFIGURE_NOTIFY]  = event_configure_notify;
    events[XCB_CIRCULATE_REQUEST] = event_circulate_request;
  }

  /// Set client state from ewmh event.
  ///
  /// Only updates the client variables, make sure to call
  /// `apply_state` afterwards
  static void handle_state(Client& client,
                           xcb_atom_t state,
                           unsigned int action) noexcept
  {
    if (state == ewmh->_NET_WM_STATE_FULLSCREEN) {
      if (action == XCB_EWMH_WM_STATE_ADD) {
        client.fullscreen = true;
      } else if (action == XCB_EWMH_WM_STATE_REMOVE) {
        client.fullscreen = false;
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        client.fullscreen = !client.fullscreen;
      }
    } else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_VERT) {
      if (action == XCB_EWMH_WM_STATE_ADD) {
        client.vmaxed = true;
      } else if (action == XCB_EWMH_WM_STATE_REMOVE) {
        client.vmaxed = false;
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        client.vmaxed = !client.vmaxed;
      }
    } else if (state == ewmh->_NET_WM_STATE_MAXIMIZED_HORZ) {
      if (action == XCB_EWMH_WM_STATE_ADD) {
        client.hmaxed = true;
      } else if (action == XCB_EWMH_WM_STATE_REMOVE) {
        client.hmaxed = false;
      } else if (action == XCB_EWMH_WM_STATE_TOGGLE) {
        client.hmaxed = !client.hmaxed;
      }
    }
  }

  /// Received client message. Either ewmh/icccm thing or
  /// message from the client.
  void handle_client_message(Client& client,
                             xcb_client_message_event_t* ev) noexcept
  {
    if (ev->type == ewmh->_NET_WM_STATE) {
      DMSG("got _NET_WM_STATE for 0x%08x\n", client.window);
      handle_state(client, ev->data.data32[1], ev->data.data32[0]);
      handle_state(client, ev->data.data32[2], ev->data.data32[0]);
    }
  }
} // namespace xcb
