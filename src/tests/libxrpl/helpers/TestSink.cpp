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

TestSink::TestSink(beast::Severity threshold) : Sink(threshold, false)
{
}

void
TestSink::write(beast::Severity level, std::string const& text)
{
    if (level < threshold())
        return;
    writeAlways(level, text);
}

void
TestSink::writeAlways(beast::Severity level, std::string const& text)
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
            case beast::Severity::Trace:
                return "\033[34m";  // blue
            case beast::Severity::Debug:
                return "\033[32m";  // green
            case beast::Severity::Info:
                return "\033[36m";  // cyan
            case beast::Severity::Warning:
                return "\033[33m";  // yellow
            case beast::Severity::Error:
                return "\033[31m";  // red
            case beast::Severity::Fatal:
            default:
                break;
        }
        return "\033[31m";  // red
    }();

    auto prefix = [level]() {
        switch (level)
        {
            case beast::Severity::Trace:
                return "TRC:";
            case beast::Severity::Debug:
                return "DBG:";
            case beast::Severity::Info:
                return "INF:";
            case beast::Severity::Warning:
                return "WRN:";
            case beast::Severity::Error:
                return "ERR:";
            case beast::Severity::Fatal:
            default:
                break;
        }
        return "FTL:";
    }();

    auto& stream = [level]() -> std::ostream& {
        switch (level)
        {
            case beast::Severity::Error:
            case beast::Severity::Fatal:
                return std::cerr;
            default:
                return std::cout;
        }
    }();

    static constexpr auto kReset = "\033[0m";

    if (supportsColor)
    {
        stream << color << prefix << " " << text << kReset << std::endl;
    }
    else
    {
        stream << prefix << " " << text << std::endl;
    }
}

}  // namespace xrpl
