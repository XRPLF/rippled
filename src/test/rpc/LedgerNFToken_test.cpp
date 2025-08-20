//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
#include <test/jtx/token.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

namespace test {

class LedgerNFToken_test : public beast::unit_test::suite
{
    void
    testLedgerNFTokenSyntheticFields()
    {
        testcase("Test nftoken_id synthetic field in ledger RPC response");

        using namespace jtx;

        Account const alice{"alice"};

        Env env{*this, FeatureBitset{featureNonFungibleTokensV1}};
        env.fund(XRP(10000), alice);
        env.close();

        // Alice mints a NFT
        uint256 const nftId{token::getNextID(env, alice, 0u, tfTransferable)};
        env(token::mint(alice, 0u), txflags(tfTransferable));
        env.close();

        // Get the ledger with transactions expanded
        Json::Value params;
        params[jss::ledger_index] = env.current()->info().seq - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;

        auto const ledgerResult = env.rpc("json", "ledger", to_string(params));

        // Verify the response has the expected structure
        BEAST_EXPECT(ledgerResult.isMember(jss::result));
        BEAST_EXPECT(ledgerResult[jss::result].isMember(jss::ledger));
        BEAST_EXPECT(ledgerResult[jss::result][jss::ledger].isMember(jss::transactions));
        
        auto const& transactions = ledgerResult[jss::result][jss::ledger][jss::transactions];
        BEAST_EXPECT(transactions.isArray());

        // Find the NFTokenMint transaction
        bool foundNFTokenMint = false;
        for (auto const& tx : transactions)
        {
            if (tx.isMember(jss::tx_json) && 
                tx[jss::tx_json].isMember(jss::TransactionType) &&
                tx[jss::tx_json][jss::TransactionType].asString() == "NFTokenMint")
            {
                foundNFTokenMint = true;
                
                // Verify that the meta contains the nftoken_id field
                BEAST_EXPECT(tx.isMember(jss::meta));
                if (tx.isMember(jss::meta))
                {
                    BEAST_EXPECT(tx[jss::meta].isMember(jss::nftoken_id));
                    
                    // Verify the nftoken_id value matches the expected NFT ID
                    if (tx[jss::meta].isMember(jss::nftoken_id))
                    {
                        uint256 metaNftId;
                        BEAST_EXPECT(metaNftId.parseHex(
                            tx[jss::meta][jss::nftoken_id].asString()));
                        BEAST_EXPECT(metaNftId == nftId);
                    }
                }
                break;
            }
        }
        
        BEAST_EXPECT(foundNFTokenMint);
    }

    void
    testLedgerNFTokenSyntheticFieldsAPIv1()
    {
        testcase("Test nftoken_id synthetic field in ledger RPC response (API v1)");

        using namespace jtx;

        Account const alice{"alice"};

        Env env{*this, FeatureBitset{featureNonFungibleTokensV1}};
        env.fund(XRP(10000), alice);
        env.close();

        // Alice mints a NFT
        uint256 const nftId{token::getNextID(env, alice, 0u, tfTransferable)};
        env(token::mint(alice, 0u), txflags(tfTransferable));
        env.close();

        // Get the ledger with transactions expanded using API v1
        Json::Value params;
        params[jss::ledger_index] = env.current()->info().seq - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        params[jss::api_version] = 1;

        auto const ledgerResult = env.rpc("json", "ledger", to_string(params));

        // Verify the response has the expected structure
        BEAST_EXPECT(ledgerResult.isMember(jss::result));
        BEAST_EXPECT(ledgerResult[jss::result].isMember(jss::ledger));
        BEAST_EXPECT(ledgerResult[jss::result][jss::ledger].isMember(jss::transactions));
        
        auto const& transactions = ledgerResult[jss::result][jss::ledger][jss::transactions];
        BEAST_EXPECT(transactions.isArray());

        // Find the NFTokenMint transaction
        bool foundNFTokenMint = false;
        for (auto const& tx : transactions)
        {
            if (tx.isMember(jss::TransactionType) &&
                tx[jss::TransactionType].asString() == "NFTokenMint")
            {
                foundNFTokenMint = true;
                
                // For API v1, meta is stored in metaData field
                BEAST_EXPECT(tx.isMember(jss::metaData));
                if (tx.isMember(jss::metaData))
                {
                    BEAST_EXPECT(tx[jss::metaData].isMember(jss::nftoken_id));
                    
                    // Verify the nftoken_id value matches the expected NFT ID
                    if (tx[jss::metaData].isMember(jss::nftoken_id))
                    {
                        uint256 metaNftId;
                        BEAST_EXPECT(metaNftId.parseHex(
                            tx[jss::metaData][jss::nftoken_id].asString()));
                        BEAST_EXPECT(metaNftId == nftId);
                    }
                }
                break;
            }
        }
        
        BEAST_EXPECT(foundNFTokenMint);
    }

public:
    void
    run() override
    {
        testLedgerNFTokenSyntheticFields();
        testLedgerNFTokenSyntheticFieldsAPIv1();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerNFToken, rpc, ripple);

}  // namespace test

}  // namespace ripple