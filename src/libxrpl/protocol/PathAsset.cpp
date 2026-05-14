/** @file
 *  Non-inline serialization interface for PathAsset.
 *
 *  All other PathAsset operations (holds, get, isXRP, operator==,
 *  hash_append, constructors) are implemented inline in PathAsset.h.
 *  to_string() and operator<< are defined here to give them a single
 *  translation-unit home and avoid ODR concerns from repeated inclusion.
 */

#include <xrpl/protocol/PathAsset.h>

#include <ostream>
#include <string>
#include <variant>

namespace xrpl {

/** Produce a human-readable description of a PathAsset.
 *
 *  Dispatches via std::visit to either to_string(Currency const&) or
 *  to_string(MPTID const&) depending on the active alternative. For a
 *  Currency this yields the ISO 4217 ticker or "XRP"; for an MPTID it
 *  yields the base-58 encoded token identifier.
 *
 *  @param asset The path asset to stringify.
 *  @return A descriptive string identifying the currency or MPT issuance.
 */
std::string
to_string(PathAsset const& asset)
{
    return std::visit([&](auto const& issue) { return to_string(issue); }, asset.value());
}

/** Stream-insert a human-readable description of a PathAsset.
 *
 *  Equivalent to `os << to_string(x)`. Intended for logging and diagnostics.
 *
 *  @param os The output stream.
 *  @param x The path asset to write.
 *  @return `os`, for chaining.
 */
std::ostream&
operator<<(std::ostream& os, PathAsset const& x)
{
    os << to_string(x);
    return os;
}

}  // namespace xrpl
