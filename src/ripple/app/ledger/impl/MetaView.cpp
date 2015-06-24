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
#include <ripple/protocol/Quality.h>
#include <ripple/app/main/Application.h>
#include <ripple/ledger/CachedView.h>
#include <ripple/app/ledger/MetaView.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Indexes.h>

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

MetaView::MetaView(Ledger::ref ledger,
    uint256 const& transactionID,
        std::uint32_t ledgerID,
            TransactionEngineParams params)
    : parent_(&*ledger)
    , mParams(params)
{
    mSet.init (transactionID, ledgerID);
}

MetaView::MetaView (BasicView& parent,
        bool openLedger)
    : parent_ (&parent)
    , mParams (openLedger
        ? tapOPEN_LEDGER : tapNONE)
{
}

MetaView::MetaView (Ledger::ref ledger,
    TransactionEngineParams tep)
    : parent_(&*ledger)
    , mParams (tep)
{
}

MetaView::MetaView (MetaView& parent)
    : parent_(&parent)
    , mParams(parent.mParams)
{
}

//------------------------------------------------------------------------------

bool
MetaView::exists (Keylet const& k) const
{
    assert(k.key.isNonZero());
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return parent_->exists(k);
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
    list_type::const_iterator iter;
    // Find parent successor that is
    // not also deleted in our list
    do
    {
        next = parent_->succ(*next, last);
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
            parent_->read(k);
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

bool
MetaView::unchecked_erase (uint256 const& key)
{
    auto const iter =
        items_.lower_bound(key);
    if (iter == items_.end() ||
        iter->first != key)
    {
        assert(parent_->exists(
            keylet::unchecked(key)));
        using namespace std;
        items_.emplace_hint(iter, piecewise_construct,
            forward_as_tuple(key), forward_as_tuple(
                taaDELETE, make_shared<SLE>(
                    *parent_->read(keylet::unchecked(key)))));
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
        assert(! parent_->exists(Keylet{
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
    assert(parent_->exists(
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
        assert(parent_->exists(Keylet{
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
            parent_->read(k);
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
        assert(! parent_->exists(
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
    assert(parent_->exists(
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
        assert(parent_->exists(
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

bool
MetaView::openLedger() const
{
    return mParams & tapOPEN_LEDGER;
}

//------------------------------------------------------------------------------

void MetaView::apply()
{

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
            assert(parent_->exists(
                Keylet(sle->getType(), item.first)));
            break;

        case taaCREATE:
            // VFALCO Is this logging necessary anymore?
            WriteLog (lsDEBUG, View) <<
                "applyTransaction: taaCREATE: " << sle->getText ();
            parent_->unchecked_insert(std::move(sle));
            break;

        case taaMODIFY:
        {
            WriteLog (lsDEBUG, View) <<
                "applyTransaction: taaMODIFY: " << sle->getText ();
            parent_->unchecked_replace(std::move(sle));
            break;
        }

        case taaDELETE:
            WriteLog (lsDEBUG, View) <<
                "applyTransaction: taaDELETE: " << sle->getText ();
            parent_->unchecked_erase(sle->key());
            break;
        }
    }

    // Safety precaution since we moved the
    // entries out, apply() cannot be called twice.
    items_.clear();
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

    ret[jss::metaData] = mSet.getJson (0);

    return ret;
}

//------------------------------------------------------------------------------

std::shared_ptr<SLE>
MetaView::getForMod (uint256 const& key,
    Mods& mods)
{
    auto iter = items_.find (key);
    if (iter != items_.end ())
    {
        if (iter->second.first == taaDELETE)
        {
            WriteLog (lsFATAL, View) <<
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
    auto const csle = parent_->read(
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
MetaView::threadTx (RippleAddress const& to,
    Mods& mods)
{
    auto const sle = getForMod(keylet::account(
        to.getAccountID()).key, mods);
#ifdef META_DEBUG
    WriteLog (lsTRACE, View) << "Thread to " << threadTo.getAccountID ();
#endif
    if (! sle)
    {
        WriteLog (lsFATAL, View) <<
            "Threading to non-existent account: " << to.humanAccountID ();
        assert (false);
        return false;
    }

    return threadTx (sle, mods);
}

bool
MetaView::threadTx(
    std::shared_ptr<SLE> const& to,
        Mods& mods)
{
    uint256 prevTxID;
    std::uint32_t prevLgrID;
    if (! to->thread(mSet.getTxID(),
            mSet.getLgrSeq(), prevTxID, prevLgrID))
        return false;
    if (prevTxID.isZero () ||
        TransactionMetaSet::thread(
            mSet.getAffectedNode(to,
                sfModifiedNode), prevTxID,
                    prevLgrID))
        return true;
    assert (false);
    return false;
}

bool
MetaView::threadOwners(std::shared_ptr<
    SLE const> const& sle, Mods& mods)
{
    // thread new or modified sle to owner or owners
    if (sle->hasOneOwner())
    {
        // thread to owner's account
    #ifdef META_DEBUG
        WriteLog (lsTRACE, View) << "Thread to single owner";
    #endif
        return threadTx (sle->getOwner(), mods);
    }
    else if (sle->hasTwoOwners ()) // thread to owner's accounts
    {
    #ifdef META_DEBUG
        WriteLog (lsTRACE, View) << "Thread to two owners";
    #endif
        return threadTx(sle->getFirstOwner(), mods) &&
            threadTx(sle->getSecondOwner(), mods);
    }
    return false;
}

void
MetaView::calcRawMeta (Serializer& s,
    TER result, std::uint32_t index)
{
    // calculate the raw meta data and return it. This must be called before the set is committed

    // Entries modified only as a result of building the transaction metadata
    Mods newMod;

    for (auto& it : items_)
    {
        auto type = &sfGeneric;

        switch (it.second.first)
        {
        case taaMODIFY:
#ifdef META_DEBUG
            WriteLog (lsTRACE, View) << "Modified Node " << it.first;
#endif
            type = &sfModifiedNode;
            break;

        case taaDELETE:
#ifdef META_DEBUG
            WriteLog (lsTRACE, View) << "Deleted Node " << it.first;
#endif
            type = &sfDeletedNode;
            break;

        case taaCREATE:
#ifdef META_DEBUG
            WriteLog (lsTRACE, View) << "Created Node " << it.first;
#endif
            type = &sfCreatedNode;
            break;

        default: // ignore these
            break;
        }

        if (type == &sfGeneric)
            continue;

        auto const origNode =
            parent_->read(keylet::unchecked(it.first));
        auto curNode = it.second.second;

        if ((type == &sfModifiedNode) && (*curNode == *origNode))
            continue;

        std::uint16_t nodeType = curNode
            ? curNode->getFieldU16 (sfLedgerEntryType)
            : origNode->getFieldU16 (sfLedgerEntryType);

        mSet.setAffectedNode (it.first, *type, nodeType);

        if (type == &sfDeletedNode)
        {
            assert (origNode && curNode);
            threadOwners (origNode, newMod); // thread transaction to owners

            STObject prevs (sfPreviousFields);
            for (auto const& obj : *origNode)
            {
                // go through the original node for modified fields saved on modification
                if (obj.getFName ().shouldMeta (SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry (obj))
                    prevs.emplace_back (obj);
            }

            if (!prevs.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(prevs));

            STObject finals (sfFinalFields);
            for (auto const& obj : *curNode)
            {
                // go through the final node for final fields
                if (obj.getFName ().shouldMeta (SField::sMD_Always | SField::sMD_DeleteFinal))
                    finals.emplace_back (obj);
            }

            if (!finals.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(finals));
        }
        else if (type == &sfModifiedNode)
        {
            assert (curNode && origNode);

            if (curNode->isThreadedType ()) // thread transaction to node it modified
                threadTx (curNode, newMod);

            STObject prevs (sfPreviousFields);
            for (auto const& obj : *origNode)
            {
                // search the original node for values saved on modify
                if (obj.getFName ().shouldMeta (SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry (obj))
                    prevs.emplace_back (obj);
            }

            if (!prevs.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(prevs));

            STObject finals (sfFinalFields);
            for (auto const& obj : *curNode)
            {
                // search the final node for values saved always
                if (obj.getFName ().shouldMeta (SField::sMD_Always | SField::sMD_ChangeNew))
                    finals.emplace_back (obj);
            }

            if (!finals.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(finals));
        }
        else if (type == &sfCreatedNode) // if created, thread to owner(s)
        {
            assert (curNode && !origNode);
            threadOwners (curNode, newMod);

            if (curNode->isThreadedType ()) // always thread to self
                threadTx (curNode, newMod);

            STObject news (sfNewFields);
            for (auto const& obj : *curNode)
            {
                // save non-default values
                if (!obj.isDefault () && obj.getFName ().shouldMeta (SField::sMD_Create | SField::sMD_Always))
                    news.emplace_back (obj);
            }

            if (!news.empty ())
                mSet.getAffectedNode (it.first).emplace_back (std::move(news));
        }
        else assert (false);
    }

    // add any new modified nodes to the modification set
    for (auto& it : newMod)
        update (it.second);

    mSet.addRaw (s, result, index);
    WriteLog (lsTRACE, View) << "Metadata:" << mSet.getJson (0);
}

} // ripple
