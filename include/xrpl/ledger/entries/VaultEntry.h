#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {

template <typename ViewT>
class VaultEntry : public SLEBase<ViewT, ltVAULT>
{
public:
    using Base = SLEBase<ViewT, ltVAULT>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit VaultEntry(
        AccountID const& owner,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::vault(owner, seq), view, j)
    {
    }

    explicit VaultEntry(
        UInt256 const& vaultID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::vault(vaultID), view, j)
    {
    }

    /**
     * Resolves the Vault's LEVersion, the single point every accounting touch
     * point should call to determine which recognition model (instant
     * interest recognition vs. cash-basis) the Vault uses. Vaults created
     * before featureLendingProtocolV1_1 activated never have sfLEVersion set,
     * which resolves here to VaultVersion::Legacy.
     *
     * @return The Vault's LEVersion, or VaultVersion::Legacy if the field is
     * absent.
     */
    [[nodiscard]] VaultVersion
    version() const;
};

using VaultEntryR = VaultEntry<ReadView>;
using VaultEntryW = VaultEntry<ApplyView>;

}  // namespace xrpl
