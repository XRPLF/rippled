#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

#include <cstdint>

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
        std::uint32_t seq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::vault(owner, seq), view, j)
    {
    }

    explicit VaultEntry(
        uint256 const& vaultID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::vault(vaultID), view, j)
    {
    }
};

using RVaultEntry = VaultEntry<ReadView>;
using WVaultEntry = VaultEntry<ApplyView>;

}  // namespace xrpl
