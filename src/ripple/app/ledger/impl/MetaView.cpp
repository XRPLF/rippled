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
#include <ripple/app/ledger/MetaView.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/Quality.h>
#include <ripple/app/main/Application.h>
#include <ripple/ledger/CachedView.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/types.h>

namespace ripple {

// #define META_DEBUG

// VFALCO TODO Replace this macro with a documented language constant
//
/** Maximum number of entries in a directory page
    A change would be protocol-breaking.
*/
#ifndef DIR_NODE_MAX
#define DIR_NODE_MAX  32
#endif

shallow_copy_t const shallow_copy {};
open_ledger_t const open_ledger {};

MetaView::MetaView (shallow_copy_t,
        MetaView const& other)
    : base_ (other.base_)
    , flags_ (other.flags_)
    , info_ (other.info_)
    , txs_ (other.txs_)
    , items_ (other.items_)
    , destroyedCoins_(
        other.destroyedCoins_)
    , hold_(other.hold_)
{
}

MetaView::MetaView (open_ledger_t,
    BasicView const& parent,
        std::shared_ptr<
            void const> hold)
    : base_ (parent)
    , flags_ (tapNONE)
    , info_ (parent.info())
    , hold_(std::move(hold))
{
    assert(! parent.open());
    info_.open = true;
    info_.seq = parent.info().seq + 1;
    info_.parentCloseTime =
        parent.info().closeTime;
    // Give clients a unique but
    // meaningless hash for open ledgers.
    info_.hash = parent.info().hash + uint256(1);
}

MetaView::MetaView (BasicView const& base,
        ViewFlags flags, std::shared_ptr<
            void const> hold)
    : base_ (base)
    , flags_ (flags)
    , info_ (base.info())
    , hold_(std::move(hold))
{
}

//------------------------------------------------------------------------------

bool
MetaView::exists (Keylet const& k) const
{
    assert(k.key.isNonZero());
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base_.exists(k);
    if (iter->second.first == taaDELETE)
        return false;
    if (! k.check(*iter->second.second))
        return false;
    return true;
}

/*  This works by first calculating succ() on the parent,
    then calculating succ() our internal list, and taking
    the lower of the two.
*/
boost::optional<uint256>
MetaView::succ (uint256 const& key,
    boost::optional<uint256> last) const
{
    boost::optional<uint256> next = key;
    item_list::const_iterator iter;
    // Find base successor that is
    // not also deleted in our list
    do
    {
        next = base_.succ(*next, last);
        if (! next)
            break;
        iter = items_.find(*next);
    }
    while (iter != items_.end() &&
        iter->second.first == taaDELETE);
    // Find non-deleted successor in our list
    for (iter = items_.upper_bound(key);
        iter != items_.end (); ++iter)
    {
        if (iter->second.first != taaDELETE)
        {
            // Found both, return the lower key
            if (! next || next > iter->first)
                next = iter->first;
            break;
        }
    }
    // Nothing in our list, return
    // what we got from the parent.
    if (last && next >= last)
        return boost::none;
    return next;
}

std::shared_ptr<SLE const>
MetaView::read (Keylet const& k) const
{
    assert(k.key.isNonZero());
    if (k.key.isZero())
        return nullptr;
    // VFALCO TODO Shouldn't we create taaCACHED
    //             items to optimize reads?
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
    {
        auto const sle =
            base_.read(k);
        if (! sle)
            return nullptr;
        return sle;
    }
    if (iter->second.first == taaDELETE)
        return nullptr;
    auto const& sle =
        iter->second.second;
    if (! k.check(*sle))
        return nullptr;
    return sle;
}

//------------------------------------------------------------------------------

class MetaView::tx_iterator_impl
    : public BasicView::iterator_impl
{
private:
    bool metadata_;
    tx_map::const_iterator iter_;

public:
    explicit
    tx_iterator_impl (bool metadata,
            tx_map::const_iterator iter)
        : metadata_(metadata)
        , iter_(iter)
    {
    }

    std::unique_ptr<iterator_impl>
    copy() const override
    {
        return std::make_unique<
            tx_iterator_impl>(
                metadata_, iter_);
    }

    bool
    equal (iterator_impl const& impl) const override
    {
        auto const& other = dynamic_cast<
            tx_iterator_impl const&>(impl);
        return iter_ == other.iter_;
    }

    void
    increment() override
    {
        ++iter_;
    }

    txs_type::value_type
    dereference() const override
    {
        txs_type::value_type result;
        {
            SerialIter sit(
                iter_->second.first->slice());
            result.first = std::make_shared<
                STTx const>(sit);
        }
        if (metadata_)
        {
            SerialIter sit(
                iter_->second.second->slice());
            result.second = std::make_shared<
                STObject const>(sit, sfMetadata);
        }
        return result;
    }
};

bool
MetaView::txEmpty() const
{
    return txs_.empty();
}

auto
MetaView::txBegin() const ->
    std::unique_ptr<iterator_impl>
{
    return std::make_unique<
        tx_iterator_impl>(
            closed(), txs_.cbegin());
}

auto
MetaView::txEnd() const ->
    std::unique_ptr<iterator_impl>
{
    return std::make_unique<
        tx_iterator_impl>(
            closed(), txs_.cend());
}

//------------------------------------------------------------------------------

bool
MetaView::unchecked_erase (uint256 const& key)
{
    auto const iter =
        items_.lower_bound(key);
    if (iter == items_.end() ||
        iter->first != key)
    {
        assert(base_.exists(
            keylet::unchecked(key)));
        using namespace std;
        items_.emplace_hint(iter, piecewise_construct,
            forward_as_tuple(key), forward_as_tuple(
                taaDELETE, make_shared<SLE>(
                    *base_.read(keylet::unchecked(key)))));
        return true;
    }
    if (iter->second.first == taaCREATE)
    {
        items_.erase(iter);
        return true;
    }
    assert(iter->second.first != taaDELETE);
    iter->second.first = taaDELETE;
    return true;
}

void
MetaView::unchecked_insert(
    std::shared_ptr<SLE>&& sle)
{
    auto const iter =
        items_.lower_bound(sle->key());
    if (iter == items_.end() ||
        iter->first != sle->key())
    {
        // VFALCO return Keylet from SLE
        assert(! base_.exists(Keylet{
            sle->getType(), sle->key()}));
        using namespace std;
        items_.emplace_hint(iter, piecewise_construct,
            forward_as_tuple(sle->key()),
                forward_as_tuple(taaCREATE,
                    move(sle)));
        return;
    }
    switch(iter->second.first)
    {
    case taaMODIFY:
        throw std::runtime_error(
            "insert after modify");
    case taaCREATE:
        throw std::runtime_error(
            "insert after create");
    case taaCACHED:
        throw std::runtime_error(
            "insert after peek");
    case taaDELETE:
    default:
        break;
    };
    // VFALCO return Keylet from SLE
    assert(base_.exists(
        Keylet{sle->getType(), sle->key()}));
    iter->second.first = taaMODIFY;
    iter->second.second = std::move(sle);
}

void
MetaView::unchecked_replace (std::shared_ptr<SLE>&& sle)
{
    auto const iter =
        items_.lower_bound(sle->key());
    if (iter == items_.end() ||
        iter->first != sle->key())
    {
        // VFALCO return Keylet from SLE
        assert(base_.exists(Keylet{
            sle->getType(), sle->key()}));
        using namespace std;
        items_.emplace_hint(iter, piecewise_construct,
            forward_as_tuple(sle->key()),
                forward_as_tuple(taaMODIFY,
                    move(sle)));
        return;
    }
    if (iter->second.first == taaDELETE)
        throw std::runtime_error(
            "replace after delete");
    if (iter->second.first != taaCREATE)
        iter->second.first = taaMODIFY;
    iter->second.second = std::move(sle);
}

void
MetaView::destroyCoins (std::uint64_t feeDrops)
{
    destroyedCoins_ += feeDrops;
}

std::size_t
MetaView::txCount() const
{
    return base_.txCount() + txs_.size();
}

bool
MetaView::txExists (uint256 const& key) const
{
    if (txs_.count(key) > 0)
        return true;
    return base_.txExists(key);
}

void
MetaView::txInsert (uint256 const& key,
    std::shared_ptr<Serializer const
        > const& txn, std::shared_ptr<
            Serializer const> const& metaData)
{
    if (base_.txExists(key) ||
        ! txs_.emplace(key,
            std::make_pair(txn, metaData)).second)
        LogicError("duplicate_tx: " + to_string(key));
}

std::vector<uint256>
MetaView::txList() const
{
    std::vector<uint256> list;
    list.reserve(txs_.size());
    for (auto const& e : txs_)
        list.push_back(e.first);
    return list;
}

std::shared_ptr<SLE>
MetaView::peek (Keylet const& k)
{
    assert(k.key.isNonZero());
    if (k.key.isZero())
        return nullptr;
    auto iter = items_.lower_bound(k.key);
    if (iter == items_.end() ||
        iter->first != k.key)
    {
        auto const sle =
            base_.read(k);
        if (! sle)
            return nullptr;
        // Make our own copy
        iter = items_.emplace_hint (iter,
            std::piecewise_construct,
                std::forward_as_tuple(sle->getIndex()),
                    std::forward_as_tuple(taaCACHED,
                        std::make_shared<SLE>(*sle)));
        return iter->second.second;
    }
    if (iter->second.first == taaDELETE)
        return nullptr;
    if (! k.check(*iter->second.second))
        return nullptr;
    return iter->second.second;
}

void
MetaView::erase (std::shared_ptr<SLE> const& sle)
{
    auto const iter =
        items_.find(sle->getIndex());
    assert(iter != items_.end());
    if (iter == items_.end())
        return;
    assert(iter->second.first != taaDELETE);
    assert(iter->second.second == sle);
    if (iter->second.first == taaDELETE)
        return;
    if (iter->second.first == taaCREATE)
    {
        items_.erase(iter);
        return;
    }
    assert(iter->second.first == taaCACHED ||
            iter->second.first == taaMODIFY);
    iter->second.first = taaDELETE;
}

void
MetaView::insert (std::shared_ptr<SLE> const& sle)
{
    auto const iter = items_.lower_bound(sle->key());
    if (iter == items_.end() ||
        iter->first != sle->key())
    {
        // VFALCO return Keylet from SLE
        assert(! base_.exists(
            Keylet{sle->getType(), sle->key()}));
        items_.emplace_hint(iter, std::piecewise_construct,
            std::forward_as_tuple(sle->getIndex()),
                std::forward_as_tuple(taaCREATE, sle));
        return;
    }
    switch(iter->second.first)
    {
    case taaMODIFY:
        throw std::runtime_error(
            "insert after modify");
    case taaCREATE:
        // This could be made to work (?)
        throw std::runtime_error(
            "insert after create");
    case taaCACHED:
        throw std::runtime_error(
            "insert after copy");
    default:
        break;
    }
    // Existed in parent, deleted here
    assert(base_.exists(
        Keylet{sle->getType(), sle->key()}));
    iter->second.first = taaMODIFY;
    iter->second.second = sle;
}

void
MetaView::update (std::shared_ptr<SLE> const& sle)
{
    auto const iter = items_.lower_bound(sle->key());
    if (iter == items_.end() ||
        iter->first != sle->key())
    {
        // VFALCO return Keylet from SLE
        assert(base_.exists(
            Keylet{sle->getType(), sle->key()}));
        items_.emplace_hint(iter, std::piecewise_construct,
            std::forward_as_tuple(sle->key()),
                std::forward_as_tuple(taaMODIFY, sle));
        return;
    }
    // VFALCO Should we throw?
    assert(iter->second.second == sle);
    if (iter->second.first == taaDELETE)
        throw std::runtime_error(
            "update after delete");
    if (iter->second.first != taaCREATE)
        iter->second.first = taaMODIFY;
}

//------------------------------------------------------------------------------

void MetaView::apply(
    BasicView& to, beast::Journal j)
{
    assert(&to == &base_);
    assert(to.info().open == info_.open);
    // Write back the account states
    for (auto& item : items_)
    {
        // VFALCO TODO rvalue move the second, make
        //             sure the mNodes is not used after
        //             this function is called.
        auto& sle = item.second.second;
        switch (item.second.first)
        {
        case taaCACHED:
            assert(to.exists(
                Keylet(sle->getType(), item.first)));
            break;

        case taaCREATE:
            // VFALCO Is this logging necessary anymore?
            WriteLog (lsDEBUG, View) <<
                "applyTransaction: taaCREATE: " << sle->getText ();
            to.unchecked_insert(std::move(sle));
            break;

        case taaMODIFY:
        {
            WriteLog (lsDEBUG, View) <<
                "applyTransaction: taaMODIFY: " << sle->getText ();
            to.unchecked_replace(std::move(sle));
            break;
        }

        case taaDELETE:
            WriteLog (lsDEBUG, View) <<
                "applyTransaction: taaDELETE: " << sle->getText ();
            to.unchecked_erase(sle->key());
            break;
        }
    }

    // Write the transactions
    for (auto& tx : txs_)
        to.txInsert(tx.first,
            tx.second.first,
                tx.second.second);

    to.destroyCoins(destroyedCoins_);
}

Json::Value MetaView::getJson (int) const
{
    Json::Value ret (Json::objectValue);

    Json::Value nodes (Json::arrayValue);

    for (auto it = items_.begin (), end = items_.end (); it != end; ++it)
    {
        Json::Value entry (Json::objectValue);
        entry[jss::node] = to_string (it->first);

        switch (it->second.second->getType ())
        {
        case ltINVALID:
            entry[jss::type] = "invalid";
            break;

        case ltACCOUNT_ROOT:
            entry[jss::type] = "acccount_root";
            break;

        case ltDIR_NODE:
            entry[jss::type] = "dir_node";
            break;

        case ltRIPPLE_STATE:
            entry[jss::type] = "ripple_state";
            break;

        case ltNICKNAME:
            entry[jss::type] = "nickname";
            break;

        case ltOFFER:
            entry[jss::type] = "offer";
            break;

        default:
            assert (false);
        }

        switch (it->second.first)
        {
        case taaCACHED:
            entry[jss::action] = "cache";
            break;

        case taaMODIFY:
            entry[jss::action] = "modify";
            break;

        case taaDELETE:
            entry[jss::action] = "delete";
            break;

        case taaCREATE:
            entry[jss::action] = "create";
            break;

        default:
            assert (false);
        }

        nodes.append (entry);
    }

    ret[jss::nodes] = nodes;

    // VFALCO The meta only exists during apply() now
    //ret[jss::metaData] = meta.getJson (0);

    return ret;
}

//------------------------------------------------------------------------------

void
MetaView::apply (BasicView& to,
    STTx const& tx, TER ter, beast::Journal j)
{
    auto const sTx =
        std::make_shared<Serializer>();
    tx.add(*sTx);

    std::shared_ptr<Serializer> sMeta;

    if (closed())
    {
        TxMeta meta;
        // VFALCO Shouldn't TxMeta ctor do this?
        meta.init (tx.getTransactionID(), seq());
        if (deliverAmount_)
            meta.setDeliveredAmount(
                *deliverAmount_);

        Mods newMod;
        for (auto& it : items_)
        {
            auto type = &sfGeneric;
            switch (it.second.first)
            {
            case taaMODIFY:
            #ifdef META_DEBUG
                JLOG(j.trace) << "modify " << it.first;
            #endif
                type = &sfModifiedNode;
                break;

            case taaDELETE:
            #ifdef META_DEBUG
                JLOG(j.trace) << "delete " << it.first;
            #endif
                type = &sfDeletedNode;
                break;

            case taaCREATE:
            #ifdef META_DEBUG
                JLOG(j.trace) << "insert " << it.first;
            #endif
                type = &sfCreatedNode;
                break;

            default: // ignore these
                break;
            }
            if (type == &sfGeneric)
                continue;
            auto const origNode =
                base_.read(keylet::unchecked(it.first));
            auto curNode = it.second.second;
            if ((type == &sfModifiedNode) && (*curNode == *origNode))
                continue;
            std::uint16_t nodeType = curNode
                ? curNode->getFieldU16 (sfLedgerEntryType)
                : origNode->getFieldU16 (sfLedgerEntryType);
            meta.setAffectedNode (it.first, *type, nodeType);
            if (type == &sfDeletedNode)
            {
                assert (origNode && curNode);
                threadOwners (meta, origNode, newMod, j);

                STObject prevs (sfPreviousFields);
                for (auto const& obj : *origNode)
                {
                    // go through the original node for
                    // modified  fields saved on modification
                    if (obj.getFName().shouldMeta(
                            SField::sMD_ChangeOrig) &&
                                ! curNode->hasMatchingEntry (obj))
                        prevs.emplace_back (obj);
                }

                if (!prevs.empty ())
                    meta.getAffectedNode(it.first).emplace_back(std::move(prevs));

                STObject finals (sfFinalFields);
                for (auto const& obj : *curNode)
                {
                    // go through the final node for final fields
                    if (obj.getFName().shouldMeta(
                            SField::sMD_Always | SField::sMD_DeleteFinal))
                        finals.emplace_back (obj);
                }

                if (!finals.empty ())
                    meta.getAffectedNode (it.first).emplace_back (std::move(finals));
            }
            else if (type == &sfModifiedNode)
            {
                assert (curNode && origNode);

                if (curNode->isThreadedType ()) // thread transaction to node it modified
                    threadTx (meta, curNode, newMod);

                STObject prevs (sfPreviousFields);
                for (auto const& obj : *origNode)
                {
                    // search the original node for values saved on modify
                    if (obj.getFName ().shouldMeta (SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry (obj))
                        prevs.emplace_back (obj);
                }

                if (!prevs.empty ())
                    meta.getAffectedNode (it.first).emplace_back (std::move(prevs));

                STObject finals (sfFinalFields);
                for (auto const& obj : *curNode)
                {
                    // search the final node for values saved always
                    if (obj.getFName ().shouldMeta (SField::sMD_Always | SField::sMD_ChangeNew))
                        finals.emplace_back (obj);
                }

                if (!finals.empty ())
                    meta.getAffectedNode (it.first).emplace_back (std::move(finals));
            }
            else if (type == &sfCreatedNode) // if created, thread to owner(s)
            {
                assert (curNode && !origNode);
                threadOwners (meta, curNode, newMod, j);

                if (curNode->isThreadedType ()) // always thread to self
                    threadTx (meta, curNode, newMod);

                STObject news (sfNewFields);
                for (auto const& obj : *curNode)
                {
                    // save non-default values
                    if (!obj.isDefault () &&
                            obj.getFName().shouldMeta(
                                SField::sMD_Create | SField::sMD_Always))
                        news.emplace_back (obj);
                }

                if (!news.empty ())
                    meta.getAffectedNode (it.first).emplace_back (std::move(news));
            }
            else
            {
                assert (false);
            }
        }

        // add any new modified nodes to the modification set
        for (auto& it : newMod)
            update (it.second);

        sMeta = std::make_shared<Serializer>();
        meta.addRaw (*sMeta, ter, txCount());

        // VFALCO For diagnostics do we want to show
        //        metadata even when the base view is open?
        JLOG(j.trace) <<
            "metadata " << meta.getJson (0);
    }

    txInsert (tx.getTransactionID(),
        sTx, sMeta);

    apply(to);
}

//------------------------------------------------------------------------------

bool
MetaView::threadTx (TxMeta& meta,
    std::shared_ptr<SLE> const& to,
        Mods& mods)
{
    uint256 prevTxID;
    std::uint32_t prevLgrID;
    if (! to->thread(meta.getTxID(),
            meta.getLgrSeq(), prevTxID, prevLgrID))
        return false;
    if (prevTxID.isZero () ||
        TxMeta::thread(
            meta.getAffectedNode(to,
                sfModifiedNode), prevTxID,
                    prevLgrID))
        return true;
    assert (false);
    return false;
}

std::shared_ptr<SLE>
MetaView::getForMod (uint256 const& key,
    Mods& mods, beast::Journal j)
{
    auto iter = items_.find (key);
    if (iter != items_.end ())
    {
        if (iter->second.first == taaDELETE)
        {
            JLOG(j.fatal) <<
                "Trying to thread to deleted node";
            return nullptr;
        }
        if (iter->second.first == taaCACHED)
            iter->second.first = taaMODIFY;
        return iter->second.second;
    }
    {
        auto miter = mods.find (key);
        if (miter != mods.end ())
        {
            assert (miter->second);
            return miter->second;
        }
    }
    // VFALCO NOTE Should this be read() or peek()?
    auto const csle = base_.read(
        keylet::unchecked(key));
    if (! csle)
        return nullptr;
    // Need to make a copy here
    auto sle =
        std::make_shared<SLE>(*csle);
    mods.emplace(key, sle);
    return sle;
}

bool
MetaView::threadTx (TxMeta& meta,
    AccountID const& to, Mods& mods,
        beast::Journal j)
{
    auto const sle = getForMod(
        keylet::account(to).key, mods, j);
#ifdef META_DEBUG
    JLOG(j.trace) <<
        "Thread to " << toBase58(to);
#endif
    assert(sle);
    if (! sle)
    {
        JLOG(j.fatal) <<
            "Threading to non-existent account: " <<
                toBase58(to);
        return false;
    }

    return threadTx (meta, sle, mods);
}

bool
MetaView::threadOwners (TxMeta& meta,
    std::shared_ptr<
        SLE const> const& sle, Mods& mods,
            beast::Journal j)
{
    // thread new or modified sle to owner or owners
    // VFALCO Why not isFieldPresent?
    if (sle->getType() != ltACCOUNT_ROOT &&
        sle->getFieldIndex(sfAccount) != -1)
    {
        // thread to owner's account
    #ifdef META_DEBUG
        JLOG(j.trace) << "Thread to single owner";
    #endif
        return threadTx (meta, sle->getAccountID(
            sfAccount), mods, j);
    }
    else if (sle->getType() == ltRIPPLE_STATE)
    {
        // thread to owner's accounts
    #ifdef META_DEBUG
        JLOG(j.trace) << "Thread to two owners";
    #endif
        return
            threadTx (meta, sle->getFieldAmount(
                sfLowLimit).getIssuer(), mods, j) &&
            threadTx (meta, sle->getFieldAmount(
                sfHighLimit).getIssuer(), mods, j);
    }
    return false;
}

} // ripple
