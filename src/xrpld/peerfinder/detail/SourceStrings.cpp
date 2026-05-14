/** @file
 *  Implements the static-string peer address source for PeerFinder.
 *
 *  Bridges the gap between raw configuration strings (from `[ips]` /
 *  `[ips_fixed]` stanzas) and the typed `beast::IP::Endpoint` objects that
 *  the rest of PeerFinder requires. All parsing and validation is centralised
 *  here rather than scattered across callers.
 */

#include <xrpld/peerfinder/detail/SourceStrings.h>

#include <xrpld/peerfinder/detail/Source.h>

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>

#include <memory>
#include <string>
#include <utility>

namespace xrpl::PeerFinder {

/** Hidden concrete implementation of `SourceStrings`.
 *
 *  Defined entirely within this translation unit so that callers only ever
 *  see the abstract `Source` interface. This pimpl-adjacent pattern means
 *  implementation changes — additional fields, altered parsing logic — never
 *  force recompilation of files that include `SourceStrings.h`.
 *
 *  Constructed exclusively via `SourceStrings::make()`.
 */
class SourceStringsImp : public SourceStrings
{
public:
    /** Constructs the source, taking ownership of @p name and @p strings.
     *
     *  @param name     Human-readable label returned by `name()`, used by
     *      `Logic` in diagnostic log messages.
     *  @param strings  Raw address strings to be parsed on each `fetch()`.
     */
    SourceStringsImp(std::string name, Strings strings)
        : name_(std::move(name)), strings_(std::move(strings))
    {
    }

    ~SourceStringsImp() override = default;

    /** Returns the diagnostic label supplied at construction. */
    std::string const&
    name() override
    {
        return name_;
    }

    /** Parses the stored strings into validated endpoints.
     *
     *  Iterates over `strings_`, attempts to parse each entry via
     *  `beast::IP::Endpoint::fromString()`, and appends successfully parsed
     *  endpoints to `results.addresses`. Entries that fail to parse — empty
     *  strings, malformed addresses — are silently dropped; no error is set
     *  and no warning is logged. This is intentional: a bad config entry is
     *  a configuration problem, not a runtime fault, and the node should
     *  still connect to whichever addresses parse correctly.
     *
     *  The `journal` parameter is accepted to satisfy the `Source` interface
     *  but is unused here; a static string list has nothing asynchronous
     *  to report.
     *
     *  @param results  Output struct; `addresses` is populated with every
     *      valid endpoint found. `results.error` is never set by this
     *      implementation.
     *  @param journal  Unused; present only to satisfy the `Source` contract.
     *
     *  @note The loop contains a redundant second call to `fromString()` on
     *      the same string when the first parse fails. This produces an
     *      identical (unspecified) result and is vestigial — the effective
     *      behavior is simply: parse once, keep if valid.
     */
    void
    fetch(Results& results, beast::Journal journal) override
    {
        results.addresses.resize(0);
        results.addresses.reserve(strings_.size());
        for (int i = 0; i < strings_.size(); ++i)
        {
            beast::IP::Endpoint ep(beast::IP::Endpoint::fromString(strings_[i]));
            if (isUnspecified(ep))
                ep = beast::IP::Endpoint::fromString(strings_[i]);
            if (!isUnspecified(ep))
                results.addresses.push_back(ep);
        }
    }

private:
    /** Human-readable label for log messages. */
    std::string name_;

    /** Raw address strings to parse on each `fetch()` call. */
    Strings strings_;
};

//------------------------------------------------------------------------------

/** Creates a `Source` backed by a static list of address strings.
 *
 *  The sole factory for `SourceStringsImp`. Called from
 *  `PeerfinderManagerImp::addFallbackStrings()`, which passes the result
 *  directly to `Logic::addStaticSource()` for use as a bootstrap fallback.
 *
 *  @param name     Diagnostic label used in log output by `Logic::fetch()`.
 *  @param strings  Address strings from the node configuration (`[ips]` /
 *      `[ips_fixed]`); malformed entries are silently dropped by `fetch()`.
 *  @return A `shared_ptr<Source>` owning a newly constructed
 *      `SourceStringsImp`; callers never see the concrete type.
 */
std::shared_ptr<Source>
SourceStrings::make(std::string const& name, Strings const& strings)
{
    return std::make_shared<SourceStringsImp>(name, strings);
}

}  // namespace xrpl::PeerFinder
