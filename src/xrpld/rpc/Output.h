/**
 * @file
 * @brief Streaming write-sink abstraction for the RPC layer.
 *
 * Mirrors the pattern established in `include/xrpl/json/Output.h` for the
 * JSON serialization subsystem, but uses `boost::utility/string_ref` rather
 * than `boost::beast::string_view`. The divergence reflects the older lineage
 * of this header; no files in the repository currently include it, making it
 * a vestigial parallel to the canonical `json::Output` infrastructure.
 */

#pragma once

#include <boost/utility/string_ref.hpp>

namespace xrpl {
namespace RPC {

/**
 * Streaming write-sink callable for RPC response data.
 *
 * Models a write sink that accepts successive non-owning string fragments
 * without any knowledge of the underlying destination (network socket,
 * in-memory buffer, test harness, etc.). Decouples response generation
 * from delivery: any callable matching this signature satisfies the interface.
 *
 * @note This type uses `boost::string_ref` (from `boost/utility/string_ref`).
 *     The canonical JSON-layer counterpart `json::Output` uses
 *     `boost::beast::string_view` instead — the difference reflects the older
 *     lineage of this header.
 */
using Output = std::function<void(boost::string_ref const&)>;

/**
 * Create an `Output` sink that appends all fragments to a string.
 *
 * Returns a lambda that captures `s` by reference and calls
 * `s.append(b.data(), b.size())` for each fragment written to the sink.
 * Use this when a fully-materialized RPC response is needed — for example,
 * in tests or when a caller must hold the complete result as a single string.
 *
 * @param s The string to append response fragments to; must outlive the
 *     returned `Output` object.
 * @return An `Output` callable that appends each fragment to `s`.
 */
inline Output
stringOutput(std::string& s)
{
    return [&](boost::string_ref const& b) { s.append(b.data(), b.size()); };
}

}  // namespace RPC
}  // namespace xrpl
