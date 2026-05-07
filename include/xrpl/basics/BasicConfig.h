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

struct Sections
{
    static constexpr auto kAMENDMENTS = "amendments";
    static constexpr auto kAMENDMENT_MAJORITY_TIME = "amendment_majority_time";
    static constexpr auto kBETA_RPC_API = "beta_rpc_api";
    static constexpr auto kCLUSTER_NODES = "cluster_nodes";
    static constexpr auto kCOMPRESSION = "compression";
    static constexpr auto kCRAWL = "crawl";
    static constexpr auto kDATABASE_PATH = "database_path";
    static constexpr auto kDEBUG_LOGFILE = "debug_logfile";
    static constexpr auto kELB_SUPPORT = "elb_support";
    static constexpr auto kFEATURES = "features";
    static constexpr auto kFEE_DEFAULT = "fee_default";
    static constexpr auto kFETCH_DEPTH = "fetch_depth";
    static constexpr auto kHASHROUTER = "hashrouter";
    static constexpr auto kIMPORT_NODE_DATABASE = "import_db";
    static constexpr auto kINSIGHT = "insight";
    static constexpr auto kIO_WORKERS = "io_workers";
    static constexpr auto kIPS = "ips";
    static constexpr auto kIPS_FIXED = "ips_fixed";
    static constexpr auto kLEDGER_HISTORY = "ledger_history";
    static constexpr auto kLEDGER_REPLAY = "ledger_replay";
    static constexpr auto kLEDGER_TX_TABLES = "ledger_tx_tables";
    static constexpr auto kMAX_TRANSACTIONS = "max_transactions";
    static constexpr auto kNETWORK_ID = "network_id";
    static constexpr auto kNETWORK_QUORUM = "network_quorum";
    static constexpr auto kNODE_DATABASE = "node_db";
    static constexpr auto kNODE_SEED = "node_seed";
    static constexpr auto kNODE_SIZE = "node_size";
    static constexpr auto kOVERLAY = "overlay";
    static constexpr auto kPATH_SEARCH = "path_search";
    static constexpr auto kPATH_SEARCH_FAST = "path_search_fast";
    static constexpr auto kPATH_SEARCH_MAX = "path_search_max";
    static constexpr auto kPATH_SEARCH_OLD = "path_search_old";
    static constexpr auto kPEER_PRIVATE = "peer_private";
    static constexpr auto kPEERS_IN_MAX = "peers_in_max";
    static constexpr auto kPEERS_MAX = "peers_max";
    static constexpr auto kPEERS_OUT_MAX = "peers_out_max";
    static constexpr auto kPERF = "perf";
    static constexpr auto kPORT_GRPC = "port_grpc";
    static constexpr auto kPORT_PEER = "port_peer";
    static constexpr auto kPORT_RPC = "port_rpc";
    static constexpr auto kPORT_WS = "port_ws";
    static constexpr auto kPORT_WSS_ADMIN = "port_wss_admin";
    static constexpr auto kPREFETCH_WORKERS = "prefetch_workers";
    static constexpr auto kREDUCE_RELAY = "reduce_relay";
    static constexpr auto kRELATIONAL_DB = "relational_db";
    static constexpr auto kRELAY_PROPOSALS = "relay_proposals";
    static constexpr auto kRELAY_VALIDATIONS = "relay_validations";
    static constexpr auto kRPC_STARTUP = "rpc_startup";
    static constexpr auto kSERVER = "server";
    static constexpr auto kSERVER_DOMAIN = "server_domain";
    static constexpr auto kSIGNING_SUPPORT = "signing_support";
    static constexpr auto kSNTP = "sntp_servers";
    static constexpr auto kSQDB = "sqdb";
    static constexpr auto kSQLITE = "sqlite";
    static constexpr auto kSSL_VERIFY = "ssl_verify";
    static constexpr auto kSSL_VERIFY_DIR = "ssl_verify_dir";
    static constexpr auto kSSL_VERIFY_FILE = "ssl_verify_file";
    static constexpr auto kSWEEP_INTERVAL = "sweep_interval";
    static constexpr auto kTRANSACTION_QUEUE = "transaction_queue";
    static constexpr auto kVALIDATION_SEED = "validation_seed";
    static constexpr auto kVALIDATOR_KEYS = "validator_keys";
    static constexpr auto kVALIDATOR_KEY_REVOCATION = "validator_key_revocation";
    static constexpr auto kVALIDATOR_LIST_KEYS = "validator_list_keys";
    static constexpr auto kVALIDATOR_LIST_SITES = "validator_list_sites";
    static constexpr auto kVALIDATOR_LIST_THRESHOLD = "validator_list_threshold";
    static constexpr auto kVALIDATOR_TOKEN = "validator_token";
    static constexpr auto kVALIDATORS = "validators";
    static constexpr auto kVALIDATORS_FILE = "validators_file";
    static constexpr auto kVETO_AMENDMENTS = "veto_amendments";
    static constexpr auto kVL = "vl";
    static constexpr auto kVOTING = "voting";
    static constexpr auto kWORKERS = "workers";
};

struct Keys
{
    static constexpr auto kACCOUNT_RESERVE = "account_reserve";
    static constexpr auto kADDRESS = "address";
    static constexpr auto kADMIN = "admin";
    static constexpr auto kADMIN_PASSWORD = "admin_password";
    static constexpr auto kADMIN_USER = "admin_user";
    static constexpr auto kADVISORY_DELETE = "advisory_delete";
    static constexpr auto kAGE_THRESHOLD_SECONDS = "age_threshold_seconds";
    static constexpr auto kBACK_OFF = "backOff";
    static constexpr auto kBACK_OFF_MILLISECONDS = "back_off_milliseconds";
    static constexpr auto kBACKEND = "backend";
    static constexpr auto kBBT_OPTIONS = "bbt_options";
    static constexpr auto kBG_THREADS = "bg_threads";
    static constexpr auto kBLOCK_SIZE = "block_size";
    static constexpr auto kCACHE_MB = "cache_mb";
    static constexpr auto kCLIENT_MAX_WINDOW_BITS = "client_max_window_bits";
    static constexpr auto kCLIENT_NO_CONTEXT_TAKEOVER = "client_no_context_takeover";
    static constexpr auto kCOMPRESS_LEVEL = "compress_level";
    static constexpr auto kCOUNTS = "counts";
    static constexpr auto kDELETE_BATCH = "delete_batch";
    static constexpr auto kEARLIEST_SEQ = "earliest_seq";
    static constexpr auto kFAST_LOAD = "fast_load";
    static constexpr auto kFILE_SIZE_MB = "file_size_mb";
    static constexpr auto kFILE_SIZE_MULT = "file_size_mult";
    static constexpr auto kFILTER_BITS = "filter_bits";
    static constexpr auto kFILTER_FULL = "filter_full";
    static constexpr auto kHARD_SET = "hard_set";
    static constexpr auto kHIGH_THREADS = "high_threads";
    static constexpr auto kHOLD_TIME = "hold_time";
    static constexpr auto kIP = "ip";
    static constexpr auto kJOURNAL_MODE = "journal_mode";
    static constexpr auto kJOURNAL_SIZE_LIMIT = "journal_size_limit";
    static constexpr auto kLEDGERS_IN_QUEUE = "ledgers_in_queue";
    static constexpr auto kLIMIT = "limit";
    static constexpr auto kLOG_INTERVAL = "log_interval";
    static constexpr auto kMAX_DIVERGED_TIME = "max_diverged_time";
    static constexpr auto kMAX_LEDGER_COUNTS_TO_STORE = "max_ledger_counts_to_store";
    static constexpr auto kMAX_UNKNOWN_TIME = "max_unknown_time";
    static constexpr auto kMEMORY_LEVEL = "memory_level";
    static constexpr auto kMIN_LEDGERS_TO_COMPUTE_SIZE_LIMIT = "min_ledgers_to_compute_size_limit";
    static constexpr auto kMINIMUM_QUEUE_SIZE = "minimum_queue_size";
    static constexpr auto kMINIMUM_TXN_IN_LEDGER = "minimum_txn_in_ledger";
    static constexpr auto kMINIMUM_TXN_IN_LEDGER_STANDALONE = "minimum_txn_in_ledger_standalone";
    static constexpr auto kNORMAL_CONSENSUS_INCREASE_PERCENT = "normal_consensus_increase_percent";
    static constexpr auto kNUDB_BLOCK_SIZE = "nudb_block_size";
    static constexpr auto kONLINE_DELETE = "online_delete";
    static constexpr auto kOPEN_FILES = "open_files";
    static constexpr auto kOPTIONS = "options";
    static constexpr auto kOVERLAY = "overlay";
    static constexpr auto kOWNER_RESERVE = "owner_reserve";
    static constexpr auto kPAGE_SIZE = "page_size";
    static constexpr auto kPASSWORD = "password";
    static constexpr auto kPATH = "path";
    static constexpr auto kPERMESSAGE_DEFLATE = "permessage_deflate";
    static constexpr auto kPORT = "port";
    static constexpr auto kPREFIX = "prefix";
    static constexpr auto kPROTOCOL = "protocol";
    static constexpr auto kRECOVERY_WAIT_SECONDS = "recovery_wait_seconds";
    static constexpr auto kREFERENCE_FEE = "reference_fee";
    static constexpr auto kRELAY_TIME = "relay_time";
    static constexpr auto kRETRY_SEQUENCE_PERCENT = "retry_sequence_percent";
    static constexpr auto kRQ_BUNDLE = "rq_bundle";
    static constexpr auto kSAFETY_LEVEL = "safety_level";
    static constexpr auto kSECURE_GATEWAY = "secureGateway";
    static constexpr auto kSEND_QUEUE_LIMIT = "send_queue_limit";
    static constexpr auto kSERVER = "server";
    static constexpr auto kSERVER_MAX_WINDOW_BITS = "server_max_window_bits";
    static constexpr auto kSERVER_NO_CONTEXT_TAKEOVER = "server_no_context_takeover";
    static constexpr auto kSSL_CERT = "ssl_cert";
    static constexpr auto kSSL_CERT_CHAIN = "ssl_cert_chain";
    static constexpr auto kSSL_CHAIN = "ssl_chain";
    static constexpr auto kSSL_CIPHERS = "ssl_ciphers";
    static constexpr auto kSSL_CLIENT_CA = "ssl_client_ca";
    static constexpr auto kSSL_KEY = "ssl_key";
    static constexpr auto kSYNCHRONOUS = "synchronous";
    static constexpr auto kTARGET_TXN_IN_LEDGER = "target_txn_in_ledger";
    static constexpr auto kTEMP_STORE = "temp_store";
    static constexpr auto kTX_ENABLE = "tx_enable";
    static constexpr auto kTX_METRICS = "tx_metrics";
    static constexpr auto kTX_MIN_PEERS = "tx_min_peers";
    static constexpr auto kTX_RELAY_PERCENTAGE = "tx_relay_percentage";
    static constexpr auto kTYPE = "type";
    static constexpr auto kUNIVERSAL_COMPACTION = "universal_compaction";
    static constexpr auto kUNL = "unl";
    static constexpr auto kUSE_TX_TABLES = "use_tx_tables";
    static constexpr auto kUSER = "user";
    static constexpr auto kVP_BASE_SQUELCH_ENABLE = "vp_base_squelch_enable";
    static constexpr auto kVP_BASE_SQUELCH_MAX_SELECTED_PEERS =
        "vp_base_squelch_max_selected_peers";
    static constexpr auto kVP_ENABLE = "vp_enable";
};

}  // namespace xrpl
