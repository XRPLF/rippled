#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/BasicConfig.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/SystemParameters.h>  // VFALCO Breaks levelization
#include <xrpl/rdb/DatabaseCon.h>

#include <boost/filesystem.hpp>  // VFALCO FIX: This include should not be here

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl {

class Rules;

//------------------------------------------------------------------------------

enum class SizedItem : std::size_t {
    SweepInterval = 0,
    TreeCacheSize,
    TreeCacheAge,
    LedgerSize,
    LedgerAge,
    LedgerFetch,
    HashNodeDbCache,
    TxnDbCache,
    LgrDbCache,
    OpenFinalLimit,
    BurstSize,
    RamSizeGb,
    AccountIdCacheSize,
};

/** Fee schedule for startup / standalone, and to vote for.
During voting ledgers, the FeeVote logic will try to move towards
these values when injecting fee-setting transactions.
A default-constructed Setup contains recommended values.
*/
struct FeeSetup
{
    /** The cost of a reference transaction in drops. */
    XRPAmount referenceFee{10};

    /** The account reserve requirement in drops. */
    XRPAmount accountReserve{10 * kDropsPerXrp};

    /** The per-owned item reserve requirement in drops. */
    XRPAmount ownerReserve{2 * kDropsPerXrp};

    /* (Remember to update the example cfg files when changing any of these
     * values.) */

    /** Convert to a Fees object for use with Ledger construction. */
    [[nodiscard]] Fees
    toFees() const
    {
        return Fees{referenceFee, accountReserve, ownerReserve};
    }
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
    static char const* const kConfigFileName;
    static char const* const kConfigLegacyName;
    static char const* const kDatabaseDirName;
    static char const* const kValidatorsFileName;

    /** Returns the full path and filename of the debug log file. */
    [[nodiscard]] boost::filesystem::path
    getDebugLogFile() const;

private:
    boost::filesystem::path configFile_;

public:
    boost::filesystem::path configDir;

private:
    boost::filesystem::path debugLogfile_;

    void
    load();
    beast::Journal const j_;

    bool quiet_ = false;   // Minimize logging verbosity.
    bool silent_ = false;  // No output to console after startup.
    /** Operate in stand-alone mode.

        In stand alone mode:

        - Peer connections are not attempted or accepted
        - The ledger is not advanced automatically.
        - If no ledger is loaded, the default ledger with the root
          account is created.
    */
    bool runStandalone_ = false;

    bool useTxTables_ = true;

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
    bool elbSupport = false;

    // Entries from [ips] config stanza
    std::vector<std::string> ips;

    // Entries from [ips_fixed] config stanza
    std::vector<std::string> ipsFixed;

    StartUpType startUp = StartUpType::Normal;

    bool startValid = false;

    std::string startLedger;

    std::optional<uint256> trapTxHash;

    // Network parameters
    uint32_t networkId = 0;

    // Note: The following parameters do not relate to the UNL or trust at all
    // Minimum number of nodes to consider the network present
    std::size_t networkQuorum = 1;

    // Peer networking parameters
    // 1 = relay, 0 = do not relay (but process), -1 = drop completely (do NOT
    // process)
    int relayUntrustedValidations = 1;
    int relayUntrustedProposals = 0;

    // True to ask peers not to relay current IP.
    bool peerPrivate = false;
    // peers_max is a legacy configuration, which is going to be replaced
    // with individual inbound peers peers_in_max and outbound peers
    // peers_out_max configuration. for now we support both the legacy and
    // the new configuration. if peers_max is configured then peers_in_max and
    // peers_out_max are ignored.
    std::size_t peersMax = 0;
    std::size_t peersOutMax = 0;
    std::size_t peersInMax = 0;

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
    int pathSearchOld = 2;
    int pathSearch = 2;
    int pathSearchFast = 2;
    int pathSearchMax = 3;

    // Validation
    std::optional<std::size_t> validationQuorum;  // validations to consider ledger authoritative

    FeeSetup fees;

    // Node storage configuration
    std::uint32_t ledgerHistory = 256;
    std::uint32_t fetchDepth = 1000000000;

    // Tunable that adjusts various parameters, typically associated
    // with hardware parameters (RAM size and CPU cores). The default
    // is 'tiny'.
    std::size_t nodeSize = 0;

    bool sslVerify = true;
    std::string sslVerifyFile;
    std::string sslVerifyDir;

    // Compression
    bool compression = false;

    // Enable the experimental Ledger Replay functionality
    bool ledgerReplay = false;

    // Work queue limits
    int maxTransactions = 250;
    static constexpr int kMaxJobQueueTx = 1000;
    static constexpr int kMinJobQueueTx = 100;

    // Amendment majority time
    std::chrono::seconds amendmentMajorityTime = kDefaultAmendmentMajorityTime;

    // Thread pool configuration (0 = choose for me)
    int workers = 0;          // jobqueue thread count. default: upto 6
    int ioWorkers = 0;        // io svc thread count. default: 2
    int prefetchWorkers = 0;  // prefetch thread count. default: 4

    // Can only be set in code, specifically unit tests
    bool forceMultiThread = false;

    // Normally the sweep timer is automatically deduced based on the node
    // size, but we allow admins to explicitly set it in the config.
    std::optional<int> sweepInterval;

    // Reduce-relay - Experimental parameters to control p2p routing algorithms

    // Enable base squelching of duplicate validation/proposal messages
    bool vpReduceRelayBaseSquelchEnable = false;

    /////////////////////  !!TEMPORARY CODE BLOCK!! ////////////////////////
    // Temporary squelching config for the peers selected as a source of  //
    // validator messages. The config must be removed once squelching is  //
    // made the default routing algorithm                                 //
    std::size_t vpReduceRelaySquelchMaxSelectedPeers = 5;
    /////////////////    END OF TEMPORARY CODE BLOCK    /////////////////////

    // Transaction reduce-relay feature
    bool txReduceRelayEnable = false;
    // If tx reduce-relay feature is disabled
    // and this flag is enabled then some
    // tx-related metrics is collected. It
    // is ignored if tx reduce-relay feature is
    // enabled. It is used in debugging to compare
    // metrics with the feature disabled/enabled.
    bool txReduceRelayMetrics = false;
    // Minimum peers a server should have before
    // selecting random peers
    std::size_t txReduceRelayMinPeers = 20;
    // Percentage of peers with the tx reduce-relay feature enabled
    // to relay to out of total active peers
    std::size_t txRelayPercentage = 25;

    // These override the command line client settings
    std::optional<beast::IP::Endpoint> rpcIp;

    std::unordered_set<uint256, beast::Uhash<>> features;

    std::string serverDomain;

    // How long can a peer remain in the "unknown" state
    std::chrono::seconds maxUnknownTime{600};

    // How long can a peer remain in the "diverged" state
    std::chrono::seconds maxDivergedTime{300};

    // Enable the beta API version
    bool betaRpcApi = false;

    // First, attempt to load the latest ledger directly from disk.
    bool fastLoad = false;
    // When starting xrpld with existing database it do not know it has those
    // ledgers locally until the server naturally tries to backfill. This makes
    // is difficult to test some functionality (in particular performance
    // testing sidechains). With this variable the user is able to force xrpld
    // to consider the ledger range to be present. It should be used for testing
    // only.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> forcedLedgerRangePresent;

    std::optional<std::size_t> validatorListThreshold;

public:
    Config();

    /* Be very careful to make sure these bool params
        are in the right order. */
    void
    setup(std::string const& strConf, bool bQuiet, bool bSilent, bool bStandalone);

    void
    setupControl(bool bQuiet, bool bSilent, bool bStandalone);

    /**
     *  Load the config from the contents of the string.
     *
     *  @param fileContents String representing the config contents.
     */
    void
    loadFromString(std::string const& fileContents);

    [[nodiscard]] bool
    quiet() const
    {
        return quiet_;
    }
    [[nodiscard]] bool
    silent() const
    {
        return silent_;
    }
    [[nodiscard]] bool
    standalone() const
    {
        return runStandalone_;
    }

    [[nodiscard]] bool
    useTxTables() const
    {
        return useTxTables_;
    }

    [[nodiscard]] bool
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
    [[nodiscard]] int
    getValueFor(SizedItem item, std::optional<std::size_t> node = std::nullopt) const;

    [[nodiscard]] beast::Journal
    journal() const
    {
        return j_;
    }
};

FeeSetup
setupFeeVote(Section const& section);

DatabaseCon::Setup
setupDatabaseCon(Config const& c, std::optional<beast::Journal> j = std::nullopt);

}  // namespace xrpl
