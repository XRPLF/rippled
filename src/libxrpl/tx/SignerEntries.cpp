#include <xrpl/tx/SignerEntries.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

namespace xrpl {

std::expected<std::vector<SignerEntries::SignerEntry>, NotTEC>
SignerEntries::deserialize(STObject const& obj, beast::Journal journal, std::string_view annotation)
{
    if (!obj.isFieldPresent(sfSignerEntries))
    {
        JLOG(journal.trace()) << "Malformed " << annotation << ": Need signer entry array.";
        return std::unexpected(temMALFORMED);
    }

    std::vector<SignerEntry> accountVec;
    accountVec.reserve(STTx::kMaxMultiSigners);

    STArray const& sEntries(obj.getFieldArray(sfSignerEntries));
    for (STObject const& sEntry : sEntries)
    {
        // Validate the SignerEntry.
        if (sEntry.getFName() != sfSignerEntry)
        {
            JLOG(journal.trace()) << "Malformed " << annotation << ": Expected SignerEntry.";
            return std::unexpected(temMALFORMED);
        }

        // Extract SignerEntry fields.
        AccountID const account = sEntry.getAccountID(sfAccount);
        std::uint16_t const weight = sEntry.getFieldU16(sfSignerWeight);
        std::optional<uint256> const tag = sEntry.at(~sfWalletLocator);

        accountVec.emplace_back(account, weight, tag);
    }
    return accountVec;
}

}  // namespace xrpl
