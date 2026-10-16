#pragma once

#include <xrpl/beast/net/IPEndpoint.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace xrpl::peer_finder {

/**
 * Abstract persistence for PeerFinder data.
 */
class Store
{
public:
    virtual ~Store() = default;

    // load the bootstrap cache
    using LoadCallback = std::function<void(beast::ip::Endpoint, int)>;
    virtual std::size_t
    load(LoadCallback const& cb) = 0;

    // save the bootstrap cache
    struct Entry
    {
        explicit Entry() = default;

        beast::ip::Endpoint endpoint;
        int valence{};
    };
    virtual void
    save(std::vector<Entry> const& v) = 0;
};

}  // namespace xrpl::peer_finder
