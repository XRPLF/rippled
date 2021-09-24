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

#ifndef RIPPLE_SIDECHAIN_IMPL_SIGNATURE_COLLECTOR_H
#define RIPPLE_SIDECHAIN_IMPL_SIGNATURE_COLLECTOR_H

#include <ripple/app/sidechain/impl/ChainListener.h>
#include <ripple/basics/Buffer.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/container/aged_unordered_map.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple.pb.h>

#include <mutex>
#include <optional>
#include <set>

namespace Json {
class Value;
}

namespace ripple {

class Application;

namespace sidechain {

class SignerList;
class Federator;

using PeerSignatureMap = hash_map<PublicKey, Buffer>;
using MessageId = uint256;
struct MultiSigMessage
{
    PeerSignatureMap sigMaps_;
    std::optional<STTx> tx_;
    bool submitted_ = false;
};

class SignatureCollector
{
    std::shared_ptr<ChainListener> rpcChannel_;
    bool const isMainChain_;
    SecretKey const mySecKey_;
    PublicKey const myPubKey_;
    mutable std::mutex mtx_;
    beast::aged_unordered_map<
        MessageId,
        MultiSigMessage,
        std::chrono::steady_clock,
        beast::uhash<>>
        GUARDED_BY(mtx_) messages_;

    SignerList& signers_;
    Federator& federator_;
    Application& app_;
    beast::Journal j_;

public:
    SignatureCollector(
        bool isMainChain,
        SecretKey const& mySecKey,
        PublicKey const& myPubKey,
        beast::abstract_clock<std::chrono::steady_clock>& c,
        SignerList& signers,
        Federator& federator,
        Application& app,
        beast::Journal j);

    /**
     * sign the tx, and share with network
     * once quorum signatures are collected, the tx will be submitted
     * @param tx the transaction to be signed and later submitted
     */
    void
    signAndSubmit(Json::Value const& tx);

    /**
     * verify the signature and remember it.
     * If quorum signatures are collected for the same MessageId,
     * a tx will be submitted.
     *
     * @param mId identify the tx
     * @param pk public key of signer
     * @param sig signature
     * @param txOpt the transaction, only used by the local node
     * @return if the signature is from a federator
     */
    bool
    processSig(
        MessageId const& mId,
        PublicKey const& pk,
        Buffer const& sig,
        std::optional<STTx> const& txOpt);

    /**
     * remove stale signatures
     */
    void
    expire() EXCLUDES(mtx_);  // TODO retry logic

    void
    setRpcChannel(std::shared_ptr<ChainListener> channel);

private:
    // verify a signature (if it is from a peer) and add to a collection
    bool
    addSig(
        MessageId const& mId,
        PublicKey const& pk,
        Buffer const& sig,
        std::optional<STTx> const& txOpt) EXCLUDES(mtx_);

    // share a signature to the network
    void
    shareSig(MessageId const& mId, Buffer const& sig);
    // submit a tx since it collected quorum signatures
    void
    submit(MessageId const& mId, std::lock_guard<std::mutex> const&)
        REQUIRES(mtx_);
};

}  // namespace sidechain
}  // namespace ripple

#endif
