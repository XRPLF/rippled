#include <xrpl/basics/TraceLog.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>

namespace xrpl {

namespace {

std::atomic<bool> s_enabled{false};
std::atomic<double> s_samplingRate{1.0};
std::atomic<std::uint64_t> s_nextSpanId{1};
std::mutex s_fileMutex;
std::FILE* s_file{nullptr};
std::string s_basePath;
std::uint64_t s_maxFileSize{500ULL * 1024 * 1024};
int s_maxFiles{10};
std::uint64_t s_currentFileSize{0};

constexpr int kMaxStackDepth = 512;
constexpr std::size_t kFlushThreshold = 65536;

struct ThreadContext
{
    std::uint64_t traceId{0};
    std::uint64_t spanStack[kMaxStackDepth];
    int stackDepth{0};
    bool sampled{true};
    std::string buffer;
};

thread_local ThreadContext t_ctx;

std::uint64_t
generateTraceId()
{
    thread_local std::mt19937_64 rng(
        std::chrono::steady_clock::now().time_since_epoch().count() ^
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    return rng();
}

void
rotateFiles()
{
    if (s_file)
    {
        std::fclose(s_file);
        s_file = nullptr;
    }

    // traces.jsonl.9 -> delete, traces.jsonl.8 -> .9, ... .0 -> .1
    for (int i = s_maxFiles - 1; i >= 0; --i)
    {
        std::string src =
            (i == 0) ? s_basePath : (s_basePath + "." + std::to_string(i));
        std::string dst = s_basePath + "." + std::to_string(i + 1);

        if (i == s_maxFiles - 1)
            std::remove(src.c_str());
        else
            std::rename(src.c_str(), dst.c_str());
    }

    s_file = std::fopen(s_basePath.c_str(), "w");
    s_currentFileSize = 0;
}

void
flushBuffer()
{
    if (t_ctx.buffer.empty())
        return;
    std::lock_guard<std::mutex> lock(s_fileMutex);
    if (s_file)
    {
        auto written =
            std::fwrite(t_ctx.buffer.data(), 1, t_ctx.buffer.size(), s_file);
        std::fflush(s_file);
        s_currentFileSize += written;

        if (s_currentFileSize >= s_maxFileSize)
            rotateFiles();
    }
    t_ctx.buffer.clear();
}

char const*
extractFilename(char const* path)
{
    char const* last = path;
    for (char const* p = path; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
            last = p + 1;
    }
    return last;
}

void
appendJsonString(std::string& buf, char const* s)
{
    buf += '"';
    for (; *s; ++s)
    {
        switch (*s)
        {
            case '"':
                buf += "\\\"";
                break;
            case '\\':
                buf += "\\\\";
                break;
            case '\n':
                buf += "\\n";
                break;
            case '\r':
                buf += "\\r";
                break;
            case '\t':
                buf += "\\t";
                break;
            default:
                buf += *s;
        }
    }
    buf += '"';
}

void
appendHex(std::string& buf, std::uint64_t val)
{
    static constexpr char kHEX[] = "0123456789abcdef";
    char hex[17];
    for (int i = 15; i >= 0; --i)
    {
        hex[i] = kHEX[val & 0xf];
        val >>= 4;
    }
    hex[16] = '\0';
    buf.append(hex, 16);
}

void
writeSpan(
    std::uint64_t traceId,
    std::uint64_t spanId,
    std::uint64_t parentId,
    char const* name,
    char const* file,
    int line,
    std::int64_t startUs,
    std::int64_t durUs)
{
    auto& buf = t_ctx.buffer;

    buf += "{\"name\":";
    appendJsonString(buf, name);
    buf += ",\"trace\":\"";
    appendHex(buf, traceId);
    buf += "\",\"span\":\"";
    appendHex(buf, spanId);
    buf += "\",\"parent\":\"";
    appendHex(buf, parentId);
    buf += "\",\"file\":";
    appendJsonString(buf, extractFilename(file));
    buf += ",\"line\":";
    buf += std::to_string(line);
    buf += ",\"start_us\":";
    buf += std::to_string(startUs);
    buf += ",\"dur_us\":";
    buf += std::to_string(durUs);
    buf += "}\n";

    if (buf.size() > kFlushThreshold || t_ctx.stackDepth == 0)
        flushBuffer();
}

}  // namespace

namespace tracing {

void
init(char const* outputPath, double samplingRate, std::uint64_t maxFileSizeMB, int maxFiles)
{
    std::lock_guard<std::mutex> lock(s_fileMutex);
    if (s_file)
        std::fclose(s_file);
    s_basePath = outputPath;
    s_maxFileSize = maxFileSizeMB * 1024ULL * 1024ULL;
    s_maxFiles = maxFiles;
    s_file = std::fopen(outputPath, "a");
    if (s_file)
    {
        std::fseek(s_file, 0, SEEK_END);
        s_currentFileSize = static_cast<std::uint64_t>(std::ftell(s_file));
    }
    s_samplingRate.store(samplingRate, std::memory_order_relaxed);
    s_enabled.store(s_file != nullptr, std::memory_order_release);
}

void
shutdown()
{
    s_enabled.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(s_fileMutex);
    if (s_file)
    {
        std::fclose(s_file);
        s_file = nullptr;
    }
}

}  // namespace tracing

TraceScope::TraceScope(
    char const* func,
    char const* file,
    int line) noexcept
    : spanId_(0)
    , traceId_(0)
    , parentId_(0)
    , func_(func)
    , file_(file)
    , line_(line)
    , startUs_(0)
    , active_(false)
{
    if (!s_enabled.load(std::memory_order_relaxed))
        return;

    spanId_ = s_nextSpanId.fetch_add(1, std::memory_order_relaxed);

    if (t_ctx.stackDepth == 0)
    {
        traceId_ = generateTraceId();
        parentId_ = 0;

        double rate = s_samplingRate.load(std::memory_order_relaxed);
        if (rate < 1.0)
        {
            thread_local std::mt19937 samplerRng(std::random_device{}());
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            t_ctx.sampled = dist(samplerRng) < rate;
        }
        else
        {
            t_ctx.sampled = true;
        }
    }
    else
    {
        traceId_ = t_ctx.traceId;
        parentId_ = t_ctx.spanStack[t_ctx.stackDepth - 1];
    }

    t_ctx.traceId = traceId_;
    if (t_ctx.stackDepth < kMaxStackDepth)
        t_ctx.spanStack[t_ctx.stackDepth] = spanId_;
    ++t_ctx.stackDepth;

    if (!t_ctx.sampled)
        return;

    startUs_ = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    active_ = true;
}

TraceScope::~TraceScope() noexcept
{
    if (spanId_ == 0)
        return;

    if (active_)
    {
        auto const endUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        writeSpan(
            traceId_,
            spanId_,
            parentId_,
            func_,
            file_,
            line_,
            startUs_,
            endUs - startUs_);
    }

    --t_ctx.stackDepth;
    if (t_ctx.stackDepth == 0)
        t_ctx.traceId = 0;
}

}  // namespace xrpl
