#include <xrpl/ledger/entries/CredentialEntry.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
TER
CredentialEntry<ViewT>::removeFromLedger()
    requires Base::kIsWritable
{
    if (!this->exists())
        return tecNO_ENTRY;

    auto delSLE = [this](AccountID const& account, SField const& node, bool isOwner) -> TER {
        auto const sleAccount = this->applyView().peek(keylet::account(account));
        if (!sleAccount)
        {
            // LCOV_EXCL_START
            JLOG(this->journal().fatal()) << "Internal error: can't retrieve Owner account.";
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        std::uint64_t const page = Base::operator->()->getFieldU64(node);
        if (!this->applyView().dirRemove(keylet::ownerDir(account), page, this->key(), false))
        {
            // LCOV_EXCL_START
            JLOG(this->journal().fatal()) << "Unable to delete Credential from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        if (isOwner)
        {
            decreaseOwnerCountForObject(
                this->applyView(), sleAccount, this->mutableRawSle(), 1, this->journal());
        }

        return tesSUCCESS;
    };

    auto const issuer = Base::operator->()->getAccountID(sfIssuer);
    auto const subject = Base::operator->()->getAccountID(sfSubject);
    bool const accepted = Base::operator->()->isFlag(lsfAccepted);

    auto err = delSLE(issuer, sfIssuerNode, !accepted || (subject == issuer));
    if (!isTesSuccess(err))
        return err;

    if (subject != issuer)
    {
        err = delSLE(subject, sfSubjectNode, accepted);
        if (!isTesSuccess(err))
            return err;
    }

    // Remove object from ledger
    this->erase();

    return tesSUCCESS;
}

template class CredentialEntry<ReadView>;
template class CredentialEntry<ApplyView>;

}  // namespace xrpl
