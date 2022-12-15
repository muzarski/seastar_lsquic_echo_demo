#ifndef __TUT_LOGGER_H__
#define __TUT_LOGGER_H__

#include <cstdlib>  // std::exit, EXIT_FAILURE
#include <iostream> // ostreams

#if __cplusplus >= 202002L

#include <source_location>

namespace zpp {

template<typename T, typename Stream = std::ostream>
concept printable = requires(Stream s, T t) {
    { s << t } -> std::same_as<std::decay_t<Stream>&>;
};

namespace detail {

inline std::ostream &get_stderr_stream() {
    return std::cerr;
}

inline std::ostream &get_stdout_stream() {
    return std::cout;
}

template<typename... Ts>
    requires (printable<Ts> && ...)
inline void log(std::ostream &stream, const Ts &...ts) {
    ([&](const auto &msg) {
        stream << msg;
    }(ts), ...);
    stream << "\n";
}

} // namespace detail

// Log a message
template<typename... Ts>
    requires (printable<Ts> && ...)
inline void log(const Ts &...ts) {
    detail::log(detail::get_stdout_stream(), ts...);
}

// Log an error message
template<typename... Ts>
    requires (printable<Ts> && ...)
inline void elog(const Ts &...ts) {
    detail::log(detail::get_stderr_stream(), ts...);
}

// Exit the program with a message
template<typename... Ts>
    requires (printable<Ts> && ...)
inline void fail(const Ts &...ts) {
    elog(ts...);
    std::exit(EXIT_FAILURE);
}

} // namespace zpp

// Clang still doesn't support std::source_location
#if not defined(__clang__)

namespace zpp {
namespace detail {

inline void log_location(std::ostream &stream, const std::source_location &location) {
    stream << '[' << location.file_name() << ", " << location.function_name() << ':' << location.line() << "]: ";
}

template<typename... Ts>
    requires (printable<Ts> && ...)
inline void flog(const std::source_location &location, const Ts &...ts) {
    decltype(auto) stream = get_stdout_stream();
    log_location(stream, location);
    log(stream, ts...);
}

template<typename... Ts>
    requires (printable<Ts> && ...)
inline void eflog(const std::source_location &location, const Ts &...ts) {
    decltype(auto) stream = get_stderr_stream();
    log_location(stream, location);
    log(stream, ts...);
}

template<typename... Ts>
    requires (printable<Ts> && ...)
inline void ffail(const std::source_location &location, const Ts &...ts) {
    eflog(location, ts...);
    std::exit(EXIT_FAILURE);
}

} // namespace detail
} // namespace zpp

// Log with the location
#define  flog(...)  detail::flog(std::source_location::current(), __VA_ARGS__)
// Log an error with the location
#define eflog(...) detail::eflog(std::source_location::current(), __VA_ARGS__)
// Exit the program with the location
#define ffail(...) detail::ffail(std::source_location::current(), __VA_ARGS__)

#else // aka IF (defined(__clang__))

#if not defined(PROJECT_ROOT_PATH)
#define PROJECT_ROOT_PATH ""
#endif

#define LOGGER_IS_PREFIX(prefix, string)                                    \
    ([](const char *const prefix_, const char *const string_) constexpr {   \
        std::size_t idx = 0;                                                \
        while (prefix_[idx] != '\0') {                                      \
            if (string_[idx] == '\0' || prefix_[idx] != string_[idx]) {     \
                return false;                                               \
            }                                                               \
            ++idx;                                                          \
        }                                                                   \
        return true;                                                        \
    }(prefix, string))

#define LOGGER_DISCARD_PREFIX(prefix, string)                               \
    ([](const char *const prefix_, const char *const string_) constexpr {   \
        std::size_t idx = 0;                                                \
        while (prefix_[idx] != '\0') {                                      \
            ++idx;                                                          \
        }                                                                   \
        return &string_[idx];                                               \
    }(prefix, string))

#define LOGGER_GET_FILEPATH(filepath)                                   \
    ([](const char *const filepath_) constexpr {                        \
        if (LOGGER_IS_PREFIX(PROJECT_ROOT_PATH, filepath_)) {           \
            return LOGGER_DISCARD_PREFIX(PROJECT_ROOT_PATH, filepath_); \
        } else {                                                        \
            return filepath_;                                           \
        }                                                               \
    }(filepath))

// Log with the location
#define  flog(...)  log('[', LOGGER_GET_FILEPATH(__FILE__), ", ", __func__, ':', __LINE__, "]: ", __VA_ARGS__)
// Log an error with the location
#define eflog(...) elog('[', LOGGER_GET_FILEPATH(__FILE__), ", ", __func__, ':', __LINE__, "]: ", __VA_ARGS__)
// Exit the program with the location
#define ffail(...) fail('[', LOGGER_GET_FILEPATH(__FILE__), ", ", __func__, ':', __LINE__, "]: ", __VA_ARGS__)

#endif // not defined(__clang__)

#else // aka IF NOT (__cplusplus >= 202002L)

#include <type_traits>

namespace zpp {
namespace detail {

// SFINAE version
template<typename T>
struct is_ostream_defined_aux {
    template<typename U>
    static auto test(U*) -> decltype(std::declval<std::ostream>() << std::declval<U>());
    template<typename>
    static auto test(...) -> std::false_type;

    using type = typename std::is_same<std::ostream, std::decay_t<decltype(test<T>(0))>>::type;
};

template<typename T>
struct is_ostream_defined : is_ostream_defined_aux<T>::type {};

template<typename... Ts>
inline void log(std::ostream &stream, const Ts &...ts) {
    static_assert((is_ostream_defined<Ts>::value && ...),
        "Operator << is not defined for one of the types passed to the function.");

    ([&](const auto &msg) {
        stream << msg;
    }(ts), ...);
    stream << "\n";
}

inline std::ostream &get_stderr_stream() {
    return std::cerr;
}

inline std::ostream &get_stdout_stream() {
    return std::cout;
}

} // namespace detail

// Log a message
template<typename... Ts>
inline void log(const Ts &...ts) {
    detail::log(detail::get_stdout_stream(), ts...);
}

// Log an error message
template<typename... Ts>
inline void elog(const Ts &...ts) {
    detail::log(detail::get_stderr_stream(), ts...);
}

// Exit the program with a message
template<typename... Ts>
inline void fail(const Ts &...ts) {
    elog(ts...);
    std::exit(EXIT_FAILURE);
}

} // namespace zpp

// Log with the location
#define  flog(...)  log('[', __FILE__, ", ", __func__, ':', __LINE__, "]: ", __VA_ARGS__)
// Log an error with the location
#define eflog(...) elog('[', __FILE__, ", ", __func__, ':', __LINE__, "]: ", __VA_ARGS__)
// Exit the program with the location
#define ffail(...) fail('[', __FILE__, ", ", __func__, ':', __LINE__, "]: ", __VA_ARGS__)

#endif // __cplusplus >= 202002L
#endif // __TUT_LOGGER_H__
