#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <limits>

namespace xrpl {

template <typename ViewT>
class CredentialEntry : public SLEBase<ViewT, ltCREDENTIAL>
{
public:
    using Base = SLEBase<ViewT, ltCREDENTIAL>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit CredentialEntry(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credType,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::credential(subject, issuer, credType), view, j)
    {
    }

    explicit CredentialEntry(
        UInt256 const& credentialID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::credential(credentialID), view, j)
    {
    }

    // Check if the credential's sfExpiration field has passed the given
    // ledger close time.
    [[nodiscard]] bool
    isExpired(NetClock::time_point const& closed) const
    {
        std::uint32_t const exp =
            Base::operator*()[~sfExpiration].value_or(std::numeric_limits<std::uint32_t>::max());
        std::uint32_t const now = closed.time_since_epoch().count();
        return now > exp;
    }

    // Remove this credential's SLE from the ledger: delete it from the
    // issuer's (and, unless it is the same account, the subject's) owner
    // directory, adjust owner counts, and erase the SLE.
    [[nodiscard]] TER
    removeFromLedger()
        requires Base::kIsWritable;

    // Check whether this credential belongs to src and is accepted.
    // Expiration is checked separately by isExpired().
    [[nodiscard]] TER
    isValid(AccountID const& src) const
    {
        if (Base::operator->()->getAccountID(sfSubject) != src)
        {
            JLOG(this->journal().trace()) << "Credential doesn't belong to the source account.";
            return tecBAD_CREDENTIALS;
        }

        if (!Base::operator->()->isFlag(lsfAccepted))
        {
            JLOG(this->journal().trace()) << "Credential isn't accepted.";
            return tecBAD_CREDENTIALS;
        }

        return tesSUCCESS;
    }
};

using CredentialEntryR = CredentialEntry<ReadView>;
using CredentialEntryW = CredentialEntry<ApplyView>;

}  // namespace xrpl
