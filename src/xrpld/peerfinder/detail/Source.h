#ifndef XRPL_PEERFINDER_SOURCE_H_INCLUDED
#define XRPL_PEERFINDER_SOURCE_H_INCLUDED

#include <xrpld/peerfinder/PeerfinderManager.h>

#include <boost/system/error_code.hpp>

namespace ripple {
namespace PeerFinder {

/** A static or dynamic source of peer addresses.
    These are used as fallbacks when we are bootstrapping and don't have
    a local cache, or when none of our addresses are functioning. Typically
    sources will represent things like static text in the config file, a
    separate local file with addresses, or a remote HTTPS URL that can
    be updated automatically. Another solution is to use a custom DNS server
    that hands out peer IP addresses when name lookups are performed.
*/
class Source
{
public:
    /** The results of a fetch. */
    struct Results
    {
        explicit Results() = default;

        // error_code on a failure
        boost::system::error_code error;

        // list of fetched endpoints
        IPAddresses addresses;
    };

    virtual ~Source()
    {
    }
    virtual std::string const&
    name() = 0;
    virtual void
    cancel()
    {
    }
    virtual void
    fetch(Results& results, beast::Journal journal) = 0;
};

}  // namespace PeerFinder
}  // namespace ripple

#endif
