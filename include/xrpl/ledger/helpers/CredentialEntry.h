#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>

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
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::credential(subject, issuer, credType), view, j)
    {
    }

    explicit CredentialEntry(
        uint256 const& credentialID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::credential(credentialID), view, j)
    {
    }
};

using RCredentialEntry = CredentialEntry<ReadView>;
using WCredentialEntry = CredentialEntry<ApplyView>;

}  // namespace xrpl
