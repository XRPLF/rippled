#pragma once

#include <xrpl/basics/contract.h>

#include <boost/beast/core/string.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xrpl {

using IniFileSections = std::unordered_map<std::string, std::vector<std::string>>;

//------------------------------------------------------------------------------

/** Holds a collection of configuration values.
    A configuration file contains zero or more sections.
*/
class Section
{
private:
    std::string name_;
    std::unordered_map<std::string, std::string> lookup_;
    std::vector<std::string> lines_;
    std::vector<std::string> values_;
    bool hadTrailingComments_ = false;

    using const_iterator = decltype(lookup_)::const_iterator;

public:
    /** Create an empty section. */
    explicit Section(std::string name = "");

    /** Returns the name of this section. */
    [[nodiscard]] std::string const&
    name() const
    {
        return name_;
    }

    /** Returns all the lines in the section.
        This includes everything.
    */
    [[nodiscard]] std::vector<std::string> const&
    lines() const
    {
        return lines_;
    }

    /** Returns all the values in the section.
        Values are non-empty lines which are not key/value pairs.
    */
    [[nodiscard]] std::vector<std::string> const&
    values() const
    {
        return values_;
    }

    /**
     * Set the legacy value for this section.
     */
    void
    legacy(std::string value)
    {
        if (lines_.empty())
        {
            lines_.emplace_back(std::move(value));
        }
        else
        {
            lines_[0] = std::move(value);
        }
    }

    /**
     * Get the legacy value for this section.
     *
     * @return The retrieved value. A section with an empty legacy value returns
               an empty string.
     */
    [[nodiscard]] std::string
    legacy() const
    {
        if (lines_.empty())
            return "";
        if (lines_.size() > 1)
        {
            Throw<std::runtime_error>(
                "A legacy value must have exactly one line. Section: " + name_);
        }
        return lines_[0];
    }

    /** Set a key/value pair.
        The previous value is discarded.
    */
    void
    set(std::string const& key, std::string const& value);

    /** Append a set of lines to this section.
        Lines containing key/value pairs are added to the map,
        else they are added to the values list. Everything is
        added to the lines list.
    */
    void
    append(std::vector<std::string> const& lines);

    /** Append a line to this section. */
    void
    append(std::string const& line)
    {
        append(std::vector<std::string>{line});
    }

    /** Returns `true` if a key with the given name exists. */
    [[nodiscard]] bool
    exists(std::string const& name) const;

    template <class T = std::string>
    [[nodiscard]] std::optional<T>
    get(std::string const& name) const
    {
        auto const iter = lookup_.find(name);
        if (iter == lookup_.end())
            return std::nullopt;
        return boost::lexical_cast<T>(iter->second);
    }

    /// Returns a value if present, else another value.
    template <class T>
    [[nodiscard]] T
    valueOr(std::string const& name, T const& other) const
    {
        auto const v = get<T>(name);
        return v.has_value() ? *v : other;
    }

    // indicates if trailing comments were seen
    // during the appending of any lines/values
    [[nodiscard]] bool
    hadTrailingComments() const
    {
        return hadTrailingComments_;
    }

    friend std::ostream&
    operator<<(std::ostream&, Section const& section);

    // Returns `true` if there are no key/value pairs.
    [[nodiscard]] bool
    empty() const
    {
        return lookup_.empty();
    }

    // Returns the number of key/value pairs.
    [[nodiscard]] std::size_t
    size() const
    {
        return lookup_.size();
    }

    // For iteration of key/value pairs.
    [[nodiscard]] const_iterator
    begin() const
    {
        return lookup_.cbegin();
    }

    // For iteration of key/value pairs.
    [[nodiscard]] const_iterator
    cbegin() const
    {
        return lookup_.cbegin();
    }

    // For iteration of key/value pairs.
    [[nodiscard]] const_iterator
    end() const
    {
        return lookup_.cend();
    }

    // For iteration of key/value pairs.
    [[nodiscard]] const_iterator
    cend() const
    {
        return lookup_.cend();
    }
};

//------------------------------------------------------------------------------

/** Holds unparsed configuration information.
    The raw data sections are processed with intermediate parsers specific
    to each module instead of being all parsed in a central location.
*/
class BasicConfig
{
private:
    std::unordered_map<std::string, Section> map_;

public:
    /** Returns `true` if a section with the given name exists. */
    [[nodiscard]] bool
    exists(std::string const& name) const;

    /** Returns the section with the given name.
        If the section does not exist, an empty section is returned.
    */
    /** @{ */
    Section&
    section(std::string const& name);

    [[nodiscard]] Section const&
    section(std::string const& name) const;

    Section const&
    operator[](std::string const& name) const
    {
        return section(name);
    }

    Section&
    operator[](std::string const& name)
    {
        return section(name);
    }
    /** @} */

    /** Overwrite a key/value pair with a command line argument
        If the section does not exist it is created.
        The previous value, if any, is overwritten.
    */
    void
    overwrite(std::string const& section, std::string const& key, std::string const& value);

    /** Remove all the key/value pairs from the section.
     */
    void
    deprecatedClearSection(std::string const& section);

    /**
     *  Set a value that is not a key/value pair.
     *
     *  The value is stored as the section's first value and may be retrieved
     *  through section::legacy.
     *
     *  @param section Name of the section to modify.
     *  @param value Contents of the legacy value.
     */
    void
    legacy(std::string const& section, std::string value);

    /**
     *  Get the legacy value of a section. A section with a
     *  single-line value may be retrieved as a legacy value.
     *
     *  @param sectionName Retrieve the contents of this section's
     *         legacy value.
     *  @return Contents of the legacy value.
     */
    [[nodiscard]] std::string
    legacy(std::string const& sectionName) const;

    friend std::ostream&
    operator<<(std::ostream& ss, BasicConfig const& c);

    // indicates if trailing comments were seen
    // in any loaded Sections
    [[nodiscard]] bool
    hadTrailingComments() const
    {
        return std::ranges::any_of(map_, [](auto s) { return s.second.hadTrailingComments(); });
    }

protected:
    void
    build(IniFileSections const& ifs);
};

//------------------------------------------------------------------------------

/** Set a value from a configuration Section
    If the named value is not found or doesn't parse as a T,
    the variable is unchanged.
    @return `true` if value was set.
*/
template <class T>
bool
set(T& target, std::string const& name, Section const& section)
{
    bool foundAndValid = false;
    try
    {
        auto const val = section.get<T>(name);
        if ((foundAndValid = val.has_value()))
            target = *val;
    }
    catch (boost::bad_lexical_cast const&)  // NOLINT(bugprone-empty-catch)
    {
    }
    return foundAndValid;
}

/** Set a value from a configuration Section
    If the named value is not found or doesn't cast to T,
    the variable is assigned the default.
    @return `true` if the named value was found and is valid.
*/
template <class T>
bool
set(T& target, T const& defaultValue, std::string const& name, Section const& section)
{
    bool const foundAndValid = set<T>(target, name, section);
    if (!foundAndValid)
        target = defaultValue;
    return foundAndValid;
}

/** Retrieve a key/value pair from a section.
    @return The value string converted to T if it exists
            and can be parsed, or else defaultValue.
*/
// NOTE This routine might be more clumsy than the previous two
template <class T = std::string>
T
get(Section const& section, std::string const& name, T const& defaultValue = T{})
{
    try
    {
        return section.valueOr<T>(name, defaultValue);
    }
    catch (boost::bad_lexical_cast const&)  // NOLINT(bugprone-empty-catch)
    {
    }
    return defaultValue;
}

inline std::string
get(Section const& section, std::string const& name, char const* defaultValue)
{
    try
    {
        auto const val = section.get(name);
        if (val.has_value())
            return *val;
    }
    catch (boost::bad_lexical_cast const&)  // NOLINT(bugprone-empty-catch)
    {
    }
    return defaultValue;
}

template <class T>
bool
getIfExists(Section const& section, std::string const& name, T& v)
{
    return set<T>(v, name, section);
}

template <>
inline bool
getIfExists<bool>(Section const& section, std::string const& name, bool& v)
{
    int intVal = 0;
    auto stat = getIfExists(section, name, intVal);
    if (stat)
        v = bool(intVal);
    return stat;
}

//------------------------------------------------------------------------------

// Section name constants.
inline constexpr auto kSECTION_AMENDMENTS = "amendments";
inline constexpr auto kSECTION_AMENDMENT_MAJORITY_TIME = "amendment_majority_time";
inline constexpr auto kSECTION_BETA_RPC_API = "beta_rpc_api";
inline constexpr auto kSECTION_CLUSTER_NODES = "cluster_nodes";
inline constexpr auto kSECTION_COMPRESSION = "compression";
inline constexpr auto kSECTION_CRAWL = "crawl";
inline constexpr auto kSECTION_DATABASE_PATH = "database_path";
inline constexpr auto kSECTION_DEBUG_LOGFILE = "debug_logfile";
inline constexpr auto kSECTION_ELB_SUPPORT = "elb_support";
inline constexpr auto kSECTION_FEE_DEFAULT = "fee_default";
inline constexpr auto kSECTION_FEATURES = "features";
inline constexpr auto kSECTION_FETCH_DEPTH = "fetch_depth";
inline constexpr auto kSECTION_HASHROUTER = "hashrouter";
inline constexpr auto kSECTION_IMPORT_NODE_DATABASE = "import_db";
inline constexpr auto kSECTION_INSIGHT = "insight";
inline constexpr auto kSECTION_IO_WORKERS = "io_workers";
inline constexpr auto kSECTION_IPS = "ips";
inline constexpr auto kSECTION_IPS_FIXED = "ips_fixed";
inline constexpr auto kSECTION_LEDGER_HISTORY = "ledger_history";
inline constexpr auto kSECTION_LEDGER_REPLAY = "ledger_replay";
inline constexpr auto kSECTION_LEDGER_TX_TABLES = "ledger_tx_tables";
inline constexpr auto kSECTION_MAX_TRANSACTIONS = "max_transactions";
inline constexpr auto kSECTION_NETWORK_ID = "network_id";
inline constexpr auto kSECTION_NETWORK_QUORUM = "network_quorum";
inline constexpr auto kSECTION_NODE_DATABASE = "node_db";
inline constexpr auto kSECTION_NODE_SEED = "node_seed";
inline constexpr auto kSECTION_NODE_SIZE = "node_size";
inline constexpr auto kSECTION_OVERLAY = "overlay";
inline constexpr auto kSECTION_PATH_SEARCH_OLD = "path_search_old";
inline constexpr auto kSECTION_PATH_SEARCH = "path_search";
inline constexpr auto kSECTION_PATH_SEARCH_FAST = "path_search_fast";
inline constexpr auto kSECTION_PATH_SEARCH_MAX = "path_search_max";
inline constexpr auto kSECTION_PEER_PRIVATE = "peer_private";
inline constexpr auto kSECTION_PEERS_MAX = "peers_max";
inline constexpr auto kSECTION_PEERS_IN_MAX = "peers_in_max";
inline constexpr auto kSECTION_PEERS_OUT_MAX = "peers_out_max";
inline constexpr auto kSECTION_PERF = "perf";
inline constexpr auto kSECTION_PORT_GRPC = "port_grpc";
inline constexpr auto kSECTION_PORT_PEER = "port_peer";
inline constexpr auto kSECTION_PORT_RPC = "port_rpc";
inline constexpr auto kSECTION_PORT_WS = "port_ws";
inline constexpr auto kSECTION_PORT_WSS_ADMIN = "port_wss_admin";
inline constexpr auto kSECTION_PREFETCH_WORKERS = "prefetch_workers";
inline constexpr auto kSECTION_REDUCE_RELAY = "reduce_relay";
inline constexpr auto kSECTION_RELATIONAL_DB = "relational_db";
inline constexpr auto kSECTION_RELAY_PROPOSALS = "relay_proposals";
inline constexpr auto kSECTION_RELAY_VALIDATIONS = "relay_validations";
inline constexpr auto kSECTION_RPC_STARTUP = "rpc_startup";
inline constexpr auto kSECTION_SERVER = "server";
inline constexpr auto kSECTION_SERVER_DOMAIN = "server_domain";
inline constexpr auto kSECTION_SIGNING_SUPPORT = "signing_support";
inline constexpr auto kSECTION_SNTP = "sntp_servers";
inline constexpr auto kSECTION_SQDB = "sqdb";
inline constexpr auto kSECTION_SQLITE = "sqlite";
inline constexpr auto kSECTION_SSL_VERIFY = "ssl_verify";
inline constexpr auto kSECTION_SSL_VERIFY_FILE = "ssl_verify_file";
inline constexpr auto kSECTION_SSL_VERIFY_DIR = "ssl_verify_dir";
inline constexpr auto kSECTION_SWEEP_INTERVAL = "sweep_interval";
inline constexpr auto kSECTION_TRANSACTION_QUEUE = "transaction_queue";
inline constexpr auto kSECTION_VALIDATORS_FILE = "validators_file";
inline constexpr auto kSECTION_VALIDATION_SEED = "validation_seed";
inline constexpr auto kSECTION_VALIDATOR_KEYS = "validator_keys";
inline constexpr auto kSECTION_VALIDATOR_KEY_REVOCATION = "validator_key_revocation";
inline constexpr auto kSECTION_VALIDATOR_LIST_KEYS = "validator_list_keys";
inline constexpr auto kSECTION_VALIDATOR_LIST_SITES = "validator_list_sites";
inline constexpr auto kSECTION_VALIDATOR_LIST_THRESHOLD = "validator_list_threshold";
inline constexpr auto kSECTION_VALIDATORS = "validators";
inline constexpr auto kSECTION_VALIDATOR_TOKEN = "validator_token";
inline constexpr auto kSECTION_VETO_AMENDMENTS = "veto_amendments";
inline constexpr auto kSECTION_VL = "vl";
inline constexpr auto kSECTION_VOTING = "voting";
inline constexpr auto kSECTION_WORKERS = "workers";

// Key name constants: nodestore backend.
inline constexpr auto kKEY_ADVISORY_DELETE = "advisory_delete";
inline constexpr auto kKEY_AGE_THRESHOLD_SECONDS = "age_threshold_seconds";
inline constexpr auto kKEY_BACK_OFF = "backOff";
inline constexpr auto kKEY_BACK_OFF_MILLISECONDS = "back_off_milliseconds";
inline constexpr auto kKEY_BBT_OPTIONS = "bbt_options";
inline constexpr auto kKEY_BG_THREADS = "bg_threads";
inline constexpr auto kKEY_BLOCK_SIZE = "block_size";
inline constexpr auto kKEY_CACHE_MB = "cache_mb";
inline constexpr auto kKEY_DELETE_BATCH = "delete_batch";
inline constexpr auto kKEY_EARLIEST_SEQ = "earliest_seq";
inline constexpr auto kKEY_FAST_LOAD = "fast_load";
inline constexpr auto kKEY_FILE_SIZE_MB = "file_size_mb";
inline constexpr auto kKEY_FILE_SIZE_MULT = "file_size_mult";
inline constexpr auto kKEY_FILTER_BITS = "filter_bits";
inline constexpr auto kKEY_FILTER_FULL = "filter_full";
inline constexpr auto kKEY_HARD_SET = "hard_set";
inline constexpr auto kKEY_HIGH_THREADS = "high_threads";
inline constexpr auto kKEY_NUDB_BLOCK_SIZE = "nudb_block_size";
inline constexpr auto kKEY_ONLINE_DELETE = "online_delete";
inline constexpr auto kKEY_OPEN_FILES = "open_files";
inline constexpr auto kKEY_OPTIONS = "options";
inline constexpr auto kKEY_PATH = "path";
inline constexpr auto kKEY_RECOVERY_WAIT_SECONDS = "recovery_wait_seconds";
inline constexpr auto kKEY_RQ_BUNDLE = "rq_bundle";
inline constexpr auto kKEY_TYPE = "type";
inline constexpr auto kKEY_UNIVERSAL_COMPACTION = "universal_compaction";
inline constexpr auto kKEY_USE_TX_TABLES = "use_tx_tables";

// Key name constants: port configuration.
inline constexpr auto kKEY_ADMIN = "admin";
inline constexpr auto kKEY_ADMIN_PASSWORD = "admin_password";
inline constexpr auto kKEY_ADMIN_USER = "admin_user";
inline constexpr auto kKEY_CLIENT_MAX_WINDOW_BITS = "client_max_window_bits";
inline constexpr auto kKEY_CLIENT_NO_CONTEXT_TAKEOVER = "client_no_context_takeover";
inline constexpr auto kKEY_COMPRESS_LEVEL = "compress_level";
inline constexpr auto kKEY_IP = "ip";
inline constexpr auto kKEY_LIMIT = "limit";
inline constexpr auto kKEY_MEMORY_LEVEL = "memory_level";
inline constexpr auto kKEY_PASSWORD = "password";
inline constexpr auto kKEY_PERMESSAGE_DEFLATE = "permessage_deflate";
inline constexpr auto kKEY_PORT = "port";
inline constexpr auto kKEY_PROTOCOL = "protocol";
inline constexpr auto kKEY_SECURE_GATEWAY = "secureGateway";
inline constexpr auto kKEY_SEND_QUEUE_LIMIT = "send_queue_limit";
inline constexpr auto kKEY_SERVER_MAX_WINDOW_BITS = "server_max_window_bits";
inline constexpr auto kKEY_SERVER_NO_CONTEXT_TAKEOVER = "server_no_context_takeover";
inline constexpr auto kKEY_SSL_CERT = "ssl_cert";
inline constexpr auto kKEY_SSL_CERT_CHAIN = "ssl_cert_chain";
inline constexpr auto kKEY_SSL_CHAIN = "ssl_chain";
inline constexpr auto kKEY_SSL_CIPHERS = "ssl_ciphers";
inline constexpr auto kKEY_SSL_CLIENT_CA = "ssl_client_ca";
inline constexpr auto kKEY_SSL_KEY = "ssl_key";
inline constexpr auto kKEY_USER = "user";

// Key name constants: reduce_relay section.
inline constexpr auto kKEY_TX_ENABLE = "tx_enable";
inline constexpr auto kKEY_TX_METRICS = "tx_metrics";
inline constexpr auto kKEY_TX_MIN_PEERS = "tx_min_peers";
inline constexpr auto kKEY_TX_RELAY_PERCENTAGE = "tx_relay_percentage";
inline constexpr auto kKEY_VP_BASE_SQUELCH_ENABLE = "vp_base_squelch_enable";
inline constexpr auto kKEY_VP_BASE_SQUELCH_MAX_SELECTED_PEERS =
    "vp_base_squelch_max_selected_peers";
inline constexpr auto kKEY_VP_ENABLE = "vp_enable";

// Key name constants: overlay section.
inline constexpr auto kKEY_COUNTS = "counts";
inline constexpr auto kKEY_MAX_DIVERGED_TIME = "max_diverged_time";
inline constexpr auto kKEY_MAX_UNKNOWN_TIME = "max_unknown_time";
inline constexpr auto kKEY_OVERLAY = "overlay";
inline constexpr auto kKEY_SERVER = "server";
inline constexpr auto kKEY_UNL = "unl";

// Key name constants: transaction_queue section.
inline constexpr auto kKEY_LEDGERS_IN_QUEUE = "ledgers_in_queue";
inline constexpr auto kKEY_MAX_LEDGER_COUNTS_TO_STORE = "max_ledger_counts_to_store";
inline constexpr auto kKEY_MIN_LEDGERS_TO_COMPUTE_SIZE_LIMIT = "min_ledgers_to_compute_size_limit";
inline constexpr auto kKEY_MINIMUM_QUEUE_SIZE = "minimum_queue_size";
inline constexpr auto kKEY_MINIMUM_TXN_IN_LEDGER = "minimum_txn_in_ledger";
inline constexpr auto kKEY_MINIMUM_TXN_IN_LEDGER_STANDALONE = "minimum_txn_in_ledger_standalone";
inline constexpr auto kKEY_NORMAL_CONSENSUS_INCREASE_PERCENT = "normal_consensus_increase_percent";
inline constexpr auto kKEY_RETRY_SEQUENCE_PERCENT = "retry_sequence_percent";
inline constexpr auto kKEY_TARGET_TXN_IN_LEDGER = "target_txn_in_ledger";

// Key name constants: hashrouter section.
inline constexpr auto kKEY_HOLD_TIME = "hold_time";
inline constexpr auto kKEY_RELAY_TIME = "relay_time";

// Key name constants: sqlite section.
inline constexpr auto kKEY_JOURNAL_MODE = "journal_mode";
inline constexpr auto kKEY_JOURNAL_SIZE_LIMIT = "journal_size_limit";
inline constexpr auto kKEY_PAGE_SIZE = "page_size";
inline constexpr auto kKEY_SAFETY_LEVEL = "safety_level";
inline constexpr auto kKEY_SYNCHRONOUS = "synchronous";
inline constexpr auto kKEY_TEMP_STORE = "temp_store";

// Key name constants: insight section.
inline constexpr auto kKEY_ADDRESS = "address";
inline constexpr auto kKEY_BACKEND = "backend";
inline constexpr auto kKEY_PREFIX = "prefix";

// Key name constants: voting section.
inline constexpr auto kKEY_ACCOUNT_RESERVE = "account_reserve";
inline constexpr auto kKEY_OWNER_RESERVE = "owner_reserve";
inline constexpr auto kKEY_REFERENCE_FEE = "reference_fee";

// Key name constants: perflog section.
inline constexpr auto kKEY_LOG_INTERVAL = "log_interval";

}  // namespace xrpl
