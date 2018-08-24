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
#include <unordered_map>
#include <type_traits>

namespace ripple {

namespace detail {

static
std::unordered_map<
    TERUnderlyingType,
    std::pair<char const* const, char const* const>> const&
transResults()
{
    static
    std::unordered_map<
        TERUnderlyingType,
        std::pair<char const* const, char const* const>> const
    results
    {
        { tecCLAIM,                  { "tecCLAIM",                 "Fee claimed. Sequence used. No action."                                        } },
        { tecDIR_FULL,               { "tecDIR_FULL",              "Can not add entry to full directory."                                          } },
        { tecFAILED_PROCESSING,      { "tecFAILED_PROCESSING",     "Failed to correctly process transaction."                                      } },
        { tecINSUF_RESERVE_LINE,     { "tecINSUF_RESERVE_LINE",    "Insufficient reserve to add trust line."                                       } },
        { tecINSUF_RESERVE_OFFER,    { "tecINSUF_RESERVE_OFFER",   "Insufficient reserve to create offer."                                         } },
        { tecNO_DST,                 { "tecNO_DST",                "Destination does not exist. Send XRP to create it."                            } },
        { tecNO_DST_INSUF_XRP,       { "tecNO_DST_INSUF_XRP",      "Destination does not exist. Too little XRP sent to create it."                 } },
        { tecNO_LINE_INSUF_RESERVE,  { "tecNO_LINE_INSUF_RESERVE", "No such line. Too little reserve to create it."                                } },
        { tecNO_LINE_REDUNDANT,      { "tecNO_LINE_REDUNDANT",     "Can't set non-existent line to default."                                       } },
        { tecPATH_DRY,               { "tecPATH_DRY",              "Path could not send partial amount."                                           } },
        { tecPATH_PARTIAL,           { "tecPATH_PARTIAL",          "Path could not send full amount."                                              } },
        { tecNO_ALTERNATIVE_KEY,     { "tecNO_ALTERNATIVE_KEY",    "The operation would remove the ability to sign transactions with the account." } },
        { tecNO_REGULAR_KEY,         { "tecNO_REGULAR_KEY",        "Regular key is not set."                                                       } },
        { tecOVERSIZE,               { "tecOVERSIZE",              "Object exceeded serialization limits."                                         } },
        { tecUNFUNDED,               { "tecUNFUNDED",              "One of _ADD, _OFFER, or _SEND. Deprecated."                                    } },
        { tecUNFUNDED_ADD,           { "tecUNFUNDED_ADD",          "Insufficient XRP balance for WalletAdd."                                       } },
        { tecUNFUNDED_OFFER,         { "tecUNFUNDED_OFFER",        "Insufficient balance to fund created offer."                                   } },
        { tecUNFUNDED_PAYMENT,       { "tecUNFUNDED_PAYMENT",      "Insufficient XRP balance to send."                                             } },
        { tecOWNERS,                 { "tecOWNERS",                "Non-zero owner count."                                                         } },
        { tecNO_ISSUER,              { "tecNO_ISSUER",             "Issuer account does not exist."                                                } },
        { tecNO_AUTH,                { "tecNO_AUTH",               "Not authorized to hold asset."                                                 } },
        { tecNO_LINE,                { "tecNO_LINE",               "No such line."                                                                 } },
        { tecINSUFF_FEE,             { "tecINSUFF_FEE",            "Insufficient balance to pay fee."                                              } },
        { tecFROZEN,                 { "tecFROZEN",                "Asset is frozen."                                                              } },
        { tecNO_TARGET,              { "tecNO_TARGET",             "Target account does not exist."                                                } },
        { tecNO_PERMISSION,          { "tecNO_PERMISSION",         "No permission to perform requested operation."                                 } },
        { tecNO_ENTRY,               { "tecNO_ENTRY",              "No matching entry found."                                                      } },
        { tecINSUFFICIENT_RESERVE,   { "tecINSUFFICIENT_RESERVE",  "Insufficient reserve to complete requested operation."                         } },
        { tecNEED_MASTER_KEY,        { "tecNEED_MASTER_KEY",       "The operation requires the use of the Master Key."                             } },
        { tecDST_TAG_NEEDED,         { "tecDST_TAG_NEEDED",        "A destination tag is required."                                                } },
        { tecINTERNAL,               { "tecINTERNAL",              "An internal error has occurred during processing."                             } },
        { tecCRYPTOCONDITION_ERROR,  { "tecCRYPTOCONDITION_ERROR", "Malformed, invalid, or mismatched conditional or fulfillment."                 } },
        { tecINVARIANT_FAILED,       { "tecINVARIANT_FAILED",      "One or more invariants for the transaction were not satisfied."                } },
        { tecEXPIRED,                { "tecEXPIRED",               "Expiration time is passed."                                                    } },
        { tecDUPLICATE,              { "tecDUPLICATE",             "Ledger object already exists."                                                 } },
        { tecKILLED,                 { "tecKILLED",                "FillOrKill offer killed."                                                      } },
        { tecMANIFEST_BAD_SIGNATURE, { "tecMANIFEST_BAD_SIGNATURE","The specified manifest is malformed and cannot be parsed."                     } },
        { tecMANIFEST_BAD_SEQUENCE,  { "tecMANIFEST_BAD_SEQUENCE", "The specified manifest is not newer than than the manifest presently stored."  } },
        { tecMANIFEST_BAD_DOMAIN,    { "tecMANIFEST_BAD_DOMAIN",   "The specified manifest contains a malformed or invalid domain."                } },

        { tefALREADY,                { "tefALREADY",               "The exact transaction was already in this ledger."                             } },
        { tefBAD_ADD_AUTH,           { "tefBAD_ADD_AUTH",          "Not authorized to add account."                                                } },
        { tefBAD_AUTH,               { "tefBAD_AUTH",              "Transaction's public key is not authorized."                                   } },
        { tefBAD_LEDGER,             { "tefBAD_LEDGER",            "Ledger in unexpected state."                                                   } },
        { tefBAD_QUORUM,             { "tefBAD_QUORUM",            "Signatures provided do not meet the quorum."                                   } },
        { tefBAD_SIGNATURE,          { "tefBAD_SIGNATURE",         "A signature is provided for a non-signer."                                     } },
        { tefCREATED,                { "tefCREATED",               "Can't add an already created account."                                         } },
        { tefEXCEPTION,              { "tefEXCEPTION",             "Unexpected program state."                                                     } },
        { tefFAILURE,                { "tefFAILURE",               "Failed to apply."                                                              } },
        { tefINTERNAL,               { "tefINTERNAL",              "Internal error."                                                               } },
        { tefMASTER_DISABLED,        { "tefMASTER_DISABLED",       "Master key is disabled."                                                       } },
        { tefMAX_LEDGER,             { "tefMAX_LEDGER",            "Ledger sequence too high."                                                     } },
        { tefNO_AUTH_REQUIRED,       { "tefNO_AUTH_REQUIRED",      "Auth is not required."                                                         } },
        { tefNOT_MULTI_SIGNING,      { "tefNOT_MULTI_SIGNING",     "Account has no appropriate list of multi-signers."                             } },
        { tefPAST_SEQ,               { "tefPAST_SEQ",              "This sequence number has already passed."                                      } },
        { tefWRONG_PRIOR,            { "tefWRONG_PRIOR",           "This previous transaction does not match."                                     } },
        { tefBAD_AUTH_MASTER,        { "tefBAD_AUTH_MASTER",       "Auth for unclaimed account needs correct master key."                          } },
        { tefINVARIANT_FAILED,       { "tefINVARIANT_FAILED",      "Fee claim violated invariants for the transaction."                            } },

        { telLOCAL_ERROR,            { "telLOCAL_ERROR",           "Local failure."                                                                } },
        { telBAD_DOMAIN,             { "telBAD_DOMAIN",            "Domain too long."                                                              } },
        { telBAD_PATH_COUNT,         { "telBAD_PATH_COUNT",        "Malformed: Too many paths."                                                    } },
        { telBAD_PUBLIC_KEY,         { "telBAD_PUBLIC_KEY",        "Public key too long."                                                          } },
        { telFAILED_PROCESSING,      { "telFAILED_PROCESSING",     "Failed to correctly process transaction."                                      } },
        { telINSUF_FEE_P,            { "telINSUF_FEE_P",           "Fee insufficient."                                                             } },
        { telNO_DST_PARTIAL,         { "telNO_DST_PARTIAL",        "Partial payment to create account not allowed."                                } },
        { telCAN_NOT_QUEUE,          { "telCAN_NOT_QUEUE",         "Can not queue at this time."                                                   } },
        { telCAN_NOT_QUEUE_BALANCE,  { "telCAN_NOT_QUEUE_BALANCE", "Can not queue at this time: insufficient balance to pay all queued fees."      } },
        { telCAN_NOT_QUEUE_BLOCKS,   { "telCAN_NOT_QUEUE_BLOCKS",  "Can not queue at this time: would block later queued transaction(s)."          } },
        { telCAN_NOT_QUEUE_BLOCKED,  { "telCAN_NOT_QUEUE_BLOCKED", "Can not queue at this time: blocking transaction in queue."                    } },
        { telCAN_NOT_QUEUE_FEE,      { "telCAN_NOT_QUEUE_FEE",     "Can not queue at this time: fee insufficient to replace queued transaction."   } },
        { telCAN_NOT_QUEUE_FULL,     { "telCAN_NOT_QUEUE_FULL",    "Can not queue at this time: queue is full."                                    } },

        { temMALFORMED,              { "temMALFORMED",             "Malformed transaction."                                                        } },
        { temBAD_AMOUNT,             { "temBAD_AMOUNT",            "Can only send positive amounts."                                               } },
        { temBAD_CURRENCY,           { "temBAD_CURRENCY",          "Malformed: Bad currency."                                                      } },
        { temBAD_EXPIRATION,         { "temBAD_EXPIRATION",        "Malformed: Bad expiration."                                                    } },
        { temBAD_FEE,                { "temBAD_FEE",               "Invalid fee, negative or not XRP."                                             } },
        { temBAD_ISSUER,             { "temBAD_ISSUER",            "Malformed: Bad issuer."                                                        } },
        { temBAD_LIMIT,              { "temBAD_LIMIT",             "Limits must be non-negative."                                                  } },
        { temBAD_OFFER,              { "temBAD_OFFER",             "Malformed: Bad offer."                                                         } },
        { temBAD_PATH,               { "temBAD_PATH",              "Malformed: Bad path."                                                          } },
        { temBAD_PATH_LOOP,          { "temBAD_PATH_LOOP",         "Malformed: Loop in path."                                                      } },
        { temBAD_QUORUM,             { "temBAD_QUORUM",            "Malformed: Quorum is unreachable."                                             } },
        { temBAD_SEND_XRP_LIMIT,     { "temBAD_SEND_XRP_LIMIT",    "Malformed: Limit quality is not allowed for XRP to XRP."                       } },
        { temBAD_SEND_XRP_MAX,       { "temBAD_SEND_XRP_MAX",      "Malformed: Send max is not allowed for XRP to XRP."                            } },
        { temBAD_SEND_XRP_NO_DIRECT, { "temBAD_SEND_XRP_NO_DIRECT","Malformed: No Ripple direct is not allowed for XRP to XRP."                    } },
        { temBAD_SEND_XRP_PARTIAL,   { "temBAD_SEND_XRP_PARTIAL",  "Malformed: Partial payment is not allowed for XRP to XRP."                     } },
        { temBAD_SEND_XRP_PATHS,     { "temBAD_SEND_XRP_PATHS",    "Malformed: Paths are not allowed for XRP to XRP."                              } },
        { temBAD_SEQUENCE,           { "temBAD_SEQUENCE",          "Malformed: Sequence is not in the past."                                       } },
        { temBAD_SIGNATURE,          { "temBAD_SIGNATURE",         "Malformed: Bad signature."                                                     } },
        { temBAD_SIGNER,             { "temBAD_SIGNER",            "Malformed: No signer may duplicate account or other signers."                  } },
        { temBAD_SRC_ACCOUNT,        { "temBAD_SRC_ACCOUNT",       "Malformed: Bad source account."                                                } },
        { temBAD_TRANSFER_RATE,      { "temBAD_TRANSFER_RATE",     "Malformed: Transfer rate must be >= 1.0 and <= 2.0"                                       } },
        { temBAD_WEIGHT,             { "temBAD_WEIGHT",            "Malformed: Weight must be a positive value."                                   } },
        { temDST_IS_SRC,             { "temDST_IS_SRC",            "Destination may not be source."                                                } },
        { temDST_NEEDED,             { "temDST_NEEDED",            "Destination not specified."                                                    } },
        { temINVALID,                { "temINVALID",               "The transaction is ill-formed."                                                } },
        { temINVALID_FLAG,           { "temINVALID_FLAG",          "The transaction has an invalid flag."                                          } },
        { temREDUNDANT,              { "temREDUNDANT",             "Sends same currency to self."                                                  } },
        { temRIPPLE_EMPTY,           { "temRIPPLE_EMPTY",          "PathSet with no paths."                                                        } },
        { temUNCERTAIN,              { "temUNCERTAIN",             "In process of determining result. Never returned."                             } },
        { temUNKNOWN,                { "temUNKNOWN",               "The transaction requires logic that is not implemented yet."                   } },
        { temDISABLED,               { "temDISABLED",              "The transaction requires logic that is currently disabled."                    } },
        { temBAD_TICK_SIZE,          { "temBAD_TICK_SIZE",         "Malformed: Tick size out of range."                                            } },
        { temINVALID_ACCOUNT_ID,     { "temINVALID_ACCOUNT_ID",    "Malformed: A field contains an invalid account ID."                            } },
        { temCANNOT_PREAUTH_SELF,    { "temCANNOT_PREAUTH_SELF",   "Malformed: An account may not preauthorize itself."                            } },

        { terRETRY,                  { "terRETRY",                 "Retry transaction."                                                            } },
        { terFUNDS_SPENT,            { "terFUNDS_SPENT",           "Can't set password, password set funds already spent."                         } },
        { terINSUF_FEE_B,            { "terINSUF_FEE_B",           "Account balance can't pay fee."                                                } },
        { terLAST,                   { "terLAST",                  "Process last."                                                                 } },
        { terNO_RIPPLE,              { "terNO_RIPPLE",             "Path does not permit rippling."                                                } },
        { terNO_ACCOUNT,             { "terNO_ACCOUNT",            "The source account does not exist."                                            } },
        { terNO_AUTH,                { "terNO_AUTH",               "Not authorized to hold IOUs."                                                  } },
        { terNO_LINE,                { "terNO_LINE",               "No such line."                                                                 } },
        { terPRE_SEQ,                { "terPRE_SEQ",               "Missing/inapplicable prior transaction."                                       } },
        { terOWNERS,                 { "terOWNERS",                "Non-zero owner count."                                                         } },
        { terQUEUED,                 { "terQUEUED",                "Held until escalated fee drops."                                               } },

        { tesSUCCESS,                { "tesSUCCESS",               "The transaction was applied. Only final in a validated ledger."                } },
    };
    return results;
}

}

bool transResultInfo (TER code, std::string& token, std::string& text)
{
    auto& results = detail::transResults();

    auto const r = results.find (TERtoInt (code));

    if (r == results.end())
        return false;

    token = r->second.first;
    text = r->second.second;
    return true;
}

std::string transToken (TER code)
{
    std::string token;
    std::string text;

    return transResultInfo (code, token, text) ? token : "-";
}

std::string transHuman (TER code)
{
    std::string token;
    std::string text;

    return transResultInfo (code, token, text) ? text : "-";
}

boost::optional<TER>
transCode(std::string const& token)
{
    static
    auto const results = []
    {
        auto& byTer = detail::transResults();
        auto range = boost::make_iterator_range(byTer.begin(),
            byTer.end());
        auto tRange = boost::adaptors::transform(
            range,
            [](auto const& r)
            {
            return std::make_pair(r.second.first, r.first);
            }
        );
        std::unordered_map<
            std::string,
            TERUnderlyingType> const
        byToken(tRange.begin(), tRange.end());
        return byToken;
    }();

    auto const r = results.find(token);

    if (r == results.end())
        return boost::none;

    return TER::fromInt (r->second);
}

} // ripple
