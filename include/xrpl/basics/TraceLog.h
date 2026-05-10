#pragma once

#include <cstdint>

namespace xrpl {

namespace tracing {
void init(
    char const* outputPath,
    double samplingRate = 1.0,
    std::uint64_t maxFileSizeMB = 500,
    int maxFiles = 10);
void shutdown();
}  // namespace tracing

class TraceScope
{
    std::uint64_t spanId_;
    std::uint64_t traceId_;
    std::uint64_t parentId_;
    char const* func_;
    char const* file_;
    int line_;
    std::int64_t startUs_;
    bool active_;

public:
    TraceScope(char const* func, char const* file, int line) noexcept;
    ~TraceScope() noexcept;
    TraceScope(TraceScope const&) = delete;
    TraceScope& operator=(TraceScope const&) = delete;
};

}  // namespace xrpl

#define TRACE_FUNC() \
    xrpl::TraceScope xrpl_trace_scope_(__func__, __FILE__, __LINE__)
