#ifndef XRPL_APP_LEDGER_ABSTRACTFETCHPACKCONTAINER_H_INCLUDED
#define XRPL_APP_LEDGER_ABSTRACTFETCHPACKCONTAINER_H_INCLUDED

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>

#include <optional>

namespace ripple {

/** An interface facilitating retrieval of fetch packs without
    an application or ledgermaster object.
*/
class AbstractFetchPackContainer
{
public:
    virtual ~AbstractFetchPackContainer() = default;

    /** Retrieves partial ledger data of the coresponding hash from peers.`

        @param nodeHash The 256-bit hash of the data to fetch.
        @return `std::nullopt` if the hash isn't cached,
            otherwise, the hash associated data.
    */
    virtual std::optional<Blob>
    getFetchPack(uint256 const& nodeHash) = 0;
};

}  // namespace ripple

#endif
