#include <sstream>

#include "common.hpp"

#include <xcb/xcb_util.h>
#include <unistd.h>

std::string request_fifo_name()
{
  char* host;
  int display, screen;
  xcb_parse_display(nullptr, &host, &display, &screen);

  std::ostringstream stream;
  stream << "/tmp/" << __NAME__ << "-" << host << "-" << display << "-" << screen << ".fifo";
  return stream.str();
}

std::string response_fifo_name(__pid_t pid)
{
  char* host;
  int display, screen;

  std::ostringstream stream;
  stream << "/tmp/" << __NAME__ << "-response-" << pid << ".fifo";
  return stream.str();
}
