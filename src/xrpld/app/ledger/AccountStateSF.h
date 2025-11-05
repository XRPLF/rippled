#ifndef XRPL_APP_LEDGER_ACCOUNTSTATESF_H_INCLUDED
#define XRPL_APP_LEDGER_ACCOUNTSTATESF_H_INCLUDED

#include <xrpld/app/ledger/AbstractFetchPackContainer.h>

#include <xrpl/nodestore/Database.h>
#include <xrpl/shamap/SHAMapSyncFilter.h>

namespace ripple {

// This class is only needed on add functions
// sync filter for account state nodes during ledger sync
class AccountStateSF : public SHAMapSyncFilter
{
public:
    AccountStateSF(NodeStore::Database& db, AbstractFetchPackContainer& fp)
        : db_(db), fp_(fp)
    {
    }

    void
    gotNode(
        bool fromFilter,
        SHAMapHash const& nodeHash,
        std::uint32_t ledgerSeq,
        Blob&& nodeData,
        SHAMapNodeType type) const override;

    std::optional<Blob>
    getNode(SHAMapHash const& nodeHash) const override;

private:
    NodeStore::Database& db_;
    AbstractFetchPackContainer& fp_;
};

}  // namespace ripple

#endif
