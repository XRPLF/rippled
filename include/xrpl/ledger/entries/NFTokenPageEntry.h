#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/entries/SLEBase.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>

#include <optional>

namespace xrpl {

template <typename ViewT>
class NFTokenPageEntry : public SLEBase<ViewT, ltNFTOKEN_PAGE>
{
public:
    using Base = SLEBase<ViewT, ltNFTOKEN_PAGE>;

    // Inherit base constructors: adopt an existing SLE, or resolve one from a
    // Keylet against the view.
    using Base::Base;

    explicit NFTokenPageEntry(
        Keylet const& page,
        UInt256 const& token,
        Base::ViewRefType view,
        beast::Journal j = beast::Journal{beast::Journal::getNullSink()})
        : Base(keylet::nftokenPage(page, token), view, j)
    {
    }

    /**
     * Returns the token with ID @p id if this page holds it.
     *
     * @param id the ID of the token to look for.
     * @return the token, or std::nullopt if this page does not hold it.
     */
    [[nodiscard]] std::optional<STObject>
    findToken(UInt256 const& id) const
    {
        for (auto const& t : (*this)->getFieldArray(sfNFTokens))
        {
            if (t[sfNFTokenID] == id)
                return t;
        }

        return std::nullopt;
    }

    /**
     * Adds @p nft to this page's NFTokens array, keeps the array sorted, and
     * updates the page.
     *
     * The caller must make sure that the token belongs on this page and that
     * the page has space for it.
     *
     * @param nft the token to add.
     */
    void
    insertToken(STObject&& nft)
        requires Base::kIsWritable;
};

using NFTokenPageEntryR = NFTokenPageEntry<ReadView>;
using NFTokenPageEntryW = NFTokenPageEntry<ApplyView>;

}  // namespace xrpl
