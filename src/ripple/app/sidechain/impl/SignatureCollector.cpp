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

#include <ripple/app/sidechain/impl/SignatureCollector.h>

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/sidechain/Federator.h>
#include <ripple/app/sidechain/impl/SignerList.h>
#include <ripple/basics/Slice.h>
#include <ripple/core/Job.h>
#include <ripple/core/JobQueue.h>
#include <ripple/json/json_value.h>
#include <ripple/json/json_writer.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/overlay/Peer.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/Serializer.h>
#include <mutex>

namespace ripple {
namespace sidechain {

std::chrono::seconds messageExpire = std::chrono::minutes{10};

uint256
computeMessageSuppression(MessageId const& mId, Slice const& signature)
{
    Serializer s(128);
    s.addBitString(mId);
    s.addVL(signature);
    return s.getSHA512Half();
}

SignatureCollector::SignatureCollector(
    bool isMainChain,
    SecretKey const& mySecKey,
    PublicKey const& myPubKey,
    beast::abstract_clock<std::chrono::steady_clock>& c,
    SignerList& signers,
    Federator& federator,
    Application& app,
    beast::Journal j)
    : isMainChain_(isMainChain)
    , mySecKey_(mySecKey)
    , myPubKey_(myPubKey)
    , messages_(c)
    , signers_(signers)
    , federator_(federator)
    , app_(app)
    , j_(j)
{
}

void
SignatureCollector::signAndSubmit(Json::Value const& txJson)
{
    auto job = [tx = txJson,
                myPK = myPubKey_,
                mySK = mySecKey_,
                chain =
                    isMainChain_ ? Federator::mainChain : Federator::sideChain,
                f = federator_.weak_from_this(),
                j = j_](Job&) mutable {
        auto federator = f.lock();
        if (!federator)
            return;

        STParsedJSONObject parsed(std::string(jss::tx_json), tx);
        if (parsed.object == std::nullopt)
        {
            JLOGV(j.fatal(), "cannot parse transaction", jv("tx", tx));
            assert(0);
            return;
        }
        try
        {
            parsed.object->setFieldVL(sfSigningPubKey, Slice(nullptr, 0));
            STTx tx(std::move(parsed.object.value()));

            MessageId mId{tx.getSigningHash()};
            Buffer sig{tx.getMultiSignature(calcAccountID(myPK), myPK, mySK)};
            federator->getSignatureCollector(chain).processSig(
                mId, myPK, std::move(sig), std::move(tx));
        }
        catch (...)
        {
            JLOGV(j.fatal(), "invalid transaction", jv("tx", tx));
            assert(0);
        }
    };

    app_.getJobQueue().addJob(jtFEDERATORSIGNATURE, "federator signature", job);
}

bool
SignatureCollector::processSig(
    MessageId const& mId,
    PublicKey const& pk,
    Buffer const& sig,
    std::optional<STTx> const& txOpt)
{
    JLOGV(
        j_.trace(),
        "processSig",
        jv("public key", strHex(pk)),
        jv("message", mId));
    if (!signers_.isFederator(pk))
    {
        return false;
    }

    auto valid = addSig(mId, pk, sig, txOpt);
    if (txOpt)
        shareSig(mId, sig);
    return valid;
}

void
SignatureCollector::expire()
{
    std::lock_guard lock(mtx_);
    beast::expire(messages_, messageExpire);
}

bool
SignatureCollector::addSig(
    MessageId const& mId,
    PublicKey const& pk,
    Buffer const& sig,
    std::optional<STTx> const& txOpt)
{
    JLOGV(
        j_.trace(),
        "addSig",
        jv("message", mId),
        jv("public key", strHex(pk)),
        jv("sig", strHex(sig)));

    std::lock_guard lock(mtx_);
    auto txi = messages_.find(mId);
    if (txi == messages_.end())
    {
        PeerSignatureMap sigMaps;
        sigMaps.emplace(pk, sig);
        MultiSigMessage m{sigMaps, txOpt};
        messages_.emplace(mId, std::move(m));
        return true;
    }

    auto const verifySingle = [&](PublicKey const& pk,
                                  Buffer const& sig) -> bool {
        Serializer s;
        s.add32(HashPrefix::txMultiSign);
        (*txi->second.tx_).addWithoutSigningFields(s);
        s.addBitString(calcAccountID(pk));
        return verify(pk, s.slice(), sig, true);
    };

    MultiSigMessage& message = txi->second;
    if (txOpt)
    {
        message.tx_.emplace(std::move(*txOpt));

        for (auto i = message.sigMaps_.begin(); i != message.sigMaps_.end();)
        {
            if (verifySingle(i->first, i->second))
                ++i;
            else
            {
                JLOGV(
                    j_.trace(),
                    "verifySingle failed",
                    jv("public key", strHex(i->first)));
                message.sigMaps_.erase(i);
            }
        }
    }
    else
    {
        if (message.tx_)
        {
            if (!verifySingle(pk, sig))
            {
                JLOGV(
                    j_.trace(),
                    "verifySingle failed",
                    jv("public key", strHex(pk)));
                return false;
            }
        }
    }
    message.sigMaps_.emplace(pk, sig);

    if (!message.submitted_ && message.tx_ &&
        message.sigMaps_.size() >= signers_.quorum())
    {
        // message.submitted_ = true;
        submit(mId, lock);
    }
    return true;
}

void
SignatureCollector::shareSig(MessageId const& mId, Buffer const& sig)
{
    JLOGV(j_.trace(), "shareSig", jv("message", mId), jv("sig", strHex(sig)));

    std::shared_ptr<Message> toSend = [&]() -> std::shared_ptr<Message> {
        protocol::TMFederatorAccountCtrlSignature m;
        m.set_chain(isMainChain_ ? ::protocol::fct_MAIN : ::protocol::fct_SIDE);
        m.set_publickey(myPubKey_.data(), myPubKey_.size());
        m.set_messageid(mId.data(), mId.size());
        m.set_signature(sig.data(), sig.size());

        return std::make_shared<Message>(
            m, protocol::mtFederatorAccountCtrlSignature);
    }();

    Overlay& overlay = app_.overlay();
    HashRouter& hashRouter = app_.getHashRouter();
    auto const suppression = computeMessageSuppression(mId, sig);

    overlay.foreach([&](std::shared_ptr<Peer> const& p) {
        hashRouter.addSuppressionPeer(suppression, p->id());
        JLOGV(
            j_.trace(),
            "sending signature to peer",
            jv("pid", p->id()),
            jv("mid", mId));
        p->send(toSend);
    });
}

void
SignatureCollector::submit(
    MessageId const& mId,
    std::lock_guard<std::mutex> const&)
{
    JLOGV(j_.trace(), "submit", jv("message", mId));

    assert(messages_.find(mId) != messages_.end());
    auto& message = messages_[mId];
    assert(!message.submitted_);
    message.submitted_ = true;

    STArray signatures;
    auto sigCount = message.sigMaps_.size();
    assert(sigCount >= signers_.quorum());
    signatures.reserve(sigCount);

    for (auto const& item : message.sigMaps_)
    {
        STObject obj{sfSigner};
        obj[sfAccount] = calcAccountID(item.first);
        obj[sfSigningPubKey] = item.first;
        obj[sfTxnSignature] = item.second;
        signatures.push_back(std::move(obj));
    };

    std::sort(
        signatures.begin(),
        signatures.end(),
        [](STObject const& lhs, STObject const& rhs) {
            return lhs[sfAccount] < rhs[sfAccount];
        });

    message.tx_->setFieldArray(sfSigners, std::move(signatures));

    auto sp = message.tx_->getSeqProxy();
    if (sp.isTicket())
    {
        Json::Value r;
        r[jss::tx_blob] = strHex(message.tx_->getSerializer().peekData());

        JLOGV(j_.trace(), "submit", jv("tx", r));
        auto callback = [&](Json::Value const& response) {
            JLOGV(
                j_.trace(),
                "SignatureCollector::submit ",
                jv("response", response));
        };
        rpcChannel_->send("submit", r, callback);
    }
    else
    {
        JLOGV(
            j_.trace(),
            "forward to federator to submit",
            jv("tx", strHex(message.tx_->getSerializer().peekData())));
        federator_.addTxToSend(
            (isMainChain_ ? Federator::ChainType::mainChain
                          : Federator::ChainType::sideChain),
            sp.value(),
            *(message.tx_));
    }
}

void
SignatureCollector::setRpcChannel(std::shared_ptr<ChainListener> channel)
{
    rpcChannel_ = std::move(channel);
}

}  // namespace sidechain
}  // namespace ripple
