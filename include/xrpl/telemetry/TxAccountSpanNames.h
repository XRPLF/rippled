#pragma once

/**
 * Span attribute keys for the account-typed fields of a transaction.
 *
 * A transaction names one or more accounts: the sender in `Account`, and
 * depending on the type a `Destination`, `Owner`, `Issuer`, `Holder` and so
 * on. The tx.process span emits every one it finds as its own attribute, so
 * an account can be searched for in traces whatever role it played. An
 * account address is a public ledger identifier, so each is emitted as the
 * raw r-address and never hashed.
 *
 * One key per protocol field: `tx_` followed by the field's JSON name in
 * lower snake case. The full table is the initializer in
 * src/libxrpl/telemetry/TxAccountSpanNames.cpp; the common ones are
 *
 *   STTx field         span attribute key
 *   -----------------  ------------------
 *   Account            tx_account
 *   Destination        tx_destination
 *   Owner              tx_owner
 *   Issuer             tx_issuer
 *   RegularKey         tx_regular_key
 *   NFTokenMinter      tx_nftoken_minter
 *
 * Only fields that some transaction format carries at top level have a key.
 * Account-typed fields that appear only in ledger entries or inner objects
 * (LowSponsor, LockingChainDoor, ...) map to nullopt. A library test walks
 * TxFormats and fails when a format gains an account field with no key.
 *
 * Why this header lives in libxrpl rather than beside TxSpanNames.h: the
 * mapping is keyed by protocol fields and its completeness is checked from
 * TxFormats, which a library test can reach and a daemon header cannot.
 *
 * Data flow:
 *
 *   NetworkOPs::processTransaction            (src/xrpld)
 *       │  for each top-level field with getSType() == STI_ACCOUNT
 *       ▼
 *   accountFieldAttributeKey(field.getFName())   (this header)
 *       │  the key, or nullopt for a field with no key
 *       ▼
 *   span->setAttribute(key, field.getText())
 *
 * @code
 *     // Primary use: emit every account the transaction names. An empty
 *     // account field is skipped so it is not rendered as the zero address.
 *     for (auto const& field : stx)
 *     {
 *         if (field.getSType() != STI_ACCOUNT || field.isDefault())
 *             continue;
 *         if (auto const key = telemetry::accountFieldAttributeKey(field.getFName()))
 *             span.setAttribute(*key, toBase58(stx.getAccountID(field.getFName())));
 *     }
 * @endcode
 *
 * @code
 *     // Edge case: a field that is not a top-level transaction account has
 *     // no key, so a caller must test the optional before using it.
 *     accountFieldAttributeKey(sfFee);         // == std::nullopt
 *     accountFieldAttributeKey(sfLowSponsor);  // == std::nullopt
 * @endcode
 *
 * @note Only top-level fields are covered. Accounts nested in Signers, in a
 * Batch's inner transactions, or as the issuer inside an Amount are not
 * emitted.
 * @note accountFieldAttributeKey() is thread-safe. Its table is built once
 * on first use and is read-only afterwards.
 */

#include <xrpl/telemetry/SpanNames.h>

#include <optional>
#include <string_view>

namespace xrpl {
class SField;
}  // namespace xrpl

namespace xrpl::telemetry {

namespace tx_account_span::attr {
/**
 * "tx_account" — the sending account (`Account`). Every transaction has one.
 */
inline constexpr auto account = makeStr("tx_account");
/**
 * "tx_destination" — the receiving account (`Destination`).
 */
inline constexpr auto destination = makeStr("tx_destination");
/**
 * "tx_owner" — the owner of the object acted on (`Owner`).
 */
inline constexpr auto owner = makeStr("tx_owner");
/**
 * "tx_issuer" — the issuer named by the transaction (`Issuer`).
 */
inline constexpr auto issuer = makeStr("tx_issuer");
/**
 * "tx_authorize" — the account being authorised (`Authorize`).
 */
inline constexpr auto authorize = makeStr("tx_authorize");
/**
 * "tx_unauthorize" — the account whose authorisation is removed (`Unauthorize`).
 */
inline constexpr auto unauthorize = makeStr("tx_unauthorize");
/**
 * "tx_regular_key" — the regular key being set (`RegularKey`).
 */
inline constexpr auto regularKey = makeStr("tx_regular_key");
/**
 * "tx_nftoken_minter" — the authorised NFToken minter (`NFTokenMinter`).
 */
inline constexpr auto nftokenMinter = makeStr("tx_nftoken_minter");
/**
 * "tx_holder" — the token holder acted on (`Holder`).
 */
inline constexpr auto holder = makeStr("tx_holder");
/**
 * "tx_delegate" — the delegate signing on the sender's behalf (`Delegate`).
 */
inline constexpr auto delegate = makeStr("tx_delegate");
/**
 * "tx_sponsor" — the account paying the fee or reserve (`Sponsor`).
 */
inline constexpr auto sponsor = makeStr("tx_sponsor");
/**
 * "tx_sponsee" — the account being sponsored (`Sponsee`).
 */
inline constexpr auto sponsee = makeStr("tx_sponsee");
/**
 * "tx_counterparty" — the other party to a loan (`Counterparty`).
 */
inline constexpr auto counterparty = makeStr("tx_counterparty");
/**
 * "tx_counterparty_sponsor" — the counterparty's sponsor (`CounterpartySponsor`).
 */
inline constexpr auto counterpartySponsor = makeStr("tx_counterparty_sponsor");
/**
 * "tx_subject" — the subject of a credential (`Subject`).
 */
inline constexpr auto subject = makeStr("tx_subject");
/**
 * "tx_other_chain_source" — the source account on the other chain (`OtherChainSource`).
 */
inline constexpr auto otherChainSource = makeStr("tx_other_chain_source");
/**
 * "tx_other_chain_destination" — destination on the other chain (`OtherChainDestination`).
 */
inline constexpr auto otherChainDestination = makeStr("tx_other_chain_destination");
/**
 * "tx_attestation_signer_account" — the attestation signer (`AttestationSignerAccount`).
 */
inline constexpr auto attestationSignerAccount = makeStr("tx_attestation_signer_account");
/**
 * "tx_attestation_reward_account" — attestation reward account (`AttestationRewardAccount`).
 */
inline constexpr auto attestationRewardAccount = makeStr("tx_attestation_reward_account");
}  // namespace tx_account_span::attr

/**
 * Look up the span attribute key for an account-typed transaction field.
 *
 * @param field The protocol field, as returned by STBase::getFName().
 * @return The `tx_*` key for a top-level transaction account field, or
 * nullopt when the field is not account-typed or is carried only by ledger
 * entries and inner objects.
 */
[[nodiscard]] std::optional<std::string_view>
accountFieldAttributeKey(SField const& field);

}  // namespace xrpl::telemetry
