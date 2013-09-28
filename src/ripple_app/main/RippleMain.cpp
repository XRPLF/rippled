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

RippleMain::RippleMain()
{
    ProtectedCall::setHandler (*this);
}

RippleMain::~RippleMain()
{

}

void RippleMain::onException (ProtectedCall::Exception const& e) const
{
#if BEAST_MSVC
    ExitProcess(0);
#else
    _Exit(EXIT_FAILURE);
#endif
}

//------------------------------------------------------------------------------

namespace po = boost::program_options;

void setupServer ()
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

    getApp().setup ();
}

void startServer ()
{
    //
    // Execute start up rpc commands.
    //
    if (getConfig ().RPC_STARTUP.isArray ())
    {
        for (int i = 0; i != getConfig ().RPC_STARTUP.size (); ++i)
        {
            const Json::Value& jvCommand    = getConfig ().RPC_STARTUP[i];

            if (!getConfig ().QUIET)
                Log::out() << "Startup RPC: " << jvCommand;

            RPCHandler  rhHandler (&getApp().getOPs ());

            // VFALCO TODO Clean up this magic number
            LoadType loadType = LT_RPCReference;
            Json::Value jvResult    = rhHandler.doCommand (jvCommand, Config::ADMIN, &loadType);

            if (!getConfig ().QUIET)
                Log::out() << "Result: " << jvResult;
        }
    }

    getApp().run ();                 // Blocks till we get a stop RPC.
}

void printHelp (const po::options_description& desc)
{
    using namespace std;

    cerr << SYSTEM_NAME "d [options] <command> <params>" << endl;

    cerr << desc << endl;

    cerr << "Commands: " << endl;
    cerr << "     account_info <account>|<nickname>|<seed>|<pass_phrase>|<key> [<ledger>] [strict]" << endl;
    cerr << "     account_lines <account> <account>|\"\" [<ledger>]" << endl;
    cerr << "     account_offers <account>|<nickname>|<account_public_key> [<ledger>]" << endl;
    cerr << "     account_tx accountID [ledger_min [ledger_max [limit [offset]]]] [binary] [count] [descending]" << endl;
    cerr << "     book_offers <taker_pays> <taker_gets> [<taker [<ledger> [<limit> [<proof> [<marker>]]]]]" << endl;
    cerr << "     connect <ip> [<port>]" << endl;
    cerr << "     consensus_info" << endl;
#if ENABLE_INSECURE
    cerr << "     data_delete <key>" << endl;
    cerr << "     data_fetch <key>" << endl;
    cerr << "     data_store <key> <value>" << endl;
#endif
    cerr << "     get_counts" << endl;
    cerr << "     json <method> <json>" << endl;
    cerr << "     ledger [<id>|current|closed|validated] [full]" << endl;
    cerr << "     ledger_accept" << endl;
    cerr << "     ledger_closed" << endl;
    cerr << "     ledger_current" << endl;
    cerr << "     ledger_header <ledger>" << endl;
    cerr << "     logrotate " << endl;
    cerr << "     peers" << endl;
    cerr << "     proof_create [<difficulty>] [<secret>]" << endl;
    cerr << "     proof_solve <token>" << endl;
    cerr << "     proof_verify <token> <solution> [<difficulty>] [<secret>]" << endl;
    cerr << "     random" << endl;
    cerr << "     ripple ..." << endl;
    cerr << "     ripple_path_find <json> [<ledger>]" << endl;
    //  cerr << "     send <seed> <paying_account> <account_id> <amount> [<currency>] [<send_max>] [<send_currency>]" << endl;
    cerr << "     stop" << endl;
    cerr << "     tx <id>" << endl;
    cerr << "     unl_add <domain>|<public> [<comment>]" << endl;
    cerr << "     unl_delete <domain>|<public_key>" << endl;
    cerr << "     unl_list" << endl;
    cerr << "     unl_load" << endl;
    cerr << "     unl_network" << endl;
    cerr << "     unl_reset" << endl;
    cerr << "     validation_create [<seed>|<pass_phrase>|<key>]" << endl;
    cerr << "     validation_seed [<seed>|<pass_phrase>|<key>]" << endl;
    cerr << "     wallet_add <regular_seed> <paying_account> <master_seed> [<initial_funds>] [<account_annotation>]" << endl;
    cerr << "     wallet_accounts <seed>" << endl;
    cerr << "     wallet_claim <master_seed> <regular_seed> [<source_tag>] [<account_annotation>]" << endl;
    cerr << "     wallet_seed [<seed>|<passphrase>|<passkey>]" << endl;
    cerr << "     wallet_propose [<passphrase>]" << endl;

    // Transaction helpers (that were removed):
    //  cerr << "     account_domain_set <seed> <paying_account> [<domain>]" << endl;
    //  cerr << "     account_email_set <seed> <paying_account> [<email_address>]" << endl;
    //  cerr << "     account_rate_set <seed> <paying_account> <rate>" << endl;
    //  cerr << "     account_wallet_set <seed> <paying_account> [<wallet_hash>]" << endl;
    //  cerr << "     nickname_info <nickname>" << endl;
    //  cerr << "     nickname_set <seed> <paying_account> <nickname> [<offer_minimum>] [<authorization>]" << endl;
    //  cerr << "     offer_create <seed> <paying_account> <taker_pays_amount> <taker_pays_currency> <taker_pays_issuer> <takers_gets_amount> <takers_gets_currency> <takers_gets_issuer> <expires> [passive]" << endl;
    //  cerr << "     offer_cancel <seed> <paying_account> <sequence>" << endl;
    //  cerr << "     password_fund <seed> <paying_account> [<account>]" << endl;
    //  cerr << "     password_set <master_seed> <regular_seed> [<account>]" << endl;
    //  cerr << "     trust_set <seed> <paying_account> <destination_account> <limit_amount> <currency> [<quality_in>] [<quality_out>]" << endl;
}

//------------------------------------------------------------------------------

// OUr custom unit test runner
//
class RippleUnitTests : public UnitTests
{
public:
    explicit RippleUnitTests (bool shouldLog)
        : m_shouldLog (shouldLog)
    {
        setupConfigForUnitTests (&getConfig ());

        // VFALCO NOTE It sucks that we have to do this but some
        //             code demands the Application object exists.
        //
        //        TODO To find out who, just comment the next line out
        m_app = Application::New ();

        setAssertOnFailure (false);
    }

    void logMessage (String const& message)
    {
        if (m_shouldLog)
        {
#if BEAST_MSVC
            if (beast_isRunningUnderDebugger ())
            {
                Logger::outputDebugString (message);
            }
            else
            {
                std::cout << message.toStdString () << std::endl;
            }

#else
            std::cout << message.toStdString () << std::endl;

#endif
        }
    }

private:
    void setupConfigForUnitTests (Config* config)
    {
        config->nodeDatabase = parseDelimitedKeyValueString ("type=memory");
        config->ephemeralNodeDatabase = StringPairArray ();
        config->importNodeDatabase = StringPairArray ();
    }

private:
    bool const m_shouldLog;
    ScopedPointer <Application> m_app;
};

static int runUnitTests (String const& match, String const& format)
{
    bool const shouldLog = format != "junit";

    if (format != "junit" && format != "text" && format != "")
    {
        String s;
        s << "Warning, unknown unittest-format='" << format << "'";
        Log::out () << s.toStdString ();
    }

    RippleUnitTests tr (shouldLog);

    tr.runSelectedTests (match);

    if (format == "junit")
    {
        UnitTestUtilities::JUnitXMLFormatter f (tr);

        String const s = f.createDocumentString ();

        std::cout << s.toStdString ();
    }
    else
    {
        UnitTests::Results const& r (tr.getResults ());

        String s;

        s << "Summary: " <<
            String (r.suites.size ()) << " suites, " <<
            String (r.cases) << " cases, " <<
            String (r.tests) << " tests, " <<
            String (r.failures) << " failure" << ((r.failures != 1) ? "s" : "") << ".";

        tr.logMessage (s);
    }

    return tr.anyTestsFailed () ? EXIT_FAILURE : EXIT_SUCCESS;
}

//------------------------------------------------------------------------------

int RippleMain::run (int argc, char const* const* argv)
{
    FatalErrorReporter reporter;

    using namespace std;

    setCallingThreadName ("main");
    int iResult = 0;
    po::variables_map   vm;                                     // Map of options.

    String importDescription;
    {
        importDescription <<
            "Import an existing node database (specified in the " <<
            "[" << ConfigSection::importNodeDatabase () << "] configuration file section) "
            "into the current node database (specified in the " <<
            "[" << ConfigSection::nodeDatabase () << "] configuration file section). ";
    }

    // VFALCO TODO Replace boost program options with something from Beast.
    //
    // Set up option parsing.
    //
    po::options_description desc ("General Options");
    desc.add_options ()
    ("help,h", "Display this message.")
    ("conf", po::value<std::string> (), "Specify the configuration file.")
    ("rpc", "Perform rpc command (default).")
    ("rpc_ip", po::value <std::string> (), "Specify the IP address for RPC command. Format: <ip-address>[':'<port-number>]")
    ("rpc_port", po::value <int> (), "Specify the port number for RPC command.")
    ("standalone,a", "Run with no peers.")
    ("testnet,t", "Run in test net mode.")
    ("unittest,u", po::value <std::string> ()->implicit_value (""), "Perform unit tests.")
    ("unittest-format", po::value <std::string> ()->implicit_value ("text"), "Format unit test output. Choices are 'text', 'junit'")
    ("parameters", po::value< vector<string> > (), "Specify comma separated parameters.")
    ("quiet,q", "Reduce diagnotics.")
    ("verbose,v", "Verbose logging.")
    ("load", "Load the current ledger from the local DB.")
    ("replay","Replay a ledger close.")
    ("ledger", po::value<std::string> (), "Load the specified ledger and start from .")
    ("start", "Start from a fresh Ledger.")
    ("net", "Get the initial ledger from the network.")
    ("fg", "Run in the foreground.")
    ("import", importDescription.toStdString ().c_str ())
    ;

    // Interpret positional arguments as --parameters.
    po::positional_options_description p;
    p.add ("parameters", -1);

    // NOTE: These must be added before the
    //       Application object is created.
    //
    NodeStore::Database::addAvailableBackends ();

    // VFALCO NOTE SqliteFactory is here because it has
    //             dependencies like SqliteDatabase and DatabaseCon
    //
    NodeStore::Database::addFactory (SqliteFactory::getInstance ());

    if (! RandomNumbers::getInstance ().initialize ())
    {
        Log::out() << "Unable to add system entropy";
        iResult = 2;
    }

    if (iResult)
    {
        nothing ();
    }
    else
    {
        // Parse options, if no error.
        try
        {
            po::store (po::command_line_parser (argc, argv)
                       .options (desc)                                         // Parse options.
                       .positional (p)                                         // Remainder as --parameters.
                       .run (),
                       vm);
            po::notify (vm);                                            // Invoke option notify functions.
        }
        catch (...)
        {
            iResult = 1;
        }
    }

    if (iResult)
    {
        nothing ();
    }
    else if (vm.count ("help"))
    {
        iResult = 1;
    }

    // Use a watchdog process unless we're invoking a stand alone type of mode
    //
    if (HaveSustain ()
        && !iResult
        && !vm.count ("parameters")
        && !vm.count ("fg")
        && !vm.count ("standalone")
        && !vm.count ("unittest"))
    {
        std::string logMe = DoSustain (getConfig ().DEBUG_LOGFILE.string());

        if (!logMe.empty ())
            Log (lsWARNING) << logMe;
    }

    if (vm.count ("quiet"))
    {
        LogSink::get()->setMinSeverity (lsFATAL, true);
    }
    else if (vm.count ("verbose"))
    {
        LogSink::get()->setMinSeverity (lsTRACE, true);
    }
    else
    {
        LogSink::get()->setMinSeverity (lsINFO, true);
    }

    // Run the unit tests if requested.
    // The unit tests will exit the application with an appropriate return code.
    //
    if (vm.count ("unittest"))
    {
        String format;

        if (vm.count ("unittest-format"))
            format = vm ["unittest-format"].as <std::string> ();

        return runUnitTests (vm ["unittest"].as <std::string> (), format);
    }

    if (!iResult)
    {
        getConfig ().setup (
            vm.count ("conf") ? vm["conf"].as<std::string> () : "", // Config file.
            !!vm.count ("testnet"),                                 // Testnet flag.
            !!vm.count ("quiet"));                                  // Quiet flag.

        if (vm.count ("standalone"))
        {
            getConfig ().RUN_STANDALONE = true;
            getConfig ().LEDGER_HISTORY = 0;
        }
    }

    if (vm.count ("start")) getConfig ().START_UP = Config::FRESH;

    // Handle a one-time import option
    //
    if (vm.count ("import"))
    {
        String const optionString (vm ["import"].as <std::string> ());

        getConfig ().importNodeDatabase = parseDelimitedKeyValueString (optionString);
    }

    if (vm.count ("ledger"))
    {
        getConfig ().START_LEDGER = vm["ledger"].as<std::string> ();
        if (vm.count("replay"))
            getConfig ().START_UP = Config::REPLAY;
        else
            getConfig ().START_UP = Config::LOAD;
    }
    else if (vm.count ("load"))
    {
        getConfig ().START_UP = Config::LOAD;
    }
    else if (vm.count ("net"))
    {
        getConfig ().START_UP = Config::NETWORK;

        if (getConfig ().VALIDATION_QUORUM < 2)
            getConfig ().VALIDATION_QUORUM = 2;
    }

    if (iResult == 0)
    {
        // These overrides must happen after the config file is loaded.

        // Override the RPC destination IP address
        //
        if (vm.count ("rpc_ip"))
        {
            getConfig ().setRpcIpAndOptionalPort (vm ["rpc_ip"].as <std::string> ());
        }

        // Override the RPC destination port number
        //
        if (vm.count ("rpc_port"))
        {
            // VFALCO TODO This should be a short.
            getConfig ().setRpcPort (vm ["rpc_port"].as <int> ());
        }
    }

    if (iResult == 0)
    {
        if (!vm.count ("parameters"))
        {
            // No arguments. Run server.
            ScopedPointer <Application> app (Application::New ());
            setupServer ();
            startServer ();
        }
        else
        {
            // Have a RPC command.
            setCallingThreadName ("rpc");
            std::vector<std::string> vCmd   = vm["parameters"].as<std::vector<std::string> > ();

            iResult = RPCCall::fromCommandLine (vCmd);
        }
    }

    if (1 == iResult && !vm.count ("quiet"))
        printHelp (desc);

    return iResult;
}


