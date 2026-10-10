#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <utility>

namespace xrpl::test {

class TokenPreauth_test : public beast::unit_test::Suite
{
    // There is no transaction that creates these entries yet, so insert them
    // into the open ledger directly, linked into the issuer's owner directory.
    static void
    insertEntry(
        jtx::Env& env,
        Keylet const& keylet,
        jtx::Account const& issuer,
        jtx::Account const& holder,
        MPTID const& issuanceID)
    {
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal) {
            Sandbox sb(&view, TapNone);
            auto const page =
                sb.dirInsert(keylet::ownerDir(issuer.id()), keylet, describeOwnerDir(issuer.id()));
            if (!page)
                return false;
            auto sle = std::make_shared<SLE>(keylet);
            sle->setAccountID(sfAccount, issuer.id());
            sle->setAccountID(sfHolder, holder.id());
            sle->setFieldH192(sfMPTokenIssuanceID, issuanceID);
            sle->setFieldU64(sfOwnerNode, *page);
            sb.insert(sle);
            sb.apply(view);
            return true;
        });
    }

    void
    testRPC()
    {
        testcase("ledger_entry and account_objects");
        using namespace jtx;

        Env env{*this};
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        MPTTester mpt(env, gw, {.holders = {alice, bob}});
        mpt.create({.flags = tfMPTCanLock});
        auto const id = mpt.issuanceID();

        auto const blockKeylet = keylet::tokenBlock(alice, id);
        auto const preauthKeylet = keylet::tokenPreauth(bob, id);
        BEAST_EXPECT(blockKeylet.key != keylet::tokenPreauth(alice, id).key);
        insertEntry(env, blockKeylet, gw, alice, id);
        insertEntry(env, preauthKeylet, gw, bob, id);
        // Raw insertions only live in the open ledger, so query "current".

        auto const byKey = [&](json::StaticString const& type, Account const& holder) {
            json::Value params;
            params[jss::ledger_index] = jss::current;
            params[type][jss::holder] = holder.human();
            params[type][jss::mpt_issuance_id] = to_string(id);
            return env.rpc("json", "ledger_entry", to_string(params))[jss::result];
        };

        // By business key: {holder, mpt_issuance_id}.
        {
            auto const jrr = byKey(jss::token_block, alice);
            BEAST_EXPECT(jrr[jss::index].asString() == to_string(blockKeylet.key));
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::TokenBlock);
            BEAST_EXPECT(jrr[jss::node][sfHolder.jsonName] == alice.human());
        }
        {
            auto const jrr = byKey(jss::token_preauth, bob);
            BEAST_EXPECT(jrr[jss::index].asString() == to_string(preauthKeylet.key));
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::TokenPreauth);
            BEAST_EXPECT(jrr[jss::node][sfHolder.jsonName] == bob.human());
        }

        // By index.
        {
            json::Value params;
            params[jss::ledger_index] = jss::current;
            params[jss::token_block] = to_string(blockKeylet.key);
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::TokenBlock);
        }

        // Wrong type, or the issuer as holder -> entryNotFound.
        BEAST_EXPECT(byKey(jss::token_preauth, alice)[jss::error] == "entryNotFound");
        BEAST_EXPECT(byKey(jss::token_block, bob)[jss::error] == "entryNotFound");
        BEAST_EXPECT(byKey(jss::token_block, gw)[jss::error] == "entryNotFound");

        // Missing holder -> malformed.
        {
            json::Value params;
            params[jss::ledger_index] = jss::current;
            params[jss::token_block][jss::mpt_issuance_id] = to_string(id);
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "malformedRequest");
        }

        // The issuer's block list and pre-authorization list via
        // account_objects.
        for (auto const& [type, keylet] :
             {std::pair{jss::token_block, blockKeylet},
              std::pair{jss::token_preauth, preauthKeylet}})
        {
            json::Value params;
            params[jss::account] = gw.human();
            params[jss::ledger_index] = jss::current;
            params[jss::type] = type;
            auto const jrr = env.rpc("json", "account_objects", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::account_objects].size() == 1);
            BEAST_EXPECT(
                jrr[jss::account_objects][0u][jss::index].asString() == to_string(keylet.key));
        }
    }

public:
    void
    run() override
    {
        testRPC();
    }
};

BEAST_DEFINE_TESTSUITE(TokenPreauth, app, xrpl);

}  // namespace xrpl::test
