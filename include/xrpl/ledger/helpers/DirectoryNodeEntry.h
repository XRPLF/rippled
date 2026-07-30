#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/helpers/SLEBase.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>

namespace xrpl {

template <typename ViewT>
class DirectoryNodeEntry : public SLEBase<ViewT>
{
public:
    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using SLEBase<ViewT>::SLEBase;

    explicit DirectoryNodeEntry(
        AccountID const& id,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::ownerDir(id), view, j)
    {
    }

    /**
     * Resolve a specific page of the directory rooted at @p root.
     */
    explicit DirectoryNodeEntry(
        uint256 const& root,
        std::uint64_t index,
        SLEBase<ViewT>::view_ref_type view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : SLEBase<ViewT>(keylet::page(root, index), view, j)
    {
    }
};

}  // namespace xrpl
