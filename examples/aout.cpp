#include <cctype>
#include <cstring>
#include <iostream>
#include <string>

#include <cfloat>

#include "caf/all.hpp"
#include "caf/io/all.hpp"

#define CAF_SUITE parserv2
#include "caf/test/unit_test.hpp"
#include "caf/test/unit_test_impl.hpp"

#define start()                                                                \
  char ch = ps.current();                                                      \
  goto s_init;                                                                 \
  s_unexpected_eof:                                                            \
  ps.code = ec::unexpected_eof;                                                \
  return;                                                                      \
  {                                                                            \
    static constexpr ec mismatch_ec = ec::unexpected_character

#define state(name)                                                            \
  ps.code = mismatch_ec;                                                       \
  return;                                                                      \
  }                                                                            \
  {                                                                            \
    static constexpr ec mismatch_ec = ec::unexpected_character;                \
    s_##name : __attribute__((unused));                                        \
    if (ch == '\0')                                                            \
      goto s_unexpected_eof;                                                   \
    e_##name : __attribute__((unused));

#define term_state(name)                                                       \
  ps.code = mismatch_ec;                                                       \
  return;                                                                      \
  }                                                                            \
  {                                                                            \
    static constexpr ec mismatch_ec = ec::trailing_character;                  \
    s_##name : __attribute__((unused));                                        \
    if (ch == '\0')                                                            \
      goto s_fin;                                                              \
    e_##name : __attribute__((unused));

#define fin()                                                                  \
  ps.code = mismatch_ec;                                                       \
  return;                                                                      \
  }                                                                            \
  s_fin:                                                                       \
  ps.code = ec::success;                                                       \
  return;

#define input(predicate, target)                                               \
  if (predicate(ch)) {                                                         \
    ch = ps.next();                                                            \
    goto s_##target;                                                           \
  }

/*
#define blacklist(predicate, error_code)                                       \
  if (predicate(ch)) {                                                         \
    ps.code = error_code;                                                      \
    return;                                                                    \
  }
*/

#define action(predicate, target, statement)                                   \
  if (predicate(ch)) {                                                         \
    statement;                                                                 \
    ch = ps.next();                                                            \
    goto s_##target;                                                           \
  }

#define checked_action(predicate, target, statement, error_code)               \
  if (predicate(ch)) {                                                         \
    if (statement) {                                                           \
      ch = ps.next();                                                          \
      goto s_##target;                                                         \
    } else {                                                                   \
      ps.code = error_code;                                                    \
      return;                                                                  \
    }                                                                          \
  }

#define epsilon(target) goto e_##target;

using caf::none;
using caf::none_t;
using caf::variant;
using std::string;

enum class ec : uint8_t {
  /// Not-an-error.
  success,
  /// Parser succeeded but found trailing character(s).
  trailing_character,
  /// Parser stopped after reaching the end while still expecting input.
  unexpected_eof,
  /// Parser stopped after reading an unexpected character.
  unexpected_character,
  /// Cannot construct a negative caf::duration.
  negative_duration,
  /// Cannot construct a caf::duration with a value exceeding uint32_t.
  duration_overflow,
  /// Too many characters for an atom.
  too_many_characters,
  /// Unrecognized character after escaping `\`.
  illegal_escape_sequence,
  /// Misplaced newline, e.g., inside a string.
  unexpected_newline,
  /// Parsed positive integer exceeds the number of available bits.
  integer_overflow,
  /// Parsed negative integer exceeds the number of available bits.
  integer_underflow,
  /// Exponent of parsed double is less than `DBL_MIN_EXP`.
  exponent_underflow,
  /// Exponent of parsed double is greater than `DBL_MAX_EXP`.
  exponent_overflow,
};

std::string to_string(ec x) {
  static constexpr const char* tbl[] = {
    "success",
    "trailing_character",
    "unexpected_eof",
    "unexpected_character",
    "negative_duration",
    "duration_overflow",
    "too_many_characters",
    "illegal_escape_sequence",
    "unexpected_newline",
    "integer_overflow",
    "integer_underflow",
    "exponent_underflow",
    "exponent_overflow",
  };
  return tbl[static_cast<uint8_t>(x)];
}

template <class Iterator, class Sentinel = Iterator>
struct parser_state {
  Iterator i;
  Sentinel e;
  ec code;
  int32_t line = 1;
  int32_t column = 1;

  /// Returns the null terminator when reaching the end of the string.
  /// Otherwise the next character.
  inline char next() noexcept {
    ++i;
    if (i != e) {
      auto c = *i;
      if (c == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
      return c;
    }
    return '\0';
  }

  /// Returns the null terminator if `i == e`, otherwise the current character.
  inline char current() const noexcept {
    return i != e ? *i : '\0';
  }
};

template <int Base, class T>
struct ascii_to_int {
  constexpr T operator()(char c) const {
    return c - '0';
  }
};

template <class T>
struct ascii_to_int<16, T> {
  inline T operator()(char c) const {
    // Numbers start at position 48 in the ASCII table.
    if (c <= '9')
      return c - '0';
    // Uppercase characters start at position 65 in the ASCII table.
    if (c <= 'F')
      return 10 + (c - 'A');
    // Lowercase characters start at position 97 in the ASCII table.
    return 10 + (c - 'a');
  }
};

// Sum up integers when parsing positive integers.
// @returns `false` on an overflow, otherwise `true`.
// @pre `isdigit(c) || (Base == 16 && isxdigit(c))`
template <int Base, class T>
bool add_ascii(T& x, char c) {
  ascii_to_int<Base, T> f;
  auto before = x;
  x = (x * 10) + f(c);
  return before <= x;
}

// Subtracs integers when parsing negative integers.
// @returns `false` on an underflow, otherwise `true`.
// @pre `isdigit(c) || (Base == 16 && isxdigit(c))`
template <int Base, class T>
bool sub_ascii(T& x, char c) {
  ascii_to_int<Base, T> f;
  auto before = x;
  x = (x * 10) - f(c);
  return before >= x;
}

/// Parses a number, i.e., on success produces either an `int64_t` or a
/// `double`.
template <class Iterator, class Sentinel, class Consumer>
void parse_number(parser_state<Iterator, Sentinel>& ps, Consumer& consumer) {
  // We assume a simple integer until proven wrong.
  enum result_type_t { integer, positive_double, negative_double };
  auto result_type = integer;
  // Exponent part of a floating point literal.
  auto exp = 0;
  // Our result when reading a floating point number.
  auto dbl_res = 0.0;
  // Our result when reading an integer number.
  int64_t int_res = 0;
  // Computes the result on success.
  auto g = caf::detail::make_scope_guard([&] {
    if (ps.code <= ec::trailing_character) {
      if (result_type == integer) {
        consumer.value(int_res);
        return;
      }
      // Compute correct floating point number.
      // 1) Check whether exponent is in valid range.
      if (exp < DBL_MIN_EXP) {
        ps.code = ec::exponent_underflow;
        return;
      }
      if (exp > DBL_MAX_EXP) {
        ps.code = ec::exponent_overflow;
        return;
      }
      // 2) Fix sign.
      if (result_type == negative_double)
        dbl_res = -dbl_res;
      // 3) Scale result.
      auto p = 10.;
      for (auto n = std::abs(exp); n != 0; n >>= 1, p *= 10) {
        if (n & 0x01) {
          if (exp < 0)
            dbl_res /= p;
          else
            dbl_res *= p;
        }
      }
      // Done.
      consumer.value(dbl_res);
    }
  });
  // Switches from parsing an integer to parsing a double.
  auto ch_res = [&](result_type_t x) {
    CAF_ASSERT(result_type = integer);
    result_type = x;
    // We parse the double number always as positive a positive number and
    // restore the sign in `g`.
    dbl_res = result_type == negative_double
              ? -static_cast<double>(int_res)
              : static_cast<double>(int_res);
  };
  // Character predicates.
  auto is_plus = [](char c) {
    return c == '+';
  };
  auto is_minus = [](char c) {
    return c == '-';
  };
  auto is_dot = [](char c) {
    return c == '.';
  };
  auto is_e = [](char c) {
    return c == 'E' || c == 'e';
  };
  // Reads the a decimal place.
  auto rd_decimal = [&](char c) {
    --exp;
    return add_ascii<10>(dbl_res, c);
  };
  // Definition of our parser FSM.
  start();
  state(init) {
    input(isspace, init)
    input(is_plus, has_plus)
    input(is_minus, has_minus)
    epsilon(has_plus)
  }
  // "+" or "-" alone aren't numbers.
  state(has_plus) {
    action(is_dot, leading_dot, ch_res(positive_double))
    epsilon(pos_dec)
  }
  state(has_minus) {
    action(is_dot, leading_dot, ch_res(negative_double))
    epsilon(neg_dec)
  }
  // Reads the integer part of the mantissa or a positive decimal integer.
  term_state(pos_dec) {
    checked_action(isdigit, pos_dec, add_ascii<10>(int_res, ch), ec::integer_overflow)
    action(is_e, has_e, ch_res(positive_double))
    action(is_dot, trailing_dot, ch_res(positive_double))
  }
  // Reads the integer part of the mantissa or a negative decimal integer.
  term_state(neg_dec) {
    checked_action(isdigit, neg_dec, sub_ascii<10>(int_res, ch), ec::integer_underflow)
    action(is_e, has_e, ch_res(negative_double))
    action(is_dot, trailing_dot, ch_res(negative_double))
  }
  /// ".", "+.", etc. aren't valid numbers, so this state isn't terminal.
  state(leading_dot) {
    checked_action(isdigit, after_dot, rd_decimal(ch), ec::exponent_underflow)
  }
  /// "1." is a valid number, so a trailing dot is a terminal state.
  term_state(trailing_dot) {
    checked_action(isdigit, after_dot, rd_decimal(ch), ec::exponent_underflow)
  }
  term_state(after_dot) {
    checked_action(isdigit, after_dot, rd_decimal(ch), ec::exponent_underflow)
  }
  state(has_e) {
    input(is_plus, has_plus_after_e)
    input(is_minus, has_minus_after_e)
    epsilon(pos_exp)
  }
  state(has_plus_after_e) {
    epsilon(pos_exp)
  }
  state(has_minus_after_e) {
    epsilon(neg_exp)
  }
  term_state(pos_exp) {
    checked_action(isdigit, pos_exp, add_ascii<10>(exp, ch), ec::exponent_overflow)
  }
  term_state(neg_exp) {
    checked_action(isdigit, neg_exp, sub_ascii<10>(exp, ch), ec::exponent_underflow)
  }
  fin();
}

struct numbers_parser_consumer {
  variant<int64_t, double> x;
  inline void value(double y) {
    x = y;
  }
  inline void value(int64_t y) {
    x = y;
  }
};

using res_t = variant<ec, double, int64_t>;

struct numbers_parser {
  res_t operator()(std::string str) {
    parser_state<string::iterator> res;
    numbers_parser_consumer f;
    res.i = str.begin();
    res.e = str.end();
    parse_number(res, f);
    if (res.code == ec::success)
      return f.x;
    return res.code;
  }
};

template <class T>
typename std::enable_if<std::is_integral<T>::value, res_t>::type res(T x) {
  return {static_cast<int64_t>(x)};
}

template <class T>
typename std::enable_if<std::is_floating_point<T>::value, res_t>::type
res(T x) {
  return {static_cast<double>(x)};
}

res_t res(ec x) {
  return {x};
}

CAF_TEST(numbers) {
  std::cout << caf::deep_to_string(res(42)) << std::endl;
  numbers_parser p;
  CAF_CHECK_EQUAL(p("0"), res(0));
  CAF_CHECK_EQUAL(p("10"), res(10));
  CAF_CHECK_EQUAL(p("123"), res(123));
  CAF_CHECK_EQUAL(p("+0"), res(0));
  CAF_CHECK_EQUAL(p("+10"), res(10));
  CAF_CHECK_EQUAL(p("+123"), res(123));
  CAF_CHECK_EQUAL(p("-0"), res(0));
  CAF_CHECK_EQUAL(p("-10"), res(-10));
  CAF_CHECK_EQUAL(p("-123"), res(-123));
  CAF_CHECK_EQUAL(p("0x10"), res(16));
  CAF_CHECK_EQUAL(p("1."), res(1.));
  CAF_CHECK_EQUAL(p("+1."), res(1.));
  CAF_CHECK_EQUAL(p("-1."), res(-1.));
  CAF_CHECK_EQUAL(p("1.123"), res(1.123));
  CAF_CHECK_EQUAL(p("+1.123"), res(1.123));
  CAF_CHECK_EQUAL(p("-1.123"), res(-1.123));
  CAF_CHECK_EQUAL(p(".123"), res(.123));
  CAF_CHECK_EQUAL(p("+.123"), res(.123));
  CAF_CHECK_EQUAL(p("-.123"), res(-.123));
  CAF_CHECK_EQUAL(p("1e1"), res(1e1));
  CAF_CHECK_EQUAL(p("1e2"), res(1e2));
  CAF_CHECK_EQUAL(p("-1e2"), res(-1e2));
  CAF_CHECK_EQUAL(p("1e-2"), res(1e-2));
  CAF_CHECK_EQUAL(p("-1e-2"), res(-1e-2));
}

