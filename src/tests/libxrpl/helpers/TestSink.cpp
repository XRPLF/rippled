#include <helpers/TestSink.h>

#include <xrpl/beast/utility/Journal.h>

#include <boost/predef.h>

#include <cstdlib>  // for getenv
#include <string>

#if BOOST_OS_WINDOWS
#include <io.h>     // for _isatty, _fileno
#include <stdio.h>  // for stdout
#else
#include <unistd.h>  // for isatty, STDOUT_FILENO
#endif

#include <iostream>

namespace xrpl {

TestSink::TestSink(beast::severities::Severity threshold) : Sink(threshold, false)
{
}

void
TestSink::write(beast::severities::Severity level, std::string const& text)
{
    if (level < threshold())
        return;
    writeAlways(level, text);
}

void
TestSink::writeAlways(beast::severities::Severity level, std::string const& text)
{
    auto supportsColor = [] {
        // 1. Check for "NO_COLOR" environment variable (Standard convention)
        if (std::getenv("NO_COLOR") != nullptr)
        {
            return false;
        }

        // 2. Check for "CLICOLOR_FORCE" (Force color)
        if (std::getenv("CLICOLOR_FORCE") != nullptr)
        {
            return true;
        }

        // 3. Platform-specific check to see if stdout is a terminal
#if BOOST_OS_WINDOWS
        // Windows: Check if the output handle is a character device
        // _fileno(stdout) is usually 1
        // _isatty returns non-zero if the handle is a character device, 0
        // otherwise.
        return _isatty(_fileno(stdout)) != 0;
#else
        // Linux/macOS: Check if file descriptor 1 (stdout) is a TTY
        // STDOUT_FILENO is 1
        // isatty returns 1 if the file descriptor is a TTY, 0 otherwise.
        return isatty(STDOUT_FILENO) != 0;
#endif
    }();

    auto color = [level]() {
        switch (level)
        {
            case beast::severities::KTrace:
                return "\033[34m";  // blue
            case beast::severities::KDebug:
                return "\033[32m";  // green
            case beast::severities::KInfo:
                return "\033[36m";  // cyan
            case beast::severities::KWarning:
                return "\033[33m";  // yellow
            case beast::severities::KError:
                return "\033[31m";  // red
            case beast::severities::KFatal:
            default:
                break;
        }
        return "\033[31m";  // red
    }();

    auto prefix = [level]() {
        switch (level)
        {
            case beast::severities::KTrace:
                return "TRC:";
            case beast::severities::KDebug:
                return "DBG:";
            case beast::severities::KInfo:
                return "INF:";
            case beast::severities::KWarning:
                return "WRN:";
            case beast::severities::KError:
                return "ERR:";
            case beast::severities::KFatal:
            default:
                break;
        }
        return "FTL:";
    }();

    auto& stream = [level]() -> std::ostream& {
        switch (level)
        {
            case beast::severities::KError:
            case beast::severities::KFatal:
                return std::cerr;
            default:
                return std::cout;
        }
    }();

    constexpr auto kRESET = "\033[0m";

    if (supportsColor)
    {
        stream << color << prefix << " " << text << kRESET << std::endl;
    }
    else
    {
        stream << prefix << " " << text << std::endl;
    }
}

}  // namespace xrpl
