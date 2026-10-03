#include <xrpl/ledger/entries/NFTokenPageEntry.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace xrpl {

template <typename ViewT>
void
NFTokenPageEntry<ViewT>::insertToken(STObject&& nft)
    requires Base::kIsWritable
{
    {
        auto arr = (*this)->getFieldArray(sfNFTokens);
        arr.pushBack(std::move(nft));

        arr.sort([](STObject const& o1, STObject const& o2) {
            return nft::compareTokens(o1.getFieldH256(sfNFTokenID), o2.getFieldH256(sfNFTokenID));
        });

        (*this)->setFieldArray(sfNFTokens, arr);
    }

    this->update();
}

template <typename ViewT>
TER
NFTokenPageEntry<ViewT>::changeTokenURI(UInt256 const& id, std::optional<Slice> const& uri)
    requires Base::kIsWritable
{
    // Locate the NFT in the page
    STArray& arr = (*this)->peekFieldArray(sfNFTokens);

    auto const nftIter =
        std::ranges::find_if(arr, [&id](STObject const& obj) { return (obj[sfNFTokenID] == id); });

    if (nftIter == arr.end())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (uri)
    {
        nftIter->setFieldVL(sfURI, *uri);
    }
    else if (nftIter->isFieldPresent(sfURI))
    {
        nftIter->makeFieldAbsent(sfURI);
    }

    this->update();
    return tesSUCCESS;
}

template class NFTokenPageEntry<ReadView>;
template class NFTokenPageEntry<ApplyView>;

}  // namespace xrpl
