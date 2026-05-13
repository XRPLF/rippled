#pragma once

#include <xrpld/peerfinder/detail/Source.h>

#include <memory>

namespace xrpl::PeerFinder {

/** Bootstrap peer address source backed by a static list of config strings.
 *
 *  Wraps the operator-supplied `[ips]` / `[ips_fixed]` address strings from
 *  the node configuration into the polymorphic `Source` interface consumed by
 *  `PeerFinder::Logic`. It is registered as a static (non-refreshable) source
 *  via `Logic::addStaticSource()`, meaning `fetch()` is called once at startup
 *  and the results are fed directly into `Bootcache`.
 *
 *  The concrete implementation (`SourceStringsImp`) is hidden in the `.cpp`
 *  file. The `make()` factory returns a `shared_ptr<Source>`, so callers are
 *  never exposed to the concrete type and coupling is minimised.
 *
 *  @note `fetch()` silently drops malformed address strings — no error is set
 *      and no warning is logged. A bad config entry is a configuration problem,
 *      not a runtime fault. `cancel()` is a no-op: there is no async I/O to
 *      interrupt.
 */
class SourceStrings : public Source
{
public:
    explicit SourceStrings() = default;

    /** Ordered list of raw address strings from the node configuration. */
    using Strings = std::vector<std::string>;

    /** Creates a `Source` backed by a static list of address strings.
     *
     *  The sole factory for the hidden `SourceStringsImp` subclass. Invoked
     *  from `PeerfinderManagerImp::addFallbackStrings()`, which passes the
     *  result directly to `Logic::addStaticSource()`.
     *
     *  @param name     Diagnostic label used in log output by `Logic::fetch()`.
     *  @param strings  Raw address strings (e.g. from `[ips]` / `[ips_fixed]`);
     *      entries that fail to parse as `beast::IP::Endpoint` are silently
     *      dropped during `fetch()`.
     *  @return A `shared_ptr<Source>` owning the newly constructed source;
     *      callers never see the concrete `SourceStringsImp` type.
     */
    static std::shared_ptr<Source>
    make(std::string const& name, Strings const& strings);
};

}  // namespace xrpl::PeerFinder
