#ifndef ALPHA_VERSION_HPP
#define ALPHA_VERSION_HPP

#pragma once

namespace alpha {

    /// Project semantic version components
    inline constexpr int version_major = 0;
    inline constexpr int version_minor = 1;
    inline constexpr int version_patch = 0;

    /// Combined version string (e.g. "0.1.0")
    inline constexpr const char* version_string = "0.1.0";

} // namespace alpha

#endif // ALPHA_VERSION_HPP
