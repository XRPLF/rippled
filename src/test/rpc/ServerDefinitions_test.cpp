#include <test/jtx.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

namespace test {

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
            BEAST_EXPECT(
                result[jss::result].isMember(jss::TRANSACTION_RESULTS));
            BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::hash));

            // test a random element of each result
            // (testing the whole output would be difficult to maintain)

            {
                auto const firstField = result[jss::result][jss::FIELDS][0u];
                BEAST_EXPECT(firstField[0u].asString() == "Generic");
                BEAST_EXPECT(
                    firstField[1][jss::isSerialized].asBool() == false);
                BEAST_EXPECT(
                    firstField[1][jss::isSigningField].asBool() == false);
                BEAST_EXPECT(firstField[1][jss::isVLEncoded].asBool() == false);
                BEAST_EXPECT(firstField[1][jss::nth].asUInt() == 0);
                BEAST_EXPECT(firstField[1][jss::type].asString() == "Unknown");
            }

            BEAST_EXPECT(
                result[jss::result][jss::LEDGER_ENTRY_TYPES]["AccountRoot"]
                    .asUInt() == 97);
            BEAST_EXPECT(
                result[jss::result][jss::TRANSACTION_RESULTS]["tecDIR_FULL"]
                    .asUInt() == 121);
            BEAST_EXPECT(
                result[jss::result][jss::TRANSACTION_TYPES]["Payment"]
                    .asUInt() == 0);
            BEAST_EXPECT(
                result[jss::result][jss::TYPES]["AccountID"].asUInt() == 8);

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
                BEAST_EXPECT(
                    result[jss::result].isMember(jss::LEDGER_ENTRY_FLAGS));
                Json::Value const& leFlags =
                    result[jss::result][jss::LEDGER_ENTRY_FLAGS];
                // at present time, there are a total of 11 ledger objects with
                // flags
                BEAST_EXPECT(leFlags.size() == 11);

                unsigned int totalCountLedgerSpecificFlags = 0;
                for (auto const& ledgerObjectFlags : leFlags)
                {
                    totalCountLedgerSpecificFlags += ledgerObjectFlags.size();
                }
                BEAST_EXPECT(totalCountLedgerSpecificFlags == 54);

                // sanity test the mapped value of a few arbitrarily chosen
                // flags
                BEAST_EXPECT(
                    leFlags["ltACCOUNT_ROOT"]["lsfDisallowXRP"] == 0x00080000);
                BEAST_EXPECT(
                    leFlags["ltACCOUNT_ROOT"]["lsfDepositAuth"] == 0x01000000);
                BEAST_EXPECT(
                    leFlags["ltACCOUNT_ROOT"]["lsfAllowTrustLineClawback"] ==
                    0x80000000);

                BEAST_EXPECT(
                    leFlags["ltRIPPLE_STATE"]["lsfHighFreeze"] == 0x00800000);
                BEAST_EXPECT(
                    leFlags["ltRIPPLE_STATE"]["lsfAMMNode"] == 0x01000000);

                BEAST_EXPECT(
                    leFlags["ltDIR_NODE"]["lsfNFTokenBuyOffers"] == 0x00000001);
                BEAST_EXPECT(
                    leFlags["ltMPTOKEN_ISSUANCE"]["lsfMPTCanTrade"] ==
                    0x00000010);
                BEAST_EXPECT(
                    leFlags["ltCREDENTIAL"]["lsfAccepted"] == 0x00010000);
                BEAST_EXPECT(
                    leFlags["ltLOAN"]["lsfLoanImpaired"] == 0x00020000);
                BEAST_EXPECT(
                    leFlags["ltVAULT"]["lsfVaultPrivate"] == 0x00010000);
                BEAST_EXPECT(
                    leFlags["ltMPTOKEN"]["lsfMPTAuthorized"] == 0x00000002);
            }
            // validate the correctness of few chosen transaction flags
            {
                BEAST_EXPECT(
                    result[jss::result].isMember(jss::TRANSACTION_FLAGS));
                Json::Value const& txFlags =
                    result[jss::result][jss::TRANSACTION_FLAGS];

                // count the transactions which allow for custom flag values
                BEAST_EXPECT(txFlags.size() == 24);

                BEAST_EXPECT(
                    txFlags["Universal"]["tfFullyCanonicalSig"] == 0x80000000);
                BEAST_EXPECT(
                    txFlags["Universal"]["tfInnerBatchTxn"] == 0x40000000);

                BEAST_EXPECT(
                    txFlags["AccountSet"]["tfRequireAuth"] == 0x00040000);
                BEAST_EXPECT(txFlags["AccountSet"]["tfAllowXRP"] == 0x00200000);

                BEAST_EXPECT(
                    txFlags["MPTokenIssuanceSet"]["tfMPTLock"] == 0x00000001);
                BEAST_EXPECT(
                    txFlags["MPTokenIssuanceSet"]["tfMPTUnlock"] == 0x00000002);

                BEAST_EXPECT(txFlags["AMM"]["tfLPToken"] == 0x00010000);
                BEAST_EXPECT(txFlags["AMM"]["tfLimitLPToken"] == 0x00400000);

                unsigned int totalTxFlags = 0;
                for (auto const& txSection : txFlags)
                    totalTxFlags += txSection.size();
                BEAST_EXPECT(totalTxFlags == 116);
            }

            // validate the correctness of the AccountSpecificFlags section
            {
                BEAST_EXPECT(
                    result[jss::result].isMember(jss::ACCOUNT_SPECIFIC_FLAGS));
                Json::Value const& asFlags =
                    result[jss::result][jss::ACCOUNT_SPECIFIC_FLAGS];

                BEAST_EXPECT(asFlags.size() == 16);
                BEAST_EXPECT(asFlags["asfDisallowXRP"] == 3);
                BEAST_EXPECT(asFlags["asfGlobalFreeze"] == 7);
                BEAST_EXPECT(asFlags["asfDisallowIncomingNFTokenOffer"] == 12);
                BEAST_EXPECT(asFlags["asfDisallowIncomingTrustline"] == 15);
            }

            // validate the correctness of the ledgerNameSpaces section
            {
                BEAST_EXPECT(
                    result[jss::result].isMember(jss::LEDGER_NAME_SPACE));
                Json::Value const& lnSpace =
                    result[jss::result][jss::LEDGER_NAME_SPACE];

                BEAST_EXPECT(lnSpace.size() == 37);
                BEAST_EXPECT(lnSpace["ESCROW"] == 'u');
                BEAST_EXPECT(lnSpace["DEPOSIT_PREAUTH"] == 'p');
                BEAST_EXPECT(lnSpace["ORACLE"] == 'R');
                BEAST_EXPECT(lnSpace["LOAN_BROKER"] == 'l');
            }

            // test the response fields of the TRANSACTION_FORMATS section
            {
                BEAST_EXPECT(
                    result[jss::result].isMember(jss::TRANSACTION_FORMATS));
                Json::Value const& txnFormats =
                    result[jss::result][jss::TRANSACTION_FORMATS];
                BEAST_EXPECT(txnFormats.size() == 75);

                // validate the contents of four arbitrarily selected
                // transactions
                // // validate the format of the OracleSet transaction
                {
                    BEAST_EXPECT(txnFormats.isMember("OracleSet"));
                    BEAST_EXPECT(txnFormats["OracleSet"][jss::hexCode] == 51);

                    // common_fields + unique_fields for the OracleSet
                    // transaction
                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields].size() == 6 + 17);

                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][0u]
                                  [jss::sfield_Name] == "OracleDocumentID");
                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][0u]
                                  [jss::optionality] == "REQUIRED");

                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][1u]
                                  [jss::sfield_Name] == "Provider");
                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][1u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][2u]
                                  [jss::sfield_Name] == "URI");
                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][2u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][3u]
                                  [jss::sfield_Name] == "AssetClass");
                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][3u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][4u]
                                  [jss::sfield_Name] == "LastUpdateTime");
                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][4u]
                                  [jss::optionality] == "REQUIRED");

                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][5u]
                                  [jss::sfield_Name] == "PriceDataSeries");
                    BEAST_EXPECT(
                        txnFormats["OracleSet"][jss::sfields][5u]
                                  [jss::optionality] == "REQUIRED");
                }

                // validate the format of the PermissionedDomainDelete
                // transaction
                {
                    BEAST_EXPECT(
                        txnFormats.isMember("PermissionedDomainDelete"));
                    BEAST_EXPECT(
                        txnFormats["PermissionedDomainDelete"][jss::hexCode] ==
                        63);

                    // common_fields + unique_fields for the
                    // PermissionedDomainDelete transaction
                    BEAST_EXPECT(
                        txnFormats["PermissionedDomainDelete"][jss::sfields]
                            .size() == 1 + 17);

                    BEAST_EXPECT(
                        txnFormats["PermissionedDomainDelete"][jss::sfields][0u]
                                  [jss::sfield_Name] == "DomainID");
                    BEAST_EXPECT(
                        txnFormats["PermissionedDomainDelete"][jss::sfields][0u]
                                  [jss::optionality] == "REQUIRED");
                }

                // validate the format of the Clawback transaction
                {
                    BEAST_EXPECT(txnFormats.isMember("Clawback"));
                    BEAST_EXPECT(txnFormats["Clawback"][jss::hexCode] == 30);

                    // common + unique fields for the Clawback transaction
                    BEAST_EXPECT(
                        txnFormats["Clawback"][jss::sfields].size() == 2 + 17);

                    BEAST_EXPECT(
                        txnFormats["Clawback"][jss::sfields][0u]
                                  [jss::sfield_Name] == "Amount");
                    BEAST_EXPECT(
                        txnFormats["Clawback"][jss::sfields][0u]
                                  [jss::optionality] == "REQUIRED");

                    BEAST_EXPECT(
                        txnFormats["Clawback"][jss::sfields][1u]
                                  [jss::sfield_Name] == "Holder");
                    BEAST_EXPECT(
                        txnFormats["Clawback"][jss::sfields][1u]
                                  [jss::optionality] == "OPTIONAL");
                }

                // validate the format of the SetFee transaction
                {
                    BEAST_EXPECT(txnFormats.isMember("SetFee"));
                    BEAST_EXPECT(txnFormats["SetFee"][jss::hexCode] == 101);

                    // common + unique fields for the SetFee transaction
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields].size() == 8 + 17);

                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][0u]
                                  [jss::sfield_Name] == "LedgerSequence");
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][0u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][1u]
                                  [jss::sfield_Name] == "BaseFee");
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][1u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][2u]
                                  [jss::sfield_Name] == "ReferenceFeeUnits");
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][2u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][3u]
                                  [jss::sfield_Name] == "ReserveBase");
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][3u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][4u]
                                  [jss::sfield_Name] == "ReserveIncrement");
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][4u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][5u]
                                  [jss::sfield_Name] == "BaseFeeDrops");
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][5u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][6u]
                                  [jss::sfield_Name] == "ReserveBaseDrops");
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][6u]
                                  [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][7u]
                                  [jss::sfield_Name] ==
                        "ReserveIncrementDrops");
                    BEAST_EXPECT(
                        txnFormats["SetFee"][jss::sfields][7u]
                                  [jss::optionality] == "OPTIONAL");
                }
            }

            // test the properties of the LEDGER_ENTRIES section in
            // server_definitions response
            {
                BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRIES));
                BEAST_EXPECT(
                    result[jss::result][jss::LEDGER_ENTRIES].size() == 30);

                // test the contents of an arbitrary ledger-entry (DID)
                // For the purposes of software maintainance, this test does not
                // exhaustively validate all the ledger_entries
                {
                    Json::Value const& observedDIDLedgerEntry =
                        result[jss::result][jss::LEDGER_ENTRIES]["DID"];

                    BEAST_EXPECT(observedDIDLedgerEntry[jss::hexCode] == 73);
                    // unique + common fields for the DID Ledger Entry
                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields].size() == 7 + 3);

                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][0u]
                                              [jss::sfield_Name] == "Account");
                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][0u]
                                              [jss::optionality] == "REQUIRED");

                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][1u]
                                              [jss::sfield_Name] ==
                        "DIDDocument");
                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][1u]
                                              [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][2u]
                                              [jss::sfield_Name] == "URI");
                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][2u]
                                              [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][3u]
                                              [jss::sfield_Name] == "Data");
                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][3u]
                                              [jss::optionality] == "OPTIONAL");

                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][4u]
                                              [jss::sfield_Name] ==
                        "OwnerNode");
                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][4u]
                                              [jss::optionality] == "REQUIRED");

                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][5u]
                                              [jss::sfield_Name] ==
                        "PreviousTxnID");
                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][5u]
                                              [jss::optionality] == "REQUIRED");

                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][6u]
                                              [jss::sfield_Name] ==
                        "PreviousTxnLgrSeq");
                    BEAST_EXPECT(
                        observedDIDLedgerEntry[jss::sfields][6u]
                                              [jss::optionality] == "REQUIRED");
                }

                // test the contents of an arbitrary ledger-entry (NegativeUNL)
                {
                    Json::Value const& observedNunlLedgerEntry =
                        result[jss::result][jss::LEDGER_ENTRIES]["NegativeUNL"];

                    BEAST_EXPECT(observedNunlLedgerEntry[jss::hexCode] == 78);
                    // unique + common fields for the NegativeUNL Ledger Entry
                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields].size() == 5 + 3);

                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][0u]
                                               [jss::sfield_Name] ==
                        "DisabledValidators");
                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][0u]
                                               [jss::optionality] ==
                        "OPTIONAL");

                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][1u]
                                               [jss::sfield_Name] ==
                        "ValidatorToDisable");
                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][1u]
                                               [jss::optionality] ==
                        "OPTIONAL");

                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][2u]
                                               [jss::sfield_Name] ==
                        "ValidatorToReEnable");
                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][2u]
                                               [jss::optionality] ==
                        "OPTIONAL");

                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][3u]
                                               [jss::sfield_Name] ==
                        "PreviousTxnID");
                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][3u]
                                               [jss::optionality] ==
                        "OPTIONAL");

                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][4u]
                                               [jss::sfield_Name] ==
                        "PreviousTxnLgrSeq");
                    BEAST_EXPECT(
                        observedNunlLedgerEntry[jss::sfields][4u]
                                               [jss::optionality] ==
                        "OPTIONAL");
                }
            }
        }

        // test providing the same hash
        {
            Env env(*this);
            auto const firstResult = env.rpc("server_definitions");
            auto const hash = firstResult[jss::result][jss::hash].asString();
            auto const hashParam =
                std::string("{ ") + "\"hash\": \"" + hash + "\"}";

            auto const result =
                env.rpc("json", "server_definitions", hashParam);
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(!result[jss::result].isMember(jss::FIELDS));
            BEAST_EXPECT(
                !result[jss::result].isMember(jss::LEDGER_ENTRY_TYPES));
            BEAST_EXPECT(
                !result[jss::result].isMember(jss::TRANSACTION_RESULTS));
            BEAST_EXPECT(!result[jss::result].isMember(jss::TRANSACTION_TYPES));
            BEAST_EXPECT(!result[jss::result].isMember(jss::TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::hash));
        }

        // test providing a different hash
        {
            Env env(*this);
            std::string const hash =
                "54296160385A27154BFA70A239DD8E8FD4CC2DB7BA32D970BA3A5B132CF749"
                "D1";
            auto const hashParam =
                std::string("{ ") + "\"hash\": \"" + hash + "\"}";

            auto const result =
                env.rpc("json", "server_definitions", hashParam);
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::FIELDS));
            BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_TYPES));
            BEAST_EXPECT(
                result[jss::result].isMember(jss::TRANSACTION_RESULTS));
            BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::hash));
        }
    }

    void
    run() override
    {
        testServerDefinitions();
    }
};

BEAST_DEFINE_TESTSUITE(ServerDefinitions, rpc, xrpl);

}  // namespace test
}  // namespace xrpl
