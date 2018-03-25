#include <unistd.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <err.h>
#include <sys/stat.h>
#include <xcb/xcb_util.h>
#include <csignal>
#include <cstring>

#include "handlers.hpp"
#include "server.hpp"

namespace ipc {

  void send_response(__pid_t dst, std::string message)
  {
    auto name = response_fifo_name(dst);

    // If the response pipe doesnt exist, the client is ignoring responses.
    struct stat buf;
    if(stat(name.c_str(), &buf) != 0) return;

    auto stream = std::ofstream(name);
    if (!stream.is_open()) {
      throw std::runtime_error(str_join("Error opening response pipe: %s", strerror(errno)));
    }
    stream << message;
  }

  struct Request {
    __pid_t client;
    std::string command;
    std::vector<std::string> args;
  };

  Request get_request(std::ifstream& stream)
  {
    std::string pid_str;
    char c;
    while (stream.get(c)) {
      if (c == ':') {
        break;
      } else {
        pid_str += c;
      }
    }
    Request res;
    res.client = std::stoi(pid_str);
    if (res.client < 1) {
      throw std::runtime_error("Error parsing message");
    }

    std::getline(stream, res.command, '\t');
    while (stream.good()) {
      if (stream.peek() == '\n') {
        stream.seekg(1, std::ios::cur);
        break;
      }
      res.args.emplace_back();
      std::getline(stream, res.args.back(), '\t');
    }
    return res;
  }

  /// Automatically construct array of handlers from enum.
  namespace detail {
    template<Command cmd>
    constexpr auto get_handler() -> function_ptr<std::string, Args>
    {
      using Ret = decltype(handler(For<cmd>(), std::declval<Args>()));
      return [](Args args) {
        if constexpr (std::is_void_v<Ret>) {
          handler(For<cmd>{}, std::move(args));
          return std::string{};
        } else {
          auto res = handler(For<cmd>{}, std::move(args));
          return to_string(std::move(res));
        }
      };
    }

    template<std::size_t... idxs>
    constexpr auto get_handlers(std::index_sequence<idxs...>)
    {
      std::array<function_ptr<std::string, Args>, n_commands> arr = {};
      return std::array<function_ptr<std::string, Args>, n_commands>{
        get_handler<static_cast<Command>(idxs)>()...};
    }

    constexpr auto get_handlers()
    {
      return get_handlers(std::make_index_sequence<n_commands>());
    }

  } // namespace detail


  /// Call the handler for a command.
  ///
  /// \throws `std::runtime_error` if no handler was found
  auto call_handler(Command cmd, Args args)
  {
    static constexpr auto handlers = detail::get_handlers();
    return handlers.at(static_cast<std::size_t>(cmd))(std::move(args));
  }


  static auto name = request_fifo_name();
  static bool halt = false;

  void run()
  {
    // Multiple writers to one fifo is guarantied to not be interleaved if the
    // messages are < PIPE_BUF, which is at least 512. On OSX and some BSD
    // systems it's 512, on linux it's 4096. Either way, it shouldn't be an
    // issue, as practically all messages are shorter.
    std::cout << "Request pipe: " << name << '\n';

    // The response pipe must be created before sending the request
    if (mkfifo(name.c_str(), 0666) != 0) {
      errx(EXIT_FAILURE, "Error creating response pipe: %s", strerror(errno));
    }

    std::atexit([]() { remove(name.c_str()); });

    std::signal(SIGINT, [](int sig) {
      remove(name.c_str());
      std::exit(sig);
    });

    std::signal(SIGABRT, [](int sig) {
      remove(name.c_str());
      std::exit(sig);
    });

    std::signal(SIGTERM, [](int sig) {
      remove(name.c_str());
      std::exit(sig);
    });

    while (!halt) {
      errno       = 0;
      auto stream = std::ifstream(name);
      if (!stream.is_open()) {
        errx(EXIT_FAILURE, "Error opening request pipe: %s", strerror(errno));
      }

      while (stream.good() && !halt) {
        Request req;
        try {
          req = get_request(stream);
          if (halt) break;
          std::ostringstream response_buf;
          std::cout << "Recieved command from " << req.client << ": "
                    << req.command << " [ ";
          for (auto& arg : req.args) {
            std::cout << arg << " ";
          }
          std::cout << "]" << '\n';

          std::unique_lock lock(wm::global_lock);

          auto response = call_handler(parse<Command>(req.command),
                                       Args{std::move(req.args)});

          lock.unlock();

          send_response(req.client, std::move(response));
        } catch (std::exception& e) {
          std::cout << "Error: " << e.what() << std::endl;
          try {
            send_response(req.client, str_join("Error: ", e.what()));
          } catch (...) {}
        }
        std::flush(std::cout);
      }
    }
    remove(name.c_str());
  }

  void exit() 
  {
    halt = true;
    std::ofstream stream(name);
    stream << "QUIT";
    stream.close();

    remove(name.c_str());
  }
} // namespace ipc
