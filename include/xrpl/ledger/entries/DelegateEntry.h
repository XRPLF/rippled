#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>

#include <unordered_set>

namespace xrpl {

class STTx;

template <typename ViewT>
class DelegateEntry : public SLEBase<ViewT, ltDELEGATE>
{
public:
    using Base = SLEBase<ViewT, ltDELEGATE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit DelegateEntry(
        AccountID const& account,
        AccountID const& authorizedAccount,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::delegate(account, authorizedAccount), view, j)
    {
    }

    /**
     * Check if this delegate account has permission to execute the
     * transaction.
     * @param tx The transaction that the delegate account intends to
     * execute.
     * @return tesSUCCESS if the transaction is allowed,
     * terNO_DELEGATE_PERMISSION if not.
     */
    [[nodiscard]] NotTEC
    checkTxPermission(STTx const& tx) const;

    /**
     * Load the granular permissions granted to this delegate account for the
     * specified transaction type.
     * @param type Used to determine which granted granular permissions to
     * load, based on the transaction type.
     * @return the granted granular permissions tied to the transaction type.
     */
    [[nodiscard]] std::unordered_set<GranularPermissionType>
    granularPermission(TxType const& type) const;

    /**
     * Removes this Delegate object from the ledger: unlinks it from both
     * the delegating and (if present) the authorized account's owner
     * directories, restores the delegating account's owner count, and
     * erases the entry.
     * @param owner The account whose owner directory entry led here.
     * @return tesSUCCESS on success; tecINTERNAL if the entry or the
     * delegating account's root does not exist; tefBAD_LEDGER if removal
     * from an owner directory fails.
     */
    TER
    removeFromLedger(AccountID const& owner)
        requires Base::kIsWritable;
};

using DelegateEntryR = DelegateEntry<ReadView>;
using DelegateEntryW = DelegateEntry<ApplyView>;

}  // namespace xrpl
