#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/temp_dir.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/erase.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/system/detail/error_code.hpp>

#include <cassert>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace xrpl {

class LedgerLoad_test : public beast::unit_test::Suite
{
    auto static ledgerConfig(
        std::unique_ptr<Config> cfg,
        std::string const& dbPath,
        std::string const& ledger,
        StartUpType type,
        std::optional<uint256> trapTxHash)
    {
        cfg->startLedger = ledger;
        cfg->startUp = type;
        cfg->trapTxHash = trapTxHash;
        assert(!dbPath.empty());
        cfg->legacy("database_path", dbPath);
        return cfg;
    }

    // setup for test cases
    struct SetupData
    {
        std::string const dbPath;
        // NOLINTBEGIN(readability-redundant-member-init)
        std::string ledgerFile = {};
        json::Value ledger = {};
        json::Value hashes = {};
        uint256 trapTxHash = {};
        // NOLINTEND(readability-redundant-member-init)
    };

    SetupData
    setupLedger(beast::TempDir const& td)
    {
        using namespace test::jtx;
        SetupData retval = {.dbPath = td.path()};

        retval.ledgerFile = td.file("ledgerdata.json");

        Env env{*this};
        std::optional<Account> prev;

        for (auto i = 0; i < 20; ++i)
        {
            Account const acct{"A" + std::to_string(i)};
            env.fund(XRP(10000), acct);
            env.close();
            if (i > 0 && BEAST_EXPECT(prev.has_value()))
            {
                env.trust(acct["USD"](1000), *prev);  // NOLINT(bugprone-unchecked-optional-access)
                env(pay(
                    acct, *prev, acct["USD"](5)));  // NOLINT(bugprone-unchecked-optional-access)
            }
            env(offer(acct, XRP(100), acct["USD"](1)));
            env.close();
            prev.emplace(acct);
        }

        retval.ledger = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(retval.ledger[jss::ledger][jss::accountState].size() == 102);

        retval.hashes = [&] {
            for (auto const& it : retval.ledger[jss::ledger][jss::accountState])
            {
                if (it[sfLedgerEntryType.fieldName] == jss::LedgerHashes)
                    return it[sfHashes.fieldName];
            }
            return json::Value{};
        }();

        BEAST_EXPECT(retval.hashes.size() == 41);
        retval.trapTxHash = [&]() {
            auto const txs = env.rpc(
                "ledger", std::to_string(41), "tx")[jss::result][jss::ledger][jss::transactions];
            BEAST_EXPECT(txs.isArray() && txs.size() > 0);
            uint256 tmp;
            BEAST_EXPECT(tmp.parseHex(txs[0u][jss::hash].asString()));
            return tmp;
        }();

        // write this ledger data to a file.
        std::ofstream o(retval.ledgerFile, std::ios::out | std::ios::trunc);
        o << to_string(retval.ledger);
        o.close();
        return retval;
    }

    void
    testLoad(SetupData const& sd)
    {
        testcase("Load a saved ledger");
        using namespace test::jtx;

        // create a new env with the ledger file specified for startup
        Env env(
            *this,
            envconfig(ledgerConfig, sd.dbPath, sd.ledgerFile, StartUpType::LoadFile, std::nullopt),
            nullptr,
            beast::Severity::Disabled);
        auto jrb = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(
            sd.ledger[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

    void
    testBadFiles(SetupData const& sd)
    {
        testcase("Load ledger: Bad Files");
        using namespace test::jtx;
        using namespace boost::filesystem;

        // empty path
        except([&] {
            Env const env(
                *this,
                envconfig(ledgerConfig, sd.dbPath, "", StartUpType::LoadFile, std::nullopt),
                nullptr,
                beast::Severity::Disabled);
        });

        // file does not exist
        except([&] {
            Env const env(
                *this,
                envconfig(
                    ledgerConfig, sd.dbPath, "badfile.json", StartUpType::LoadFile, std::nullopt),
                nullptr,
                beast::Severity::Disabled);
        });

        // make a corrupted version of the ledger file (last 10 bytes removed).
        boost::system::error_code ec;
        auto ledgerFileCorrupt = boost::filesystem::path{sd.dbPath} / "ledgerdata_bad.json";
        copy_file(sd.ledgerFile, ledgerFileCorrupt, copy_options::overwrite_existing, ec);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        auto filesize = file_size(ledgerFileCorrupt, ec);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        resize_file(ledgerFileCorrupt, filesize - 10, ec);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;

        except([&] {
            Env const env(
                *this,
                envconfig(
                    ledgerConfig,
                    sd.dbPath,
                    ledgerFileCorrupt.string(),
                    StartUpType::LoadFile,
                    std::nullopt),
                nullptr,
                beast::Severity::Disabled);
        });
    }

    void
    testLoadByHash(SetupData const& sd)
    {
        testcase("Load by hash");
        using namespace test::jtx;

        // create a new env with the ledger hash specified for startup
        auto ledgerHash = to_string(sd.hashes[sd.hashes.size() - 1]);
        boost::erase_all(ledgerHash, "\"");
        Env env(
            *this,
            envconfig(ledgerConfig, sd.dbPath, ledgerHash, StartUpType::Load, std::nullopt),
            nullptr,
            beast::Severity::Disabled);
        auto jrb = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(jrb[jss::ledger][jss::accountState].size() == 98);
        BEAST_EXPECT(
            jrb[jss::ledger][jss::accountState].size() <=
            sd.ledger[jss::ledger][jss::accountState].size());
    }

    void
    testReplay(SetupData const& sd)
    {
        testcase("Load and replay by hash");
        using namespace test::jtx;

        // create a new env with the ledger hash specified for startup
        auto ledgerHash = to_string(sd.hashes[sd.hashes.size() - 1]);
        boost::erase_all(ledgerHash, "\"");
        Env env(
            *this,
            envconfig(ledgerConfig, sd.dbPath, ledgerHash, StartUpType::Replay, std::nullopt),
            nullptr,
            beast::Severity::Disabled);
        auto const jrb = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(jrb[jss::ledger][jss::accountState].size() == 97);
        // in replace mode do not automatically accept the ledger being replayed

        env.close();
        auto const closed = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(closed[jss::ledger][jss::accountState].size() == 98);
        BEAST_EXPECT(
            closed[jss::ledger][jss::accountState].size() <=
            sd.ledger[jss::ledger][jss::accountState].size());
    }

    void
    testReplayTx(SetupData const& sd)
    {
        testcase("Load and replay transaction by hash");
        using namespace test::jtx;

        // create a new env with the ledger hash specified for startup
        auto ledgerHash = to_string(sd.hashes[sd.hashes.size() - 1]);
        boost::erase_all(ledgerHash, "\"");
        Env env(
            *this,
            envconfig(ledgerConfig, sd.dbPath, ledgerHash, StartUpType::Replay, sd.trapTxHash),
            nullptr,
            beast::Severity::Disabled);
        auto const jrb = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(jrb[jss::ledger][jss::accountState].size() == 97);
        // in replace mode do not automatically accept the ledger being replayed

        env.close();
        auto const closed = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(closed[jss::ledger][jss::accountState].size() == 98);
        BEAST_EXPECT(
            closed[jss::ledger][jss::accountState].size() <=
            sd.ledger[jss::ledger][jss::accountState].size());
    }

    void
    testReplayTxFail(SetupData const& sd)
    {
        testcase("Load and replay transaction by hash failure");
        using namespace test::jtx;

        // create a new env with the ledger hash specified for startup
        auto ledgerHash = to_string(sd.hashes[sd.hashes.size() - 1]);
        boost::erase_all(ledgerHash, "\"");
        try
        {
            // will throw an exception, because we cannot load a ledger for
            // replay when trapTxHash is set to an invalid transaction
            Env const env(
                *this,
                envconfig(ledgerConfig, sd.dbPath, ledgerHash, StartUpType::Replay, ~sd.trapTxHash),
                nullptr,
                beast::Severity::Disabled);
            BEAST_EXPECT(false);
        }
        catch (std::runtime_error const&)
        {
            BEAST_EXPECT(true);
        }
        catch (...)
        {
            BEAST_EXPECT(false);
        }
    }

    void
    testLoadLatest(SetupData const& sd)
    {
        testcase("Load by keyword");
        using namespace test::jtx;

        // create a new env with the ledger "latest" specified for startup
        Env env(
            *this,
            envconfig(ledgerConfig, sd.dbPath, "latest", StartUpType::Load, std::nullopt),
            nullptr,
            beast::Severity::Disabled);
        auto jrb = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(
            sd.ledger[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

    void
    testLoadIndex(SetupData const& sd)
    {
        testcase("Load by index");
        using namespace test::jtx;

        // create a new env with specific ledger index at startup
        Env env(
            *this,
            envconfig(ledgerConfig, sd.dbPath, "43", StartUpType::Load, std::nullopt),
            nullptr,
            beast::Severity::Disabled);
        auto jrb = env.rpc("ledger", "current", "full")[jss::result];
        BEAST_EXPECT(
            sd.ledger[jss::ledger][jss::accountState].size() ==
            jrb[jss::ledger][jss::accountState].size());
    }

public:
    void
    run() override
    {
        beast::TempDir const td;
        auto sd = setupLedger(td);

        // test cases
        testLoad(sd);
        testBadFiles(sd);
        testLoadByHash(sd);
        testReplay(sd);
        testReplayTx(sd);
        testReplayTxFail(sd);
        testLoadLatest(sd);
        testLoadIndex(sd);
    }
};

BEAST_DEFINE_TESTSUITE(LedgerLoad, app, xrpl);

}  // namespace xrpl
