#include <err.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "common.hpp"

namespace client {

  void send_fifo(int argc, char** argv)
  {
    bool get_response = true;
    // Multiple writers to one fifo is guarantied to not be interleaved if the
    // messages are < PIPE_BUF, which is at least 512. On OSX and some BSD systems
    // it's 512, on linux it's 4096. Either way, it shouldn't be an issue, as
    // practically all messages are shorter.
    auto name = request_fifo_name();

    errno = 0;
    auto stream = std::ofstream(name);
    if (!stream.is_open()) {
        errx(EXIT_FAILURE, "Error opening request pipe: %s", strerror(errno));
    }

    auto resp_name = response_fifo_name();
    if (get_response) {
      // The response pipe must be created before sending the request
      if (mkfifo(resp_name.c_str(), 0666) != 0) {
        errx(EXIT_FAILURE, "Error creating response pipe: %s", strerror(errno));
      }
    }

    stream << getpid() << ":";
    for (int i = 1; i < argc; i++) {
      stream << argv[i] << '\t';
    }
    stream << '\n';
    stream.close();

    if (get_response) {
      auto resp_name = response_fifo_name();
      errno = 0;
      auto stream = std::ifstream(resp_name);
      if (errno != 0) {
        remove(resp_name.c_str());
        errx(EXIT_FAILURE, "Error opening response pipe: %s", strerror(errno));
      }
      char buffer[512];
      while (stream.good()) {
        stream.read(buffer, 512);
        std::cout << buffer;
      }
      std::cout << std::endl;
      if (errno != 0) {
        remove(resp_name.c_str());
        errx(EXIT_FAILURE, "Error reading response from pipe: %s", strerror(errno));
      }
      remove(resp_name.c_str());
    }
  }
} // namespace

using namespace client;

int main(int argc, char** argv)
{
  send_fifo(argc, argv);
}
