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

struct RPCErr; // for Log

SETUP_LOG (RPCErr)

Json::Value rpcError (int iError, Json::Value jvResult)
{
    static struct
    {
        int         iError;
        const char* pToken;
        const char* pMessage;
    } errorInfoA[] =
    {
        { rpcACT_BITCOIN,           "actBitcoin",       "Account is bitcoin address."                           },
        { rpcACT_EXISTS,            "actExists",        "Account already exists."                               },
        { rpcACT_MALFORMED,         "actMalformed",     "Account malformed."                                    },
        { rpcACT_NOT_FOUND,         "actNotFound",      "Account not found."                                    },
        { rpcBAD_BLOB,              "badBlob",          "Blob must be a non-empty hex string."                  },
        { rpcBAD_FEATURE,           "badFeature",       "Feature unknown or invalid."                           },
        { rpcBAD_ISSUER,            "badIssuer",        "Issuer account malformed."                             },
        { rpcBAD_MARKET,            "badMarket",        "No such market."                                       },
        { rpcBAD_SECRET,            "badSecret",        "Secret does not match account."                        },
        { rpcBAD_SEED,              "badSeed",          "Disallowed seed."                                      },
        { rpcBAD_SYNTAX,            "badSyntax",        "Syntax error."                                         },
        { rpcCOMMAND_MISSING,       "commandMissing",   "Missing command entry."                                },
        { rpcDST_ACT_MALFORMED,     "dstActMalformed",  "Destination account is malformed."                     },
        { rpcDST_ACT_MISSING,       "dstActMissing",    "Destination account does not exist."                   },
        { rpcDST_AMT_MALFORMED,     "dstAmtMalformed",  "Destination amount/currency/issuer is malformed."      },
        { rpcDST_ISR_MALFORMED,     "dstIsrMalformed",  "Destination issuer is malformed."                      },
        { rpcFORBIDDEN,             "forbidden",        "Bad credentials."                                      },
        { rpcFAIL_GEN_DECRPYT,      "failGenDecrypt",   "Failed to decrypt generator."                          },
        { rpcGETS_ACT_MALFORMED,    "getsActMalformed", "Gets account malformed."                               },
        { rpcGETS_AMT_MALFORMED,    "getsAmtMalformed", "Gets amount malformed."                                },
        { rpcHOST_IP_MALFORMED,     "hostIpMalformed",  "Host IP is malformed."                                 },
        { rpcINSUF_FUNDS,           "insufFunds",       "Insufficient funds."                                   },
        { rpcINTERNAL,              "internal",         "Internal error."                                       },
        { rpcINVALID_PARAMS,        "invalidParams",    "Invalid parameters."                                   },
        { rpcJSON_RPC,              "json_rpc",         "JSON-RPC transport error."                             },
        { rpcLGR_IDXS_INVALID,      "lgrIdxsInvalid",   "Ledger indexes invalid."                               },
        { rpcLGR_IDX_MALFORMED,     "lgrIdxMalformed",  "Ledger index malformed."                               },
        { rpcLGR_NOT_FOUND,         "lgrNotFound",      "Ledger not found."                                     },
        { rpcNICKNAME_MALFORMED,    "nicknameMalformed", "Nickname is malformed."                                },
        { rpcNICKNAME_MISSING,      "nicknameMissing",  "Nickname does not exist."                              },
        { rpcNICKNAME_PERM,         "nicknamePerm",     "Account does not control nickname."                    },
        { rpcNOT_IMPL,              "notImpl",          "Not implemented."                                      },
        { rpcNO_ACCOUNT,            "noAccount",        "No such account."                                      },
        { rpcNO_CLOSED,             "noClosed",         "Closed ledger is unavailable."                         },
        { rpcNO_CURRENT,            "noCurrent",        "Current ledger is unavailable."                        },
        { rpcNO_EVENTS,             "noEvents",         "Current transport does not support events."            },
        { rpcNO_GEN_DECRPYT,        "noGenDectypt",     "Password failed to decrypt master public generator."   },
        { rpcNO_NETWORK,            "noNetwork",        "Network not available."                                },
        { rpcNO_PATH,               "noPath",           "Unable to find a ripple path."                         },
        { rpcNO_PERMISSION,         "noPermission",     "You don't have permission for this command."           },
        { rpcNO_PF_REQUEST,         "noPathRequest",    "No pathfinding request in progress."                   },
        { rpcNOT_STANDALONE,        "notStandAlone",    "Operation valid in debug mode only."                   },
        { rpcNOT_SUPPORTED,         "notSupported",     "Operation not supported."                              },
        { rpcPASSWD_CHANGED,        "passwdChanged",    "Wrong key, password changed."                          },
        { rpcPAYS_ACT_MALFORMED,    "paysActMalformed", "Pays account malformed."                               },
        { rpcPAYS_AMT_MALFORMED,    "paysAmtMalformed", "Pays amount malformed."                                },
        { rpcPORT_MALFORMED,        "portMalformed",    "Port is malformed."                                    },
        { rpcPUBLIC_MALFORMED,      "publicMalformed",  "Public key is malformed."                              },
        { rpcQUALITY_MALFORMED,     "qualityMalformed", "Quality malformed."                                    },
        { rpcSRC_ACT_MALFORMED,     "srcActMalformed",  "Source account is malformed."                          },
        { rpcSRC_ACT_MISSING,       "srcActMissing",    "Source account not provided."                          },
        { rpcSRC_ACT_NOT_FOUND,     "srcActNotFound",   "Source account not found."                             },
        { rpcSRC_AMT_MALFORMED,     "srcAmtMalformed",  "Source amount/currency/issuer is malformed."           },
        { rpcSRC_CUR_MALFORMED,     "srcCurMalformed",  "Source currency is malformed."                         },
        { rpcSRC_ISR_MALFORMED,     "srcIsrMalformed",  "Source issuer is malformed."                           },
        { rpcSRC_UNCLAIMED,         "srcUnclaimed",     "Source account is not claimed."                        },
        { rpcTXN_NOT_FOUND,         "txnNotFound",      "Transaction not found."                                },
        { rpcUNKNOWN_COMMAND,       "unknownCmd",       "Unknown method."                                       },
        { rpcWRONG_SEED,            "wrongSeed",        "The regular key does not point as the master key."     },
        { rpcTOO_BUSY,              "tooBusy",          "The server is too busy to help you now."               },
        { rpcSLOW_DOWN,             "slowDown",         "You are placing too much load on the server."          },
    };

    int     i;

    for (i = NUMBER (errorInfoA); i-- && errorInfoA[i].iError != iError;)
        ;

    jvResult["error"]           = i >= 0 ? errorInfoA[i].pToken : lexicalCast <std::string> (iError);
    jvResult["error_message"]   = i >= 0 ? errorInfoA[i].pMessage : lexicalCast <std::string> (iError);
    jvResult["error_code"]      = iError;

    if (i >= 0)
    {
        WriteLog (lsDEBUG, RPCErr) << "rpcError: "
                                   << errorInfoA[i].pToken << ": " << errorInfoA[i].pMessage << std::endl;
    }

    return jvResult;
}

bool isRpcError (Json::Value jvResult)
{
    return jvResult.isObject () && jvResult.isMember ("error");
}

// vim:ts=4
