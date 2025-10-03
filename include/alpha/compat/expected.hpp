/**
* @file expected.hpp
 * @brief Compatibility shim for std::expected (C++23) and tl::expected (C++20).
 *
 * This header provides a unified alias for expected/unexpected so the rest
 * of the codebase does not depend directly on a specific implementation.
 *
 * - In C++23 and later: uses <expected> from the standard library.
 * - In C++20 or earlier: falls back to <tl/expected.hpp>, a header-only
 *   backport by TartanLlama (https://github.com/TartanLlama/expected).
 */
#pragma once

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
  #include <expected>
  namespace alpha_detail {
      template<class T, class E> using expected   = std::expected<T,E>;
      template<class E>          using unexpected = std::unexpected<E>;
  }
#else
#include <tl/expected.hpp>
namespace alpha_detail {
      template<class T, class E> using expected   = tl::expected<T,E>;
      template<class E>          using unexpected = tl::unexpected<E>;
  }
#endif
