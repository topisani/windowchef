#pragma once

#include <vector>
#include <memory>

/// An iterator wrapper that dereferences twice.
template<typename Iter>
struct double_iterator {
  using wrapped = Iter;

  using value_type        = std::decay_t<decltype(*std::declval<typename wrapped::value_type>())>;
  using difference_type   = typename wrapped::difference_type;
  using reference         = value_type&;
  using pointer           = value_type*;
  using iterator_category = std::random_access_iterator_tag;

  using self_t = double_iterator<Iter>;

  double_iterator(wrapped w)
    : _iter (std::move(w))
  {}

  reference operator*() {return (**_iter);}
  pointer operator->() {return (*_iter).operator->(); }

  self_t& operator++() { _iter.operator++(); return *this; }
  self_t operator++(int i) { return _iter.operator++(i); }
  self_t& operator--() { _iter.operator--(); return *this; }
  self_t operator--(int i) { return _iter.operator--(i); }

  auto operator==(const self_t& rhs) { return _iter == rhs._iter; }
  auto operator!=(const self_t& rhs) { return _iter != rhs._iter; }
  auto operator<(const self_t& rhs) { return _iter < rhs._iter; }
  auto operator>(const self_t& rhs) { return _iter > rhs._iter; }
  auto operator<=(const self_t& rhs) { return _iter <= rhs._iter; }
  auto operator>=(const self_t& rhs) { return _iter >= rhs._iter; }

  self_t operator+(difference_type d) { return _iter + d; }
  self_t operator-(difference_type d) { return _iter - d; }
  auto operator-(const self_t& rhs) { return _iter - rhs._iter; }

  self_t& operator+=(difference_type d) { _iter += d; return *this; }
  self_t& operator-=(difference_type d) { _iter -= d; return *this; }

  operator wrapped&() { return _iter; }
  operator const wrapped&() const { return _iter; }

  wrapped& data() { return _iter; }
  const wrapped& data() const { return _iter; }

private:
  wrapped _iter;
};

/// To avoid clients being moved, they are stored in unique_ptrs, which are
/// moved around in a vector. This class is purely for convenience, to still
/// have iterator semantics, and a few other utility functions
template<typename T>
struct nomove_vector {

  using value_type = T;

  std::vector<std::unique_ptr<value_type>> _order;

  using iterator = double_iterator<typename decltype(_order)::iterator>;
  using const_iterator = double_iterator<typename decltype(_order)::const_iterator>;

  using reverse_iterator = double_iterator<typename decltype(_order)::reverse_iterator>;
  using const_reverse_iterator = double_iterator<typename decltype(_order)::const_reverse_iterator>;

  value_type& push_back(const value_type& v)
  {
    auto ptr = std::make_unique<value_type>(v);
    auto res = ptr.get();
    _order.push_back(std::move(ptr));
    return *res;
  }

  value_type& push_back(value_type&& v)
  {
    auto ptr = std::make_unique<value_type>(std::move(v));
    auto res = ptr.get();
    _order.push_back(std::move(ptr));
    return *res;
  }

  iterator erase(const value_type& v)
  {
    return _order.erase(
      std::remove_if(_order.begin(), _order.end(),
                     [&v](auto&& uptr) { return uptr.get() == &v; }),
      _order.end());
  }

  iterator rotate_to_back(iterator iter) {
    if (iter != _order.end()) { { return std::rotate(iter.data(), iter.data() + 1, _order.end());
}
}
    return end();
  }

  std::size_t size() const noexcept {
    return _order.size();
  }

  bool empty() const noexcept {
    return _order.empty();
  }

  std::size_t capacity() const noexcept {
    return _order.capacity();
  }

  value_type& operator[](std::size_t n) {
    return *_order[n];
  }

  const value_type& operator[](std::size_t n) const {
    return *_order[n];
  }

  value_type& at(std::size_t n) {
    return *_order.at(n);
  }

  const value_type& at(std::size_t n) const {
    return *_order.at(n);
  }

  iterator begin() {return _order.begin();}
  iterator end() {return _order.end();}
  const_iterator begin() const {return _order.begin();}
  const_iterator end() const {return _order.end();}

  reverse_iterator rbegin() {return _order.rbegin();}
  reverse_iterator rend() {return _order.rend();}
  const_reverse_iterator rbegin() const {return _order.rbegin();}
  const_reverse_iterator rend() const {return _order.rend();}

};
