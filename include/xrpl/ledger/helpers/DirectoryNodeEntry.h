#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class DirectoryNodeEntry : public SLEBase<ViewT, ltDIR_NODE>
{
public:
    using Base = SLEBase<ViewT, ltDIR_NODE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit DirectoryNodeEntry(
        AccountID const& id,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::ownerDir(id), view, j)
    {
    }

    /**
     * Resolve a specific page of the directory rooted at @p root.
     */
    explicit DirectoryNodeEntry(
        uint256 const& root,
        std::uint64_t index,
        Base::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::page(root, index), view, j)
    {
    }
};

using RDirectoryNodeEntry = DirectoryNodeEntry<ReadView>;
using WDirectoryNodeEntry = DirectoryNodeEntry<ApplyView>;

}  // namespace xrpl
