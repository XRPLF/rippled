#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {

template <typename ViewT>
class RepoEntry : public SLEBase<ViewT, ltREPO>
{
public:
    using Base = SLEBase<ViewT, ltREPO>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit RepoEntry(
        AccountID const& seller,
        SeqProxy const& seq,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::repo(seller, seq), view, j)
    {
    }

    explicit RepoEntry(
        UInt256 const& repoID,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::repo(repoID), view, j)
    {
    }
};

using RepoEntryR = RepoEntry<ReadView>;
using RepoEntryW = RepoEntry<ApplyView>;

}  // namespace xrpl
