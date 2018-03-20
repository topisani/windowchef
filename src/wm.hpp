#pragma once
#include <optional>

#include <xcb/xcb_ewmh.h>
#include "types.hpp"

namespace wm {

  extern Conf conf;

  /* base for checking randr events */
  extern bool halt;
  extern bool should_close;
  extern int exit_code;

  std::vector<Workspace>& workspaces() noexcept;
  std::vector<xcb_window_t>& on_top() noexcept;
  Workspace& get_workspace(int idx);
  Workspace& current_ws() noexcept;

  void cleanup();
  int setup();

  Geometry get_monitor_size(Client& client,
                        bool include_padding = true);
  void arrange_by_monitor(Monitor& mon);
  Client* setup_window(xcb_window_t win, bool require_type = false);
  Client* focused_client();
  void set_focused(Client& client, bool raise = true);
  void set_focused_last_best();
  void resize_window(Client& client, int16_t w, int16_t h);
  void fit_on_screen(Client& client);
  void refresh_maxed(Client& client);
  void fullscreen_window(Client& client);
  void maximize_window(Client& client);
  void hmaximize_window(Client& client);
  void vmaximize_window(Client& client);
  void unmaximize_geometry(Client& client);
  void unmaximize_window(Client& client);
  bool is_maxed(Client& client);
  void cycle_window(Client& client);
  void rcycle_window(Client& client);
  void cycle_window_in_workspace(Client&);
  void rcycle_window_in_workspace(Client&);
  void cardinal_focus(uint32_t dir);
  void cardinal_move(Client&, direction dir);
  void cardinal_resize(Client&, direction dir, bool shrink = false);
  bool is_in_valid_direction(uint32_t direction,
                             float window_direction,
                             float delta);
  bool is_in_cardinal_direction(uint32_t direction, Client& a, Client& b);
  void save_original_size(Client& client, bool overwrite = true);
  xcb_atom_t get_atom(const char* name);
  void center_pointer(Client& client);
  Client* find_client(xcb_window_t& win);
  std::optional<Geometry> get_geometry(xcb_window_t& win);
  void set_borders(Client& client, uint32_t color);
  void free_window(Client& cl);

  void workspace_add_window(Client& client, Workspace& workspace);
  //  void workspace_remove_window(Client&);
  //  void workspace_remove_all_windows(Workspace&);
  void workspace_goto(Workspace& workspace);

  bool show_bar(Workspace& ws = current_ws());
  void update_bar_visibility();

  // void change_nr_of_workspaces(uint32_t);
  void refresh_borders(Client& client, Client* focused = focused_client());
  void refresh_borders();

  void snap_window(Client& client, enum Position pos);
  void grid_window(Client& client,
                   uint32_t grid_width,
                   uint32_t grid_height,
                   uint32_t grid_x,
                   uint32_t grid_y);

  void register_event_handlers();
  void event_configure_request(xcb_generic_event_t* ev);
  void event_destroy_notify(xcb_generic_event_t* ev);
  void event_enter_notify(xcb_generic_event_t* ev);
  void event_map_request(xcb_generic_event_t* ev);
  void event_map_notify(xcb_generic_event_t* ev);
  void event_unmap_notify(xcb_generic_event_t* ev);
  void event_configure_notify(xcb_generic_event_t* ev);
  void event_circulate_request(xcb_generic_event_t* ev);
  void event_client_message(xcb_generic_event_t* ev);
  void event_focus_in(xcb_generic_event_t* ev);
  void event_focus_out(xcb_generic_event_t* ev);
  void event_button_press(xcb_generic_event_t* ev);

  void pointer_init();
  int16_t pointer_modfield_from_keysym(xcb_keysym_t keysym);
  bool pointer_grab(PointerAction pac);
  enum resize_handle get_handle(Client& client,
                                xcb_point_t pos,
                                PointerAction pac);
  void track_pointer(Client& client, PointerAction pac, xcb_point_t pos);
  void grab_buttons();
  void ungrab_buttons();

  void usage(char* name);
  void version();
  void load_defaults();
  void load_config(char* config_path);

} // namespace wm
