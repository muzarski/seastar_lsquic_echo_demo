#ifndef __TUT_LOGGER_H__
#define __TUT_LOGGER_H__

#include <cstdlib>      // std::exit, EXIT_FAILURE
#include <iostream>     // ostreams
#include <string_view>

namespace zpp {

namespace detail {

inline std::ostream &get_stderr_stream() {
    return std::cerr;
}

} // namespace detail

inline void log(std::ostream &stream, const std::string_view msg) {
    stream << msg << "\n";
}

inline void log(const std::string_view msg) {
    log(detail::get_stderr_stream(), msg);
}

inline void fail(std::ostream &stream, const std::string_view msg) {
    stream << msg << "\n";
    std::exit(EXIT_FAILURE);
}

inline void fail(const std::string_view msg) {
    fail(detail::get_stderr_stream(), msg);
}

} // namespace zpp

#endif // __TUT_LOGGER_H__
