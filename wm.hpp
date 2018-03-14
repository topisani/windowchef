#pragma once
#include <xcb/xcb_ewmh.h>
#include "types.hpp"

namespace wm {

  /* button identifiers */
  enum { BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT, NR_BUTTONS };

  /* connection to the X server */
  extern xcb_connection_t* conn;
  extern xcb_ewmh_connection_t* ewmh;
  extern xcb_screen_t* scr;
  extern Conf conf;
  /* number of the screen we're using */
  extern int scrno;
  /* base for checking randr events */
  extern int randr_base;
  extern bool halt;
  extern bool should_close;
  extern int exit_code;

  std::vector<Workspace>& workspaces() noexcept;
  Workspace& get_workspace(int idx);
  Workspace& current_ws() noexcept;

  void cleanup();
  int setup();
  int setup_randr();
  void get_randr();
  void get_outputs(xcb_randr_output_t* outputs,
                   int len,
                   xcb_timestamp_t timestamp);
  Monitor* find_monitor(xcb_randr_output_t mon);
  Monitor* find_monitor_by_coord(int16_t x, int16_t y);
  Monitor* find_clones(xcb_randr_output_t mon, int16_t x, int16_t y);
  Monitor& add_monitor(xcb_randr_output_t mon,
                       char* name,
                       int16_t x,
                       int16_t y,
                       uint16_t width,
                       uint16_t height);
  void free_monitor(Monitor& mon);
  void get_monitor_size(Client& client,
                        int16_t& mon_x,
                        int16_t& mon_y,
                        uint16_t& mon_width,
                        uint16_t& mon_height,
                        bool include_padding = true);
  void arrange_by_monitor(Monitor& mon);
  Client* setup_window(xcb_window_t win, bool require_type = false);
  Client* focused_client();
  void set_focused_no_raise(Client& client);
  void set_focused(Client& client);
  void set_focused_last_best();
  void raise_window(xcb_window_t win);
  void close_window(Client& client);
  void delete_window(xcb_window_t win);
  void teleport_window(xcb_window_t win, int16_t x, int16_t y);
  void move_window(xcb_window_t win, int16_t x, int16_t y);
  void resize_window_absolute(xcb_window_t win, uint16_t w, uint16_t h);
  void resize_window(xcb_window_t win, int16_t w, int16_t h);
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
  float get_distance_between_windows(Client& a, Client& b);
  float get_angle_between_windows(Client& a, Client& b);
  WinPosition get_window_position(uint32_t mode, Client& client);
  bool is_overlapping(Client& a, Client& b);
  bool is_in_valid_direction(uint32_t direction,
                             float window_direction,
                             float delta);
  bool is_in_cardinal_direction(uint32_t direction, Client& a, Client& b);
  void save_original_size(Client& client);
  xcb_atom_t get_atom(const char* name);
  bool get_pointer_location(xcb_window_t&, int16_t&, int16_t&);
  void center_pointer(Client& client);
  Client* find_client(xcb_window_t& win);
  bool get_geometry(xcb_window_t& win,
                    int16_t& x,
                    int16_t& y,
                    uint16_t& width,
                    uint16_t& height);
  void set_borders(Client& client, uint32_t color);
  bool is_mapped(xcb_window_t win);
  void free_window(Client& cl);

  void add_to_client_list(xcb_window_t win);
  void update_client_list();
  void update_wm_desktop(Client& client);

  void workspace_add_window(Client& client, Workspace& workspace);
  //  void workspace_remove_window(Client&);
  //  void workspace_remove_all_windows(Workspace&);
  void workspace_goto(Workspace& workspace);

  bool show_bar(Workspace& ws = current_ws());
  void update_bar_visibility();

  // void change_nr_of_workspaces(uint32_t);
  void refresh_borders();
  void update_ewmh_wm_state(Client& client);
  void handle_wm_state(Client& client, xcb_atom_t state, unsigned int action);

  void snap_window(Client& client, enum position pos);
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
  void window_grab_buttons(xcb_window_t win);
  void window_grab_button(xcb_window_t win, uint8_t button, uint16_t modifier);
  bool pointer_grab(enum pointer_action pac);
  enum resize_handle get_handle(Client& client,
                                xcb_point_t pos,
                                enum pointer_action pac);
  void track_pointer(Client& client, enum pointer_action pac, xcb_point_t pos);
  void grab_buttons();
  void ungrab_buttons();

  void usage(char* name);
  void version();
  void load_defaults();
  void load_config(char* config_path);

} // namespace wm
