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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/rdb/Vacuum.h>
#include <xrpld/core/Config.h>
#include <xrpld/core/ConfigSections.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/net/RPCCall.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/protocol/BuildInfo.h>

#ifdef ENABLE_TESTS
#include <test/unit_test/multi_runner.h>

#include <xrpl/beast/unit_test/match.h>
#endif  // ENABLE_TESTS
#include <boost/process.hpp>
#include <boost/program_options.hpp>

#include <google/protobuf/stubs/common.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>

#if BOOST_OS_WINDOWS
#include <sys/timeb.h>
#include <sys/types.h>
#endif

// Do we know the platform we're compiling on? If you're adding new platforms
// modify this check accordingly.
#if !BOOST_OS_LINUX && !BOOST_OS_WINDOWS && !BOOST_OS_MACOS
#error Supported platforms are: Linux, Windows and MacOS
#endif

// Ensure that precisely one platform is detected.
#if (BOOST_OS_LINUX && (BOOST_OS_WINDOWS || BOOST_OS_MACOS)) || \
    (BOOST_OS_MACOS && (BOOST_OS_WINDOWS || BOOST_OS_LINUX)) || \
    (BOOST_OS_WINDOWS && (BOOST_OS_LINUX || BOOST_OS_MACOS))
#error Multiple supported platforms appear active at once
#endif

#ifdef ENABLE_VOIDSTAR
#include "antithesis_instrumentation.h"
#endif

namespace po = boost::program_options;

namespace ripple {

bool
adjustDescriptorLimit(int needed, beast::Journal j)
{
#ifdef RLIMIT_NOFILE
    // Get the current limit, then adjust it to what we need.
    struct rlimit rl;

    int available = 0;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
    {
        // If the limit is infinite, then we are good.
        if (rl.rlim_cur == RLIM_INFINITY)
            available = needed;
        else
            available = rl.rlim_cur;

        if (available < needed)
        {
            // Ignore the rlim_max, as the process may
            // be configured to override it anyways. We
            // ask for the number descriptors we need.
            rl.rlim_cur = needed;

            if (setrlimit(RLIMIT_NOFILE, &rl) == 0)
                available = rl.rlim_cur;
        }
    }

    if (needed > available)
    {
        j.fatal() << "Insufficient number of file descriptors: " << needed
                  << " are needed, but only " << available << " are available.";

        std::cerr << "Insufficient number of file descriptors: " << needed
                  << " are needed, but only " << available
                  << " are available.\n";

        return false;
    }
#endif

    return true;
}

void
printHelp(po::options_description const& desc)
{
    std::cerr
        << systemName() << "d [options] <command> <params>\n"
        << desc << std::endl
        << "Commands: \n"
           "     account_currencies <account> [<ledger>]\n"
           "     account_info <account>|<key> [<ledger>]\n"
           "     account_lines <account> <account>|\"\" [<ledger>]\n"
           "     account_channels <account> <account>|\"\" [<ledger>]\n"
           "     account_objects <account> [<ledger>]\n"
           "     account_offers <account>|<account_public_key> [<ledger>]\n"
           "     account_tx accountID [ledger_index_min [ledger_index_max "
           "[limit "
           "]]] [binary]\n"
           "     book_changes [<ledger hash|id>]\n"
           "     book_offers <taker_pays> <taker_gets> [<taker [<ledger> "
           "[<limit> [<proof> [<marker>]]]]]\n"
           "     can_delete [<ledgerid>|<ledgerhash>|now|always|never]\n"
           "     channel_authorize <private_key> <channel_id> <drops>\n"
           "     channel_verify <public_key> <channel_id> <drops> <signature>\n"
           "     connect <ip> [<port>]\n"
           "     consensus_info\n"
           "     deposit_authorized <source_account> <destination_account> "
           "[<ledger> [<credentials>, ...]]\n"
           "     feature [<feature> [accept|reject]]\n"
           "     fetch_info [clear]\n"
           "     gateway_balances [<ledger>] <issuer_account> [ <hotwallet> [ "
           "<hotwallet> ]]\n"
           "     get_counts\n"
           "     json <method> <json>\n"
           "     ledger [<id>|current|closed|validated] [full]\n"
           "     ledger_accept\n"
           "     ledger_cleaner\n"
           "     ledger_closed\n"
           "     ledger_current\n"
           "     ledger_request <ledger>\n"
           "     log_level [[<partition>] <severity>]\n"
           "     logrotate\n"
           "     manifest <public_key>\n"
           "     peers\n"
           "     ping\n"
           "     random\n"
           "     peer_reservations_add <public_key> [<description>]\n"
           "     peer_reservations_del <public_key>\n"
           "     peer_reservations_list\n"
           "     ripple ...\n"
           "     ripple_path_find <json> [<ledger>]\n"
           "     server_definitions [<hash>]\n"
           "     server_info [counters]\n"
           "     server_state [counters]\n"
           "     sign <private_key> <tx_json> [offline]\n"
           "     sign_for <signer_address> <signer_private_key> <tx_json> "
           "[offline]\n"
           "     stop\n"
           "     simulate [<tx_blob>|<tx_json>] [<binary>]\n"
           "     submit <tx_blob>|[<private_key> <tx_json>]\n"
           "     submit_multisigned <tx_json>\n"
           "     tx <id>\n"
           "     validation_create [<seed>|<pass_phrase>|<key>]\n"
           "     validator_info\n"
           "     validators\n"
           "     validator_list_sites\n"
           "     version\n"
           "     wallet_propose [<passphrase>]\n";
}

//------------------------------------------------------------------------------

#ifdef ENABLE_TESTS
/* simple unit test selector that allows a comma separated list
 * of selectors
 */
class multi_selector
{
private:
    std::vector<beast::unit_test::selector> selectors_;

public:
    explicit multi_selector(std::string const& patterns = "")
    {
        std::vector<std::string> v;
        boost::split(v, patterns, boost::algorithm::is_any_of(","));
        selectors_.reserve(v.size());
        std::for_each(v.begin(), v.end(), [this](std::string s) {
            boost::trim(s);
            if (selectors_.empty() || !s.empty())
                selectors_.emplace_back(
                    beast::unit_test::selector::automatch, s);
        });
    }

    bool
    operator()(beast::unit_test::suite_info const& s)
    {
        for (auto& sel : selectors_)
            if (sel(s))
                return true;
        return false;
    }

    std::size_t
    size() const
    {
        return selectors_.size();
    }
};

namespace test {
extern std::atomic<bool> envUseIPv4;
}

template <class Runner>
static bool
anyMissing(Runner& runner, multi_selector const& pred)
{
    if (runner.tests() == 0)
    {
        runner.add_failures(1);
        std::cout << "Failed: No tests run" << std::endl;
        return true;
    }
    if (runner.suites() < pred.size())
    {
        auto const missing = pred.size() - runner.suites();
        runner.add_failures(missing);
        std::cout << "Failed: " << missing
                  << " filters did not match any existing test suites"
                  << std::endl;
        return true;
    }
    return false;
}

static int
runUnitTests(
    std::string const& pattern,
    std::string const& argument,
    bool quiet,
    bool log,
    bool child,
    bool ipv6,
    std::size_t num_jobs,
    int argc,
    char** argv)
{
    using namespace beast::unit_test;
    using namespace ripple::test;

    ripple::test::envUseIPv4 = (!ipv6);

    if (!child && num_jobs == 1)
    {
        multi_runner_parent parent_runner;

        multi_runner_child child_runner{num_jobs, quiet, log};
        child_runner.arg(argument);
        multi_selector pred(pattern);
        auto const any_failed =
            child_runner.run_multi(pred) || anyMissing(child_runner, pred);

        if (any_failed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
    if (!child)
    {
        multi_runner_parent parent_runner;
        std::vector<boost::process::child> children;

        std::string const exe_name = argv[0];
        std::vector<std::string> args;
        {
            args.reserve(argc);
            for (int i = 1; i < argc; ++i)
                args.emplace_back(argv[i]);
            args.emplace_back("--unittest-child");
        }

        for (std::size_t i = 0; i < num_jobs; ++i)
            children.emplace_back(
                boost::process::exe = exe_name, boost::process::args = args);

        int bad_child_exits = 0;
        int terminated_child_exits = 0;
        for (auto& c : children)
        {
            try
            {
                c.wait();
                if (c.exit_code())
                    ++bad_child_exits;
            }
            catch (...)
            {
                // wait throws if process was terminated with a signal
                ++bad_child_exits;
                ++terminated_child_exits;
            }
        }

        parent_runner.add_failures(terminated_child_exits);
        anyMissing(parent_runner, multi_selector(pattern));

        if (parent_runner.any_failed() || bad_child_exits)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
    else
    {
        // child
        multi_runner_child runner{num_jobs, quiet, log};
        runner.arg(argument);
        auto const anyFailed = runner.run_multi(multi_selector(pattern));

        if (anyFailed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
}

#endif  // ENABLE_TESTS
//------------------------------------------------------------------------------

int
run(int argc, char** argv)
{
    using namespace std;

    beast::setCurrentThreadName(
        "rippled: main " + BuildInfo::getVersionString());

    po::variables_map vm;

    std::string importText;
    {
        importText += "Import an existing node database (specified in the [";
        importText += ConfigSection::importNodeDatabase();
        importText += "] configuration file section) into the current ";
        importText += "node database (specified in the [";
        importText += ConfigSection::nodeDatabase();
        importText += "] configuration file section).";
    }

    // Set up option parsing.
    //
    po::options_description gen("General Options");
    gen.add_options()(
        "conf", po::value<std::string>(), "Specify the configuration file.")(
        "debug", "Enable normally suppressed debug logging")(
        "help,h", "Display this message.")(
        "newnodeid", "Generate a new node identity for this server.")(
        "nodeid",
        po::value<std::string>(),
        "Specify the node identity for this server.")(
        "quorum",
        po::value<std::size_t>(),
        "Override the minimum validation quorum.")(
        "silent", "No output to the console after startup.")(
        "standalone,a", "Run with no peers.")("verbose,v", "Verbose logging.")

        ("force_ledger_present_range",
         po::value<std::string>(),
         "Specify the range of present ledgers for testing purposes. Min and "
         "max values are comma separated.")(
            "version", "Display the build version.");

    po::options_description data("Ledger/Data Options");
    data.add_options()("import", importText.c_str())(
        "ledger",
        po::value<std::string>(),
        "Load the specified ledger and start from the value given.")(
        "ledgerfile",
        po::value<std::string>(),
        "Load the specified ledger file.")(
        "load", "Load the current ledger from the local DB.")(
        "net", "Get the initial ledger from the network.")(
        "replay", "Replay a ledger close.")(
        "trap_tx_hash",
        po::value<std::string>(),
        "Trap a specific transaction during replay.")(
        "start", "Start from a fresh Ledger.")(
        "vacuum", "VACUUM the transaction db.")(
        "valid", "Consider the initial ledger a valid network ledger.");

    po::options_description rpc("RPC Client Options");
    rpc.add_options()(
        "rpc",
        "Perform rpc command - see below for available commands. "
        "This is assumed if any positional parameters are provided.")(
        "rpc_ip",
        po::value<std::string>(),
        "Specify the IP address for RPC command. "
        "Format: <ip-address>[':'<port-number>]")(
        "rpc_port",
        po::value<std::uint16_t>(),
        "DEPRECATED: include with rpc_ip instead. "
        "Specify the port number for RPC command.");

#ifdef ENABLE_TESTS
    po::options_description test("Unit Test Options");
    test.add_options()(
        "quiet,q",
        "Suppress test suite messages, "
        "including suite/case name (at start) and test log messages.")(
        "unittest,u",
        po::value<std::string>()->implicit_value(""),
        "Perform unit tests. The optional argument specifies one or "
        "more comma-separated selectors. Each selector specifies a suite name, "
        "suite name prefix, full-name (lib.module.suite), module, or library "
        "(checked in that order).")(
        "unittest-arg",
        po::value<std::string>()->implicit_value(""),
        "Supplies an argument string to unit tests. If provided, this argument "
        "is made available to each suite that runs. Interpretation of the "
        "argument is handled individually by any suite that accesses it -- "
        "as such, it typically only make sense to provide this when running "
        "a single suite.")(
        "unittest-ipv6",
        "Use IPv6 localhost when running unittests (default is IPv4).")(
        "unittest-log",
        "Force unit test log message output. Only useful in combination with "
        "--quiet, in which case log messages will print but suite/case names "
        "will not.")(
        "unittest-jobs",
        po::value<std::size_t>(),
        "Number of unittest jobs to run in parallel (child processes).");
#endif  // ENABLE_TESTS

    // These are hidden options, not intended to be shown in the usage/help
    // message
    po::options_description hidden("Hidden Options");
    hidden.add_options()(
        "parameters",
        po::value<vector<string>>(),
        "Specify rpc command and parameters. This option must be repeated "
        "for each command/param. Positional parameters also serve this "
        "purpose, "
        "so this option is not needed for users")
#ifdef ENABLE_TESTS
        ("unittest-child",
         "For internal use only when spawning child unit test processes.")
#else
        ("unittest", "Disabled in this build.")(
            "unittest-child", "Disabled in this build.")
#endif  // ENABLE_TESTS
            ("fg", "Deprecated: server always in foreground mode.");

    // Interpret positional arguments as --parameters.
    po::positional_options_description p;
    p.add("parameters", -1);

    po::options_description all;
    all.add(gen)
        .add(rpc)
        .add(data)
#ifdef ENABLE_TESTS
        .add(test)
#endif  // ENABLE_TESTS
        .add(hidden);

    po::options_description desc;
    desc.add(gen)
        .add(rpc)
        .add(data)
#ifdef ENABLE_TESTS
        .add(test)
#endif  // ENABLE_TESTS
        ;

    // Parse options, if no error.
    try
    {
        po::store(
            po::command_line_parser(argc, argv)
                .options(all)   // Parse options.
                .positional(p)  // Remainder as --parameters.
                .run(),
            vm);
        po::notify(vm);  // Invoke option notify functions.
    }
    catch (std::exception const& ex)
    {
        std::cerr << "rippled: " << ex.what() << std::endl;
        std::cerr << "Try 'rippled --help' for a list of options." << std::endl;
        return 1;
    }

    if (vm.count("help"))
    {
        printHelp(desc);
        return 0;
    }

    if (vm.count("version"))
    {
        std::cout << "rippled version " << BuildInfo::getVersionString()
                  << std::endl;
#ifdef GIT_COMMIT_HASH
        std::cout << "Git commit hash: " << GIT_COMMIT_HASH << std::endl;
#endif
#ifdef GIT_BRANCH
        std::cout << "Git build branch: " << GIT_BRANCH << std::endl;
#endif
        return 0;
    }

#ifndef ENABLE_TESTS
    if (vm.count("unittest") || vm.count("unittest-child"))
    {
        std::cerr << "rippled: Tests disabled in this build." << std::endl;
        std::cerr << "Try 'rippled --help' for a list of options." << std::endl;
        return 1;
    }
#else
    // Run the unit tests if requested.
    // The unit tests will exit the application with an appropriate return code.
    //
    if (vm.count("unittest"))
    {
        std::string argument;

        if (vm.count("unittest-arg"))
            argument = vm["unittest-arg"].as<std::string>();

        std::size_t numJobs = 1;
        bool unittestChild = false;
        if (vm.count("unittest-jobs"))
            numJobs = std::max(numJobs, vm["unittest-jobs"].as<std::size_t>());
        unittestChild = bool(vm.count("unittest-child"));

        return runUnitTests(
            vm["unittest"].as<std::string>(),
            argument,
            bool(vm.count("quiet")),
            bool(vm.count("unittest-log")),
            unittestChild,
            bool(vm.count("unittest-ipv6")),
            numJobs,
            argc,
            argv);
    }
    // LCOV_EXCL_START
    else
    {
        if (vm.count("unittest-jobs"))
        {
            // unittest jobs only makes sense with `unittest`
            std::cerr << "rippled: '--unittest-jobs' specified without "
                         "'--unittest'.\n";
            std::cerr << "To run the unit tests the '--unittest' option must "
                         "be present.\n";
            return 1;
        }
    }
#endif  // ENABLE_TESTS

    auto config = std::make_unique<Config>();

    auto configFile =
        vm.count("conf") ? vm["conf"].as<std::string>() : std::string();

    // config file, quiet flag.
    config->setup(
        configFile,
        bool(vm.count("quiet")),
        bool(vm.count("silent")),
        bool(vm.count("standalone")));

    if (vm.count("vacuum"))
    {
        if (config->standalone())
        {
            std::cerr << "vacuum not applicable in standalone mode.\n";
            return -1;
        }

        try
        {
            auto setup = setup_DatabaseCon(*config);
            if (!doVacuumDB(setup, config->journal()))
                return -1;
        }
        catch (std::exception const& e)
        {
            std::cerr << "exception " << e.what() << " in function " << __func__
                      << std::endl;
            return -1;
        }

        return 0;
    }

    if (vm.contains("force_ledger_present_range"))
    {
        try
        {
            auto const r = [&vm]() -> std::vector<std::uint32_t> {
                std::vector<std::string> strVec;
                boost::split(
                    strVec,
                    vm["force_ledger_present_range"].as<std::string>(),
                    boost::algorithm::is_any_of(","));
                std::vector<std::uint32_t> result;
                for (auto& s : strVec)
                {
                    boost::trim(s);
                    if (!s.empty())
                        result.push_back(std::stoi(s));
                }
                return result;
            }();

            if (r.size() == 2)
            {
                if (r[0] > r[1])
                {
                    throw std::runtime_error(
                        "Invalid force_ledger_present_range parameter");
                }
                config->FORCED_LEDGER_RANGE_PRESENT.emplace(r[0], r[1]);
            }
            else
            {
                throw std::runtime_error(
                    "Invalid force_ledger_present_range parameter");
            }
        }
        catch (std::exception const& e)
        {
            std::cerr << "invalid 'force_ledger_present_range' parameter. The "
                         "parameter must be two numbers separated by a comma. "
                         "The first number must be <= the second."
                      << std::endl;
            return -1;
        }
    }

    if (vm.count("start"))
    {
        config->START_UP = Config::FRESH;
    }

    if (vm.count("import"))
        config->doImport = true;

    if (vm.count("ledger"))
    {
        config->START_LEDGER = vm["ledger"].as<std::string>();
        if (vm.count("replay"))
        {
            config->START_UP = Config::REPLAY;
            if (vm.count("trap_tx_hash"))
            {
                uint256 tmp = {};
                auto hash = vm["trap_tx_hash"].as<std::string>();
                if (tmp.parseHex(hash))
                {
                    config->TRAP_TX_HASH = tmp;
                }
                else
                {
                    std::cerr << "Trap parameter was ill-formed, expected "
                                 "valid transaction hash but received: "
                              << hash << std::endl;
                    return -1;
                }
            }
        }
        else
            config->START_UP = Config::LOAD;
    }
    else if (vm.count("ledgerfile"))
    {
        config->START_LEDGER = vm["ledgerfile"].as<std::string>();
        config->START_UP = Config::LOAD_FILE;
    }
    else if (vm.count("load") || config->FAST_LOAD)
    {
        config->START_UP = Config::LOAD;
    }

    if (vm.count("trap_tx_hash") && vm.count("replay") == 0)
    {
        std::cerr << "Cannot use trap option without replay option"
                  << std::endl;
        return -1;
    }

    if (vm.count("net") && !config->FAST_LOAD)
    {
        if ((config->START_UP == Config::LOAD) ||
            (config->START_UP == Config::REPLAY))
        {
            std::cerr << "Net and load/replay options are incompatible"
                      << std::endl;
            return -1;
        }

        config->START_UP = Config::NETWORK;
    }

    if (vm.count("valid"))
    {
        config->START_VALID = true;
    }

    // Override the RPC destination IP address. This must
    // happen after the config file is loaded.
    if (vm.count("rpc_ip"))
    {
        auto endpoint = beast::IP::Endpoint::from_string_checked(
            vm["rpc_ip"].as<std::string>());
        if (!endpoint)
        {
            std::cerr << "Invalid rpc_ip = " << vm["rpc_ip"].as<std::string>()
                      << "\n";
            return -1;
        }

        if (endpoint->port() == 0)
        {
            std::cerr << "No port specified in rpc_ip.\n";
            if (vm.count("rpc_port"))
            {
                std::cerr << "WARNING: using deprecated rpc_port param.\n";
                try
                {
                    endpoint =
                        endpoint->at_port(vm["rpc_port"].as<std::uint16_t>());
                    if (endpoint->port() == 0)
                        throw std::domain_error("0");
                }
                catch (std::exception const& e)
                {
                    std::cerr << "Invalid rpc_port = " << e.what() << "\n";
                    return -1;
                }
            }
            else
                return -1;
        }

        config->rpc_ip = std::move(*endpoint);
    }

    if (vm.count("quorum"))
    {
        try
        {
            config->VALIDATION_QUORUM = vm["quorum"].as<std::size_t>();
            if (config->VALIDATION_QUORUM == std::size_t{})
            {
                throw std::domain_error("0");
            }
        }
        catch (std::exception const& e)
        {
            std::cerr << "Invalid value specified for --quorum (" << e.what()
                      << ")\n";
            return -1;
        }
    }

    // Construct the logs object at the configured severity
    using namespace beast::severities;
    Severity thresh = kInfo;

    if (vm.count("quiet"))
        thresh = kFatal;
    else if (vm.count("verbose"))
        thresh = kTrace;

    auto logs = std::make_unique<Logs>(thresh);

    // No arguments. Run server.
    if (!vm.count("parameters"))
    {
        // TODO: this comment can be removed in a future release -
        // say 1.7 or higher
        if (config->had_trailing_comments())
        {
            JLOG(logs->journal("Application").warn())
                << "Trailing comments were seen in your config file. "
                << "The treatment of inline/trailing comments has changed "
                   "recently. "
                << "Any `#` characters NOT intended to delimit comments should "
                   "be "
                << "preceded by a \\";
        }

        // We want at least 1024 file descriptors. We'll
        // tweak this further.
        if (!adjustDescriptorLimit(1024, logs->journal("Application")))
            return -1;

        if (vm.count("debug"))
            setDebugLogSink(logs->makeSink("Debug", beast::severities::kTrace));

        auto app = make_Application(
            std::move(config), std::move(logs), std::make_unique<TimeKeeper>());

        if (!app->setup(vm))
            return -1;

        // With our configuration parsed, ensure we have
        // enough file descriptors available:
        if (!adjustDescriptorLimit(
                app->fdRequired(), app->logs().journal("Application")))
            return -1;

        // Start the server
        app->start(true /*start timers*/);

        // Block until we get a stop RPC.
        app->run();

        return 0;
    }

    // We have an RPC command to process:
    beast::setCurrentThreadName("rippled: rpc");
    return RPCCall::fromCommandLine(
        *config, vm["parameters"].as<std::vector<std::string>>(), *logs);
    // LCOV_EXCL_STOP
}

}  // namespace ripple

int
main(int argc, char** argv)
{
#if BOOST_OS_WINDOWS
    {
        // Work around for https://svn.boost.org/trac/boost/ticket/10657
        // Reported against boost version 1.56.0.  If an application's
        // first call to GetTimeZoneInformation is from a coroutine, an
        // unhandled exception is generated.  A workaround is to call
        // GetTimeZoneInformation at least once before launching any
        // coroutines.  At the time of this writing the _ftime call is
        // used to initialize the timezone information.
        struct _timeb t;
#ifdef _INC_TIME_INL
        _ftime_s(&t);
#else
        _ftime(&t);
#endif
    }
#endif

    atexit(&google::protobuf::ShutdownProtobufLibrary);

    return ripple::run(argc, argv);
}
