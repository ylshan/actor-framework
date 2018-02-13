/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#ifndef CAF_TIMESTAMP_HPP
#define CAF_TIMESTAMP_HPP

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

#include "caf/config.hpp"
#include "caf/fwd.hpp"

namespace caf {

/// A portable timespan type with nanosecond resolution.
using timespan = std::chrono::duration<int64_t, std::nano>;

CAF_PUSH_DEPRECATED_WARNINGS

/// Represents an "infinitely" long timeout, i.e., a timespan with maximum
/// count (0x7FFFFFFFFFFFFFFF or approx. 292 years).
struct infinite_t {
  constexpr infinite_t() {
    // nop
  }

  /// Returns `timespan::max()`.
  constexpr operator timespan() const {
    return timespan::max();
  }

  operator duration() const;
};

CAF_POP_WARNINGS

/// Convenience constant for defining infinite timeouts.
static constexpr infinite_t infinite = infinite_t{};

/// A portable timestamp with nanosecond resolution anchored at the UNIX epoch.
using timestamp = std::chrono::time_point<std::chrono::system_clock, timespan>;

/// Convenience function for returning a `timestamp` representing
/// the current system time.
timestamp make_timestamp();

/// Converts the time-since-epoch of `x` to a `string`.
std::string timestamp_to_string(const timestamp& x);

/// Appends the time-since-epoch of `y` to `x`.
void append_timestamp_to_string(std::string& x, const timestamp& y);

} // namespace caf

#endif // CAF_TIMESTAMP_HPP
