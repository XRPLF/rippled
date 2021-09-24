//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/sidechain/Federator.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/json/Output.h>
#include <ripple/json/json_reader.h>
#include <ripple/json/json_writer.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/Seed.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <ripple/rpc/impl/TransactionSign.h>
#include <mutex>
#include <ripple.pb.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include <chrono>
#include <cmath>
#include <sstream>

namespace ripple {
namespace sidechain {

[[nodiscard]] Federator::ChainType
srcChainType(event::Dir dir)
{
    return dir == event::Dir::mainToSide ? Federator::ChainType::mainChain
                                         : Federator::ChainType::sideChain;
}

[[nodiscard]] Federator::ChainType
dstChainType(event::Dir dir)
{
    return dir == event::Dir::mainToSide ? Federator::ChainType::sideChain
                                         : Federator::ChainType::mainChain;
}

[[nodiscard]] Federator::ChainType
otherChainType(Federator::ChainType ct)
{
    return ct == Federator::ChainType::mainChain
        ? Federator::ChainType::sideChain
        : Federator::ChainType::mainChain;
}

[[nodiscard]] Federator::ChainType
getChainType(bool isMainchain)
{
    return isMainchain ? Federator::ChainType::mainChain
                       : Federator::ChainType::sideChain;
}

[[nodiscard]] uint256
crossChainTxnSignatureId(
    PublicKey signingPK,
    uint256 const& srcChainTxnHash,
    std::optional<uint256> const& dstChainTxnHash,
    STAmount const& amt,
    AccountID const& src,
    AccountID const& dst,
    std::uint32_t seq,
    Slice const& signature)
{
    Serializer s(512);
    s.addBitString(src);
    s.addBitString(dst);
    amt.add(s);
    s.add32(seq);
    s.addBitString(srcChainTxnHash);
    if (dstChainTxnHash)
        s.addBitString(*dstChainTxnHash);
    s.addVL(signingPK.slice());
    s.addVL(signature);

    return s.getSHA512Half();
}

namespace detail {

std::string const rootAccount{"rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh"};

// Return the txnType as a hex id for use in a transaction memo
char const*
memoHex(Federator::TxnType txnType)
{
    constexpr static char const* names[Federator::txnTypeLast] = {"0", "1"};
    return names[static_cast<std::underlying_type_t<Federator::TxnType>>(
        txnType)];
}

Json::Value
getMemos(
    Federator::TxnType txnType,
    uint256 const& srcChainTxnHash,
    // xChainTxnHash is used for refunds
    std::optional<uint256> const& dstChainTxnHash = std::nullopt)
{
    Json::Value memos{Json::arrayValue};
    {
        Json::Value memo;
        memo[jss::Memo][jss::MemoData] = memoHex(txnType);
        memos.append(std::move(memo));
    }
    {
        Json::Value memo;
        memo[jss::Memo][jss::MemoData] = to_string(srcChainTxnHash);
        memos.append(std::move(memo));
    }
    if (dstChainTxnHash)
    {
        Json::Value memo;
        memo[jss::Memo][jss::MemoData] = to_string(*dstChainTxnHash);
        memos.append(std::move(memo));
    }
    return memos;
}

[[nodiscard]] Json::Value
getTxn(
    AccountID const& acc,
    AccountID const& dst,
    STAmount const& amt,
    std::uint32_t seq,
    Json::Value memos)
{
    Json::Value txnJson;
    // TODO: determine fee
    XRPAmount const fee{100};
    txnJson[jss::TransactionType] = "Payment";
    // TODO: Cache these strings instead of always converting to base 58
    txnJson[jss::Account] = toBase58(acc);
    txnJson[jss::Destination] = toBase58(dst);
    txnJson[jss::Amount] = amt.getJson(JsonOptions::none);
    txnJson[jss::Sequence] = seq;
    txnJson[jss::Fee] = to_string(fee);
    txnJson[jss::Memos] = std::move(memos);

    return txnJson;
};

[[nodiscard]] STTx
getSignedTxn(
    std::vector<std::pair<PublicKey, Buffer*>> const& sigs,
    AccountID const& acc,
    AccountID const& dst,
    STAmount const& amt,
    std::uint32_t seq,
    Json::Value memos,
    beast::Journal j)
{
    assert(sigs.size() > 1);
    auto const txnJson = detail::getTxn(acc, dst, amt, seq, std::move(memos));

    STParsedJSONObject parsed(std::string(jss::tx_json), txnJson);
    if (parsed.object == std::nullopt)
    {
        JLOGV(j.fatal(), "invalid transaction", jv("tx", txnJson));
        assert(0);
    }
    try
    {
        parsed.object->setFieldVL(sfSigningPubKey, Slice(nullptr, 0));
        STTx txn(std::move(parsed.object.value()));

        STArray signers;
        signers.reserve(sigs.size());
        for (auto const& [pk, sig] : sigs)
        {
            STObject obj{sfSigner};
            obj[sfAccount] = calcAccountID(pk);
            obj[sfSigningPubKey] = pk;
            obj[sfTxnSignature] = *sig;
            signers.push_back(std::move(obj));
        };

        std::sort(
            signers.begin(),
            signers.end(),
            [](STObject const& lhs, STObject const& rhs) {
                return lhs[sfAccount] < rhs[sfAccount];
            });

        txn.setFieldArray(sfSigners, std::move(signers));
        return txn;
    }
    catch (...)
    {
        JLOGV(j.fatal(), "invalid transaction", jv("txn", txnJson));
        assert(0);
    }
};

// Return the serialization of the txn with all the fields except the signing ID
// This will be used to verify signatures as they arrive.
[[nodiscard]] std::optional<Blob>
getPartialSerializedTxn(
    AccountID const& acc,
    AccountID const& dst,
    STAmount const& amt,
    std::uint32_t seq,
    Json::Value memos,
    beast::Journal j)
{
    auto const txnJson = detail::getTxn(acc, dst, amt, seq, std::move(memos));

    STParsedJSONObject parsed(std::string(jss::tx_json), txnJson);
    if (parsed.object == std::nullopt)
    {
        JLOGV(j.fatal(), "invalid transaction", jv("tx", txnJson));
        assert(0);
    }
    try
    {
        parsed.object->setFieldVL(sfSigningPubKey, Slice(nullptr, 0));
        STTx txn(std::move(parsed.object.value()));
        Serializer s;
        s.add32(HashPrefix::txMultiSign);
        txn.addWithoutSigningFields(s);
        return s.getData();
    }
    catch (...)
    {
        JLOGV(j.fatal(), "invalid transaction", jv("tx", txnJson));
        assert(0);
    }
    return {};  // should never happen
};

// For each line in a stanza whose lines all contain a single word (no words
// separated by spaces) call the function `callback` with the single word on
// each line with the leading and trailing spaces removed filter out the empty
// lines and comments. If the stanza line contains a multiple words, call the
// function `errorCallback` with the line and return.
template <class F, class EF>
void
foreachStanzaWord(Section const& stanza, F&& callback, EF&& errorCallback)
{
    std::vector<std::string> elements;
    elements.reserve(3);
    for (auto const& l : stanza.lines())
    {
        if (l.empty() || l[0] == '#')
            continue;
        boost::split(elements, l, boost::is_any_of("\t "));

        {
            // Consecutive spaces can leave empty strings. Remove them
            auto const it = std::remove_if(
                elements.begin(), elements.end(), [&](std::string const& e) {
                    return e.empty();
                });
            elements.erase(it, elements.end());
        }

        if (elements.size() != 1)
        {
            errorCallback(l);
            return;
        }

        callback(elements[0]);
        elements.clear();
    }
}

[[nodiscard]] hash_set<PublicKey>
parseFederators(BasicConfig const& config, beast::Journal j)
{
    hash_set<PublicKey> result;

    if (!config.exists("sidechain_federators"))
    {
        std::string const msg = "missing sidechain_federators stanza";
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    }

    auto const& stanza = config["sidechain_federators"];

    auto errorCallback = [&](std::string const& l) {
        std::string const msg = "invalid sidechain_federators line: " + l;
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    };

    auto callback = [&](std::string const& element) {
        auto pk = parseBase58<PublicKey>(TokenType::AccountPublic, element);
        if (!pk)
        {
            std::string const msg =
                "invalid sidechain_federators public key: " + element;
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
        result.insert(*pk);
    };

    detail::foreachStanzaWord(stanza, callback, errorCallback);

    if (result.size() > STTx::maxMultiSigners ||
        result.size() < STTx::minMultiSigners)
    {
        std::ostringstream ostr;
        ostr << "There must be at least " << STTx::minMultiSigners
             << " and at most " << STTx::maxMultiSigners
             << " federators. Num specified: " << result.size();
        JLOG(j.fatal()) << ostr.str();
        ripple::Throw<std::logic_error>(ostr.str());
    }

    return result;
}

[[nodiscard]] std::vector<std::pair<PublicKey, SecretKey>>
parseFederatorSecrets(BasicConfig const& config, beast::Journal j)
{
    std::vector<std::pair<PublicKey, SecretKey>> result;

    if (!config.exists("sidechain_federators_secrets"))
    {
        std::string const msg = "Missing sidechain_federators_secrets stanza";
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    }

    auto const& stanza = config["sidechain_federators_secrets"];

    std::vector<std::string> elements;
    elements.reserve(3);
    for (auto const& l : stanza.lines())
    {
        if (l.empty() || l[0] == '#')
            continue;
        boost::split(elements, l, boost::is_any_of("\t "));

        {
            // Consecutive spaces can leave empty strings. Remove them
            auto const it = std::remove_if(
                elements.begin(), elements.end(), [&](std::string const& e) {
                    return e.empty();
                });
            elements.erase(it, elements.end());
        }

        if (elements.size() != 1)
        {
            std::string const msg =
                "invalid sidechain_federators_secrets line: " + l;
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }

        auto seed = parseBase58<Seed>(elements[0]);
        if (!seed)
        {
            std::string const msg =
                "invalid sidechain_federators_secrets key: " + elements[0];
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
        result.push_back(generateKeyPair(KeyType::ed25519, *seed));
        elements.clear();
    }

    return result;
}

// set the value of `toSet` to the max of its current value and `reqValue` using
// a lock-free algorithm.
void
lockfreeSetMax(std::atomic<std::uint32_t>& toSet, std::uint32_t reqValue)
{
    for (auto oldValue = toSet.load();;)
    {
        auto const newValue = std::max(oldValue, reqValue);
        if (toSet.compare_exchange_strong(oldValue, newValue))
            break;
    }
}

}  // namespace detail

// Throws a logic error if the config is invalid
std::array<
    boost::container::flat_map<Issue, Federator::OtherChainAssetProperties>,
    Federator::numChains>
Federator::makeAssetProps(BasicConfig const& config, beast::Journal j)
{
    // Make an STAmount from json string
    auto makeSTAmount = [&](Section const& section,
                            std::string const& name,
                            beast::Journal j) -> STAmount {
        auto const strOpt = section.get<std::string>(name);
        if (!strOpt)
        {
            std::string const msg =
                "invalid sidechain assets stanza. Missing " + name;
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }

        auto const dummyFieldName = "amount";
        std::string const jsonStr = [&] {
            std::ostringstream ostr;
            ostr << R"({")" << dummyFieldName << R"(":)" << *strOpt << '}';
            return ostr.str();
        }();

        Json::Reader jr;
        Json::Value jv;
        if (!jr.parse(jsonStr, jv))
        {
            std::string const msg =
                "invalid sidechain assets stanza. Invalid amount " + *strOpt +
                " for " + name;
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
        try
        {
            return amountFromJson(sfGeneric, jv[dummyFieldName]);
        }
        catch (...)
        {
            std::string const msg =
                "invalid sidechain assets stanza. Invalid amount " + *strOpt +
                " for " + name;
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
    };

    // return the mainchain and sidechain "OtherChainAssetProperties" from the
    // sidechain asset stanza. The assets specified the same way a payment
    // amount is specified The ratio of the sidechain_asset amount to the
    // mainchain_asset amount determines the mainchain
    // "OtherChainAssetProperties" quality. The stanza should look like the
    // following:
    // mainchain_asset="1"
    // mainchain_refund_penalty="200"
    // sidechain_asset="2"
    // sidechain_refund_penalty="200"
    auto makeAssetPair = [&](Section const& section, beast::Journal j)
        -> std::pair<OtherChainAssetProperties, OtherChainAssetProperties> {
        // TODO Don't hardcode the stanza key values
        auto const mainchainAsset = makeSTAmount(section, "mainchain_asset", j);
        auto const sidechainAsset = makeSTAmount(section, "sidechain_asset", j);
        auto const mainchainRefundPenalty =
            makeSTAmount(section, "mainchain_refund_penalty", j);
        auto const sidechainRefundPenalty =
            makeSTAmount(section, "sidechain_refund_penalty", j);

        for (auto const& a :
             {mainchainAsset,
              sidechainAsset,
              mainchainRefundPenalty,
              sidechainRefundPenalty})
        {
            if (a.negative())
            {
                std::string const msg =
                    "invalid sidechain assets stanza. All values must be "
                    "non-negative";
                JLOG(j.fatal()) << msg;
                ripple::Throw<std::logic_error>(msg);
            }
        }

        if (mainchainAsset.issue() != mainchainRefundPenalty.issue())
        {
            std::string const msg =
                "invalid sidechain assets stanza. Mainchain asset and "
                "mainchain refund penalty must have the same issue";
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
        if (sidechainAsset.issue() != sidechainRefundPenalty.issue())
        {
            std::string const msg =
                "invalid sidechain assets stanza. Sidechain asset and "
                "sidechain refund penalty must have the same issue";
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }

        if (mainchainAsset == mainchainAsset.zeroed())
        {
            std::string const msg =
                "invalid sidechain assets stanza. Mainchain asset must be a "
                "positive amount";
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
        if (sidechainAsset == sidechainAsset.zeroed())
        {
            std::string const msg =
                "invalid sidechain assets stanza. Sidechain asset must be a "
                "positive amount";
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }

        OtherChainAssetProperties mainOCAP{
            Quality{sidechainAsset, mainchainAsset},
            sidechainAsset.issue(),
            mainchainRefundPenalty};
        OtherChainAssetProperties sideOCAP{
            Quality{mainchainAsset, sidechainAsset},
            mainchainAsset.issue(),
            sidechainRefundPenalty};

        return {mainOCAP, sideOCAP};
    };

    if (!config.exists("sidechain_assets"))
    {
        std::string const msg = "missing sidechain_assets stanza";
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    }

    auto errorCallback = [&](std::string const& l) {
        std::string const msg = "invalid sidechain_assets line: " + l;
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    };

    std::vector<std::string> assetSectionNames;
    assetSectionNames.reserve(3);
    auto callback = [&](std::string const& element) {
        assetSectionNames.push_back(element);
    };

    detail::foreachStanzaWord(
        config["sidechain_assets"], callback, errorCallback);

    std::array<
        boost::container::flat_map<Issue, Federator::OtherChainAssetProperties>,
        Federator::numChains>
        result;

    for (auto const& n : assetSectionNames)
    {
        if (!config.exists(n))
        {
            std::string const msg = "missing sidechain_asset stanza: " + n;
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
        auto [mainOCAP, sideOCAP] = makeAssetPair(config[n], j);

        if (result[mainChain].contains(sideOCAP.issue))
        {
            std::string const msg =
                "Duplicate mainchain_asset: " + to_string(sideOCAP.issue);
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
        if (result[sideChain].contains(mainOCAP.issue))
        {
            std::string const msg =
                "Duplicate sidechain_asset: " + to_string(mainOCAP.issue);
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }

        result[mainChain][sideOCAP.issue] = mainOCAP;
        result[sideChain][mainOCAP.issue] = sideOCAP;
    }

    if (result[mainChain].empty())
    {
        std::string const msg = "Must specify at least one sidechain asset";
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    }

    return result;
}

std::shared_ptr<Federator>
make_Federator(
    Application& app,
    boost::asio::io_service& ios,
    BasicConfig const& config,
    beast::Journal j)
{
    if (!config.exists("sidechain"))
        return {};
    auto const& sidechain = config["sidechain"];
    auto const keyStr = sidechain.get<std::string>("signing_key");
    auto const ipStr = sidechain.get<std::string>("mainchain_ip");
    auto const port = sidechain.get<std::uint16_t>("mainchain_port_ws");
    auto const mainAccountStr = sidechain.get<std::string>("mainchain_account");

    if (!(keyStr && ipStr && port && mainAccountStr))
    {
        auto const missing = [&]() -> std::string {
            std::ostringstream ostr;
            int numMissing = 0;
            if (!keyStr)
            {
                ostr << "signing_key";
                ++numMissing;
            }
            if (!ipStr)
            {
                if (!numMissing)
                    ostr << "mainchain_ip";
                else
                    ostr << ", mainchain_ip";
                ++numMissing;
            }
            if (!port)
            {
                if (!numMissing)
                    ostr << "mainchain_port_ws";
                else
                    ostr << ", mainchain_port_ws";
                ++numMissing;
            }
            if (!mainAccountStr)
            {
                if (!numMissing)
                    ostr << "mainchain_account";
                else
                    ostr << ", mainchain_account";
                ++numMissing;
            }
            return ostr.str();
        }();
        std::string const msg = "invalid Sidechain stanza. Missing " + missing;
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
        return {};
    }

    auto key = parseBase58<SecretKey>(TokenType::AccountSecret, *keyStr);
    if (!key)
    {
        if (auto const seed = parseBase58<Seed>(*keyStr))
        {
            // TODO: we don't know the key type
            key = generateKeyPair(KeyType::ed25519, *seed).second;
        }
    }

    if (!key)
    {
        std::string const msg = "invalid Sidechain signing key";
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    }

    boost::asio::ip::address ip;
    {
        boost::system::error_code ec;
        ip = boost::asio::ip::make_address(ipStr->c_str(), ec);
        if (ec)
        {
            std::string const msg =
                "invalid Sidechain ip address for the main chain: " + *ipStr;
            JLOG(j.fatal()) << msg;
            ripple::Throw<std::logic_error>(msg);
        }
    }
    auto const mainAccount = parseBase58<AccountID>(*mainAccountStr);
    if (!mainAccount)
    {
        std::string const msg =
            "invalid Sidechain account for the main chain: " + *mainAccountStr;
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    }

    hash_set<PublicKey> federators = detail::parseFederators(config, j);

    if (!federators.count(derivePublicKey(KeyType::ed25519, *key)))
    {
        std::ostringstream ostr;
        ostr << "Signing key is not part of the federator's set: "
             << toBase58(
                    TokenType::AccountPublic,
                    derivePublicKey(KeyType::ed25519, *key));
        auto const msg = ostr.str();
        JLOG(j.fatal()) << msg;
        ripple::Throw<std::logic_error>(msg);
    }

    auto const sideAccount = parseBase58<AccountID>(detail::rootAccount);
    assert(sideAccount);

    auto assetProps = Federator::makeAssetProps(config, j);

    auto r = std::make_shared<Federator>(
        Federator::PrivateTag{},
        app,
        *key,
        std::move(federators),
        ip,
        *port,
        *mainAccount,
        *sideAccount,
        std::move(assetProps),
        j);

    std::shared_ptr<MainchainListener> mainchainListener =
        std::make_shared<MainchainListener>(*mainAccount, r, j);
    std::shared_ptr<SidechainListener> sidechainListener =
        std::make_shared<SidechainListener>(
            app.getOPs(), *sideAccount, r, app, j);
    r->init(
        ios,
        ip,
        *port,
        std::move(mainchainListener),
        std::move(sidechainListener));

    return r;
}

Federator::Federator(
    PrivateTag,
    Application& app,
    SecretKey signingKey,
    hash_set<PublicKey>&& federators,
    boost::asio::ip::address mainChainIp,
    std::uint16_t mainChainPort,
    AccountID const& mainAccount,
    AccountID const& sideAccount,
    std::array<
        boost::container::flat_map<Issue, OtherChainAssetProperties>,
        numChains>&& assetProps,
    beast::Journal j)
    : app_{app}
    , account_{sideAccount, mainAccount}
    , assetProps_{std::move(assetProps)}
    /* TODO we don't know that they key type is ed25519 */
    , signingPK_{derivePublicKey(KeyType::ed25519, signingKey)}
    , signingSK_{signingKey}
    , federatorPKs_{std::move(federators)}
    , mainSignerList_{mainAccount, federatorPKs_, app_.journal("mainFederatorSignerList")}
    , sideSignerList_{sideAccount, federatorPKs_, app_.journal("sideFederatorSignerList")}
    , mainSigCollector_{true, signingSK_, signingPK_, stopwatch(), mainSignerList_, *this, app, app_.journal("mainFederatorSigCollector")}
    , sideSigCollector_{false, signingSK_, signingPK_, stopwatch(), sideSignerList_, *this, app, app_.journal("sideFederatorSigCollector")}
    , ticketRunner_{mainAccount, sideAccount, *this, app_.journal("FederatorTicket")}
    , mainDoorKeeper_{true, mainAccount, ticketRunner_, *this, app_.journal("mainFederatorDoorKeeper")}
    , sideDoorKeeper_{false, sideAccount, ticketRunner_, *this, app_.journal("sideFederatorDoorKeeper")}
    , j_(j)
{
    events_.reserve(16);
}

void
Federator::init(
    boost::asio::io_service& ios,
    boost::asio::ip::address& ip,
    std::uint16_t port,
    std::shared_ptr<MainchainListener>&& mainchainListener,
    std::shared_ptr<SidechainListener>&& sidechainListener)
{
    mainchainListener_ = std::move(mainchainListener);
    mainchainListener_->init(ios, ip, port);
    sidechainListener_ = std::move(sidechainListener);
    sidechainListener_->init(app_.getOPs());

    mainSigCollector_.setRpcChannel(mainchainListener_);
    sideSigCollector_.setRpcChannel(sidechainListener_);
    ticketRunner_.setRpcChannel(true, mainchainListener_);
    ticketRunner_.setRpcChannel(false, sidechainListener_);
    mainDoorKeeper_.setRpcChannel(mainchainListener_);
    sideDoorKeeper_.setRpcChannel(sidechainListener_);
}

Federator::~Federator()
{
    assert(!running_);
}

void
Federator::start()
{
    if (running_)
        return;
    requestStop_ = false;
    running_ = true;

    thread_ = std::thread([this]() {
        beast::setCurrentThreadName("Federator");
        this->mainLoop();
    });
}

void
Federator::stop()
{
    if (running_)
    {
        requestStop_ = true;
        {
            std::lock_guard<std::mutex> l(m_);
            cv_.notify_one();
        }

        thread_.join();
        running_ = false;
    }
    mainchainListener_->shutdown();
}

void
Federator::push(FederatorEvent&& e)
{
    bool notify = false;
    {
        std::lock_guard<std::mutex> l{eventsMutex_};
        notify = events_.empty();
        events_.push_back(std::move(e));
    }
    if (notify)
    {
        std::lock_guard<std::mutex> l(m_);
        cv_.notify_one();
    }
}

void
Federator::setLastTxnSeqSentMax(ChainType chaintype, std::uint32_t reqValue)
{
    detail::lockfreeSetMax(lastTxnSeqSent_[chaintype], reqValue);
}

void
Federator::setLastTxnSeqConfirmedMax(
    ChainType chaintype,
    std::uint32_t reqValue)
{
    detail::lockfreeSetMax(lastTxnSeqConfirmed_[chaintype], reqValue);
}

void
Federator::setAccountSeqMax(ChainType chaintype, std::uint32_t reqValue)
{
    detail::lockfreeSetMax(accountSeq_[chaintype], reqValue);
}

[[nodiscard]] std::optional<STAmount>
Federator::toOtherChainAmount(ChainType srcChain, STAmount const& from) const
{
    if (!assetProps_[srcChain].contains(from.issue()))
        return {};

    auto const& assetProp = assetProps_[srcChain].at(from.issue());
    // The `Quality` class actually stores the value as a "rate", which is the
    // inverse of quality. This means it's easier to divide by rate rather than
    // multiply by quality. We could store inverse quality in the asset prop,
    // but that would cause even worse confusion.
    return divRound(
        from,
        assetProp.quality.rate(),
        assetProp.issue,
        /*roundUp*/ false);
}

void
Federator::payTxn(
    TxnType txnType,
    ChainType dstChain,
    STAmount const& amt,
    AccountID const& srcChainSrcAccount,
    AccountID const& dst,
    uint256 const& srcChainTxnHash,
    std::optional<uint256> const& dstChainTxnHash)
{
    using namespace std::chrono_literals;

    // not const so it may be moved from
    auto memos = detail::getMemos(txnType, srcChainTxnHash, dstChainTxnHash);

    JLOGV(
        j_.trace(),
        "payTxn",
        jv("dstChain",
           (dstChain == Federator::ChainType::mainChain ? "main" : "side")),
        jv("account", dst),
        jv("amt", amt),
        jv("memos", memos));

    if (amt.signum() <= 0)
    {
        JLOG(j_.error()) << "invalid transaction amount: " << amt;
        return;
    }

    auto const seq = accountSeq_[dstChain]++;

    auto job = [federator = shared_from_this(),
                txnType,
                dstChain,
                srcChainSrcAccount,
                thisChainSrcAccount = account_[dstChain],
                dstAccount = dst,
                amt,
                srcChainTxnHash,
                dstChainTxnHash,
                memos = std::move(memos),
                seq,
                signingPK = signingPK_,
                signingSK = signingSK_,
                j = j_](Job&) mutable {
        auto const txnJson = detail::getTxn(
            thisChainSrcAccount, dstAccount, amt, seq, std::move(memos));

        std::optional<Buffer> optSig = [&]() -> std::optional<Buffer> {
            STParsedJSONObject parsed(std::string(jss::tx_json), txnJson);
            if (parsed.object == std::nullopt)
            {
                JLOGV(j.fatal(), "invalid transaction", jv("tx", txnJson));
                assert(0);
            }
            try
            {
                parsed.object->setFieldVL(sfSigningPubKey, Slice(nullptr, 0));
                STTx txn(std::move(parsed.object.value()));

                return txn.getMultiSignature(
                    calcAccountID(signingPK), signingPK, signingSK);
            }
            catch (...)
            {
                JLOGV(j.fatal(), "invalid transaction", jv("tx", txnJson));
                assert(0);
            }
            return {};  // should never happen
        }();

        if (!optSig)
            return;

        Buffer sig{std::move(*optSig)};

        {
            // forward the signature to all the peers
            std::shared_ptr<Message> toSend = [&] {
                protocol::TMFederatorXChainTxnSignature m;

                ::protocol::TMFederatorChainType ct = dstChain == sideChain
                    ? ::protocol::fct_SIDE
                    : ::protocol::fct_MAIN;
                ::protocol::TMFederatorTxnType tt = [&txnType] {
                    static_assert(txnTypeLast == 2, "Add new case below");
                    switch (txnType)
                    {
                        case TxnType::xChain:
                            return ::protocol::ftxnt_XCHAIN;
                        case TxnType::refund:
                            return protocol::ftxnt_REFUND;
                    }
                    assert(0);  // case statement above is exhaustive
                    return protocol::ftxnt_XCHAIN;
                }();
                m.set_txntype(tt);
                m.set_dstchain(ct);
                m.set_signingpk(signingPK.data(), signingPK.size());
                m.set_srcchaintxnhash(
                    srcChainTxnHash.data(), srcChainTxnHash.size());
                if (dstChainTxnHash)
                    m.set_dstchaintxnhash(
                        dstChainTxnHash->data(), dstChainTxnHash->size());
                {
                    Serializer s;
                    amt.add(s);
                    m.set_amount(s.data(), s.size());
                }
                m.set_srcchainsrcaccount(
                    srcChainSrcAccount.data(), srcChainSrcAccount.size());
                m.set_dstchainsrcaccount(
                    thisChainSrcAccount.data(), thisChainSrcAccount.size());
                m.set_dstchaindstaccount(dstAccount.data(), dstAccount.size());
                m.set_seq(seq);
                m.set_signature(sig.data(), sig.size());

                return std::make_shared<Message>(
                    m, protocol::mtFederatorXChainTxnSignature);
            }();

            Overlay& overlay = federator->app_.overlay();
            HashRouter& hashRouter = federator->app_.getHashRouter();
            uint256 const suppression = crossChainTxnSignatureId(
                signingPK,
                srcChainTxnHash,
                dstChainTxnHash,
                amt,
                thisChainSrcAccount,
                dstAccount,
                seq,
                sig);

            if (auto const toSkip = hashRouter.shouldRelay(suppression))
            {
                overlay.foreach([&](std::shared_ptr<Peer> const& p) {
                    hashRouter.addSuppressionPeer(suppression, p->id());
                    if (toSkip->count(p->id()))
                    {
                        JLOGV(
                            j.trace(),
                            "not sending signature to peer",
                            jv("id", p->id()),
                            jv("suppression", suppression));
                        return;
                    }
                    JLOGV(
                        j.trace(),
                        "sending signature to peer",
                        jv("id", p->id()),
                        jv("suppression", suppression));
                    p->send(toSend);
                });
            }
        }

        federator->addPendingTxnSig(
            txnType,
            dstChain,
            signingPK,
            srcChainTxnHash,
            dstChainTxnHash,
            amt,
            srcChainSrcAccount,
            dstAccount,
            seq,
            std::move(sig));

        if (federator->app_.config().standalone())
        {
            std::optional<STTx> txnOpt = [&]() -> std::optional<STTx> {
                STParsedJSONObject parsed(std::string(jss::tx_json), txnJson);
                if (parsed.object == std::nullopt)
                {
                    JLOGV(j.fatal(), "invalid transaction", jv("tx", txnJson));
                    assert(0);
                }
                try
                {
                    parsed.object->setFieldVL(
                        sfSigningPubKey, Slice(nullptr, 0));
                    STTx txn(std::move(parsed.object.value()));
                    return txn;
                }
                catch (...)
                {
                    JLOGV(j.fatal(), "invalid transaction", jv("tx", txnJson));
                    assert(0);
                }
                return {};  // should never happen
            }();

            if (!txnOpt)
                return;
            STTx txn{std::move(*txnOpt)};

            auto getSig = [&](STTx const& txn,
                              PublicKey const& pk,
                              SecretKey const& sk) -> std::optional<Buffer> {
                try
                {
                    return txn.getMultiSignature(calcAccountID(pk), pk, sk);
                }
                catch (...)
                {
                    assert(0);
                }
                return {};  // should never happen
            };

            static std::vector<std::pair<PublicKey, SecretKey>> keypairs =
                detail::parseFederatorSecrets(federator->app_.config(), j);

            for (auto const& [pk, sk] : keypairs)
            {
                if (pk == federator->signingPK_)
                    continue;  // don't sign for this federator again

                if (auto sig = getSig(txn, pk, sk))
                {
                    federator->addPendingTxnSig(
                        txnType,
                        dstChain,
                        pk,
                        srcChainTxnHash,
                        dstChainTxnHash,
                        amt,
                        srcChainSrcAccount,
                        dstAccount,
                        seq,
                        std::move(*sig));
                }
            }
        }

        federator->updateDoorKeeper(dstChain);
    };

    app_.getJobQueue().addJob(jtFEDERATORSIGNATURE, "federator signature", job);
}

void
Federator::onEvent(event::XChainTransferDetected const& e)
{
    auto const srcChain = srcChainType(e.dir_);
    std::optional<STAmount> toSendAmt =
        toOtherChainAmount(srcChain, e.deliveredAmt_);

    if (!toSendAmt)
    {
        // not an issue used for xchain transfers
        JLOGV(
            j_.trace(),
            "XChainTransferDetected ignored",
            jv("dstChain",
               (dstChainType(e.dir_) == Federator::ChainType::mainChain
                    ? "main"
                    : "side")),
            jv("amt", e.deliveredAmt_),
            jv("src", e.src_),
            jv("dst", e.dst_));
        return;
    }
    payTxn(
        TxnType::xChain,
        dstChainType(e.dir_),
        *toSendAmt,
        e.src_,
        e.dst_,
        e.txnHash_,
        std::nullopt);
}

void
Federator::sendRefund(
    ChainType chaintype,
    STAmount const& amt,
    AccountID const& dst,
    uint256 const& xChainTxnHash,
    uint256 const& triggeringResultTxnHash)
{
    JLOGV(
        j_.trace(),
        "sendRefund",
        jv("amt", amt),
        jv("dst", dst),
        jv("chain", (chaintype == ChainType::mainChain ? "main" : "side")),
        jv("xChainTxnHash", xChainTxnHash),
        jv("triggeringResultTxnHash", triggeringResultTxnHash));

    payTxn(
        TxnType::refund,
        chaintype,
        amt,
        // the src chain src account and the dst and the same when refunding
        dst,
        dst,
        xChainTxnHash,
        triggeringResultTxnHash);
}

void
Federator::onEvent(event::XChainTransferResult const& e)
{
    JLOGV(j_.trace(), "Federator::onEvent", jv("event", e.toJson()));

    // srcChain and dstChain are the chains of the triggering transaction.
    // I.e. A srcChain of main is a transfer result is a transaction that
    // happens on the sidechain (the triggering transaction happended on the
    // mainchain)
    auto const srcChain = srcChainType(e.dir_);
    auto const dstChain = dstChainType(e.dir_);

    onResult(dstChain, e.txnSeq_);

    if (e.ter_ != tesSUCCESS)
    {
        std::lock_guard pendingTxnsLock{pendingTxnsM_};
        if (auto i = pendingTxns_[dstChain].find(e.srcChainTxnHash_);
            i != pendingTxns_[dstChain].end())
        {
            auto const& pendingTxn = i->second;
            // TODO: How are we tracking failed refunds?
            if (isTecClaim(e.ter_))
            {
                // The triggering transaction happened on the src chain. The
                // result transaction happened on the dst chain. Convert the
                // amount on the dst chain to an amount on the src chain.
                std::optional<STAmount> sentAmt =
                    toOtherChainAmount(dstChain, pendingTxn.amount);
                std::optional<STAmount> const penalty =
                    [&]() -> std::optional<STAmount> {
                    if (!sentAmt)
                        return {};
                    try
                    {
                        return assetProps_[srcChain]
                            .at(sentAmt->issue())
                            .refundPenalty;
                    }
                    catch (...)
                    {
                    }
                    return {};
                }();

                if (!sentAmt || !penalty ||
                    penalty->issue() != sentAmt->issue())
                {
                    assert(0);
                    JLOGV(
                        j_.trace(),
                        "Failed XChainTransferResult Refund",
                        jv("reason",
                           "Logic error: penalty not found or of wrong issue"),
                        jv("penalty", penalty.value_or(STAmount{})),
                        jv("event", e.toJson()),
                        jv("sentAmt", sentAmt.value_or(STAmount{})));
                    return;
                }

                if (*sentAmt <= *penalty)
                {
                    JLOGV(
                        j_.trace(),
                        "Failed XChainTransferResult Refund",
                        jv("reason", "Refund amount is less than penalty"),
                        jv("penalty", *penalty),
                        jv("event", e.toJson()),
                        jv("sentAmt", *sentAmt));
                }
                STAmount const amt{*sentAmt - *penalty};
                AccountID dst = pendingTxn.srcChainSrcAccount;
                sendRefund(srcChain, amt, dst, e.srcChainTxnHash_, e.txnHash_);
            }
        }
        else
        {
            JLOGV(
                j_.trace(),
                "Failed XChainTransferResult Refund",
                jv("reason", "Could not find pending transaction"),
                jv("event", e.toJson()));
        }
    }

    {
        // remove the signature from the signature collection
        std::lock_guard pendingTxnsLock{pendingTxnsM_};
        pendingTxns_[dstChain].erase(e.srcChainTxnHash_);
    }

    updateDoorKeeper(dstChain);
}

void
Federator::onEvent(event::RefundTransferResult const& e)
{
    JLOGV(j_.trace(), "RefundTransferResult", jv("event", e.toJson()));

    auto const srcChain = srcChainType(e.dir_);
    onResult(srcChain, e.txnSeq_);

    if (e.ter_ != tesSUCCESS)
    {
        // There's not much that can be done if a refund fails.
        JLOGV(
            j_.fatal(),
            "Failed RefundChainTransferResult",
            jv("reason", "Failed transaction"),
            jv("event", e.toJson()));
    }

    // remove the signature from the signature collection
    std::lock_guard pendingTxnsLock{pendingTxnsM_};
    pendingTxns_[srcChain].erase(e.dstChainTxnHash_);
}

void
Federator::onEvent(event::HeartbeatTimer const& e)
{
    JLOG(j_.trace()) << "HeartbeatTimer";
}

void
Federator::updateDoorKeeper(ChainType dstChain)
{
    std::uint32_t txnsCount = [&]() {
        std::lock_guard pendingTxnsLock{pendingTxnsM_};
        return pendingTxns_[dstChain].size();
    }();

    auto sourceChain = dstChain == ChainType::sideChain ? ChainType::mainChain
                                                        : ChainType::sideChain;
    switch (sourceChain)
    {
        case ChainType::mainChain:
            mainDoorKeeper_.updateQueueLength(txnsCount);
            break;
        case ChainType::sideChain:
            sideDoorKeeper_.updateQueueLength(txnsCount);
            break;
    }
}

void
Federator::onResult(ChainType chainType, std::uint32_t resultTxSeq)
{
    setLastTxnSeqSentMax(chainType, resultTxSeq);
    setLastTxnSeqConfirmedMax(chainType, resultTxSeq);
    sendTxns();
}

bool
Federator::alreadySent(ChainType chaintype, std::uint32_t seq) const
{
    return seq < lastTxnSeqSent_[chaintype];
}

void
Federator::setLastXChainTxnWithResult(
    ChainType chaintype,
    std::uint32_t seq,
    std::uint32_t seqTook,
    uint256 const& hash)
{
    ChainType const otherChain = otherChainType(chaintype);
    setLastTxnSeqSentMax(otherChain, seq);
    setLastTxnSeqConfirmedMax(otherChain, seq);
    accountSeq_[otherChain] = seq + seqTook;

    switch (chaintype)
    {
        case ChainType::mainChain:
            mainchainListener_->setLastXChainTxnWithResult(hash);
            break;
        case ChainType::sideChain:
            sidechainListener_->setLastXChainTxnWithResult(hash);
            break;
    }
}

void
Federator::setNoLastXChainTxnWithResult(ChainType chaintype)
{
    switch (chaintype)
    {
        case ChainType::mainChain:
            mainchainListener_->setNoLastXChainTxnWithResult();
            break;
        case ChainType::sideChain:
            sidechainListener_->setNoLastXChainTxnWithResult();
            break;
    }
}

void
Federator::stopHistoricalTxns(ChainType chaintype)
{
    switch (chaintype)
    {
        case ChainType::mainChain:
            mainchainListener_->stopHistoricalTxns();
            break;
        case ChainType::sideChain:
            sidechainListener_->stopHistoricalTxns(app_.getOPs());
            break;
    }
}

void
Federator::initialSyncDone(ChainType chaintype)
{
    switch (chaintype)
    {
        case ChainType::mainChain:
            ticketRunner_.init(true);
            mainDoorKeeper_.init();
            break;
        case ChainType::sideChain:
            ticketRunner_.init(false);
            sideDoorKeeper_.init();
            break;
    }
}

void
Federator::addPendingTxnSig(
    TxnType txnType,
    ChainType chaintype,
    PublicKey const& federatorPK,
    uint256 const& srcChainTxnHash,
    std::optional<uint256> const& dstChainTxnHash,
    STAmount const& amt,
    AccountID const& srcChainSrcAccount,
    AccountID const& dstChainDstAccount,
    std::uint32_t seq,
    Buffer&& sig)
{
    std::lock_guard l{federatorPKsMutex_};

    std::uint32_t sigThreshold = std::numeric_limits<std::uint32_t>::max();
    {
        sigThreshold =
            static_cast<std::uint32_t>(std::ceil(federatorPKs_.size() * 0.8));
        auto i = federatorPKs_.find(federatorPK);
        if (i == federatorPKs_.end())
        {
            // Unknown sending federator
            JLOGV(
                j_.debug(),
                "unknown sending federator",
                jv("public_key", strHex(federatorPK)),
                jv("amt", amt),
                jv("srcChainTxnHash", srcChainTxnHash));
            return;
        }
    }

    if (alreadySent(chaintype, seq))
    {
        JLOGV(
            j_.debug(),
            "transaction already sent",
            jv("public_key", strHex(federatorPK)),
            jv("amt", amt),
            jv("seq", seq),
            jv("srcChainTxnHash", srcChainTxnHash));
        return;
    }

    {
        std::lock_guard pendingTxnsLock{pendingTxnsM_};

        auto& txns =
            pendingTxns_[chaintype][dstChainTxnHash.value_or(srcChainTxnHash)];

        bool const isLocalFederator = (federatorPK == signingPK_);
        if (isLocalFederator &&
            (amt != txns.amount ||
             dstChainDstAccount != txns.dstChainDstAccount ||
             srcChainSrcAccount != txns.srcChainSrcAccount))
        {
            // another federator sent a transaction that disagrees with the
            // local federator's txn.
            txns.amount = amt;
            txns.srcChainSrcAccount = srcChainSrcAccount;
            txns.dstChainDstAccount = dstChainDstAccount;
            txns.sigs.clear();
            txns.sequenceInfo.clear();
        }

        {
            auto i = txns.sigs.find(federatorPK);
            if (i != txns.sigs.end())
            {
                // remove the old seq
                assert(txns.sequenceInfo[i->second.seq].count > 0);
                --txns.sequenceInfo[i->second.seq].count;
                JLOGV(
                    j_.trace(),
                    "duplicate federator signature",
                    jv("federator", strHex(federatorPK)),
                    jv("amt", amt),
                    jv("srcChainTxnHash", srcChainTxnHash));
                if (txns.sequenceInfo[i->second.seq].count == 0)
                {
                    // No federator is proposing this sequence number
                    // anymore
                    txns.sequenceInfo.erase(i->second.seq);
                }
            }
            {
                // Check that the signature is valid
                std::optional<Blob> partialSerializationOpt =
                    [&]() -> std::optional<Blob> {
                    if (auto i = txns.sequenceInfo.find(seq);
                        i != txns.sequenceInfo.end())
                    {
                        return i->second.partialTxnSerialization;
                    }
                    return detail::getPartialSerializedTxn(
                        account_[chaintype],
                        dstChainDstAccount,
                        amt,
                        seq,
                        detail::getMemos(
                            txnType, srcChainTxnHash, dstChainTxnHash),
                        j_);
                }();

                if (!partialSerializationOpt)
                    return;

                Blob partialSerialization{std::move(*partialSerializationOpt)};

                Serializer s(
                    partialSerialization.data(), partialSerialization.size());
                s.addBitString(calcAccountID(federatorPK));

                if (!verify(
                        federatorPK,
                        s.slice(),
                        sig,
                        /*fullyCanonical*/ true))
                {
                    JLOGV(
                        j_.error(),
                        "invalid federator signature",
                        jv("federator", strHex(federatorPK)),
                        jv("amt", amt),
                        jv("srcChainTxnHash", srcChainTxnHash));
                    return;
                }
                else
                {
                    JLOGV(
                        j_.trace(),
                        "valid federator signature",
                        jv("federator", strHex(federatorPK)),
                        jv("amt", amt),
                        jv("srcChainTxnHash", srcChainTxnHash));
                }

                if (auto i = txns.sequenceInfo.find(seq);
                    i == txns.sequenceInfo.end())
                {
                    // Store the partialSerialization so it doesn't need to
                    // be recomputed
                    txns.sequenceInfo[seq].partialTxnSerialization =
                        partialSerialization;
                }
            }
            txns.sigs.insert_or_assign(
                i, federatorPK, PeerTxnSignature{std::move(sig), seq});
            ++txns.sequenceInfo[seq].count;
        }

        if (txns.sequenceInfo[seq].count < sigThreshold)
        {
            JLOGV(
                j_.trace(),
                "not enouth signatures to send",
                jv("federator", strHex(federatorPK)),
                jv("amt", amt),
                jv("seq", seq),
                jv("srcChainTxnHash", srcChainTxnHash),
                jv("count", txns.sequenceInfo[seq].count));

            return;
        }

        if (txns.queuedToSend_)
        {
            JLOGV(
                j_.trace(),
                "transaction already queued to send",
                jv("amt", amt),
                jv("seq", seq),
                jv("srcChainTxnHash", srcChainTxnHash));
            return;
        }

        if (auto i = txns.sigs.find(signingPK_);
            i != txns.sigs.end() && i->second.seq != seq)
        {
            // TODO: this federator's sequence number needs to be adjusted
        }

        // There are enough signatures. Queue this transaction to send.

        auto const sigs = [&, sigThreshold = sigThreshold]()
            -> std::vector<std::pair<PublicKey, Buffer*>> {
            std::vector<std::pair<PublicKey, Buffer*>> r;
            r.reserve(sigThreshold);
            for (auto& [pk, sig] : txns.sigs)
            {
                if (sig.seq != seq)
                {
                    // a federator sent a signature for a different sequence
                    continue;
                }
                r.emplace_back(pk, &sig.sig);
                if (r.size() == sigThreshold)
                    break;
            }
            assert(r.size() == sigThreshold);
            return r;
        }();

        // not const so it may be moved from
        STTx txn = detail::getSignedTxn(
            sigs,
            account_[chaintype],
            dstChainDstAccount,
            amt,
            seq,
            detail::getMemos(txnType, srcChainTxnHash, dstChainTxnHash),
            j_);

        {
            std::lock_guard l{toSendTxnsM_};
            JLOGV(
                j_.trace(),
                "adding to toSendTxns",
                jv("chain", (chaintype == sideChain ? "Side" : "Main")),
                jv("amt", amt),
                jv("seq", seq),
                jv("srcChainTxnHash", srcChainTxnHash),
                jv("count", txns.sequenceInfo[seq].count));
            toSendTxns_[chaintype].emplace(seq, std::move(txn));
        }

        txns.queuedToSend_ = true;
        // close scope to release the lock before sending the transactions
    }

    sendTxns();
}

void
Federator::addPendingTxnSig(
    ChainType chaintype,
    const PublicKey& publicKey,
    const uint256& mId,
    Buffer&& sig)
{
    switch (chaintype)
    {
        case mainChain:
            mainSigCollector_.processSig(mId, publicKey, std::move(sig), {});
            break;
        case sideChain:
            sideSigCollector_.processSig(mId, publicKey, std::move(sig), {});
            break;
        default:
            assert(false);
    }
}

void
Federator::sendTxns()
{
    // Only one thread at a time should run sendTxns or transactions may be
    // sent multiple times
    std::lock_guard l{sendTxnsMutex_};

    auto sendSidechainTxn = [this](STTx const& txn) {
        Json::Value const request = [&] {
            Json::Value r;
            // TODO add failHard
            r[jss::method] = "submit";
            r[jss::jsonrpc] = "2.0";
            r[jss::ripplerpc] = "2.0";
            r[jss::tx_blob] = strHex(txn.getSerializer().peekData());
            return r;
        }();

        auto const r = [&] {
            Resource::Charge loadType = Resource::feeReferenceRPC;
            Resource::Consumer c;
            RPC::JsonContext context{
                {j_,
                 app_,
                 loadType,
                 app_.getOPs(),
                 app_.getLedgerMaster(),
                 c,
                 Role::ADMIN,
                 {},
                 {},
                 RPC::apiMaximumSupportedVersion},
                std::move(request)};

            Json::Value jvResult;
            // Make the transfer on the side chain
            RPC::doCommand(context, jvResult);
            return jvResult;
        }();

        JLOGV(j_.trace(), "main to side submit", jv("result", r));

        if (!r.isMember(jss::engine_result_code) ||
            r[jss::engine_result_code].asInt() != 0)
        {
            if (r.isMember(jss::engine_result) &&
                (r[jss::engine_result] == "tefPAST_SEQ" ||
                 // TODO got a reply:
                 // "engine_result":"tesSUCCESS",
                 // "engine_result_code":-190,
                 // "engine_result_message":
                 // "The transaction was applied.
                 // Only final in a validated ledger."
                 r[jss::engine_result] == "tesSUCCESS" ||
                 r[jss::engine_result] == "terQUEUED" ||
                 r[jss::engine_result] == "telCAN_NOT_QUEUE_FEE"))
            {
                // TODO: This is OK, but we still need to look for a
                // confirmation in the txn stream
            }
            else
            {
                auto const msg =
                    "could not transfer from the sidechain door account";
                JLOGV(j_.fatal(), msg, jv("tx", request), jv("result", r));
                auto const ter = [&]() -> std::optional<TER> {
                    if (r.isMember(jss::engine_result_code))
                        return TER::fromInt(r[jss::engine_result_code].asInt());
                    return std::nullopt;
                }();
                // tec codes will trigger a refund in
                // onEvent(XChainTransferResult)
                if (!ter || !isTecClaim(*ter))
                    ripple::Throw<std::logic_error>(msg);
            }
        }

        if (app_.config().standalone())
            app_.getOPs().acceptLedger();
    };

    auto sendMainchainTxn = [this](STTx const& txn) {
        Json::Value const request = [&] {
            Json::Value r;
            // TODO add failHard
            r[jss::tx_blob] = strHex(txn.getSerializer().peekData());
            return r;
        }();

        // TODO: Save the id and listen for errors
        auto const id = mainchainListener_->send("submit", request);
        JLOGV(j_.trace(), "mainchain submit message id", jv("id", id));
    };

    for (auto chain : {sideChain, mainChain})
    {
        auto const curAccSeq = accountSeq_[chain].load();
        auto maxToSend = [&]() -> std::uint32_t {
            auto const lastSent = lastTxnSeqSent_[chain].load();
            auto const lastConfirmed = lastTxnSeqConfirmed_[chain].load();
            assert(lastSent >= lastConfirmed);
            auto const onFly = lastSent - lastConfirmed;
            JLOGV(
                j_.trace(),
                "sendTxns, compute maxToSend",
                jv("chain", (chain == sideChain ? "Side" : "Main")),
                jv("lastSent", lastSent),
                jv("lastConfirmed", lastConfirmed),
                jv("onFly", onFly));
            if (onFly >= 8)
                return 0;
            else
                return 8 - onFly;
        }();

        for (int seq = lastTxnSeqSent_[chain] + 1;
             seq < curAccSeq && maxToSend > 0;
             ++seq, --maxToSend)
        {
            {
                std::lock_guard l{toSendTxnsM_};
                if (auto i = toSkipSeq_[chain].find(seq);
                    i != toSkipSeq_[chain].end())
                {
                    lastTxnSeqSent_[chain] = seq;
                    toSkipSeq_[chain].erase(i);
                    JLOGV(
                        j_.trace(),
                        "sendTxns",
                        jv("chain", (chain == sideChain ? "Side" : "Main")),
                        jv("skipping", seq));
                    continue;
                }
            }

            auto const txn = [&]() -> std::optional<STTx> {
                std::lock_guard l{toSendTxnsM_};
                auto i = toSendTxns_[chain].find(seq);
                if (i == toSendTxns_[chain].end())
                {
                    JLOGV(
                        j_.trace(),
                        "sendTxns",
                        jv("chain", (chain == sideChain ? "Side" : "Main")),
                        jv("breaking_on_tx_seq", seq),
                        jv("lastTxSeqSent", lastTxnSeqSent_[chain]));
                    if (!toSendTxns_[chain].empty())
                    {
                        JLOGV(
                            j_.trace(),
                            "sendTxns: next toSend",
                            jv("chain", (chain == sideChain ? "Side" : "Main")),
                            jv("seq", toSendTxns_[chain].begin()->first));
                    }
                    else
                    {
                        JLOG(j_.trace()) << "sendTxns: toSendtxns is empty";
                    }
                    // Even if there are more transactions in the
                    // collection, they can not be sent until transactions
                    // will smaller sequence numbers have been sent.
                    return {};
                }
                return std::move(i->second);
            }();
            if (!txn)
                break;

            setLastTxnSeqSentMax(chain, seq);
            if (chain == sideChain)
            {
                sendSidechainTxn(*txn);
            }
            else
            {
                sendMainchainTxn(*txn);
            }
        }
        {
            // Remove all the txns that have been sent (including those
            // added to the collection after the seq has been sent).
            std::lock_guard l{toSendTxnsM_};
            toSendTxns_[chain].erase(
                toSendTxns_[chain].begin(),
                std::find_if(
                    toSendTxns_[chain].begin(),
                    toSendTxns_[chain].end(),
                    [sent = lastTxnSeqSent_[chain].load()](auto const& item)
                        -> bool { return item.first > sent; }));
        }
    }
}

void
Federator::unlockMainLoop()
{
    std::lock_guard<std::mutex> l(m_);
    mainLoopLocked_ = false;
    mainLoopCv_.notify_one();
}

void
Federator::mainLoop()
{
    {
        std::unique_lock l{mainLoopMutex_};
        mainLoopCv_.wait(l, [this] { return !mainLoopLocked_; });
    }

    std::vector<FederatorEvent> localEvents;
    localEvents.reserve(16);
    while (!requestStop_)
    {
        {
            std::lock_guard l{eventsMutex_};
            assert(localEvents.empty());
            localEvents.swap(events_);
        }
        if (localEvents.empty())
        {
            using namespace std::chrono_literals;
            // In rare cases, an event may be pushed and the condition
            // variable signaled before the condition variable is waited on.
            // To handle this, set a timeout on the wait.
            std::unique_lock l{m_};
            // Allow for spurious wakeups. The alternative requires locking the
            // eventsMutex_
            cv_.wait_for(l, 1s);
            continue;
        }

        for (auto const& event : localEvents)
            std::visit([this](auto&& e) { this->onEvent(e); }, event);
        localEvents.clear();
    }
}

void
Federator::onEvent(event::StartOfHistoricTransactions const& e)
{
    assert(0);  // StartOfHistoricTransactions is only used in initial sync
}

void
Federator::onEvent(event::TicketCreateTrigger const& e)
{
    Federator::ChainType toChain = e.dir_ == event::Dir::mainToSide
        ? Federator::sideChain
        : Federator::mainChain;
    auto seq = accountSeq_[toChain].fetch_add(2);
    ticketRunner_.onEvent(seq, e);
}

void
Federator::onEvent(const event::TicketCreateResult& e)
{
    auto const [fromChain, toChain] = e.dir_ == event::Dir::mainToSide
        ? std::make_pair(sideChain, mainChain)
        : std::make_pair(mainChain, sideChain);

    onResult(fromChain, e.txnSeq_);

    if (e.memoStr_.empty())
    {
        ticketRunner_.onEvent(0, e);
    }
    else
    {
        auto seq = accountSeq_[toChain].fetch_add(1);
        ticketRunner_.onEvent(seq, e);
    }
}

void
Federator::onEvent(event::DepositAuthResult const& e)
{
    auto const chainType =
        (e.dir_ == event::Dir::mainToSide ? sideChain : mainChain);

    onResult(chainType, e.txnSeq_);

    switch (e.dir_)
    {
        case event::Dir::mainToSide:
            sideDoorKeeper_.onEvent(e);
            break;
        case event::Dir::sideToMain:
            mainDoorKeeper_.onEvent(e);
            break;
    }
}

void
Federator::onEvent(event::BootstrapTicket const& e)
{
    setAccountSeqMax(getChainType(e.isMainchain_), e.txnSeq_ + 1);
    setLastTxnSeqSentMax(getChainType(e.isMainchain_), e.txnSeq_);
    setLastTxnSeqConfirmedMax(getChainType(e.isMainchain_), e.txnSeq_);
    ticketRunner_.onEvent(e);
}

void
Federator::onEvent(event::DisableMasterKeyResult const& e)
{
    setAccountSeqMax(getChainType(e.isMainchain_), e.txnSeq_ + 1);
    setLastTxnSeqSentMax(getChainType(e.isMainchain_), e.txnSeq_);
    setLastTxnSeqConfirmedMax(getChainType(e.isMainchain_), e.txnSeq_);
}

// void
// Federator::onEvent(event::SignerListSetResult const& e)
//{
//    assert(0);
//}

Json::Value
Federator::getInfo() const
{
    Json::Value ret{Json::objectValue};

    auto populatePendingTransaction =
        [](PendingTransaction const& txn) -> Json::Value {
        Json::Value r{Json::objectValue};
        r[jss::amount] = txn.amount.getJson(JsonOptions::none);
        r[jss::destination_account] = to_string(txn.dstChainDstAccount);
        Json::Value sigs{Json::arrayValue};
        for (auto const& [pk, sig] : txn.sigs)
        {
            Json::Value s{Json::objectValue};
            s[jss::public_key] = toBase58(TokenType::AccountPublic, pk);
            s[jss::seq] = sig.seq;
            sigs.append(s);
        }
        r[jss::signatures] = sigs;
        return r;
    };

    auto populateChain = [&](auto const& listener,
                             ChainType chaintype) -> Json::Value {
        Json::Value r{Json::objectValue};
        Json::Value pending{Json::arrayValue};
        {
            std::lock_guard l1{pendingTxnsM_};

            for (auto const& [k, v] : pendingTxns_[chaintype])
            {
                auto txn = populatePendingTransaction(v);
                txn[jss::hash] = strHex(k);
                pending.append(txn);
            }
        }
        r[jss::pending_transactions] = pending;
        r[jss::listener_info] = listener.getInfo();
        r[jss::sequence] = accountSeq_[chaintype].load();
        r[jss::last_transaction_sent_seq] = lastTxnSeqSent_[chaintype].load();
        if (chaintype == ChainType::mainChain)
        {
            r["door_status"] = mainDoorKeeper_.getInfo();
            r["tickets"] = ticketRunner_.getInfo(true);
        }
        else
        {
            r["door_status"] = sideDoorKeeper_.getInfo();
            r["tickets"] = ticketRunner_.getInfo(false);
        }
        return r;
    };

    ret[jss::public_key] = toBase58(TokenType::AccountPublic, signingPK_);
    ret[jss::mainchain] =
        populateChain(*mainchainListener_, ChainType::mainChain);
    ret[jss::sidechain] =
        populateChain(*sidechainListener_, ChainType::sideChain);

    return ret;
}

void
Federator::sweep()
{
    updateDoorKeeper(ChainType::mainChain);
    updateDoorKeeper(ChainType::sideChain);
    mainSigCollector_.expire();
    sideSigCollector_.expire();
}

SignatureCollector&
Federator::getSignatureCollector(ChainType chain)
{
    if (chain == ChainType::mainChain)
        return mainSigCollector_;
    else
        return sideSigCollector_;
}

TicketRunner&
Federator::getTicketRunner()
{
    return ticketRunner_;
}

DoorKeeper&
Federator::getDoorKeeper(ChainType chain)
{
    if (chain == ChainType::mainChain)
        return mainDoorKeeper_;
    else
        return sideDoorKeeper_;
}

void
Federator::addSeqToSkip(ChainType chain, std::uint32_t seq)
{
    {
        JLOGV(
            j_.trace(),
            "addSeqToSkip, ticket seq to skip when processing toSendTxns",
            jv("chain", (chain == sideChain ? "Side" : "Main")),
            jv("ticket seq", seq),
            jv("account seq", accountSeq_[chain]),
            jv("lastSent", lastTxnSeqSent_[chain]));
        std::lock_guard l{toSendTxnsM_};
        toSkipSeq_[chain].insert(seq);
    }
    sendTxns();
}

void
Federator::addTxToSend(ChainType chain, std::uint32_t seq, STTx const& tx)
{
    {
        std::lock_guard l{toSendTxnsM_};
        JLOGV(
            j_.trace(),
            "adding account control tx to toSendTxns",
            jv("chain", (chain == sideChain ? "Side" : "Main")),
            jv("seq", seq),
            jv("account seq", accountSeq_[chain]),
            jv("lastSent", lastTxnSeqSent_[chain]));
        toSendTxns_[chain].emplace(seq, tx);
    }
    sendTxns();
}
}  // namespace sidechain
}  // namespace ripple
