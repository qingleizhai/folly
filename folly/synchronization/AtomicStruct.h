/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <folly/ConstexprMath.h>
#include <folly/Traits.h>
#include <folly/synchronization/detail/AtomicUtils.h>

namespace folly {

namespace detail {
template <size_t>
struct AtomicStructRaw;
template <>
struct AtomicStructRaw<0> {
  using type = uint8_t;
};
template <>
struct AtomicStructRaw<1> {
  using type = uint16_t;
};
template <>
struct AtomicStructRaw<2> {
  using type = uint32_t;
};
template <>
struct AtomicStructRaw<3> {
  using type = uint64_t;
};
} // namespace detail

/// AtomicStruct<T> work like C++ atomics, but can be used on any POD
/// type <= 8 bytes.
template <typename T, template <typename> class Atom = std::atomic>
class AtomicStruct {
 private:
  using Raw = _t<detail::AtomicStructRaw<constexpr_log2_ceil(sizeof(T))>>;

  static_assert(alignof(T) <= alignof(Raw), "underlying type is under-aligned");
  static_assert(sizeof(T) <= sizeof(Raw), "underlying type is under-sized");
  static_assert(
      std::is_trivial<T>::value || std::is_trivially_copyable<T>::value,
      "target type must be trivially copyable");

  Atom<Raw> data;

  static Raw encode(T v) noexcept {
    // we expect the compiler to optimize away the memcpy, but without
    // it we would violate strict aliasing rules
    Raw d = 0;
    memcpy(&d, static_cast<void*>(&v), sizeof(T));
    return d;
  }

  static T decode(Raw d) noexcept {
    T v;
    memcpy(static_cast<void*>(&v), &d, sizeof(T));
    return v;
  }

 public:
  AtomicStruct() = default;
  ~AtomicStruct() = default;
  AtomicStruct(AtomicStruct<T> const&) = delete;
  AtomicStruct<T>& operator=(AtomicStruct<T> const&) = delete;

  constexpr /* implicit */ AtomicStruct(T v) noexcept : data(encode(v)) {}

  bool is_lock_free() const noexcept { return data.is_lock_free(); }

  bool compare_exchange_strong(
      T& v0, T v1, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    return compare_exchange_strong(
        v0, v1, mo, detail::default_failure_memory_order(mo));
  }
  bool compare_exchange_strong(
      T& v0,
      T v1,
      std::memory_order success,
      std::memory_order failure) noexcept {
    Raw d0 = encode(v0);
    bool rv = data.compare_exchange_strong(d0, encode(v1), success, failure);
    if (!rv) {
      v0 = decode(d0);
    }
    return rv;
  }

  bool compare_exchange_weak(
      T& v0, T v1, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    return compare_exchange_weak(
        v0, v1, mo, detail::default_failure_memory_order(mo));
  }
  bool compare_exchange_weak(
      T& v0,
      T v1,
      std::memory_order success,
      std::memory_order failure) noexcept {
    Raw d0 = encode(v0);
    bool rv = data.compare_exchange_weak(d0, encode(v1), success, failure);
    if (!rv) {
      v0 = decode(d0);
    }
    return rv;
  }

  T exchange(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    return decode(data.exchange(encode(v), mo));
  }

  /* implicit */ operator T() const noexcept { return decode(data); }

  T load(std::memory_order mo = std::memory_order_seq_cst) const noexcept {
    return decode(data.load(mo));
  }

  T operator=(T v) noexcept { return decode(data = encode(v)); }

  void store(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept {
    data.store(encode(v), mo);
  }

  // std::atomic also provides volatile versions of all of the access
  // methods.  These are callable on volatile objects, and also can
  // theoretically have different implementations than their non-volatile
  // counterpart.  If someone wants them here they can easily be added
  // by duplicating the above code and the corresponding unit tests.
};

} // namespace folly
