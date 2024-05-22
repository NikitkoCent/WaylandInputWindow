#ifndef WAYLAND_INPUT_WINDOW_UTILITIES_H
#define WAYLAND_INPUT_WINDOW_UTILITIES_H

#include <type_traits>      // std::remove_cv_t, std::remove_reference_t, std::is_pointer_v, std::is_same_v
#include <utility>          // std::move, std::forward, std::pair
#include <optional>         // std::optional
#include <functional>       // std::function
#include <string_view>      // std::string_view
#include <chrono>           // std::chrono::*
#include <ctime>            // std::localtime, std::strftime
#include <thread>           // std::this_thread::get_id
#include <iostream>         // std::ostream, std::cerr
#include <iomanip>          // std::setfill, std::setw, std::hex, std::setbase, std::left, std::right
#include <sstream>          // std::ostringstream
#include <cstdio>           // std::snprintf
#include <cerrno>           // errno


template<typename T>
class WLResourceWrapper
{
public:
    WLResourceWrapper() noexcept = default;

    template<typename CustomDeleter>
    WLResourceWrapper(T&& resource, CustomDeleter&& deleter)
        : impl_{ std::in_place, std::move(resource), std::forward<CustomDeleter>(deleter) }
    {}

    // ReSharper disable once CppNonExplicitConvertingConstructor
    WLResourceWrapper(T&& resource) noexcept( std::is_nothrow_move_constructible_v<T> ) // NOLINT(*-explicit-constructor)
        : WLResourceWrapper(std::move(resource), nullptr)
    {}

    WLResourceWrapper(const WLResourceWrapper&) = delete;
    WLResourceWrapper(WLResourceWrapper&&) = default;

    ~WLResourceWrapper() noexcept
    {
        reset();
    }

public:
    WLResourceWrapper& operator=(const WLResourceWrapper&) = delete;
    WLResourceWrapper& operator=(WLResourceWrapper&& rhs) noexcept( noexcept(reset()) && noexcept(impl_ = std::move(rhs.impl_)) )
    {
        if (&rhs != this)
        {
            reset();
            impl_ = std::move(rhs.impl_);
        }

        return *this;
    }

public:
    [[nodiscard]] bool hasResource() const noexcept { return impl_.has_value(); }

    [[nodiscard]] T& getResource() { return impl_.value().first; }
    [[nodiscard]] const T& getResource() const { return impl_.value().first; }

    [[nodiscard]] T& operator*() { return getResource(); }
    [[nodiscard]] const T& operator*() const { return getResource(); }

public:
    void reset()
    {
        if (impl_.has_value())
        {
            auto& deleter = impl_->second;
            if (deleter)
                deleter(impl_->first);
            impl_.reset();
        }
    }

private:
    std::optional< std::pair<T, std::function<void(T&)>> > impl_;
};


template<typename T>
bool operator==(const T& lhs, const WLResourceWrapper<T>& rhs)
{
    return ( rhs.hasResource() && (lhs == rhs.getResource()) );
}

template<typename T>
bool operator==(const WLResourceWrapper<T>& lhs, const T& rhs)
{
    return (rhs == lhs);
}

template<typename T>
bool operator!=(const T& lhs, const WLResourceWrapper<T>& rhs)
{
    return !(lhs == rhs);
}

template<typename T>
bool operator!=(const WLResourceWrapper<T>& lhs, const T& rhs)
{
    return !(lhs == rhs);
}


template<typename T, typename TF, typename D>
WLResourceWrapper< std::decay_t<T> > makeWLResourceWrapperChecked(T&& resource, const TF& invalid, D&& deleter)
{
    if (bool(std::forward<T>(resource) == invalid) == true)
        return {};

    return { std::forward<T>(resource), std::forward<D>(deleter) };
}

template<typename T, typename TF>
WLResourceWrapper< std::decay_t<T> > makeWLResourceWrapperChecked(T&& resource, const TF& invalid)
{
    if (bool(std::forward<T>(resource) == invalid) == true)
        return {};

    return { std::forward<T>(resource) };
}


namespace logging {
    template<typename... Ts>
    std::ostream& customLog(std::ostream& logStream, Ts&&... args)
    {
        std::ostringstream strStream;

        // ReSharper disable once CppDFAConstantFunctionResult
        const auto writer = [&strStream](auto&& value) -> int {
            using WithoutCVRefs = std::remove_cv_t<std::remove_reference_t<decltype(value)>>;
            if constexpr (std::is_pointer_v<WithoutCVRefs>)
            {
                if (std::forward<decltype(value)>(value) == nullptr)
                {
                    strStream << "<nullptr>";
                    return 0;
                }
            }

            strStream << std::forward<decltype(value)>(value);

            return 0;
        };

        [[maybe_unused]] const int dummy[sizeof...(Ts)] = { writer(std::forward<Ts>(args))... };
        return logStream << strStream.str();
    }


    enum class Level : unsigned
    {
        TRACE = 0,
         INFO = 1,
         WARN = 2,
        ERROR = 3
    };

    template<typename... Ts>
    std::ostream& defaultLog(
        const Level level,
        const std::string_view srcFileName,
        const unsigned long srcFileLine,
        Ts&&... args
    ) {
        const auto now = std::chrono::system_clock::now();
        char nowStrBuf[64] = {};
        { // writing the current date and time into nowStrBuf in the format YYYY-MM-DD HH:MM:SS.999
            const auto nowTimeT = std::chrono::system_clock::to_time_t(now);
            if (const auto* nowTmLocal = std::localtime(&nowTimeT); nowTmLocal != nullptr)
            {
                const auto nowTmLocalCopy = *nowTmLocal;
                if (const auto written = std::strftime(nowStrBuf, sizeof(nowStrBuf) - 1, "%F %T", &nowTmLocalCopy); written > 0) {
                    std::snprintf(
                        &nowStrBuf[written], sizeof(nowStrBuf) - written - 1, ".%.3u",
                        (unsigned)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000)
                    );
                } else {
                    nowStrBuf[0] = 0;
                }
            }
        }

        const auto tid = std::this_thread::get_id();

        const std::string_view levelStr = [level] {
            switch (level) {
                case Level::TRACE: return "TRACE";
                case Level::INFO:  return " INFO";
                case Level::WARN:  return " WARN";
                case Level::ERROR: return "ERROR";
            }
            return "<?""?""?>";
        }();

        return customLog(std::cerr,
            nowStrBuf, ' ',
            "[tid=0x", std::hex, std::setfill('0'), std::setw(16), tid, std::setfill(' '), std::setbase(0), "] ",
            srcFileName, ':', std::left, std::setfill(' '), std::setw(4), srcFileLine, std::setfill(' '), std::right, " : ",
            levelStr, " - ",
            std::forward<Ts>(args)..., '\n'
        );
    }


    #define LOGGING_THIS_FILE                                                                   \
    []() constexpr {                                                                            \
        constexpr std::string_view result = [] {                                                \
            constexpr std::string_view thisFileFullPath = __FILE__;                             \
            constexpr std::string_view thisProjectFullPath = CMAKE_PROJECT_PATH;                \
                                                                                                \
            if constexpr (thisFileFullPath.find(thisProjectFullPath) == 0)                      \
            {                                                                                   \
                constexpr auto result = thisFileFullPath.substr(thisProjectFullPath.length());  \
                if constexpr ((result.front() == '/') || (result.front() == '\\'))              \
                {                                                                               \
                    return result.substr(1);                                                    \
                }                                                                               \
                return result;                                                                  \
            }                                                                                   \
                                                                                                \
            return thisFileFullPath;                                                            \
        }();                                                                                    \
        return result;                                                                          \
    }()

    #define MY_LOG_TRACE(...) ::logging::defaultLog(::logging::Level::TRACE, LOGGING_THIS_FILE, __LINE__, __VA_ARGS__);
    #define MY_LOG_INFO(...)  ::logging::defaultLog(::logging::Level::INFO,  LOGGING_THIS_FILE, __LINE__, __VA_ARGS__);
    #define MY_LOG_WARN(...)  ::logging::defaultLog(::logging::Level::WARN,  LOGGING_THIS_FILE, __LINE__, __VA_ARGS__);
    #define MY_LOG_ERROR(...) ::logging::defaultLog(::logging::Level::ERROR, LOGGING_THIS_FILE, __LINE__, __VA_ARGS__);

    #define MY_LOG_WLCALL_VALUELESS(FUNC_CALL)                                      \
    [&] {                                                                           \
        MY_LOG_TRACE(#FUNC_CALL, "...");                                            \
        (void)FUNC_CALL;                                                            \
        const auto savedErrno = errno;                                              \
        MY_LOG_TRACE("    ... ", #FUNC_CALL, " finished.");                         \
        errno = savedErrno;                                                         \
    }()

    #define MY_LOG_WLCALL(FUNC_CALL)                                                \
    [&] {                                                                           \
        if constexpr (std::is_same_v<void, decltype(FUNC_CALL)>)                    \
        {                                                                           \
            MY_LOG_WLCALL_VALUELESS(FUNC_CALL);                                     \
        }                                                                           \
        else                                                                        \
        {                                                                           \
            MY_LOG_TRACE(#FUNC_CALL, "...");                                        \
            auto result_local = FUNC_CALL;                                          \
            /* hoping that the assignment above hasn't touched errno */             \
            const auto savedErrno = errno;                                          \
            MY_LOG_TRACE("    ... ", #FUNC_CALL, " returned ", result_local, '.');  \
            errno = savedErrno;                                                     \
            return result_local;                                                    \
        }                                                                           \
    }()
} // namespace logging


#endif // ndef WAYLAND_INPUT_WINDOW_UTILITIES_H
