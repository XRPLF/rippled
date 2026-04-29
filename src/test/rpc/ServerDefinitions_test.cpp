#include <test/jtx/Env.h>

#include <xrpld/rpc/handlers/server_info/ServerDefinitions.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class ServerDefinitions_test : public beast::unit_test::suite
{
public:
    void
    testServerDefinitions()
    {
        testcase("server_definitions");

        using namespace test::jtx;

        {
            Env env(*this);
            auto const result = env.rpc("server_definitions");
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::FIELDS));
            BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_RESULTS));
            BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::hash));

            // test a random element of each result
            // (testing the whole output would be difficult to maintain)

            {
                auto const firstField = result[jss::result][jss::FIELDS][0u];
                BEAST_EXPECT(firstField[0u].asString() == "Generic");
                BEAST_EXPECT(firstField[1][jss::isSerialized].asBool() == false);
                BEAST_EXPECT(firstField[1][jss::isSigningField].asBool() == false);
                BEAST_EXPECT(firstField[1][jss::isVLEncoded].asBool() == false);
                BEAST_EXPECT(firstField[1][jss::nth].asUInt() == 0);
                BEAST_EXPECT(firstField[1][jss::type].asString() == "Unknown");
            }

            BEAST_EXPECT(
                result[jss::result][jss::LEDGER_ENTRY_TYPES]["AccountRoot"].asUInt() == 97);
            BEAST_EXPECT(
                result[jss::result][jss::TRANSACTION_RESULTS]["tecDIR_FULL"].asUInt() == 121);
            BEAST_EXPECT(result[jss::result][jss::TRANSACTION_TYPES]["Payment"].asUInt() == 0);
            BEAST_EXPECT(result[jss::result][jss::TYPES]["AccountID"].asUInt() == 8);

            // check exception SFields
            {
                auto const fieldExists = [&](std::string name) {
                    for (auto& field : result[jss::result][jss::FIELDS])
                    {
                        if (field[0u].asString() == name)
                        {
                            return true;
                        }
                    }
                    return false;
                };
                BEAST_EXPECT(fieldExists("Generic"));
                BEAST_EXPECT(fieldExists("Invalid"));
                BEAST_EXPECT(fieldExists("ObjectEndMarker"));
                BEAST_EXPECT(fieldExists("ArrayEndMarker"));
                BEAST_EXPECT(fieldExists("taker_gets_funded"));
                BEAST_EXPECT(fieldExists("taker_pays_funded"));
                BEAST_EXPECT(fieldExists("hash"));
                BEAST_EXPECT(fieldExists("index"));
            }

            // test that base_uint types are replaced with "Hash" prefix
            {
                auto const types = result[jss::result][jss::TYPES];
                BEAST_EXPECT(types["Hash128"].asUInt() == 4);
                BEAST_EXPECT(types["Hash160"].asUInt() == 17);
                BEAST_EXPECT(types["Hash192"].asUInt() == 21);
                BEAST_EXPECT(types["Hash256"].asUInt() == 5);
                BEAST_EXPECT(types["Hash384"].asUInt() == 22);
                BEAST_EXPECT(types["Hash512"].asUInt() == 23);
            }

            // test the properties of the LEDGER_ENTRY_FLAGS section
            {
                BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_FLAGS));
                Json::Value const& leFlags = result[jss::result][jss::LEDGER_ENTRY_FLAGS];

                // sanity test the mapped value of a few arbitrarily chosen flags
                BEAST_EXPECT(leFlags["AccountRoot"]["lsfDisallowXRP"] == 0x00080000);
                BEAST_EXPECT(leFlags["AccountRoot"]["lsfDepositAuth"] == 0x01000000);
                BEAST_EXPECT(leFlags["AccountRoot"]["lsfAllowTrustLineClawback"] == 0x80000000);

                BEAST_EXPECT(leFlags["RippleState"]["lsfHighFreeze"] == 0x00800000);
                BEAST_EXPECT(leFlags["RippleState"]["lsfAMMNode"] == 0x01000000);

                BEAST_EXPECT(leFlags["DirNode"]["lsfNFTokenBuyOffers"] == 0x00000001);
                BEAST_EXPECT(leFlags["MPTokenIssuance"]["lsfMPTCanTrade"] == 0x00000010);
                BEAST_EXPECT(leFlags["Credential"]["lsfAccepted"] == 0x00010000);
                BEAST_EXPECT(leFlags["Loan"]["lsfLoanImpaired"] == 0x00020000);
                BEAST_EXPECT(leFlags["Vault"]["lsfVaultPrivate"] == 0x00010000);
                BEAST_EXPECT(leFlags["MPToken"]["lsfMPTAuthorized"] == 0x00000002);
            }

            // validate the correctness of few chosen transaction flags
            {
                BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_FLAGS));
                Json::Value const& txFlags = result[jss::result][jss::TRANSACTION_FLAGS];

                BEAST_EXPECT(txFlags["universal"]["tfFullyCanonicalSig"] == 0x80000000);
                BEAST_EXPECT(txFlags["universal"]["tfInnerBatchTxn"] == 0x40000000);

                BEAST_EXPECT(txFlags["AccountSet"]["tfRequireAuth"] == 0x00040000);
                BEAST_EXPECT(txFlags["AccountSet"]["tfAllowXRP"] == 0x00200000);

                BEAST_EXPECT(txFlags["MPTokenIssuanceSet"]["tfMPTLock"] == 0x00000001);
                BEAST_EXPECT(txFlags["MPTokenIssuanceSet"]["tfMPTUnlock"] == 0x00000002);

                BEAST_EXPECT(txFlags["AMMDeposit"]["tfLPToken"] == 0x00010000);
                BEAST_EXPECT(txFlags["AMMDeposit"]["tfLimitLPToken"] == 0x00400000);
            }

            // validate the correctness of the AccountSpecificFlags section
            {
                BEAST_EXPECT(result[jss::result].isMember(jss::ACCOUNT_SET_FLAGS));
                Json::Value const& asFlags = result[jss::result][jss::ACCOUNT_SET_FLAGS];

                BEAST_EXPECT(asFlags["asfDisallowXRP"] == 3);
                BEAST_EXPECT(asFlags["asfGlobalFreeze"] == 7);
                BEAST_EXPECT(asFlags["asfDisallowIncomingNFTokenOffer"] == 12);
                BEAST_EXPECT(asFlags["asfDisallowIncomingTrustline"] == 15);
            }

            // test the response fields of the TRANSACTION_FORMATS section
            {
                BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_FORMATS));
                Json::Value const& txnFormats = result[jss::result][jss::TRANSACTION_FORMATS];

                // first validate the contents of "common"
                {
                    BEAST_EXPECT(txnFormats.isMember("common"));
                    Json::Value const& section = txnFormats["common"];

                    BEAST_EXPECT(section[0u][jss::name] == "TransactionType");
                    BEAST_EXPECT(section[0u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(section[1u][jss::name] == "Flags");
                    BEAST_EXPECT(section[1u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[2u][jss::name] == "SourceTag");
                    BEAST_EXPECT(section[2u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[3u][jss::name] == "Account");
                    BEAST_EXPECT(section[3u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(section[4u][jss::name] == "Sequence");
                    BEAST_EXPECT(section[4u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(section[5u][jss::name] == "PreviousTxnID");
                    BEAST_EXPECT(section[5u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[6u][jss::name] == "LastLedgerSequence");
                    BEAST_EXPECT(section[6u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[7u][jss::name] == "AccountTxnID");
                    BEAST_EXPECT(section[7u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[8u][jss::name] == "Fee");
                    BEAST_EXPECT(section[8u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(section[9u][jss::name] == "OperationLimit");
                    BEAST_EXPECT(section[9u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[10u][jss::name] == "Memos");
                    BEAST_EXPECT(section[10u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[11u][jss::name] == "SigningPubKey");
                    BEAST_EXPECT(section[11u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(section[12u][jss::name] == "TicketSequence");
                    BEAST_EXPECT(section[12u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[13u][jss::name] == "TxnSignature");
                    BEAST_EXPECT(section[13u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[14u][jss::name] == "Signers");
                    BEAST_EXPECT(section[14u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[15u][jss::name] == "NetworkID");
                    BEAST_EXPECT(section[15u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[16u][jss::name] == "Delegate");
                    BEAST_EXPECT(section[16u][jss::optionality] == soeOPTIONAL);
                }

                // validate the contents of four arbitrarily selected transactions validate the
                // format of the OracleSet transaction
                {
                    BEAST_EXPECT(txnFormats.isMember("OracleSet"));
                    Json::Value const& section = txnFormats["OracleSet"];

                    BEAST_EXPECT(section[0u][jss::name] == "OracleDocumentID");
                    BEAST_EXPECT(section[0u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(section[1u][jss::name] == "Provider");
                    BEAST_EXPECT(section[1u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[2u][jss::name] == "URI");
                    BEAST_EXPECT(section[2u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[3u][jss::name] == "AssetClass");
                    BEAST_EXPECT(section[3u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[4u][jss::name] == "LastUpdateTime");
                    BEAST_EXPECT(section[4u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(section[5u][jss::name] == "PriceDataSeries");
                    BEAST_EXPECT(section[5u][jss::optionality] == soeREQUIRED);
                }

                // validate the format of the PermissionedDomainDelete transaction
                {
                    BEAST_EXPECT(txnFormats.isMember("PermissionedDomainDelete"));
                    Json::Value const& section = txnFormats["PermissionedDomainDelete"];

                    BEAST_EXPECT(section[0u][jss::name] == "DomainID");
                    BEAST_EXPECT(section[0u][jss::optionality] == soeREQUIRED);
                }

                // validate the format of the Clawback transaction
                {
                    BEAST_EXPECT(txnFormats.isMember("Clawback"));
                    Json::Value const& section = txnFormats["Clawback"];

                    BEAST_EXPECT(section[0u][jss::name] == "Amount");
                    BEAST_EXPECT(section[0u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(section[1u][jss::name] == "Holder");
                    BEAST_EXPECT(section[1u][jss::optionality] == soeOPTIONAL);
                }

                // validate the format of the SetFee transaction
                {
                    BEAST_EXPECT(txnFormats.isMember("SetFee"));
                    Json::Value const& section = txnFormats["SetFee"];

                    BEAST_EXPECT(section[0u][jss::name] == "LedgerSequence");
                    BEAST_EXPECT(section[0u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[1u][jss::name] == "BaseFee");
                    BEAST_EXPECT(section[1u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[2u][jss::name] == "ReferenceFeeUnits");
                    BEAST_EXPECT(section[2u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[3u][jss::name] == "ReserveBase");
                    BEAST_EXPECT(section[3u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[4u][jss::name] == "ReserveIncrement");
                    BEAST_EXPECT(section[4u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[5u][jss::name] == "BaseFeeDrops");
                    BEAST_EXPECT(section[5u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[6u][jss::name] == "ReserveBaseDrops");
                    BEAST_EXPECT(section[6u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(section[7u][jss::name] == "ReserveIncrementDrops");
                    BEAST_EXPECT(section[7u][jss::optionality] == soeOPTIONAL);
                }
            }

            // test the properties of the LEDGER_ENTRY_FORMATS section in server_definitions
            // response
            {
                BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_FORMATS));

                // Note: For the purposes of software maintenance, this test does not exhaustively
                // validate all the LEDGER_ENTRY_FORMATS

                // check "common" first
                {
                    Json::Value const& observedCommonLedgerEntry =
                        result[jss::result][jss::LEDGER_ENTRY_FORMATS]["common"];

                    BEAST_EXPECT(observedCommonLedgerEntry[0u][jss::name] == "LedgerIndex");
                    BEAST_EXPECT(observedCommonLedgerEntry[0u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(observedCommonLedgerEntry[1u][jss::name] == "LedgerEntryType");
                    BEAST_EXPECT(observedCommonLedgerEntry[1u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(observedCommonLedgerEntry[2u][jss::name] == "Flags");
                    BEAST_EXPECT(observedCommonLedgerEntry[2u][jss::optionality] == soeREQUIRED);
                }

                // test the contents of an arbitrary ledger-entry (DID)
                {
                    Json::Value const& observedDIDLedgerEntry =
                        result[jss::result][jss::LEDGER_ENTRY_FORMATS]["DID"];

                    BEAST_EXPECT(observedDIDLedgerEntry[0u][jss::name] == "Account");
                    BEAST_EXPECT(observedDIDLedgerEntry[0u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(observedDIDLedgerEntry[1u][jss::name] == "DIDDocument");
                    BEAST_EXPECT(observedDIDLedgerEntry[1u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(observedDIDLedgerEntry[2u][jss::name] == "URI");
                    BEAST_EXPECT(observedDIDLedgerEntry[2u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(observedDIDLedgerEntry[3u][jss::name] == "Data");
                    BEAST_EXPECT(observedDIDLedgerEntry[3u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(observedDIDLedgerEntry[4u][jss::name] == "OwnerNode");
                    BEAST_EXPECT(observedDIDLedgerEntry[4u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(observedDIDLedgerEntry[5u][jss::name] == "PreviousTxnID");
                    BEAST_EXPECT(observedDIDLedgerEntry[5u][jss::optionality] == soeREQUIRED);

                    BEAST_EXPECT(observedDIDLedgerEntry[6u][jss::name] == "PreviousTxnLgrSeq");
                    BEAST_EXPECT(observedDIDLedgerEntry[6u][jss::optionality] == soeREQUIRED);
                }

                // test the contents of an arbitrary ledger-entry (NegativeUNL)
                {
                    Json::Value const& observedNunlLedgerEntry =
                        result[jss::result][jss::LEDGER_ENTRY_FORMATS]["NegativeUNL"];

                    BEAST_EXPECT(observedNunlLedgerEntry[0u][jss::name] == "DisabledValidators");
                    BEAST_EXPECT(observedNunlLedgerEntry[0u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(observedNunlLedgerEntry[1u][jss::name] == "ValidatorToDisable");
                    BEAST_EXPECT(observedNunlLedgerEntry[1u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(observedNunlLedgerEntry[2u][jss::name] == "ValidatorToReEnable");
                    BEAST_EXPECT(observedNunlLedgerEntry[2u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(observedNunlLedgerEntry[3u][jss::name] == "PreviousTxnID");
                    BEAST_EXPECT(observedNunlLedgerEntry[3u][jss::optionality] == soeOPTIONAL);

                    BEAST_EXPECT(observedNunlLedgerEntry[4u][jss::name] == "PreviousTxnLgrSeq");
                    BEAST_EXPECT(observedNunlLedgerEntry[4u][jss::optionality] == soeOPTIONAL);
                }
            }

            // Exhaustive test: verify all transaction flags from getAllTxFlags() appear in the
            // output
            {
                Json::Value const& txFlags = result[jss::result][jss::TRANSACTION_FLAGS];

                for (auto const& [txName, flagMap] : getAllTxFlags())
                {
                    BEAST_EXPECT(txFlags.isMember(txName));
                    if (txFlags.isMember(txName))
                    {
                        for (auto const& [flagName, flagValue] : flagMap)
                        {
                            BEAST_EXPECT(txFlags[txName].isMember(flagName));
                            if (txFlags[txName].isMember(flagName))
                            {
                                BEAST_EXPECT(txFlags[txName][flagName].asUInt() == flagValue);
                            }
                        }
                    }
                }
            }

            // Exhaustive test: verify all ledger entry flags from getAllLedgerFlags() appear in the
            // output
            {
                Json::Value const& leFlags = result[jss::result][jss::LEDGER_ENTRY_FLAGS];

                for (auto const& [ledgerType, flagMap] : getAllLedgerFlags())
                {
                    BEAST_EXPECT(leFlags.isMember(ledgerType));
                    if (leFlags.isMember(ledgerType))
                    {
                        for (auto const& [flagName, flagValue] : flagMap)
                        {
                            BEAST_EXPECT(leFlags[ledgerType].isMember(flagName));
                            if (leFlags[ledgerType].isMember(flagName))
                            {
                                BEAST_EXPECT(leFlags[ledgerType][flagName].asUInt() == flagValue);
                            }
                        }
                    }
                }
            }

            // Exhaustive test: verify all AccountSet flags from getAsfFlagMap() appear in the
            // output
            {
                Json::Value const& asFlags = result[jss::result][jss::ACCOUNT_SET_FLAGS];

                for (auto const& [flagName, flagValue] : getAsfFlagMap())
                {
                    BEAST_EXPECT(asFlags.isMember(flagName));
                    if (asFlags.isMember(flagName))
                    {
                        BEAST_EXPECT(asFlags[flagName].asInt() == flagValue);
                    }
                }
            }

            // test providing the same hash
            {
                Env env(*this);
                auto const firstResult = env.rpc("server_definitions");
                auto const hash = firstResult[jss::result][jss::hash].asString();
                auto const hashParam = std::string("{ ") + "\"hash\": \"" + hash + "\"}";

                auto const result = env.rpc("json", "server_definitions", hashParam);
                BEAST_EXPECT(!result[jss::result].isMember(jss::error));
                BEAST_EXPECT(result[jss::result][jss::status] == "success");
                BEAST_EXPECT(!result[jss::result].isMember(jss::FIELDS));
                BEAST_EXPECT(!result[jss::result].isMember(jss::LEDGER_ENTRY_TYPES));
                BEAST_EXPECT(!result[jss::result].isMember(jss::LEDGER_ENTRY_FLAGS));
                BEAST_EXPECT(!result[jss::result].isMember(jss::LEDGER_ENTRY_FORMATS));
                BEAST_EXPECT(!result[jss::result].isMember(jss::TRANSACTION_RESULTS));
                BEAST_EXPECT(!result[jss::result].isMember(jss::TRANSACTION_TYPES));
                BEAST_EXPECT(!result[jss::result].isMember(jss::TRANSACTION_FLAGS));
                BEAST_EXPECT(!result[jss::result].isMember(jss::TRANSACTION_FORMATS));
                BEAST_EXPECT(!result[jss::result].isMember(jss::TYPES));
                BEAST_EXPECT(result[jss::result].isMember(jss::hash));
            }

            // test providing a different hash
            {
                Env env(*this);
                std::string const hash =
                    "54296160385A27154BFA70A239DD8E8FD4CC2DB7BA32D970BA3A5B132CF749"
                    "D1";
                auto const hashParam = std::string("{ ") + "\"hash\": \"" + hash + "\"}";

                auto const result = env.rpc("json", "server_definitions", hashParam);
                BEAST_EXPECT(!result[jss::result].isMember(jss::error));
                BEAST_EXPECT(result[jss::result][jss::status] == "success");
                BEAST_EXPECT(result[jss::result].isMember(jss::FIELDS));
                BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_TYPES));
                BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_FLAGS));
                BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_FORMATS));
                BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_RESULTS));
                BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_TYPES));
                BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_FLAGS));
                BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_FORMATS));
                BEAST_EXPECT(result[jss::result].isMember(jss::TYPES));
                BEAST_EXPECT(result[jss::result].isMember(jss::hash));
            }
        }
    }

    void
    testGetServerDefinitionsJson()
    {
        testcase("getServerDefinitionsJson");

        auto const& defs = getServerDefinitionsJson();
        for (auto const& field :
             {jss::ACCOUNT_SET_FLAGS,
              jss::FIELDS,
              jss::LEDGER_ENTRY_FLAGS,
              jss::LEDGER_ENTRY_FORMATS,
              jss::LEDGER_ENTRY_TYPES,
              jss::TRANSACTION_FLAGS,
              jss::TRANSACTION_FORMATS,
              jss::TRANSACTION_RESULTS,
              jss::TRANSACTION_TYPES,
              jss::TYPES,
              jss::hash})
        {
            BEAST_EXPECT(defs.isMember(field));
        }

        // verify it returns the same hash as the RPC handler
        using namespace test::jtx;
        Env env(*this);
        auto const rpcResult = env.rpc("server_definitions");
        BEAST_EXPECT(defs[jss::hash] == rpcResult[jss::result][jss::hash]);
    }

    void
    run() override
    {
        testServerDefinitions();
        testGetServerDefinitionsJson();
    }
};

BEAST_DEFINE_TESTSUITE(ServerDefinitions, rpc, xrpl);

}  // namespace xrpl::test
