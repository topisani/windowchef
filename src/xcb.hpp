#pragma once

#include <optional>

#include "types.hpp"
#include "ipc.hpp"

#define EVENT_MASK(ev) (((ev) & ~0x80))

namespace xcb {
  struct free_deleter {
    template<typename T>
    void operator()(T* p) const
    {
      std::free(const_cast<std::remove_const_t<T>*>(p));
    }
  };

  template<typename XcbType>
  using unique_ptr = std::unique_ptr<XcbType, free_deleter>;

  constexpr const unsigned last_xcb_event = XCB_GET_MODIFIER_MAPPING;

  /* atoms identifiers */
  enum { WM_DELETE_WINDOW, _IPC_ATOM_COMMAND, NR_ATOMS };

  constexpr const char* atom_names[NR_ATOMS] = {
    "WM_DELETE_WINDOW",
    ATOM_COMMAND,
  };

  extern xcb_atom_t ATOMS[NR_ATOMS];

  extern uint16_t num_lock, caps_lock, scroll_lock;
  constexpr const xcb_button_index_t mouse_buttons[] = {
    XCB_BUTTON_INDEX_1,
    XCB_BUTTON_INDEX_2,
    XCB_BUTTON_INDEX_3,
  };

  /// Get a pointer to the current xcb connection.
  ///
  /// This exists between `init` and `cleanup`
  xcb_connection_t* conn() noexcept;

  /// Get root window
  xcb_window_t root() noexcept;

  /// Initialize xcb connections etc
  int init();

  /// Gracefully disconnect
  void cleanup();

  /// Tells the server we want to use randr.
  int setup_randr();

  /// Adds X event handlers to the array.
  void register_event_handlers();

  /// Get information regarding randr.
  void get_randr();

  /// Gets information about connected outputs.
  void get_outputs(xcb_randr_output_t* outputs,
                   int len,
                   xcb_timestamp_t timestamp);

  /// Finds a monitor in the list.
  Monitor* find_monitor(xcb_randr_output_t mon);

  /// Find a monitor in the list by its coordinates.
  Monitor* find_monitor_by_coord(int16_t x, int16_t y);

  /// Find cloned (mirrored) outputs.
  Monitor* find_clones(xcb_randr_output_t mon, int16_t x, int16_t y);

  /// Add a monitor to the global monitor list.
  Monitor& add_monitor(xcb_randr_output_t mon, char* name, Geometry geom);

  /// Free a monitor from the global monitor list.
  void free_monitor(Monitor& mon);

  /// Assign the appropriate monitor to `client`
  void assign_monitor(Client& client);

  /// Size of randr screen object in pixels
  Dimensions get_screen_size() noexcept;

  /// Get atom by name.
  xcb_atom_t get_atom(const char* name);

  /// Move and resize the client to its set geometry
  void apply_client_geometry(Client& cl);

  /// Put window at the top of the window stack.
  void raise_window(xcb_window_t win);

  /// Returns true if the client supports the given protocol atom (like
  /// WM_DELETE_WINDOW)
  bool window_supports_protocol(xcb_window_t window, xcb_atom_t atom);

  /// Ask window to close gracefully. If the window doesn't respond, kill it.
  void close_window(Client& client);

  /// Gracefully ask a window to close.
  void delete_window(xcb_window_t win);

  /// Teleports window absolutely to the given coordinates.
  void teleport_window(xcb_window_t win, int16_t x, int16_t y);

  /// Moves the window by a certain amount.
  void move_window(xcb_window_t win, int16_t x, int16_t y);

  /// Resizes window to the given size.
  void resize_window_absolute(xcb_window_t win, uint16_t w, uint16_t h);

  /// Get a window's geometry.
  std::optional<Geometry> get_geometry(xcb_window_t win);

  /// Grab buttons for window
  void window_grab_buttons(
    xcb_window_t win,
    int8_t click_to_focus,
    std::array<PointerAction, underlying(Buttons::Count)> pointer_actions,
    uint16_t pointer_modifier);

  /// Map a window
  void map_window(xcb_window_t win) noexcept;

  /// Unmap a window
  void unmap_window(xcb_window_t win) noexcept;

  /// Set focus and grab buttons. Does not raise the window
  void set_focused(
    xcb_window_t win,
    int8_t click_to_focus,
    std::array<PointerAction, underlying(Buttons::Count)> pointer_actions,
    uint16_t pointer_modifier) noexcept;

  /// Initialize a window for further work.
  ///
  /// \throws `std::runtime_error` if `required_type == true` and no type was
  /// found
  Client make_client(xcb_window_t win, bool require_type);

  /// Apply client.border_width and client.border_color
  void apply_borders(Client& client);

  /// Returns true if window is mapped.
  bool is_mapped(xcb_window_t win);

  /// Add window to the ewmh client list.
  void add_to_client_list(xcb_window_t win) noexcept;

  /// Clear the ewhm client list and return a vector of windows before clearing
  std::vector<xcb_window_t> clear_client_list() noexcept;

  /// Set ewmh number of desktops
  void set_number_of_desktops(int N);

  /// Set ewmh current desktop
  void set_current_desktop(int N) noexcept;

  /// Set window desktop
  void update_wm_desktop(xcb_window_t window, uint32_t ws_idx) noexcept;

  /// Apply client workspace setting
  void apply_workspace(Client& client) noexcept;

  /// Apply window state (maximization, fullscreen, etc.)
  void apply_state(Client& client) noexcept;

  /// Get the mouse pointer's coordinates relative to `win`
  std::optional<Coordinates> get_pointer_location(xcb_window_t win) noexcept;

  /// Set the mouse pointer's position relative to `win`
  void warp_pointer(xcb_window_t win, Coordinates location) noexcept;

  /// Wait for an xcb event
  /// 
  /// \param handle Whether to run internal event handlers before returning
  unique_ptr<xcb_generic_event_t> wait_for_event(bool handle = true) noexcept;

  /// Flush the xcb connection
  void flush() noexcept;

  /// Received client message. Either ewmh/icccm thing or
  /// message from the client.
  void handle_client_message(Client& client, xcb_client_message_event_t* ev) noexcept;
} // namespace xcb
