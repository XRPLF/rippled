#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class PermissionedDomainEntry : public SLEBase<ViewT, ltPERMISSIONED_DOMAIN>
{
public:
    using Base = SLEBase<ViewT, ltPERMISSIONED_DOMAIN>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit PermissionedDomainEntry(
        AccountID const& account,
        SeqProxy const& seq,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::permissionedDomain(account, seq), view, j)
    {
    }

    explicit PermissionedDomainEntry(
        uint256 const& domainID,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::permissionedDomain(domainID), view, j)
    {
    }
};

using RPermissionedDomainEntry = PermissionedDomainEntry<ReadView>;
using WPermissionedDomainEntry = PermissionedDomainEntry<ApplyView>;

}  // namespace xrpl
