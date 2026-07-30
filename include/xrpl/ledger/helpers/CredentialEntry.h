#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl {

template <typename ViewT>
class CredentialEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit CredentialEntry(
        AccountID const& subject,
        AccountID const& issuer,
        Slice const& credType,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::credential(subject, issuer, credType), view, j)
    {
    }

    explicit CredentialEntry(
        uint256 const& credentialID,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::credential(credentialID), view, j)
    {
    }
};

}  // namespace xrpl
