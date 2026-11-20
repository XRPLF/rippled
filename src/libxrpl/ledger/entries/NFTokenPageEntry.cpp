#include <xrpl/ledger/entries/NFTokenPageEntry.h>

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>

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

template class NFTokenPageEntry<ReadView>;
template class NFTokenPageEntry<ApplyView>;

}  // namespace xrpl
