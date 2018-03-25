#pragma once
#include <string>
#include <tuple>
#include <vector>

#include "commands.hpp"

#include "../common.hpp"

namespace ipc {

  /// Parse a type from string
  template<typename T>
  T parse(std::string const&);

  /// Convert to string. Used for returning data.
  template<typename T>
  std::string to_string(T&& t);

  /// Arguments recieved from the client.
  ///
  /// These are passed to the handlers, which then extract their values using
  /// `auto [window, direction] = args.parse<Window, Direction>()`
  struct Args {

    /// Get a tuple of args parsed as `Types...`
    template<typename... Types>
    std::tuple<Types...> parse() const
    {
      return arg_parser_impl<Types...>(strings.cbegin() + shifted, std::index_sequence_for<Types...>());
    }

    /// Get arg at `I` parsed as `T`
    template<std::size_t I, typename T>
    T parse() const
    {
      return ipc::parse<T>(strings.at(shifted + I));
    }

    /// Get arg at `i` parsed as `T`
    template<typename T>
    T parse(std::size_t i) const
    {
      return ipc::parse<T>(strings.at(shifted + i));
    }

    /// Get arg number `n`
    std::string const& operator[](std::size_t n) const
    {
      return strings.at(shifted + n);
    }

    void shift(std::size_t n)
    {
      shifted += n;
    }

    std::vector<std::string> strings;
    std::size_t shifted = 0;

  private:

    // util
    template<typename... Types, std::size_t... Idxs>
    static auto arg_parser_impl(
      std::vector<std::string>::const_iterator iter,
      std::index_sequence<Idxs...> seq)
    {
      return std::make_tuple(ipc::parse<Types>(*(iter + Idxs))...);
    }
  };

  /// A simple tag type for enums
  /// 
  /// Used for the command handlers. They should be functions of the form
  /// `R handler(Tag<Commands::Cmd>, Args);`, where `R` is any type that can
  /// be converted to string using `ipc::to_string`
  template<auto V>
  struct For {};

  /// Run the loop
  void run();

  /// Kill the loop;
  void exit();
}
