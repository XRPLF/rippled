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

#include <BeastConfig.h>

#include <ripple/basics/Log.h>
#include <ripple/protocol/digest.h>
#include <ripple/app/main/Application.h>
#include <ripple/basics/CheckLibraryVersions.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/Sustain.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/core/TerminateHandler.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/crypto/csprng.h>
#include <ripple/json/to_string.h>
#include <ripple/net/RPCCall.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/protocol/BuildInfo.h>
#include <ripple/beast/clock/basic_seconds_clock.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/beast/core/Time.h>
#include <ripple/beast/utility/Debug.h>

#include <beast/unit_test/dstream.hpp>
#include <beast/unit_test/global_suites.hpp>
#include <beast/unit_test/match.hpp>
#include <beast/unit_test/reporter.hpp>
#include <test/unit_test/multi_runner.h>

#include <google/protobuf/stubs/common.h>

#include <boost/program_options.hpp>
#include <boost/version.hpp>

#include <cstdlib>
#include <iostream>
#include <utility>
#include <stdexcept>


#if BOOST_VERSION >= 106400
#define HAS_BOOST_PROCESS 1
#endif

#if HAS_BOOST_PROCESS
#include <boost/process.hpp>
#endif

namespace po = boost::program_options;

namespace ripple {

boost::filesystem::path
getEntropyFile(Config const& config)
{
    auto const path = config.legacy("database_path");
    if (path.empty ())
        return {};
    return boost::filesystem::path (path) / "random.seed";
}

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
        j.fatal() <<
            "Insufficient number of file descriptors: " <<
            needed << " are needed, but only " <<
            available << " are available.";

        std::cerr <<
            "Insufficient number of file descriptors: " <<
            needed << " are needed, but only " <<
            available << " are available.\n";

        return false;
    }
#endif

    return true;
}

void printHelp (const po::options_description& desc)
{
    std::cerr
        << systemName () << "d [options] <command> <params>\n"
        << desc << std::endl
        << "Commands: \n"
           "     account_currencies <account> [<ledger>] [strict]\n"
           "     account_info <account>|<seed>|<pass_phrase>|<key> [<ledger>] [strict]\n"
           "     account_lines <account> <account>|\"\" [<ledger>]\n"
           "     account_channels <account> <account>|\"\" [<ledger>]\n"
           "     account_objects <account> [<ledger>] [strict]\n"
           "     account_offers <account>|<account_public_key> [<ledger>]\n"
           "     account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary] [count] [descending]\n"
           "     book_offers <taker_pays> <taker_gets> [<taker [<ledger> [<limit> [<proof> [<marker>]]]]]\n"
           "     can_delete [<ledgerid>|<ledgerhash>|now|always|never]\n"
           "     channel_authorize <private_key> <channel_id> <drops>\n"
           "     channel_verify <public_key> <channel_id> <drops> <signature>\n"
           "     connect <ip> [<port>]\n"
           "     consensus_info\n"
           "     feature [<feature> [accept|reject]]\n"
           "     fetch_info [clear]\n"
           "     gateway_balances [<ledger>] <issuer_account> [ <hotwallet> [ <hotwallet> ]]\n"
           "     get_counts\n"
           "     json <method> <json>\n"
           "     ledger [<id>|current|closed|validated] [full]\n"
           "     ledger_accept\n"
           "     ledger_closed\n"
           "     ledger_current\n"
           "     ledger_request <ledger>\n"
           "     log_level [[<partition>] <severity>]\n"
           "     logrotate \n"
           "     peers\n"
           "     ping\n"
           "     random\n"
           "     ripple ...\n"
           "     ripple_path_find <json> [<ledger>]\n"
           "     version\n"
           "     server_info\n"
           "     sign <private_key> <tx_json> [offline]\n"
           "     sign_for <signer_address> <signer_private_key> <tx_json> [offline]\n"
           "     stop\n"
           "     submit <tx_blob>|[<private_key> <tx_json>]\n"
           "     submit_multisigned <tx_json>\n"
           "     tx <id>\n"
           "     validation_create [<seed>|<pass_phrase>|<key>]\n"
           "     validation_seed [<seed>|<pass_phrase>|<key>]\n"
           "     wallet_propose [<passphrase>]\n";
}

//------------------------------------------------------------------------------

static int runUnitTests(
    std::string const& pattern,
    std::string const& argument,
    bool quiet,
    bool log,
    bool child,
    std::size_t num_jobs,
    int argc,
    char** argv)
{
    using namespace beast::unit_test;
    using namespace ripple::test;

#if HAS_BOOST_PROCESS
    if (!child && num_jobs == 1)
#endif
    {
        multi_runner_parent parent_runner;

        multi_runner_child child_runner{num_jobs, quiet, log};
        auto const any_failed = child_runner.run_multi(match_auto(pattern));

        if (any_failed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
#if HAS_BOOST_PROCESS
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
        for(auto& c : children)
        {
            c.wait();
            if (c.exit_code())
                ++bad_child_exits;
        }

        if (parent_runner.any_failed() || bad_child_exits)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
    else
    {
        // child
        multi_runner_child runner{num_jobs, quiet, log};
        auto const anyFailed = runner.run_multi(match_auto(pattern));

        if (anyFailed)
            return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }
#endif
}

//------------------------------------------------------------------------------

int run (int argc, char** argv)
{
    // Make sure that we have the right OpenSSL and Boost libraries.
    version::checkLibraryVersions();

    using namespace std;

    beast::setCurrentThreadName ("rippled: main");

    po::variables_map vm;

    std::string importText;
    {
        importText += "Import an existing node database (specified in the [";
        importText += ConfigSection::importNodeDatabase ();
        importText += "] configuration file section) into the current ";
        importText += "node database (specified in the [";
        importText += ConfigSection::nodeDatabase ();
        importText += "] configuration file section).";
    }
    std::string shardsText;
    {
        shardsText += "Validate an existing shard database (specified in the [";
        shardsText += ConfigSection::shardDatabase();
        shardsText += "] configuration file section).";
    }

    // Set up option parsing.
    //
    po::options_description desc ("General Options");
    desc.add_options ()
    ("help,h", "Display this message.")
    ("conf", po::value<std::string> (), "Specify the configuration file.")
    ("rpc", "Perform rpc command (default).")
    ("rpc_ip", po::value <std::string> (), "Specify the IP address for RPC command. Format: <ip-address>[':'<port-number>]")
    ("rpc_port", po::value <std::uint16_t> (), "Specify the port number for RPC command.")
    ("standalone,a", "Run with no peers.")
    ("unittest,u", po::value <std::string> ()->implicit_value (""), "Perform unit tests.")
    ("unittest-arg", po::value <std::string> ()->implicit_value (""), "Supplies argument to unit tests.")
    ("unittest-log", po::value <std::string> ()->implicit_value (""), "Force unit test log output, even in quiet mode.")
#if HAS_BOOST_PROCESS
    ("unittest-jobs", po::value <std::size_t> (), "Number of unittest jobs to run.")
    ("unittest-child", "For internal use only. Run the process as a unit test child process.")
#endif
    ("parameters", po::value< vector<string> > (), "Specify comma separated parameters.")
    ("quiet,q", "Reduce diagnotics.")
    ("quorum", po::value <std::size_t> (), "Override the minimum validation quorum.")
    ("silent", "No output to the console after startup.")
    ("verbose,v", "Verbose logging.")
    ("load", "Load the current ledger from the local DB.")
    ("valid", "Consider the initial ledger a valid network ledger.")
    ("replay","Replay a ledger close.")
    ("ledger", po::value<std::string> (), "Load the specified ledger and start from .")
    ("ledgerfile", po::value<std::string> (), "Load the specified ledger file.")
    ("start", "Start from a fresh Ledger.")
    ("net", "Get the initial ledger from the network.")
    ("debug", "Enable normally suppressed debug logging")
    ("fg", "Run in the foreground.")
    ("import", importText.c_str ())
    ("shards", shardsText.c_str ())
    ("version", "Display the build version.")
    ;

    // Interpret positional arguments as --parameters.
    po::positional_options_description p;
    p.add ("parameters", -1);

    // Parse options, if no error.
    try
    {
        po::store (po::command_line_parser (argc, argv)
            .options (desc)               // Parse options.
            .positional (p)               // Remainder as --parameters.
            .run (),
            vm);
        po::notify (vm);                  // Invoke option notify functions.
    }
    catch (std::exception const&)
    {
        std::cerr << "rippled: Incorrect command line syntax." << std::endl;
        std::cerr << "Use '--help' for a list of options." << std::endl;
        return 1;
    }

    if (vm.count ("help"))
    {
        printHelp (desc);
        return 0;
    }

    if (vm.count ("version"))
    {
        std::cout << "rippled version " <<
            BuildInfo::getVersionString () << std::endl;
        return 0;
    }

    // Run the unit tests if requested.
    // The unit tests will exit the application with an appropriate return code.
    //
    if (vm.count ("unittest"))
    {
        std::string argument;

        if (vm.count("unittest-arg"))
            argument = vm["unittest-arg"].as<std::string>();

        std::size_t numJobs = 1;
        bool unittestChild = false;
#if HAS_BOOST_PROCESS
        if (vm.count("unittest-jobs"))
            numJobs = std::max(numJobs, vm["unittest-jobs"].as<std::size_t>());
        unittestChild = bool (vm.count("unittest-child"));
#endif

        return runUnitTests(
            vm["unittest"].as<std::string>(), argument,
            bool (vm.count ("quiet")),
            bool (vm.count ("unittest-log")),
            unittestChild,
            numJobs,
            argc,
            argv);
    }
    else
    {
#if HAS_BOOST_PROCESS
        if (vm.count("unittest-jobs"))
        {
            // unittest jobs only makes sense with `unittest`
            std::cerr << "rippled: '--unittest-jobs' specified without '--unittest'.\n";
            std::cerr << "To run the unit tests the '--unittest' option must be present.\n";
            return 1;
        }
#endif
    }

    auto config = std::make_unique<Config>();

    auto configFile = vm.count ("conf") ?
            vm["conf"].as<std::string> () : std::string();

    // config file, quiet flag.
    config->setup (configFile, bool (vm.count ("quiet")),
        bool(vm.count("silent")), bool(vm.count("standalone")));

    {
        // Stir any previously saved entropy into the pool:
        auto entropy = getEntropyFile (*config);
        if (!entropy.empty ())
            crypto_prng().load_state(entropy.string ());
    }

    if (vm.count ("start"))
        config->START_UP = Config::FRESH;

    if (vm.count ("import"))
        config->doImport = true;

    if (vm.count ("shards"))
        config->valShards = true;

    if (vm.count ("ledger"))
    {
        config->START_LEDGER = vm["ledger"].as<std::string> ();
        if (vm.count("replay"))
            config->START_UP = Config::REPLAY;
        else
            config->START_UP = Config::LOAD;
    }
    else if (vm.count ("ledgerfile"))
    {
        config->START_LEDGER = vm["ledgerfile"].as<std::string> ();
        config->START_UP = Config::LOAD_FILE;
    }
    else if (vm.count ("load"))
    {
        config->START_UP = Config::LOAD;
    }

    if (vm.count ("valid"))
    {
        config->START_VALID = true;
    }

    if (vm.count ("net"))
    {
        if ((config->START_UP == Config::LOAD) ||
            (config->START_UP == Config::REPLAY))
        {
            std::cerr <<
                "Net and load/reply options are incompatible" << std::endl;
            return -1;
        }

        config->START_UP = Config::NETWORK;
    }

    // Override the RPC destination IP address. This must
    // happen after the config file is loaded.
    if (vm.count ("rpc_ip"))
    {
        try
        {
            config->rpc_ip.emplace (
                boost::asio::ip::address_v4::from_string(
                    vm["rpc_ip"].as<std::string>()));
        }
        catch(std::exception const&)
        {
            std::cerr << "Invalid rpc_ip = " <<
                vm["rpc_ip"].as<std::string>() << std::endl;
            return -1;
        }
    }

    // Override the RPC destination port number
    //
    if (vm.count ("rpc_port"))
    {
        try
        {
            config->rpc_port.emplace (
                vm["rpc_port"].as<std::uint16_t>());

            if (*config->rpc_port == 0)
                throw std::domain_error("0");
        }
        catch(std::exception const& e)
        {
            std::cerr << "Invalid rpc_port = " << e.what() << "\n";
            return -1;
        }
    }

    if (vm.count ("quorum"))
    {
        try
        {
            config->VALIDATION_QUORUM = vm["quorum"].as <std::size_t> ();
            if (config->VALIDATION_QUORUM == std::size_t{})
            {
                throw std::domain_error("0");
            }
        }
        catch(std::exception const& e)
        {
            std::cerr << "Invalid value specified for --quorum ("
                      << e.what() << ")\n";
            return -1;
        }
    }

    // Construct the logs object at the configured severity
    using namespace beast::severities;
    Severity thresh = kInfo;

    if (vm.count ("quiet"))
        thresh = kFatal;
    else if (vm.count ("verbose"))
        thresh = kTrace;

    auto logs = std::make_unique<Logs>(thresh);

    // No arguments. Run server.
    if (!vm.count ("parameters"))
    {
        // We want at least 1024 file descriptors. We'll
        // tweak this further.
        if (!adjustDescriptorLimit(1024, logs->journal("Application")))
            return -1;

        if (HaveSustain() && !vm.count ("fg") && !config->standalone())
        {
            auto const ret = DoSustain ();

            if (!ret.empty ())
                std::cerr << "Watchdog: " << ret << std::endl;
        }

        if (vm.count ("debug"))
        {
            setDebugLogSink (logs->makeSink (
                "Debug", beast::severities::kTrace));
        }

        auto timeKeeper = make_TimeKeeper(
            logs->journal("TimeKeeper"));

        auto app = make_Application(
            std::move(config),
            std::move(logs),
            std::move(timeKeeper));

        if (!app->setup ())
        {
            StopSustain();
            return -1;
        }

        // With our configuration parsed, ensure we have
        // enough file descriptors available:
        if (!adjustDescriptorLimit(
            app->fdlimit(),
            app->logs().journal("Application")))
        {
            StopSustain();
            return -1;
        }

        // Start the server
        app->doStart(true /*start timers*/);

        // Block until we get a stop RPC.
        app->run();

        // Try to write out some entropy to use the next time we start.
        auto entropy = getEntropyFile (app->config());
        if (!entropy.empty ())
            crypto_prng().save_state(entropy.string ());

        return 0;
    }

    // We have an RPC command to process:
    beast::setCurrentThreadName ("rippled: rpc");
    return RPCCall::fromCommandLine (
        *config,
        vm["parameters"].as<std::vector<std::string>>(),
        *logs);
}

} // ripple

// Must be outside the namespace for obvious reasons
//
int main (int argc, char** argv)
{
    // Workaround for Boost.Context / Boost.Coroutine
    // https://svn.boost.org/trac/boost/ticket/10657
    (void)beast::currentTimeMillis();

#ifdef _MSC_VER
    ripple::sha512_deprecatedMSVCWorkaround();
#endif

#if defined(__GNUC__) && !defined(__clang__)
    auto constexpr gccver = (__GNUC__ * 100 * 100) +
                            (__GNUC_MINOR__ * 100) +
                            __GNUC_PATCHLEVEL__;

    static_assert (gccver >= 50100,
        "GCC version 5.1.0 or later is required to compile rippled.");
#endif

    static_assert (BOOST_VERSION >= 105700,
        "Boost version 1.57 or later is required to compile rippled");

    //
    // These debug heap calls do nothing in release or non Visual Studio builds.
    //

    // Checks the heap at every allocation and deallocation (slow).
    //
    //beast::Debug::setAlwaysCheckHeap (false);

    // Keeps freed memory blocks and fills them with a guard value.
    //
    //beast::Debug::setHeapDelayedFree (false);

    // At exit, reports all memory blocks which have not been freed.
    //
#if RIPPLE_DUMP_LEAKS_ON_EXIT
    beast::Debug::setHeapReportLeaks (true);
#else
    beast::Debug::setHeapReportLeaks (false);
#endif

    atexit(&google::protobuf::ShutdownProtobufLibrary);

    std::set_terminate(ripple::terminateHandler);

    auto const result (ripple::run (argc, argv));

    beast::basic_seconds_clock_main_hook();

    return result;
}
