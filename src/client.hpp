#pragma once

#include <utility>

#include "ipc.hpp"
#include "util.hpp"

namespace client {

  struct Command {
    const char* string_command;
    enum ipc::Command command;
    int argc;
    function_ptr<bool, uint32_t*, int, char**> handler;
    function_ptr<std::string, uint32_t*> response_handler = nullptr;
  };

  struct ConfigEntry {
    const char* key;
    enum ipc::Config config;
    int argc;
    function_ptr<bool, uint32_t*, int, char**> handler;
  };

  struct WinConfigEntry {
    const char* key;
    ipc::WinConfig config;
    int argc;
    function_ptr<bool, uint32_t*, int, char**> handler;
  };

  bool fn_offset(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_naturals(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_bool(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_config(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_win_config(uint32_t* data, int argc, char** argv);
  bool fn_hex(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_position(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_gap(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_direction(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_pac(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_mod(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);
  bool fn_button(uint32_t* /*data*/, int /*argc*/, char** /*argv*/);

  std::string print_response(uint32_t*) noexcept;
}
