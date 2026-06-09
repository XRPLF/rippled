#include <xrpl/tx/invariants/VaultInvariantData.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep

namespace xrpl {

VaultInvariantData::Vault
VaultInvariantData::Vault::make(SLE const& from)
{
    XRPL_ASSERT(from.getType() == ltVAULT, "VaultInvariantData::Vault::make : from Vault object");

    Vault self;
    self.key = from.key();
    self.asset = from.at(sfAsset);
    self.pseudoId = from.getAccountID(sfAccount);
    self.owner = from.at(sfOwner);
    self.shareMPTID = from.getFieldH192(sfShareMPTID);
    self.assetsTotal = from.at(sfAssetsTotal);
    self.assetsAvailable = from.at(sfAssetsAvailable);
    self.assetsMaximum = from.at(sfAssetsMaximum);
    self.lossUnrealized = from.at(sfLossUnrealized);
    return self;
}

VaultInvariantData::Shares
VaultInvariantData::Shares::make(SLE const& from)
{
    XRPL_ASSERT(
        from.getType() == ltMPTOKEN_ISSUANCE,
        "VaultInvariantData::Shares::make : from MPTokenIssuance object");

    Shares self;
    self.share = MPTIssue(makeMptID(from.getFieldU32(sfSequence), from.getAccountID(sfIssuer)));
    self.sharesTotal = from.getFieldU64(sfOutstandingAmount);
    self.sharesMaximum = from[~sfMaximumAmount].value_or(kMaxMpTokenAmount);
    return self;
}

void
VaultInvariantData::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    XRPL_ASSERT(
        after != nullptr && (before != nullptr || !isDelete),
        "xrpl::VaultInvariantData::visitEntry : some object is available");

    if (before && before->getType() == ltVAULT)
        beforeVault_.push_back(Vault::make(*before));

    if (!isDelete && after)
    {
        switch (after->getType())
        {
            case ltVAULT:
                afterVault_.push_back(Vault::make(*after));
                break;
            case ltMPTOKEN_ISSUANCE:
                afterMPTs_.push_back(Shares::make(*after));
                break;
            default:;
        }
    }
}

std::optional<VaultInvariantData::Shares>
VaultInvariantData::findShares(uint192 const& mptID) const
{
    for (auto const& s : afterMPTs_)
    {
        if (s.share.getMptID() == mptID)
            return s;
    }
    return std::nullopt;
}

}  // namespace xrpl
