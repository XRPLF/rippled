#include <xrpld/app/tx/detail/SignerEntries.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>

#include <cstdint>
#include <optional>

namespace ripple {

Expected<std::vector<SignerEntries::SignerEntry>, NotTEC>
SignerEntries::deserialize(
    STObject const& obj,
    beast::Journal journal,
    std::string_view annotation)
{
    std::pair<std::vector<SignerEntry>, NotTEC> s;

    if (!obj.isFieldPresent(sfSignerEntries))
    {
        JLOG(journal.trace())
            << "Malformed " << annotation << ": Need signer entry array.";
        return Unexpected(temMALFORMED);
    }

    std::vector<SignerEntry> accountVec;
    accountVec.reserve(STTx::maxMultiSigners());

    STArray const& sEntries(obj.getFieldArray(sfSignerEntries));
    for (STObject const& sEntry : sEntries)
    {
        // Validate the SignerEntry.
        if (sEntry.getFName() != sfSignerEntry)
        {
            JLOG(journal.trace())
                << "Malformed " << annotation << ": Expected SignerEntry.";
            return Unexpected(temMALFORMED);
        }

        // Extract SignerEntry fields.
        AccountID const account = sEntry.getAccountID(sfAccount);
        std::uint16_t const weight = sEntry.getFieldU16(sfSignerWeight);
        std::optional<uint256> const tag = sEntry.at(~sfWalletLocator);

        accountVec.emplace_back(account, weight, tag);
    }
    return accountVec;
}

}  // namespace ripple
