#include <xrpl/telemetry/TxAccountSpanNames.h>

#include <xrpl/protocol/SField.h>

#include <optional>
#include <string_view>
#include <unordered_map>

namespace xrpl::telemetry {

std::optional<std::string_view>
accountFieldAttributeKey(SField const& field)
{
    // Built on first call, not at static initialisation: the sf* objects are
    // globals in another translation unit, so their codes are only safe to
    // read once main() has started.
    static std::unordered_map<int, std::string_view> const kTable = {
        {sfAccount.getCode(), tx_account_span::attr::account},
        {sfDestination.getCode(), tx_account_span::attr::destination},
        {sfOwner.getCode(), tx_account_span::attr::owner},
        {sfIssuer.getCode(), tx_account_span::attr::issuer},
        {sfAuthorize.getCode(), tx_account_span::attr::authorize},
        {sfUnauthorize.getCode(), tx_account_span::attr::unauthorize},
        {sfRegularKey.getCode(), tx_account_span::attr::regularKey},
        {sfNFTokenMinter.getCode(), tx_account_span::attr::nftokenMinter},
        {sfHolder.getCode(), tx_account_span::attr::holder},
        {sfDelegate.getCode(), tx_account_span::attr::delegate},
        {sfSponsor.getCode(), tx_account_span::attr::sponsor},
        {sfSponsee.getCode(), tx_account_span::attr::sponsee},
        {sfCounterparty.getCode(), tx_account_span::attr::counterparty},
        {sfCounterpartySponsor.getCode(), tx_account_span::attr::counterpartySponsor},
        {sfSubject.getCode(), tx_account_span::attr::subject},
        {sfOtherChainSource.getCode(), tx_account_span::attr::otherChainSource},
        {sfOtherChainDestination.getCode(), tx_account_span::attr::otherChainDestination},
        {sfAttestationSignerAccount.getCode(), tx_account_span::attr::attestationSignerAccount},
        {sfAttestationRewardAccount.getCode(), tx_account_span::attr::attestationRewardAccount},
    };

    auto const it = kTable.find(field.getCode());
    if (it == kTable.end())
        return std::nullopt;
    return it->second;
}

}  // namespace xrpl::telemetry
