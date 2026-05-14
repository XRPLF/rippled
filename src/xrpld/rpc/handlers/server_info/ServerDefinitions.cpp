/** @file
 *  Implements the `server_definitions` RPC handler, which exposes the
 *  complete XRPL protocol schema as a static JSON payload.
 *
 *  The payload is assembled once at startup (Meyers singleton), hashed via
 *  SHA-512-half for cache validation, and then served read-only for the
 *  lifetime of the process.  Clients that already hold a matching hash
 *  receive only the hash in the response, avoiding re-transfer of the
 *  large definitions object on every call.
 */
#include <xrpld/rpc/handlers/server_info/ServerDefinitions.h>

#include <xrpld/rpc/Context.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace xrpl {

namespace detail {

/** Immutable protocol schema snapshot used by the `server_definitions` RPC endpoint.
 *
 *  Built exactly once at first use (Meyers singleton via `getDefinitions()`).
 *  Populates nine top-level JSON sections — TYPES, LEDGER_ENTRY_TYPES,
 *  TRANSACTION_TYPES, FIELDS, TRANSACTION_RESULTS, TRANSACTION_FORMATS,
 *  LEDGER_ENTRY_FORMATS, TRANSACTION_FLAGS, LEDGER_ENTRY_FLAGS, and
 *  ACCOUNT_SET_FLAGS — then computes a SHA-512-half fingerprint of the
 *  serialized payload for client-side cache invalidation.
 *
 *  @note Thread-safe after construction; all public methods are read-only.
 */
class ServerDefinitions
{
private:
    /** Convert a raw `STI_`-stripped type name to the client-facing naming convention.
     *
     *  Applies the following rules in order:
     *  - Names containing `UINT` with a fixed bit-width (128, 160, 192, 256, 384, 512)
     *      become `Hash<N>` (e.g., `UINT256` → `Hash256`), reflecting that these
     *      fixed-width types are used as cryptographic digests, not arithmetic integers.
     *  - Other `UINT` names become `UInt<N>` (e.g., `UINT32` → `UInt32`).
     *  - A fixed lookup table handles special cases: `VL` → `Blob`,
     *      `ACCOUNT` → `AccountID`, `OBJECT` → `STObject`, `ARRAY` → `STArray`, etc.
     *  - All remaining names are converted from SCREAMING_SNAKE_CASE to CamelCase.
     *
     *  @param inp Raw type name with the `STI_` prefix already removed.
     *  @return Client-facing type name string.
     */
    static std::string
    translate(std::string const& inp);

    /** SHA-512-half fingerprint of the serialized definitions payload. */
    uint256 defsHash_;

    /** The complete protocol schema as a JSON object. */
    json::Value defs_;

public:
    /** Construct and populate the full protocol schema.
     *
     *  Iterates `SField::getKnownCodeToField()`, `LedgerFormats`, `TxFormats`,
     *  `transResults()`, `getAllTxFlags()`, `getAllLedgerFlags()`, and
     *  `getAsfFlagMap()` to build all nine sections.  After all sections are
     *  assembled, serializes the object via `Json::FastWriter` and stores the
     *  SHA-512-half of the result in both `defsHash_` and `defs_[jss::hash]`.
     */
    ServerDefinitions();

    /** Return true if @p hash matches the current definitions fingerprint.
     *
     *  Used by `doServerDefinitions` to implement the bandwidth-saving
     *  short-circuit: when the caller already holds an up-to-date copy,
     *  only the hash is returned rather than the full payload.
     *
     *  @param hash The `uint256` hash supplied by the caller.
     *  @return `true` if @p hash equals `defsHash_`; `false` otherwise.
     */
    [[nodiscard]] bool
    hashMatches(uint256 hash) const
    {
        return defsHash_ == hash;
    }

    /** Return a const reference to the complete definitions JSON object.
     *
     *  The returned reference is valid for the lifetime of the process.
     *
     *  @return The fully populated protocol schema, including the `hash` field.
     */
    [[nodiscard]] json::Value const&
    get() const
    {
        return defs_;
    }
};

std::string
ServerDefinitions::translate(std::string const& inp)
{
    auto replace = [&](std::string_view oldStr, std::string_view newStr) -> std::string {
        std::string out = inp;
        boost::replace_all(out, oldStr, newStr);
        return out;
    };

    // TODO: use string::contains with C++23 once the minimum language version is raised.
    auto contains = [&](std::string_view s) -> bool { return inp.find(s) != std::string::npos; };

    if (contains("UINT"))
    {
        if (contains("512") || contains("384") || contains("256") || contains("192") ||
            contains("160") || contains("128"))
        {
            return replace("UINT", "Hash");
        }

        return replace("UINT", "UInt");
    }

    static std::unordered_map<std::string_view, std::string_view> const kREPLACEMENTS{
        {"OBJECT", "STObject"},
        {"ARRAY", "STArray"},
        {"ACCOUNT", "AccountID"},
        {"LEDGERENTRY", "LedgerEntry"},
        {"NOTPRESENT", "NotPresent"},
        {"PATHSET", "PathSet"},
        {"VL", "Blob"},
        {"XCHAIN_BRIDGE", "XChainBridge"},
    };

    if (auto const& it = kREPLACEMENTS.find(inp); it != kREPLACEMENTS.end())
    {
        return std::string(it->second);
    }

    std::string out;
    size_t pos = 0;
    std::string inpToProcess = inp;

    // convert snake_case to CamelCase
    for (;;)
    {
        pos = inpToProcess.find('_');
        if (pos == std::string::npos)
            pos = inpToProcess.size();
        std::string token = inpToProcess.substr(0, pos);
        if (token.size() > 1)
        {
            boost::algorithm::to_lower(token);
            token.data()[0] -= ('a' - 'A');
            out += token;
        }
        else
        {
            out += token;
        }
        if (pos == inpToProcess.size())
            break;
        inpToProcess = inpToProcess.substr(pos + 1);
    }
    return out;
};

ServerDefinitions::ServerDefinitions() : defs_{json::ValueType::Object}
{
    // --- TYPES ---
    // Map client-facing type names to their SerializedTypeID integer codes.
    // typeMap is retained for reverse lookup when setting each field's `type` string below.
    defs_[jss::TYPES] = json::ValueType::Object;

    defs_[jss::TYPES]["Done"] = -1;
    std::map<int32_t, std::string> typeMap{{-1, "Done"}};
    for (auto const& [rawName, typeValue] : kS_TYPE_MAP)
    {
        std::string const typeName = translate(std::string(rawName).substr(4) /* remove STI_ */);
        defs_[jss::TYPES][typeName] = typeValue;
        typeMap[typeValue] = typeName;
    }

    // --- LEDGER_ENTRY_TYPES ---
    // Seed with sentinel Invalid = -1; then append all registered ledger entry names.
    defs_[jss::LEDGER_ENTRY_TYPES] = json::ValueType::Object;
    defs_[jss::LEDGER_ENTRY_TYPES][jss::Invalid] = -1;

    for (auto const& f : LedgerFormats::getInstance())
    {
        defs_[jss::LEDGER_ENTRY_TYPES][f.getName()] = f.getType();
    }

    // --- FIELDS ---
    // Six entries are hard-coded before the registry loop because they either have no
    // canonical SField entry or require explicit control over their serialization attributes.
    defs_[jss::FIELDS] = json::ValueType::Array;

    uint32_t i = 0;

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "Invalid";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = -1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Unknown";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "ObjectEndMarker";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = 1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = true;
        v[jss::isSigningField] = true;
        v[jss::type] = "STObject";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "ArrayEndMarker";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = 1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = true;
        v[jss::isSigningField] = true;
        v[jss::type] = "STArray";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    // taker_gets_funded / taker_pays_funded: synthetic DEX fields (nth 258/259) that exist
    // in the offer-crossing path but are not persisted or signed.
    {
        json::Value a = json::ValueType::Array;
        a[0U] = "taker_gets_funded";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = 258;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Amount";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "taker_pays_funded";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = 259;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Amount";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    // Sort by fieldCode for deterministic output; the unordered source map gives no ordering
    // guarantee across platforms.
    static std::map<int, SField const*> const kSORTED_FIELDS(
        xrpl::SField::getKnownCodeToField().begin(), xrpl::SField::getKnownCodeToField().end());

    for (auto const& [code, field] : kSORTED_FIELDS)
    {
        if (field->fieldName.empty())
            continue;

        json::Value innerObj = json::ValueType::Object;

        int32_t const type = field->fieldType;

        innerObj[jss::nth] = field->fieldValue;

        // VL-encoded types encode their byte length as a prefix in the wire format.
        // Only three types qualify: Blob (7), AccountID (8), Vector256 (19).
        innerObj[jss::isVLEncoded] =
            (type == STI_VL || type == STI_ACCOUNT || type == STI_VECTOR256);
        static_assert(
            STI_VL == 7U && STI_ACCOUNT == 8U && STI_VECTOR256 == 19U,
            "STI_VL, STI_ACCOUNT, STI_VECTOR256 must be 7, 8, 19 respectively");

        // Container pseudo-types (type >= 10000: STI_TRANSACTION, STI_LEDGERENTRY,
        // STI_VALIDATION, STI_METADATA) and the computed fields `hash` and `index`
        // are not stored in serialized objects.
        innerObj[jss::isSerialized] =
            (type < 10000 && field->fieldName != "hash" &&
             field->fieldName != "index");

        innerObj[jss::isSigningField] = field->shouldInclude(false);

        innerObj[jss::type] = typeMap[type];

        json::Value innerArray = json::ValueType::Array;
        innerArray[0U] = field->fieldName;
        innerArray[1U] = innerObj;

        defs_[jss::FIELDS][i++] = innerArray;
    }

    // --- TRANSACTION_RESULTS ---
    // Maps every TER code name (e.g., "tesSUCCESS", "tecDIR_FULL") to its integer value.
    defs_[jss::TRANSACTION_RESULTS] = json::ValueType::Object;

    for (auto const& [code, terInfo] : transResults())
    {
        defs_[jss::TRANSACTION_RESULTS][terInfo.first] = code;
    }

    // --- TRANSACTION_TYPES ---
    // Seed with sentinel Invalid = -1; then append all registered transaction type names.
    defs_[jss::TRANSACTION_TYPES] = json::ValueType::Object;
    defs_[jss::TRANSACTION_TYPES][jss::Invalid] = -1;
    for (auto const& f : TxFormats::getInstance())
    {
        defs_[jss::TRANSACTION_TYPES][f.getName()] = f.getType();
    }

    // --- TRANSACTION_FORMATS ---
    // Two-level schema: "common" lists fields shared by every transaction type; each
    // type-specific key lists only the fields not already in "common".  The txCommonFields
    // set drives the skip check in the per-type loop.
    defs_[jss::TRANSACTION_FORMATS] = json::ValueType::Object;

    defs_[jss::TRANSACTION_FORMATS][jss::common] = json::ValueType::Array;
    auto txCommonFields = std::set<std::string>();
    for (auto const& element : TxFormats::getCommonFields())
    {
        json::Value elementObj = json::ValueType::Object;
        elementObj[jss::name] = element.sField().getName();
        elementObj[jss::optionality] = element.style();
        defs_[jss::TRANSACTION_FORMATS][jss::common].append(elementObj);
        txCommonFields.insert(element.sField().getName());
    }

    for (auto const& format : TxFormats::getInstance())
    {
        auto const& soTemplate = format.getSOTemplate();
        json::Value templateArray = json::ValueType::Array;
        for (auto const& element : soTemplate)
        {
            if (txCommonFields.contains(element.sField().getName()))
                continue;  // skip common fields, already added
            json::Value elementObj = json::ValueType::Object;
            elementObj[jss::name] = element.sField().getName();
            elementObj[jss::optionality] = element.style();
            templateArray.append(elementObj);
        }
        defs_[jss::TRANSACTION_FORMATS][format.getName()] = templateArray;
    }

    // --- LEDGER_ENTRY_FORMATS ---
    // Same two-level common/per-type pattern as TRANSACTION_FORMATS.
    defs_[jss::LEDGER_ENTRY_FORMATS] = json::ValueType::Object;
    defs_[jss::LEDGER_ENTRY_FORMATS][jss::common] = json::ValueType::Array;
    auto ledgerCommonFields = std::set<std::string>();
    for (auto const& element : LedgerFormats::getCommonFields())
    {
        json::Value elementObj = json::ValueType::Object;
        elementObj[jss::name] = element.sField().getName();
        elementObj[jss::optionality] = element.style();
        defs_[jss::LEDGER_ENTRY_FORMATS][jss::common].append(elementObj);
        ledgerCommonFields.insert(element.sField().getName());
    }
    for (auto const& format : LedgerFormats::getInstance())
    {
        auto const& soTemplate = format.getSOTemplate();
        json::Value templateArray = json::ValueType::Array;
        for (auto const& element : soTemplate)
        {
            if (ledgerCommonFields.contains(element.sField().getName()))
                continue;  // skip common fields, already added
            json::Value elementObj = json::ValueType::Object;
            elementObj[jss::name] = element.sField().getName();
            elementObj[jss::optionality] = element.style();
            templateArray.append(elementObj);
        }
        defs_[jss::LEDGER_ENTRY_FORMATS][format.getName()] = templateArray;
    }

    // --- TRANSACTION_FLAGS / LEDGER_ENTRY_FLAGS ---
    // Both sourced from X-macro-driven Meyers singletons; keyed by type name
    // with a "universal" entry for globally applicable flags.
    defs_[jss::TRANSACTION_FLAGS] = json::ValueType::Object;
    for (auto const& [name, value] : getAllTxFlags())
    {
        json::Value txObj = json::ValueType::Object;
        for (auto const& [flagName, flagValue] : value)
        {
            txObj[flagName] = flagValue;
        }
        defs_[jss::TRANSACTION_FLAGS][name] = txObj;
    }

    defs_[jss::LEDGER_ENTRY_FLAGS] = json::ValueType::Object;
    for (auto const& [name, value] : getAllLedgerFlags())
    {
        json::Value ledgerObj = json::ValueType::Object;
        for (auto const& [flagName, flagValue] : value)
        {
            ledgerObj[flagName] = flagValue;
        }
        defs_[jss::LEDGER_ENTRY_FLAGS][name] = ledgerObj;
    }

    // --- ACCOUNT_SET_FLAGS ---
    defs_[jss::ACCOUNT_SET_FLAGS] = json::ValueType::Object;
    for (auto const& [name, value] : getAsfFlagMap())
    {
        defs_[jss::ACCOUNT_SET_FLAGS][name] = value;
    }

    // Fingerprint the complete payload before embedding the hash field.
    // The hash covers all nine sections but not the hash field itself.
    {
        std::string const out = json::FastWriter().write(defs_);
        defsHash_ = xrpl::sha512Half(xrpl::Slice{out.data(), out.size()});
        defs_[jss::hash] = to_string(defsHash_);
    }
}

/** Return the process-lifetime singleton `ServerDefinitions` instance.
 *
 *  Constructed on first call via C++11 thread-safe static initialization.
 *
 *  @return Const reference to the singleton; valid for the lifetime of the process.
 */
ServerDefinitions const&
getDefinitions()
{
    static ServerDefinitions const kDEFS{};
    return kDEFS;
}

}  // namespace detail

/** Return the complete protocol schema JSON built by the `server_definitions` singleton.
 *
 *  Provides direct access to the definitions payload for non-RPC contexts
 *  (e.g., the `--definitions` CLI flag in `Main.cpp`).  The returned reference
 *  is valid for the lifetime of the process.
 *
 *  @return Const reference to the fully populated definitions JSON object,
 *      including the `hash` field.
 *  @see doServerDefinitions
 */
json::Value const&
getServerDefinitionsJson()
{
    return detail::getDefinitions().get();
}

/** Handle the `server_definitions` RPC request.
 *
 *  Returns the complete XRPL protocol schema assembled at startup.  If the
 *  caller supplies a `hash` parameter matching the current definitions
 *  fingerprint, only `{"hash": "..."}` is returned to save bandwidth.  A
 *  hash mismatch (or no hash) causes the full payload to be returned so the
 *  client can update its cached copy.
 *
 *  @param context RPC dispatch context; `context.params` may contain an
 *      optional `hash` string field (64 hex characters).
 *  @return The full definitions JSON object on a cache miss, or a
 *      single-key `{"hash": "..."}` object on a cache hit.  Returns an
 *      `invalidFieldError` response if `hash` is present but not a valid
 *      64-hex-character `uint256`.
 */
json::Value
doServerDefinitions(RPC::JsonContext& context)
{
    auto& params = context.params;

    uint256 hash;
    if (params.isMember(jss::hash))
    {
        if (!params[jss::hash].isString() || !hash.parseHex(params[jss::hash].asString()))
            return RPC::invalidFieldError(jss::hash);
    }

    auto const& defs = detail::getDefinitions();
    if (defs.hashMatches(hash))
    {
        json::Value jv = json::ValueType::Object;
        jv[jss::hash] = to_string(hash);
        return jv;
    }
    return defs.get();
}

}  // namespace xrpl
