//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#include <test/jtx.h>

#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/core/ConfigSections.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/jss.h>

#include <boost/format.hpp>

namespace ripple {

namespace test {

namespace validator_data {
static auto const public_key =
    "nHBt9fsb4849WmZiCds4r5TXyBeQjqnH5kzPtqgMAQMgi39YZRPa";

static auto const token =
    "eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT\n"
    "QzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlkYWY2IiwibWFuaWZl\n"
    "c3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZDeE\n"
    "hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG\n"
    "bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2\n"
    "hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1\n"
    "NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj\n"
    "VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ==\n";
}  // namespace validator_data

class ServerInfo_test : public beast::unit_test::suite
{
public:
    static std::unique_ptr<Config>
    makeValidatorConfig()
    {
        auto p = std::make_unique<Config>();
        boost::format toLoad(R"rippleConfig(
[validator_token]
%1%

[validators]
%2%

[port_grpc]
ip = 0.0.0.0
port = 50051

[port_admin]
ip = 0.0.0.0
port = 50052
protocol = wss2
admin = 127.0.0.1
)rippleConfig");

        p->loadFromString(boost::str(
            toLoad % validator_data::token % validator_data::public_key));

        setupConfigForUnitTests(*p);

        return p;
    }

    void
    testServerInfo()
    {
        testcase("server_info");

        using namespace test::jtx;

        {
            Env env(*this);
            auto const serverinfo = env.rpc("server_info");
            BEAST_EXPECT(serverinfo.isMember(jss::result));
            auto const& result = serverinfo[jss::result];
            BEAST_EXPECT(!result.isMember(jss::error));
            BEAST_EXPECT(result[jss::status] == "success");
            BEAST_EXPECT(result.isMember(jss::info));
            auto const& info = result[jss::info];
            BEAST_EXPECT(info.isMember(jss::build_version));
            // Git info is not guaranteed to be present
            if (info.isMember(jss::git))
            {
                auto const& git = info[jss::git];
                BEAST_EXPECT(
                    git.isMember(jss::hash) || git.isMember(jss::branch));
                BEAST_EXPECT(
                    !git.isMember(jss::hash) ||
                    (git[jss::hash].isString() &&
                     git[jss::hash].asString().size() == 40));
                BEAST_EXPECT(
                    !git.isMember(jss::branch) ||
                    (git[jss::branch].isString() &&
                     git[jss::branch].asString().size() != 0));
            }
        }

        {
            Env env(*this);

            // Call NetworkOPs directly and set the admin flag to false.
            auto const result =
                env.app().getOPs().getServerInfo(true, false, 0);
            // Expect that the admin ports are not included in the result.
            auto const& ports = result[jss::ports];
            BEAST_EXPECT(ports.isArray() && ports.size() == 0);
            // Expect that git info is absent
            BEAST_EXPECT(!result.isMember(jss::git));
        }

        {
            Env env(*this, makeValidatorConfig());
            auto const& config = env.app().config();

            auto const rpc_port = config["port_rpc"].get<unsigned int>("port");
            auto const grpc_port =
                config[SECTION_PORT_GRPC].get<unsigned int>("port");
            auto const ws_port = config["port_ws"].get<unsigned int>("port");
            BEAST_EXPECT(grpc_port);
            BEAST_EXPECT(rpc_port);
            BEAST_EXPECT(ws_port);

            auto const result = env.rpc("server_info");
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::info));
            BEAST_EXPECT(
                result[jss::result][jss::info][jss::pubkey_validator] ==
                validator_data::public_key);

            auto const& ports = result[jss::result][jss::info][jss::ports];
            BEAST_EXPECT(ports.isArray() && ports.size() == 3);
            for (auto const& port : ports)
            {
                auto const& proto = port[jss::protocol];
                BEAST_EXPECT(proto.isArray());
                auto const p = port[jss::port].asUInt();
                BEAST_EXPECT(p == rpc_port || p == ws_port || p == grpc_port);
                if (p == grpc_port)
                {
                    BEAST_EXPECT(proto.size() == 1);
                    BEAST_EXPECT(proto[0u].asString() == "grpc");
                }
                if (p == rpc_port)
                {
                    BEAST_EXPECT(proto.size() == 2);
                    BEAST_EXPECT(proto[0u].asString() == "http");
                    BEAST_EXPECT(proto[1u].asString() == "ws2");
                }
                if (p == ws_port)
                {
                    BEAST_EXPECT(proto.size() == 1);
                    BEAST_EXPECT(proto[0u].asString() == "ws");
                }
            }
        }
    }

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
                BEAST_EXPECT(leFlags.size() == 43);

                // test the mapped value of a few arbitrarily chosen flags
                BEAST_EXPECT(leFlags["lsfDisallowXRP"] == 0x00080000);
                BEAST_EXPECT(leFlags["lsfDepositAuth"] == 0x01000000);
                BEAST_EXPECT(
                    leFlags["lsfAllowTrustLineClawback"] == 0x80000000);
                BEAST_EXPECT(leFlags["lsfHighFreeze"] == 0x00800000);
            }

            // test the response fields of the TRANSACTION_FORMATS section
            {
                BEAST_EXPECT(
                    result[jss::result].isMember(jss::TRANSACTION_FORMATS));
                Json::Value const& txnFormats =
                    result[jss::result][jss::TRANSACTION_FORMATS];
                BEAST_EXPECT(txnFormats.size() == 66);

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
                        txnFormats["Clawback"][jss::sfields][0u]
                                  [jss::isMPTSupported] == "MPTSupported");

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
                auto const allLedgerEntries = {
                    "NFTokenOffer",
                    "Check",
                    "DID",
                    "NegativeUNL",
                    "NFTokenPage",
                    "SignerList",
                    "Ticket",
                    "AccountRoot",
                    "DirectoryNode",
                    "Amendments",
                    "LedgerHashes",
                    "Bridge",
                    "Offer",
                    "DepositPreauth",
                    "XChainOwnedClaimID",
                    "RippleState",
                    "FeeSettings",
                    "XChainOwnedCreateAccountClaimID",
                    "Escrow",
                    "PayChannel",
                    "AMM",
                    "MPTokenIssuance",
                    "MPToken",
                    "Oracle",
                    "Credential",
                    "PermissionedDomain",
                    "Delegate",
                    "Vault"};

                for (auto const& s : allLedgerEntries)
                    BEAST_EXPECT(
                        result[jss::result][jss::LEDGER_ENTRIES].isMember(s));

                BEAST_EXPECT(
                    result[jss::result][jss::LEDGER_ENTRIES].size() ==
                    allLedgerEntries.size());

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
        testServerInfo();
        testServerDefinitions();
    }
};

BEAST_DEFINE_TESTSUITE(ServerInfo, rpc, ripple);

}  // namespace test
}  // namespace ripple
