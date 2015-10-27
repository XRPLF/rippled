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
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/Sustain.h>
#include <ripple/basics/ThreadName.h>
#include <ripple/core/Config.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/crypto/RandomNumbers.h>
#include <ripple/json/to_string.h>
#include <ripple/net/RPCCall.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/server/Role.h>
#include <ripple/protocol/BuildInfo.h>
#include <beast/chrono/basic_seconds_clock.h>
#include <beast/module/core/time/Time.h>
#include <beast/unit_test.h>
#include <beast/utility/Debug.h>
#include <beast/streams/debug_ostream.h>
#include <google/protobuf/stubs/common.h>
#include <boost/program_options.hpp>
#include <cstdlib>
#include <thread>
#include <utility>

#if defined(BEAST_LINUX) || defined(BEAST_MAC) || defined(BEAST_BSD)
#include <sys/resource.h>
#endif

namespace po = boost::program_options;

namespace ripple {

void setupServer (Application& app)
{
#ifdef RLIMIT_NOFILE
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
    {
         if (rl.rlim_cur != rl.rlim_max)
         {
             rl.rlim_cur = rl.rlim_max;
             setrlimit(RLIMIT_NOFILE, &rl);
         }
    }
#endif

    app.setup ();
}

boost::filesystem::path
getEntropyFile(Config const& config)
{
    auto const path = config.legacy("database_path");
    if (path.empty ())
        return {};
    return boost::filesystem::path (path) / "random.seed";
}

void startServer (Application& app)
{
    //
    // Execute start up rpc commands.
    //
    if (app.config().RPC_STARTUP.isArray ())
    {
        for (int i = 0; i != app.config().RPC_STARTUP.size (); ++i)
        {
            Json::Value const& jvCommand    = app.config().RPC_STARTUP[i];

            if (!app.config().QUIET)
                std::cerr << "Startup RPC: " << jvCommand << std::endl;

            Resource::Charge loadType = Resource::feeReferenceRPC;
            RPC::Context context {
                app.journal ("RPCHandler"), jvCommand, app, loadType, app.getOPs (),
                app.getLedgerMaster(), Role::ADMIN, app};

            Json::Value jvResult;
            RPC::doCommand (context, jvResult);

            if (!app.config().QUIET)
                std::cerr << "Result: " << jvResult << std::endl;
        }
    }

    // Block until we get a stop RPC.
    app.run ();

    // Try to write out some entropy to use the next time we start.
    auto entropy = getEntropyFile (app.config());
    if (!entropy.empty ())
        stir_entropy (entropy.string ());
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
           "     account_objects <account> [<ledger>] [strict]\n"
           "     account_offers <account>|<account_public_key> [<ledger>]\n"
           "     account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary] [count] [descending]\n"
           "     book_offers <taker_pays> <taker_gets> [<taker [<ledger> [<limit> [<proof> [<marker>]]]]]\n"
           "     can_delete [<ledgerid>|<ledgerhash>|now|always|never]\n"
           "     connect <ip> [<port>]\n"
           "     consensus_info\n"
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

static int runShutdownTests (std::unique_ptr<Config> config)
{
    // Shutdown tests can not be part of the normal unit tests in 'runUnitTests'
    // because it needs to create and destroy an application object.
    // FIXME: we only loop once, since the Config object will get destroyed
    int const numShutdownIterations = 1; //20;

    // Give it enough time to sync and run a bit while synced.
    std::chrono::seconds const serverUptimePerIteration (4 * 60);
    for (int i = 0; i < numShutdownIterations; ++i)
    {
        std::cerr << "\n\nStarting server. Iteration: " << i << "\n"
                  << std::endl;
        auto app = make_Application (
            std::move(config),
            std::make_unique<Logs>());
        auto shutdownApp = [&app](std::chrono::seconds sleepTime, int iteration)
        {
            std::this_thread::sleep_for (sleepTime);
            std::cerr << "\n\nStopping server. Iteration: " << iteration << "\n"
                      << std::endl;
            app->signalStop();
        };
        std::thread shutdownThread (shutdownApp, serverUptimePerIteration, i);
        setupServer(*app);
        startServer(*app);
        shutdownThread.join();
    }
    return EXIT_SUCCESS;
}

static int runUnitTests(
    std::string const& pattern,
    std::string const& argument)
{
    using namespace beast::unit_test;
    beast::debug_ostream stream;
    reporter r (stream);
    r.arg(argument);
    bool const failed (r.run_each_if (
        global_suites(), match_auto (pattern)));
    if (failed)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

//------------------------------------------------------------------------------

int run (int argc, char** argv)
{
    // Make sure that we have the right OpenSSL and Boost libraries.
    version::checkLibraryVersions();

    using namespace std;

    setCallingThreadName ("main");

    {
        // We want to seed the RNG early. We acquire a small amount of
        // questionable quality entropy from the current time and our
        // environment block which will get stirred into the RNG pool
        // along with high-quality entropy from the system.
        struct entropy_t
        {
            std::uint64_t timestamp;
            std::size_t tid;
            std::uintptr_t ptr[4];
        };

        auto entropy = std::make_unique<entropy_t> ();

        entropy->timestamp = beast::Time::currentTimeMillis ();
        entropy->tid = std::hash <std::thread::id>() (std::this_thread::get_id ());
        entropy->ptr[0] = reinterpret_cast<std::uintptr_t>(entropy.get ());
        entropy->ptr[1] = reinterpret_cast<std::uintptr_t>(&argc);
        entropy->ptr[2] = reinterpret_cast<std::uintptr_t>(argv);
        entropy->ptr[3] = reinterpret_cast<std::uintptr_t>(argv[0]);

        add_entropy (entropy.get (), sizeof (entropy_t));
    }

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
    ("shutdowntest", po::value <std::string> ()->implicit_value (""), "Perform shutdown tests.")
    ("unittest,u", po::value <std::string> ()->implicit_value (""), "Perform unit tests.")
    ("unittest-arg", po::value <std::string> ()->implicit_value (""), "Supplies argument to unit tests.")
    ("parameters", po::value< vector<string> > (), "Specify comma separated parameters.")
    ("quiet,q", "Reduce diagnotics.")
    ("quorum", po::value <int> (), "Set the validation quorum.")
    ("verbose,v", "Verbose logging.")
    ("load", "Load the current ledger from the local DB.")
    ("valid", "Consider the initial ledger a valid network ledger.")
    ("replay","Replay a ledger close.")
    ("ledger", po::value<std::string> (), "Load the specified ledger and start from .")
    ("ledgerfile", po::value<std::string> (), "Load the specified ledger file.")
    ("start", "Start from a fresh Ledger.")
    ("net", "Get the initial ledger from the network.")
    ("fg", "Run in the foreground.")
    ("import", importText.c_str ())
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
    catch (...)
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

    // Use a watchdog process unless we're invoking a stand alone type of mode
    //
    if (HaveSustain ()
        && !vm.count ("parameters")
        && !vm.count ("fg")
        && !vm.count ("standalone")
        && !vm.count ("shutdowntest")
        && !vm.count ("unittest"))
    {
        std::string logMe = DoSustain ();

        if (!logMe.empty ())
            std::cerr << logMe;
    }

    // Run the unit tests if requested.
    // The unit tests will exit the application with an appropriate return code.
    //
    if (vm.count ("unittest"))
    {
        std::string argument;

        if (vm.count("unittest-arg"))
            argument = vm["unittest-arg"].as<std::string>();

        return runUnitTests(
            vm["unittest"].as<std::string>(), argument);
    }

    auto config = std::make_unique<Config>();

    auto configFile = vm.count ("conf") ?
            vm["conf"].as<std::string> () : std::string();

    // config file, quiet flag.
    config->setup (configFile, bool (vm.count ("quiet")));

    if (vm.count ("standalone"))
    {
        config->RUN_STANDALONE = true;
        config->LEDGER_HISTORY = 0;
    }

    // Use any previously available entropy to stir the pool
    auto entropy = getEntropyFile (*config);
    if (!entropy.empty ())
        stir_entropy (entropy.string ());

    if (vm.count ("start"))
        config->START_UP = Config::FRESH;

    if (vm.count ("import"))
        config->doImport = true;

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

        if (config->VALIDATION_QUORUM < 2)
            config->VALIDATION_QUORUM = 2;
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
        catch(...)
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
                throw std::domain_error ("");
        }
        catch(...)
        {
            std::cerr << "Invalid rpc_port = " <<
                vm["rpc_port"].as<std::string>() << std::endl;
            return -1;
        }
    }

    if (vm.count ("quorum"))
    {
        try
        {
            config->VALIDATION_QUORUM = vm["quorum"].as <int> ();
            config->LOCK_QUORUM = true;

            if (config->VALIDATION_QUORUM < 0)
                throw std::domain_error ("");
        }
        catch(...)
        {
            std::cerr << "Invalid quorum = " <<
                vm["quorum"].as <std::string> () << std::endl;
            return -1;
        }
    }

    if (vm.count ("shutdowntest"))
        return runShutdownTests (std::move(config));

    // No arguments. Run server.
    if (!vm.count ("parameters"))
    {
        auto logs = std::make_unique<Logs>();

        if (vm.count ("quiet"))
            logs->severity (beast::Journal::kFatal);
        else if (vm.count ("verbose"))
            logs->severity (beast::Journal::kTrace);
        else
            logs->severity (beast::Journal::kInfo);

        auto app = make_Application (
            std::move(config),
            std::move (logs));
        setupServer (*app);
        startServer (*app);
        return 0;
    }

    // We have an RPC command to process:
    setCallingThreadName ("rpc");
    return RPCCall::fromCommandLine (
        *config,
        vm["parameters"].as<std::vector<std::string>>(), deprecatedLogs());
}

extern int run (int argc, char** argv);

} // ripple

// Must be outside the namespace for obvious reasons
//
int main (int argc, char** argv)
{
    // Workaround for Boost.Context / Boost.Coroutine
    // https://svn.boost.org/trac/boost/ticket/10657
    (void)beast::Time::currentTimeMillis();

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

    auto const result (ripple::run (argc, argv));

    beast::basic_seconds_clock_main_hook();

    return result;
}
