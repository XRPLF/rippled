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

#ifndef RIPPLE_CORE_CONFIG_H_INCLUDED
#define RIPPLE_CORE_CONFIG_H_INCLUDED

#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/FeeUnits.h>
#include <ripple/basics/base_uint.h>
#include <ripple/beast/net/IPEndpoint.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/SystemParameters.h>  // VFALCO Breaks levelization

#include <boost/beast/core/string.hpp>
#include <boost/filesystem.hpp>  // VFALCO FIX: This include should not be here
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ripple {

class Rules;

//------------------------------------------------------------------------------

enum class SizedItem : std::size_t {
    sweepInterval = 0,
    treeCacheSize,
    treeCacheAge,
    ledgerSize,
    ledgerAge,
    ledgerFetch,
    hashNodeDBCache,
    txnDBCache,
    lgrDBCache,
    openFinalLimit,
    burstSize,
    ramSizeGB,
    accountIdCacheSize,
};

/** Fee schedule for startup / standalone, and to vote for.
During voting ledgers, the FeeVote logic will try to move towards
these values when injecting fee-setting transactions.
A default-constructed Setup contains recommended values.
*/
struct FeeSetup
{
    /** The cost of a reference transaction in drops. */
    XRPAmount reference_fee{10};

    /** The account reserve requirement in drops. */
    XRPAmount account_reserve{10 * DROPS_PER_XRP};

    /** The per-owned item reserve requirement in drops. */
    XRPAmount owner_reserve{2 * DROPS_PER_XRP};

    /* (Remember to update the example cfg files when changing any of these
     * values.) */
};

//  This entire derived class is deprecated.
//  For new config information use the style implied
//  in the base class. For existing config information
//  try to refactor code to use the new style.
//
class Config : public BasicConfig
{
public:
    // Settings related to the configuration file location and directories
    static char const* const configFileName;
    static char const* const databaseDirName;
    static char const* const validatorsFileName;

    /** Returns the full path and filename of the debug log file. */
    boost::filesystem::path
    getDebugLogFile() const;

private:
    boost::filesystem::path CONFIG_FILE;

public:
    boost::filesystem::path CONFIG_DIR;

private:
    boost::filesystem::path DEBUG_LOGFILE;

    void
    load();
    beast::Journal const j_;

    bool QUIET = false;   // Minimize logging verbosity.
    bool SILENT = false;  // No output to console after startup.
    /** Operate in stand-alone mode.

        In stand alone mode:

        - Peer connections are not attempted or accepted
        - The ledger is not advanced automatically.
        - If no ledger is loaded, the default ledger with the root
          account is created.
    */
    bool RUN_STANDALONE = false;

    bool RUN_REPORTING = false;

    bool REPORTING_READ_ONLY = false;

    bool USE_TX_TABLES = true;

    /** Determines if the server will sign a tx, given an account's secret seed.

        In the past, this was allowed, but this functionality can have security
        implications. The new default is to not allow this functionality, but
        a config option is included to enable this.
    */
    bool signingEnabled_ = false;

    // The amount of RAM, in bytes, that we detected on this system.
    std::uint64_t const ramSize_;

public:
    bool doImport = false;
    bool nodeToShard = false;
    bool ELB_SUPPORT = false;

    // Entries from [ips] config stanza
    std::vector<std::string> IPS;

    // Entries from [ips_fixed] config stanza
    std::vector<std::string> IPS_FIXED;

    enum StartUpType { FRESH, NORMAL, LOAD, LOAD_FILE, REPLAY, NETWORK };
    StartUpType START_UP = NORMAL;

    bool START_VALID = false;

    std::string START_LEDGER;

    // Network parameters
    uint32_t NETWORK_ID = 0;

    // DEPRECATED - Fee units for a reference transction.
    // Only provided for backwards compatibility in a couple of places
    static constexpr std::uint32_t FEE_UNITS_DEPRECATED = 10;

    // Note: The following parameters do not relate to the UNL or trust at all
    // Minimum number of nodes to consider the network present
    std::size_t NETWORK_QUORUM = 1;

    // Peer networking parameters
    // 1 = relay, 0 = do not relay (but process), -1 = drop completely (do NOT
    // process)
    int RELAY_UNTRUSTED_VALIDATIONS = 1;
    int RELAY_UNTRUSTED_PROPOSALS = 0;

    // True to ask peers not to relay current IP.
    bool PEER_PRIVATE = false;
    // peers_max is a legacy configuration, which is going to be replaced
    // with individual inbound peers peers_in_max and outbound peers
    // peers_out_max configuration. for now we support both the legacy and
    // the new configuration. if peers_max is configured then peers_in_max and
    // peers_out_max are ignored.
    std::size_t PEERS_MAX = 0;
    std::size_t PEERS_OUT_MAX = 0;
    std::size_t PEERS_IN_MAX = 0;

    // Path searching: these were reasonable default values at some point but
    //                 further research is needed to decide if they still are
    //                 and whether all of them are needed.
    //
    //                 The performance and resource consumption of a server can
    //                 be dramatically impacted by changing these configuration
    //                 options; higher values result in exponentially higher
    //                 resource usage.
    //
    //                 Servers operating as validators disable path finding by
    //                 default by setting the `PATH_SEARCH_MAX` option to 0
    //                 unless it is explicitly set in the configuration file.
    int PATH_SEARCH_OLD = 2;
    int PATH_SEARCH = 2;
    int PATH_SEARCH_FAST = 2;
    int PATH_SEARCH_MAX = 3;

    // Validation
    std::optional<std::size_t>
        VALIDATION_QUORUM;  // validations to consider ledger authoritative

    FeeSetup FEES;

    // Node storage configuration
    std::uint32_t LEDGER_HISTORY = 256;
    std::uint32_t FETCH_DEPTH = 1000000000;

    // Tunable that adjusts various parameters, typically associated
    // with hardware parameters (RAM size and CPU cores). The default
    // is 'tiny'.
    std::size_t NODE_SIZE = 0;

    bool SSL_VERIFY = true;
    std::string SSL_VERIFY_FILE;
    std::string SSL_VERIFY_DIR;

    // Compression
    bool COMPRESSION = false;

    // Enable the experimental Ledger Replay functionality
    bool LEDGER_REPLAY = false;

    // Work queue limits
    int MAX_TRANSACTIONS = 250;
    static constexpr int MAX_JOB_QUEUE_TX = 1000;
    static constexpr int MIN_JOB_QUEUE_TX = 100;

    // Amendment majority time
    std::chrono::seconds AMENDMENT_MAJORITY_TIME = defaultAmendmentMajorityTime;

    // Thread pool configuration (0 = choose for me)
    int WORKERS = 0;           // jobqueue thread count. default: upto 6
    int IO_WORKERS = 0;        // io svc thread count. default: 2
    int PREFETCH_WORKERS = 0;  // prefetch thread count. default: 4

    // Can only be set in code, specifically unit tests
    bool FORCE_MULTI_THREAD = false;

    // Normally the sweep timer is automatically deduced based on the node
    // size, but we allow admins to explicitly set it in the config.
    std::optional<int> SWEEP_INTERVAL;

    // Reduce-relay - these parameters are experimental.
    // Enable reduce-relay features
    // Validation/proposal reduce-relay feature
    bool VP_REDUCE_RELAY_ENABLE = false;
    // Send squelch message to peers. Generally this config should
    // have the same value as VP_REDUCE_RELAY_ENABLE. It can be
    // used for testing the feature's function without
    // affecting the message relaying. To use it for testing,
    // set it to false and set VP_REDUCE_RELAY_ENABLE to true.
    // Squelch messages will not be sent to the peers in this case.
    // Set log level to debug so that the feature function can be
    // analyzed.
    bool VP_REDUCE_RELAY_SQUELCH = false;
    // Transaction reduce-relay feature
    bool TX_REDUCE_RELAY_ENABLE = false;
    // If tx reduce-relay feature is disabled
    // and this flag is enabled then some
    // tx-related metrics is collected. It
    // is ignored if tx reduce-relay feature is
    // enabled. It is used in debugging to compare
    // metrics with the feature disabled/enabled.
    bool TX_REDUCE_RELAY_METRICS = false;
    // Minimum peers a server should have before
    // selecting random peers
    std::size_t TX_REDUCE_RELAY_MIN_PEERS = 20;
    // Percentage of peers with the tx reduce-relay feature enabled
    // to relay to out of total active peers
    std::size_t TX_RELAY_PERCENTAGE = 25;

    // These override the command line client settings
    std::optional<beast::IP::Endpoint> rpc_ip;

    std::unordered_set<uint256, beast::uhash<>> features;

    std::string SERVER_DOMAIN;

    // How long can a peer remain in the "unknown" state
    std::chrono::seconds MAX_UNKNOWN_TIME{600};

    // How long can a peer remain in the "diverged" state
    std::chrono::seconds MAX_DIVERGED_TIME{300};

    // Enable the beta API version
    bool BETA_RPC_API = false;

    // First, attempt to load the latest ledger directly from disk.
    bool FAST_LOAD = false;
    // When starting rippled with existing database it do not know it has those
    // ledgers locally until the server naturally tries to backfill. This makes
    // is difficult to test some functionality (in particular performance
    // testing sidechains). With this variable the user is able to force rippled
    // to consider the ledger range to be present. It should be used for testing
    // only.
    std::optional<std::pair<std::uint32_t, std::uint32_t>>
        FORCED_LEDGER_RANGE_PRESENT;

public:
    Config();

    /* Be very careful to make sure these bool params
        are in the right order. */
    void
    setup(
        std::string const& strConf,
        bool bQuiet,
        bool bSilent,
        bool bStandalone);

    void
    setupControl(bool bQuiet, bool bSilent, bool bStandalone);

    /**
     *  Load the config from the contents of the string.
     *
     *  @param fileContents String representing the config contents.
     */
    void
    loadFromString(std::string const& fileContents);

    bool
    quiet() const
    {
        return QUIET;
    }
    bool
    silent() const
    {
        return SILENT;
    }
    bool
    standalone() const
    {
        return RUN_STANDALONE;
    }
    bool
    reporting() const
    {
        return RUN_REPORTING;
    }

    bool
    useTxTables() const
    {
        return USE_TX_TABLES;
    }

    bool
    reportingReadOnly() const
    {
        return REPORTING_READ_ONLY;
    }

    void
    setReportingReadOnly(bool b)
    {
        REPORTING_READ_ONLY = b;
    }

    bool
    canSign() const
    {
        return signingEnabled_;
    }

    /** Retrieve the default value for the item at the specified node size

        @param item The item for which the default value is needed
        @param node Optional value, used to adjust the result to match the
                    size of a node (0: tiny, ..., 4: huge). If unseated,
                    uses the configured size (NODE_SIZE).

        @throw This method can throw std::out_of_range if you ask for values
               that it does not recognize or request a non-default node-size.

        @return The value for the requested item.

        @note The defaults are selected so as to be reasonable, but the node
              size is an imprecise metric that combines multiple aspects of
              the underlying system; this means that we can't provide optimal
              defaults in the code for every case.
    */
    int
    getValueFor(SizedItem item, std::optional<std::size_t> node = std::nullopt)
        const;
};

FeeSetup
setup_FeeVote(Section const& section);

}  // namespace ripple

#endif
