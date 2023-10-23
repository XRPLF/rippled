//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/protocol/TER.h>
#include <boost/range/adaptor/transformed.hpp>
#include <type_traits>

namespace ripple {

std::unordered_map<
    TERUnderlyingType,
    std::pair<char const* const, char const* const>> const&
transResults()
{
    // clang-format off

    // Macros are generally ugly, but they can help make code readable to
    // humans without affecting the compiler.
#define MAKE_ERROR(code, desc) { code, { #code, desc } }

    static
    std::unordered_map<
            TERUnderlyingType,
            std::pair<char const* const, char const* const>> const results
    {
        MAKE_ERROR(tecAMM_BALANCE,                   "AMM has invalid balance."),
        MAKE_ERROR(tecAMM_INVALID_TOKENS,            "AMM invalid LP tokens."),
        MAKE_ERROR(tecAMM_FAILED,                    "AMM transaction failed."),
        MAKE_ERROR(tecAMM_EMPTY,                     "AMM is in empty state."),
        MAKE_ERROR(tecAMM_NOT_EMPTY,                 "AMM is not in empty state."),
        MAKE_ERROR(tecAMM_ACCOUNT,                   "This operation is not allowed on an AMM Account."),
        MAKE_ERROR(tecCLAIM,                         "Fee claimed. Sequence used. No action."),
        MAKE_ERROR(tecDIR_FULL,                      "Can not add entry to full directory."),
        MAKE_ERROR(tecFAILED_PROCESSING,             "Failed to correctly process transaction."),
        MAKE_ERROR(tecINSUF_RESERVE_LINE,            "Insufficient reserve to add trust line."),
        MAKE_ERROR(tecINSUF_RESERVE_OFFER,           "Insufficient reserve to create offer."),
        MAKE_ERROR(tecNO_DST,                        "Destination does not exist. Send XRP to create it."),
        MAKE_ERROR(tecNO_DST_INSUF_XRP,              "Destination does not exist. Too little XRP sent to create it."),
        MAKE_ERROR(tecNO_LINE_INSUF_RESERVE,         "No such line. Too little reserve to create it."),
        MAKE_ERROR(tecNO_LINE_REDUNDANT,             "Can't set non-existent line to default."),
        MAKE_ERROR(tecPATH_DRY,                      "Path could not send partial amount."),
        MAKE_ERROR(tecPATH_PARTIAL,                  "Path could not send full amount."),
        MAKE_ERROR(tecNO_ALTERNATIVE_KEY,            "The operation would remove the ability to sign transactions with the account."),
        MAKE_ERROR(tecNO_REGULAR_KEY,                "Regular key is not set."),
        MAKE_ERROR(tecOVERSIZE,                      "Object exceeded serialization limits."),
        MAKE_ERROR(tecUNFUNDED,                      "Not enough XRP to satisfy the reserve requirement."),
        MAKE_ERROR(tecUNFUNDED_ADD,                  "DEPRECATED."),
        MAKE_ERROR(tecUNFUNDED_AMM,                  "Insufficient balance to fund AMM."),
        MAKE_ERROR(tecUNFUNDED_OFFER,                "Insufficient balance to fund created offer."),
        MAKE_ERROR(tecUNFUNDED_PAYMENT,              "Insufficient XRP balance to send."),
        MAKE_ERROR(tecOWNERS,                        "Non-zero owner count."),
        MAKE_ERROR(tecNO_ISSUER,                     "Issuer account does not exist."),
        MAKE_ERROR(tecNO_AUTH,                       "Not authorized to hold asset."),
        MAKE_ERROR(tecNO_LINE,                       "No such line."),
        MAKE_ERROR(tecINSUFF_FEE,                    "Insufficient balance to pay fee."),
        MAKE_ERROR(tecFROZEN,                        "Asset is frozen."),
        MAKE_ERROR(tecNO_TARGET,                     "Target account does not exist."),
        MAKE_ERROR(tecNO_PERMISSION,                 "No permission to perform requested operation."),
        MAKE_ERROR(tecNO_ENTRY,                      "No matching entry found."),
        MAKE_ERROR(tecINSUFFICIENT_RESERVE,          "Insufficient reserve to complete requested operation."),
        MAKE_ERROR(tecNEED_MASTER_KEY,               "The operation requires the use of the Master Key."),
        MAKE_ERROR(tecDST_TAG_NEEDED,                "A destination tag is required."),
        MAKE_ERROR(tecINTERNAL,                      "An internal error has occurred during processing."),
        MAKE_ERROR(tecCRYPTOCONDITION_ERROR,         "Malformed, invalid, or mismatched conditional or fulfillment."),
        MAKE_ERROR(tecINVARIANT_FAILED,              "One or more invariants for the transaction were not satisfied."),
        MAKE_ERROR(tecEXPIRED,                       "Expiration time is passed."),
        MAKE_ERROR(tecDUPLICATE,                     "Ledger object already exists."),
        MAKE_ERROR(tecKILLED,                        "No funds transferred and no offer created."),
        MAKE_ERROR(tecHAS_OBLIGATIONS,               "The account cannot be deleted since it has obligations."),
        MAKE_ERROR(tecTOO_SOON,                      "It is too early to attempt the requested operation. Please wait."),
        MAKE_ERROR(tecMAX_SEQUENCE_REACHED,          "The maximum sequence number was reached."),
        MAKE_ERROR(tecNO_SUITABLE_NFTOKEN_PAGE,      "A suitable NFToken page could not be located."),
        MAKE_ERROR(tecNFTOKEN_BUY_SELL_MISMATCH,     "The 'Buy' and 'Sell' NFToken offers are mismatched."),
        MAKE_ERROR(tecNFTOKEN_OFFER_TYPE_MISMATCH,   "The type of NFToken offer is incorrect."),
        MAKE_ERROR(tecCANT_ACCEPT_OWN_NFTOKEN_OFFER, "An NFToken offer cannot be claimed by its owner."),
        MAKE_ERROR(tecINSUFFICIENT_FUNDS,            "Not enough funds available to complete requested transaction."),
        MAKE_ERROR(tecOBJECT_NOT_FOUND,              "A requested object could not be located."),
        MAKE_ERROR(tecINSUFFICIENT_PAYMENT,          "The payment is not sufficient."),
        MAKE_ERROR(tecINCOMPLETE,                    "Some work was completed, but more submissions required to finish."),
        MAKE_ERROR(tecXCHAIN_BAD_TRANSFER_ISSUE,     "Bad xchain transfer issue."),
        MAKE_ERROR(tecXCHAIN_NO_CLAIM_ID,            "No such xchain claim id."),
        MAKE_ERROR(tecXCHAIN_BAD_CLAIM_ID,           "Bad xchain claim id."),
        MAKE_ERROR(tecXCHAIN_CLAIM_NO_QUORUM,        "Quorum was not reached on the xchain claim."),
        MAKE_ERROR(tecXCHAIN_PROOF_UNKNOWN_KEY,      "Unknown key for the xchain proof."),
        MAKE_ERROR(tecXCHAIN_CREATE_ACCOUNT_NONXRP_ISSUE, "Only XRP may be used for xchain create account."),
        MAKE_ERROR(tecXCHAIN_WRONG_CHAIN,            "XChain Transaction was submitted to the wrong chain."),
        MAKE_ERROR(tecXCHAIN_REWARD_MISMATCH,        "The reward amount must match the reward specified in the xchain bridge."),
        MAKE_ERROR(tecXCHAIN_NO_SIGNERS_LIST,        "The account did not have a signers list."),
        MAKE_ERROR(tecXCHAIN_SENDING_ACCOUNT_MISMATCH,"The sending account did not match the expected sending account."),
        MAKE_ERROR(tecXCHAIN_INSUFF_CREATE_AMOUNT,   "Insufficient amount to create an account."),
        MAKE_ERROR(tecXCHAIN_ACCOUNT_CREATE_PAST,    "The account create count has already passed."),
        MAKE_ERROR(tecXCHAIN_ACCOUNT_CREATE_TOO_MANY, "There are too many pending account create transactions to submit a new one."),
        MAKE_ERROR(tecXCHAIN_PAYMENT_FAILED,         "Failed to transfer funds in a xchain transaction."),
        MAKE_ERROR(tecXCHAIN_SELF_COMMIT,            "Account cannot commit funds to itself."),
        MAKE_ERROR(tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR, "Bad public key account pair in an xchain transaction."),
        MAKE_ERROR(tecXCHAIN_CREATE_ACCOUNT_DISABLED, "This bridge does not support account creation."),
        MAKE_ERROR(tecEMPTY_DID,                     "The DID object did not have a URI or DIDDocument field."),

        MAKE_ERROR(tefALREADY,                     "The exact transaction was already in this ledger."),
        MAKE_ERROR(tefBAD_ADD_AUTH,                "Not authorized to add account."),
        MAKE_ERROR(tefBAD_AUTH,                    "Transaction's public key is not authorized."),
        MAKE_ERROR(tefBAD_LEDGER,                  "Ledger in unexpected state."),
        MAKE_ERROR(tefBAD_QUORUM,                  "Signatures provided do not meet the quorum."),
        MAKE_ERROR(tefBAD_SIGNATURE,               "A signature is provided for a non-signer."),
        MAKE_ERROR(tefCREATED,                     "Can't add an already created account."),
        MAKE_ERROR(tefEXCEPTION,                   "Unexpected program state."),
        MAKE_ERROR(tefFAILURE,                     "Failed to apply."),
        MAKE_ERROR(tefINTERNAL,                    "Internal error."),
        MAKE_ERROR(tefMASTER_DISABLED,             "Master key is disabled."),
        MAKE_ERROR(tefMAX_LEDGER,                  "Ledger sequence too high."),
        MAKE_ERROR(tefNO_AUTH_REQUIRED,            "Auth is not required."),
        MAKE_ERROR(tefNOT_MULTI_SIGNING,           "Account has no appropriate list of multi-signers."),
        MAKE_ERROR(tefPAST_SEQ,                    "This sequence number has already passed."),
        MAKE_ERROR(tefWRONG_PRIOR,                 "This previous transaction does not match."),
        MAKE_ERROR(tefBAD_AUTH_MASTER,             "Auth for unclaimed account needs correct master key."),
        MAKE_ERROR(tefINVARIANT_FAILED,            "Fee claim violated invariants for the transaction."),
        MAKE_ERROR(tefTOO_BIG,                     "Transaction affects too many items."),
        MAKE_ERROR(tefNO_TICKET,                   "Ticket is not in ledger."),
        MAKE_ERROR(tefNFTOKEN_IS_NOT_TRANSFERABLE, "The specified NFToken is not transferable."),

        MAKE_ERROR(telLOCAL_ERROR,            "Local failure."),
        MAKE_ERROR(telBAD_DOMAIN,             "Domain too long."),
        MAKE_ERROR(telBAD_PATH_COUNT,         "Malformed: Too many paths."),
        MAKE_ERROR(telBAD_PUBLIC_KEY,         "Public key is not valid."),
        MAKE_ERROR(telFAILED_PROCESSING,      "Failed to correctly process transaction."),
        MAKE_ERROR(telINSUF_FEE_P,            "Fee insufficient."),
        MAKE_ERROR(telNO_DST_PARTIAL,         "Partial payment to create account not allowed."),
        MAKE_ERROR(telCAN_NOT_QUEUE,          "Can not queue at this time."),
        MAKE_ERROR(telCAN_NOT_QUEUE_BALANCE,  "Can not queue at this time: insufficient balance to pay all queued fees."),
        MAKE_ERROR(telCAN_NOT_QUEUE_BLOCKS,   "Can not queue at this time: would block later queued transaction(s)."),
        MAKE_ERROR(telCAN_NOT_QUEUE_BLOCKED,  "Can not queue at this time: blocking transaction in queue."),
        MAKE_ERROR(telCAN_NOT_QUEUE_FEE,      "Can not queue at this time: fee insufficient to replace queued transaction."),
        MAKE_ERROR(telCAN_NOT_QUEUE_FULL,     "Can not queue at this time: queue is full."),
        MAKE_ERROR(telWRONG_NETWORK,          "Transaction specifies a Network ID that differs from that of the local node."),
        MAKE_ERROR(telREQUIRES_NETWORK_ID,    "Transactions submitted to this node/network must include a correct NetworkID field."),
        MAKE_ERROR(telNETWORK_ID_MAKES_TX_NON_CANONICAL, "Transactions submitted to this node/network must NOT include a NetworkID field."),

        MAKE_ERROR(temMALFORMED,                 "Malformed transaction."),
        MAKE_ERROR(temBAD_AMM_TOKENS,            "Malformed: Invalid LPTokens."),
        MAKE_ERROR(temBAD_AMOUNT,                "Can only send positive amounts."),
        MAKE_ERROR(temBAD_CURRENCY,              "Malformed: Bad currency."),
        MAKE_ERROR(temBAD_EXPIRATION,            "Malformed: Bad expiration."),
        MAKE_ERROR(temBAD_FEE,                   "Invalid fee, negative or not XRP."),
        MAKE_ERROR(temBAD_ISSUER,                "Malformed: Bad issuer."),
        MAKE_ERROR(temBAD_LIMIT,                 "Limits must be non-negative."),
        MAKE_ERROR(temBAD_OFFER,                 "Malformed: Bad offer."),
        MAKE_ERROR(temBAD_PATH,                  "Malformed: Bad path."),
        MAKE_ERROR(temBAD_PATH_LOOP,             "Malformed: Loop in path."),
        MAKE_ERROR(temBAD_QUORUM,                "Malformed: Quorum is unreachable."),
        MAKE_ERROR(temBAD_REGKEY,                "Malformed: Regular key cannot be same as master key."),
        MAKE_ERROR(temBAD_SEND_XRP_LIMIT,        "Malformed: Limit quality is not allowed for XRP to XRP."),
        MAKE_ERROR(temBAD_SEND_XRP_MAX,          "Malformed: Send max is not allowed for XRP to XRP."),
        MAKE_ERROR(temBAD_SEND_XRP_NO_DIRECT,    "Malformed: No Ripple direct is not allowed for XRP to XRP."),
        MAKE_ERROR(temBAD_SEND_XRP_PARTIAL,      "Malformed: Partial payment is not allowed for XRP to XRP."),
        MAKE_ERROR(temBAD_SEND_XRP_PATHS,        "Malformed: Paths are not allowed for XRP to XRP."),
        MAKE_ERROR(temBAD_SEQUENCE,              "Malformed: Sequence is not in the past."),
        MAKE_ERROR(temBAD_SIGNATURE,             "Malformed: Bad signature."),
        MAKE_ERROR(temBAD_SIGNER,                "Malformed: No signer may duplicate account or other signers."),
        MAKE_ERROR(temBAD_SRC_ACCOUNT,           "Malformed: Bad source account."),
        MAKE_ERROR(temBAD_TRANSFER_RATE,         "Malformed: Transfer rate must be >= 1.0 and <= 2.0"),
        MAKE_ERROR(temBAD_WEIGHT,                "Malformed: Weight must be a positive value."),
        MAKE_ERROR(temDST_IS_SRC,                "Destination may not be source."),
        MAKE_ERROR(temDST_NEEDED,                "Destination not specified."),
        MAKE_ERROR(temEMPTY_DID,                 "Malformed: No DID data provided."),
        MAKE_ERROR(temINVALID,                   "The transaction is ill-formed."),
        MAKE_ERROR(temINVALID_FLAG,              "The transaction has an invalid flag."),
        MAKE_ERROR(temREDUNDANT,                 "The transaction is redundant."),
        MAKE_ERROR(temRIPPLE_EMPTY,              "PathSet with no paths."),
        MAKE_ERROR(temUNCERTAIN,                 "In process of determining result. Never returned."),
        MAKE_ERROR(temUNKNOWN,                   "The transaction requires logic that is not implemented yet."),
        MAKE_ERROR(temDISABLED,                  "The transaction requires logic that is currently disabled."),
        MAKE_ERROR(temBAD_TICK_SIZE,             "Malformed: Tick size out of range."),
        MAKE_ERROR(temINVALID_ACCOUNT_ID,        "Malformed: A field contains an invalid account ID."),
        MAKE_ERROR(temCANNOT_PREAUTH_SELF,       "Malformed: An account may not preauthorize itself."),
        MAKE_ERROR(temINVALID_COUNT,             "Malformed: Count field outside valid range."),
        MAKE_ERROR(temSEQ_AND_TICKET,            "Transaction contains a TicketSequence and a non-zero Sequence."),
        MAKE_ERROR(temBAD_NFTOKEN_TRANSFER_FEE,  "Malformed: The NFToken transfer fee must be between 1 and 5000, inclusive."),
        MAKE_ERROR(temXCHAIN_EQUAL_DOOR_ACCOUNTS,       "Malformed: Bridge must have unique door accounts."),
        MAKE_ERROR(temXCHAIN_BAD_PROOF,          "Malformed: Bad cross-chain claim proof."),
        MAKE_ERROR(temXCHAIN_BRIDGE_BAD_ISSUES,      "Malformed: Bad bridge issues."),
        MAKE_ERROR(temXCHAIN_BRIDGE_NONDOOR_OWNER,   "Malformed: Bridge owner must be one of the door accounts."),
        MAKE_ERROR(temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT,   "Malformed: Bad min account create amount."),
        MAKE_ERROR(temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT, "Malformed: Bad reward amount."),

        MAKE_ERROR(terRETRY,                  "Retry transaction."),
        MAKE_ERROR(terFUNDS_SPENT,            "DEPRECATED."),
        MAKE_ERROR(terINSUF_FEE_B,            "Account balance can't pay fee."),
        MAKE_ERROR(terLAST,                   "DEPRECATED."),
        MAKE_ERROR(terNO_RIPPLE,              "Path does not permit rippling."),
        MAKE_ERROR(terNO_ACCOUNT,             "The source account does not exist."),
        MAKE_ERROR(terNO_AUTH,                "Not authorized to hold IOUs."),
        MAKE_ERROR(terNO_LINE,                "No such line."),
        MAKE_ERROR(terPRE_SEQ,                "Missing/inapplicable prior transaction."),
        MAKE_ERROR(terOWNERS,                 "Non-zero owner count."),
        MAKE_ERROR(terQUEUED,                 "Held until escalated fee drops."),
        MAKE_ERROR(terPRE_TICKET,             "Ticket is not yet in ledger."),
        MAKE_ERROR(terNO_AMM,                 "AMM doesn't exist for the asset pair."),
        MAKE_ERROR(terSUBMITTED,              "Transaction has been submitted."),

        MAKE_ERROR(tesSUCCESS,                "The transaction was applied. Only final in a validated ledger."),
    };
    // clang-format on

#undef MAKE_ERROR

    return results;
}

bool
transResultInfo(TER code, std::string& token, std::string& text)
{
    auto& results = transResults();

    auto const r = results.find(TERtoInt(code));

    if (r == results.end())
        return false;

    token = r->second.first;
    text = r->second.second;
    return true;
}

std::string
transToken(TER code)
{
    std::string token;
    std::string text;

    return transResultInfo(code, token, text) ? token : "-";
}

std::string
transHuman(TER code)
{
    std::string token;
    std::string text;

    return transResultInfo(code, token, text) ? text : "-";
}

std::optional<TER>
transCode(std::string const& token)
{
    static auto const results = [] {
        auto& byTer = transResults();
        auto range = boost::make_iterator_range(byTer.begin(), byTer.end());
        auto tRange = boost::adaptors::transform(range, [](auto const& r) {
            return std::make_pair(r.second.first, r.first);
        });
        std::unordered_map<std::string, TERUnderlyingType> const byToken(
            tRange.begin(), tRange.end());
        return byToken;
    }();

    auto const r = results.find(token);

    if (r == results.end())
        return std::nullopt;

    return TER::fromInt(r->second);
}

}  // namespace ripple
