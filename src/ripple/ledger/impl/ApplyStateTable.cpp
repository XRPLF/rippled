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

#include <ripple/ledger/detail/ApplyStateTable.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/st.h>
#include <cassert>

namespace ripple {
namespace detail {

void
ApplyStateTable::apply (RawView& to) const
{
    to.rawDestroyXRP(dropsDestroyed_);
    for (auto const& item : items_)
    {
        auto const& sle =
            item.second.second;
        switch(item.second.first)
        {
        case Action::cache:
            break;
        case Action::erase:
            to.rawErase(sle);
            break;
        case Action::insert:
            to.rawInsert(sle);
            break;
        case Action::modify:
            to.rawReplace(sle);
            break;
        };
    }
}

std::size_t
ApplyStateTable::size () const
{
    std::size_t ret = 0;
    for (auto& item : items_)
    {
        switch (item.second.first)
        {
        case Action::erase:
        case Action::insert:
        case Action::modify:
            ++ret;
        default:
            break;
        }
    }
    return ret;
}

void
ApplyStateTable::visit (ReadView const& to,
    std::function <void (
        uint256 const& key,
        bool isDelete,
        std::shared_ptr <SLE const> const& before,
        std::shared_ptr <SLE const> const& after)> const& func) const
{
    for (auto& item : items_)
    {
        switch (item.second.first)
        {
        case Action::erase:
            func (item.first, true,
                to.read (keylet::unchecked (item.first)), item.second.second);
            break;

        case Action::insert:
            func (item.first, false,
                nullptr, item.second.second);
            break;

        case Action::modify:
            func (item.first, false,
                to.read (keylet::unchecked (item.first)), item.second.second);
            break;

        default:
            break;
        }
    }
}

void
ApplyStateTable::apply (OpenView& to,
    STTx const& tx, TER ter,
        boost::optional<STAmount> const& deliver,
            beast::Journal j)
{
    // Build metadata and insert
    auto const sTx =
        std::make_shared<Serializer>();
    tx.add(*sTx);
    std::shared_ptr<Serializer> sMeta;
    if (!to.open())
    {
        TxMeta meta;
        // VFALCO Shouldn't TxMeta ctor do this?
        meta.init (tx.getTransactionID(), to.seq());
        if (deliver)
            meta.setDeliveredAmount(*deliver);
        Mods newMod;
        for (auto& item : items_)
        {
            SField const* type;
            switch (item.second.first)
            {
            default:
            case Action::cache:
                continue;
            case Action::erase:
                type = &sfDeletedNode;
                break;
            case Action::insert:
                type = &sfCreatedNode;
                break;
            case Action::modify:
                type = &sfModifiedNode;
                break;
            }
            auto const origNode =
                to.read(keylet::unchecked(item.first));
            auto curNode = item.second.second;
            if ((type == &sfModifiedNode) && (*curNode == *origNode))
                continue;
            std::uint16_t nodeType = curNode
                ? curNode->getFieldU16 (sfLedgerEntryType)
                : origNode->getFieldU16 (sfLedgerEntryType);
            meta.setAffectedNode (item.first, *type, nodeType);
            if (type == &sfDeletedNode)
            {
                assert (origNode && curNode);
                threadOwners (to, meta, origNode, newMod, j);

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
                    meta.getAffectedNode(item.first).emplace_back(std::move(prevs));

                STObject finals (sfFinalFields);
                for (auto const& obj : *curNode)
                {
                    // go through the final node for final fields
                    if (obj.getFName().shouldMeta(
                            SField::sMD_Always | SField::sMD_DeleteFinal))
                        finals.emplace_back (obj);
                }

                if (!finals.empty ())
                    meta.getAffectedNode (item.first).emplace_back (std::move(finals));
            }
            else if (type == &sfModifiedNode)
            {
                assert (curNode && origNode);

                if (curNode->isThreadedType ()) // thread transaction to node item modified
                    threadItem (meta, curNode);

                STObject prevs (sfPreviousFields);
                for (auto const& obj : *origNode)
                {
                    // search the original node for values saved on modify
                    if (obj.getFName ().shouldMeta (SField::sMD_ChangeOrig) && !curNode->hasMatchingEntry (obj))
                        prevs.emplace_back (obj);
                }

                if (!prevs.empty ())
                    meta.getAffectedNode (item.first).emplace_back (std::move(prevs));

                STObject finals (sfFinalFields);
                for (auto const& obj : *curNode)
                {
                    // search the final node for values saved always
                    if (obj.getFName ().shouldMeta (SField::sMD_Always | SField::sMD_ChangeNew))
                        finals.emplace_back (obj);
                }

                if (!finals.empty ())
                    meta.getAffectedNode (item.first).emplace_back (std::move(finals));
            }
            else if (type == &sfCreatedNode) // if created, thread to owner(s)
            {
                assert (curNode && !origNode);
                threadOwners (to, meta, curNode, newMod, j);

                if (curNode->isThreadedType ()) // always thread to self
                    threadItem (meta, curNode);

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
                    meta.getAffectedNode (item.first).emplace_back (std::move(news));
            }
            else
            {
                assert (false);
            }
        }

        // add any new modified nodes to the modification set
        for (auto& mod : newMod)
            to.rawReplace (mod.second);

        sMeta = std::make_shared<Serializer>();
        meta.addRaw (*sMeta, ter, to.txCount());

        // VFALCO For diagnostics do we want to show
        //        metadata even when the base view is open?
        JLOG(j.trace()) <<
            "metadata " << meta.getJson (JsonOption::none);
    }
    to.rawTxInsert(
        tx.getTransactionID(),
            sTx, sMeta);
    apply(to);
}

//---

bool
ApplyStateTable::exists (ReadView const& base,
    Keylet const& k) const
{
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.exists(k);
    auto const& item = iter->second;
    auto const& sle = item.second;
    switch (item.first)
    {
    case Action::erase:
        return false;
    case Action::cache:
    case Action::insert:
    case Action::modify:
        break;
    }
    if (! k.check(*sle))
        return false;
    return true;
}

auto
ApplyStateTable::succ (ReadView const& base,
    key_type const& key, boost::optional<
        key_type> const& last) const ->
            boost::optional<key_type>
{
    boost::optional<key_type> next = key;
    items_t::const_iterator iter;
    // Find base successor that is
    // not also deleted in our list
    do
    {
        next = base.succ(*next, last);
        if (! next)
            break;
        iter = items_.find(*next);
    }
    while (iter != items_.end() &&
        iter->second.first == Action::erase);
    // Find non-deleted successor in our list
    for (iter = items_.upper_bound(key);
        iter != items_.end (); ++iter)
    {
        if (iter->second.first != Action::erase)
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
ApplyStateTable::read (ReadView const& base,
    Keylet const& k) const
{
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.read(k);
    auto const& item = iter->second;
    auto const& sle = item.second;
    switch (item.first)
    {
    case Action::erase:
        return nullptr;
    case Action::cache:
    case Action::insert:
    case Action::modify:
        break;
    };
    if (! k.check(*sle))
        return nullptr;
    return sle;
}

std::shared_ptr<SLE>
ApplyStateTable::peek (ReadView const& base,
    Keylet const& k)
{
    auto iter = items_.lower_bound(k.key);
    if (iter == items_.end() ||
        iter->first != k.key)
    {
        auto const sle = base.read(k);
        if (! sle)
            return nullptr;
        // Make our own copy
        using namespace std;
        iter = items_.emplace_hint (iter,
            piecewise_construct,
                forward_as_tuple(sle->key()),
                    forward_as_tuple(Action::cache,
                        make_shared<SLE>(*sle)));
        return iter->second.second;
    }
    auto const& item = iter->second;
    auto const& sle = item.second;
    switch (item.first)
    {
    case Action::erase:
        return nullptr;
    case Action::cache:
    case Action::insert:
    case Action::modify:
        break;
    };
    if (! k.check(*sle))
        return nullptr;
    return sle;
}

void
ApplyStateTable::erase(
    ReadView const& base,
        std::shared_ptr<SLE> const& sle)
{
    auto const iter =
        items_.find(sle->key());
    if (iter == items_.end())
        LogicError("ApplyStateTable::erase: missing key");
    auto& item = iter->second;
    if (item.second != sle)
        LogicError("ApplyStateTable::erase: unknown SLE");
    switch(item.first)
    {
    case Action::erase:
        LogicError("ApplyStateTable::erase: double erase");
        break;
    case Action::insert:
        items_.erase(iter);
        break;
    case Action::cache:
    case Action::modify:
        item.first = Action::erase;
        break;
    }
}

void
ApplyStateTable::rawErase (ReadView const& base,
    std::shared_ptr<SLE> const& sle)
{
    using namespace std;
    auto const result = items_.emplace(
        piecewise_construct,
            forward_as_tuple(sle->key()),
                forward_as_tuple(Action::erase, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch(item.first)
    {
    case Action::erase:
        LogicError("ApplyStateTable::rawErase: double erase");
        break;
    case Action::insert:
        items_.erase(result.first);
        break;
    case Action::cache:
    case Action::modify:
        item.first = Action::erase;
        item.second = sle;
        break;
    }
}

void
ApplyStateTable::insert (ReadView const& base,
    std::shared_ptr<SLE> const& sle)
{
    auto const iter =
        items_.lower_bound(sle->key());
    if (iter == items_.end() ||
        iter->first != sle->key())
    {
        using namespace std;
        items_.emplace_hint(iter,
            piecewise_construct,
                forward_as_tuple(sle->key()),
                    forward_as_tuple(Action::insert, sle));
        return;
    }
    auto& item = iter->second;
    switch(item.first)
    {
    case Action::cache:
        LogicError("ApplyStateTable::insert: already cached");
    case Action::insert:
        LogicError("ApplyStateTable::insert: already inserted");
    case Action::modify:
        LogicError("ApplyStateTable::insert: already modified");
    case Action::erase:
        break;
    }
    item.first = Action::modify;
    item.second = sle;
}

void
ApplyStateTable::replace (ReadView const& base,
    std::shared_ptr<SLE> const& sle)
{
    auto const iter =
        items_.lower_bound(sle->key());
    if (iter == items_.end() ||
        iter->first != sle->key())
    {
        using namespace std;
        items_.emplace_hint(iter, piecewise_construct,
            forward_as_tuple(sle->key()),
                forward_as_tuple(Action::modify, sle));
        return;
    }
    auto& item = iter->second;
    switch (item.first)
    {
    case Action::erase:
        LogicError("ApplyStateTable::replace: already erased");
    case Action::cache:
        item.first = Action::modify;
        break;
    case Action::insert:
    case Action::modify:
        break;
    }
    item.second = sle;
}

void
ApplyStateTable::update (ReadView const& base,
    std::shared_ptr<SLE> const& sle)
{
    auto const iter =
        items_.find(sle->key());
    if (iter == items_.end())
        LogicError("ApplyStateTable::update: missing key");
    auto& item = iter->second;
    if (item.second != sle)
        LogicError("ApplyStateTable::update: unknown SLE");
    switch (item.first)
    {
    case Action::erase:
        LogicError("ApplyStateTable::update: erased");
        break;
    case Action::cache:
        item.first = Action::modify;
        break;
    case Action::insert:
    case Action::modify:
        break;
    };
}

void
ApplyStateTable::destroyXRP(XRPAmount const& fee)
{
    dropsDestroyed_ += fee;
}

//------------------------------------------------------------------------------

// Insert this transaction to the SLE's threading list
void
ApplyStateTable::threadItem (TxMeta& meta,
    std::shared_ptr<SLE> const& sle)
{
    key_type prevTxID;
    LedgerIndex prevLgrID;
    if (! sle->thread(meta.getTxID(),
            meta.getLgrSeq(), prevTxID, prevLgrID))
        return;
    if (prevTxID.isZero())
        return;
    TxMeta::thread(meta.getAffectedNode(
        sle, sfModifiedNode), prevTxID, prevLgrID);
}

std::shared_ptr<SLE>
ApplyStateTable::getForMod (ReadView const& base,
    key_type const& key, Mods& mods, beast::Journal j)
{
    {
        auto miter = mods.find (key);
        if (miter != mods.end ())
        {
            assert (miter->second);
            return miter->second;
        }
    }
    {
        auto iter = items_.find (key);
        if (iter != items_.end ())
        {
            auto const& item = iter->second;
            if (item.first == Action::erase)
            {
                // VFALCO We need to think about throwing
                //        an exception or calling LogicError
                JLOG(j.fatal()) <<
                    "Trying to thread to deleted node";
                return nullptr;
            }
            if (item.first != Action::cache)
                return item.second;

            // If it's only cached, then the node is being modified only by
            // metadata; fall through and track it in the mods table.
        }
    }
    auto c = base.read (keylet::unchecked (key));
    if (! c)
    {
        // VFALCO We need to think about throwing
        //        an exception or calling LogicError
        JLOG(j.fatal()) <<
            "ApplyStateTable::getForMod: key not found";
        return nullptr;
    }
    auto sle = std::make_shared<SLE> (*c);
    mods.emplace(key, sle);
    return sle;
}

void
ApplyStateTable::threadTx (ReadView const& base,
    TxMeta& meta, AccountID const& to,
        Mods& mods, beast::Journal j)
{
    auto const sle = getForMod(base,
        keylet::account(to).key, mods, j);
    assert(sle);
    if (! sle)
    {
        // VFALCO We need to think about throwing
        //        an exception or calling LogicError
        JLOG(j.fatal()) <<
            "Threading to non-existent account: " <<
                toBase58(to);
        return;
    }
    threadItem (meta, sle);
}

void
ApplyStateTable::threadOwners (ReadView const& base,
    TxMeta& meta, std::shared_ptr<
        SLE const> const& sle, Mods& mods,
            beast::Journal j)
{
    switch(sle->getType())
    {
    case ltACCOUNT_ROOT:
    {
        // Nothing to do
        break;
    }
    case ltESCROW:
    {
        threadTx (base, meta, (*sle)[sfAccount], mods, j);
        threadTx (base, meta, (*sle)[sfDestination], mods, j);
        break;
    }
    case ltPAYCHAN:
        {
            threadTx (base, meta, (*sle)[sfAccount], mods, j);
            threadTx (base, meta, (*sle)[sfDestination], mods, j);
            break;
        }
    case ltRIPPLE_STATE:
    {
        threadTx (base, meta, (*sle)[sfLowLimit].getIssuer(), mods, j);
        threadTx (base, meta, (*sle)[sfHighLimit].getIssuer(), mods, j);
        break;
    }
    default:
    {
        // If sfAccount is present, thread to that account
        if ((*sle)[~sfAccount])
            threadTx (base, meta, (*sle)[sfAccount], mods, j);
        break;
    }
    }
}

} // detail
} // ripple
