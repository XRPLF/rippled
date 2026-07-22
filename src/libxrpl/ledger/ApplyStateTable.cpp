#include <xrpl/ledger/detail/ApplyStateTable.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace xrpl::detail {

void
ApplyStateTable::apply(RawView& to) const
{
    to.rawDestroyXRP(dropsDestroyed_);
    for (auto const& item : items_)
    {
        auto const& sle = item.second.second;
        switch (item.second.first)
        {
            case Action::Cache:
                break;
            case Action::Erase:
                to.rawErase(sle);
                break;
            case Action::Insert:
                to.rawInsert(sle);
                break;
            case Action::Modify:
                to.rawReplace(sle);
                break;
        };
    }
}

std::size_t
ApplyStateTable::size() const
{
    std::size_t ret = 0;
    for (auto& item : items_)
    {
        switch (item.second.first)
        {
            case Action::Erase:
            case Action::Insert:
            case Action::Modify:
                ++ret;
            default:
                break;
        }
    }
    return ret;
}

void
ApplyStateTable::visit(
    ReadView const& to,
    std::function<
        void(uint256 const& key, bool isDelete, SLE::const_ref before, SLE::const_ref after)> const&
        func) const
{
    for (auto& item : items_)
    {
        switch (item.second.first)
        {
            case Action::Erase:
                func(item.first, true, to.read(keylet::unchecked(item.first)), item.second.second);
                break;

            case Action::Insert:
                func(item.first, false, nullptr, item.second.second);
                break;

            case Action::Modify:
                func(item.first, false, to.read(keylet::unchecked(item.first)), item.second.second);
                break;

            default:
                break;
        }
    }
}

std::optional<TxMeta>
ApplyStateTable::apply(
    OpenView& to,
    STTx const& tx,
    TER ter,
    std::optional<STAmount> const& deliver,
    std::optional<uint256 const> const& parentBatchId,
    bool isDryRun,
    beast::Journal j)
{
    // Build metadata and insert
    auto const sTx = std::make_shared<Serializer>();
    tx.add(*sTx);
    std::shared_ptr<Serializer> sMeta;
    std::optional<TxMeta> metadata;
    if (!to.open() || isDryRun)
    {
        TxMeta meta(tx.getTransactionID(), to.seq());

        meta.setDeliveredAmount(deliver);
        meta.setParentBatchID(parentBatchId);

        Mods newMod;
        for (auto& item : items_)
        {
            SField const* type = nullptr;
            switch (item.second.first)
            {
                default:
                case Action::Cache:
                    continue;
                case Action::Erase:
                    type = &sfDeletedNode;
                    break;
                case Action::Insert:
                    type = &sfCreatedNode;
                    break;
                case Action::Modify:
                    type = &sfModifiedNode;
                    break;
            }
            auto const origNode = to.read(keylet::unchecked(item.first));
            auto curNode = item.second.second;
            if ((type == &sfModifiedNode) && (*curNode == *origNode))
                continue;
            std::uint16_t const nodeType = curNode ? curNode->getFieldU16(sfLedgerEntryType)
                                                   : origNode->getFieldU16(sfLedgerEntryType);
            meta.setAffectedNode(item.first, *type, nodeType);
            if (type == &sfDeletedNode)
            {
                XRPL_ASSERT(
                    origNode && curNode,
                    "xrpl::detail::ApplyStateTable::apply : valid nodes for "
                    "deletion");
                threadOwners(to, meta, origNode, newMod, j);

                STObject prevs(sfPreviousFields);
                for (auto const& obj : *origNode)
                {
                    // go through the original node for
                    // modified  fields saved on modification
                    if (obj.getFName().shouldMeta(SField::kSmdChangeOrig) &&
                        !curNode->hasMatchingEntry(obj))
                        prevs.emplaceBack(obj);
                }

                if (!prevs.empty())
                    meta.getAffectedNode(item.first).emplaceBack(std::move(prevs));

                STObject finals(sfFinalFields);
                for (auto const& obj : *curNode)
                {
                    // go through the final node for final fields
                    if (obj.getFName().shouldMeta(SField::kSmdAlways | SField::kSmdDeleteFinal))
                        finals.emplaceBack(obj);
                }

                if (!finals.empty())
                    meta.getAffectedNode(item.first).emplaceBack(std::move(finals));
            }
            else if (type == &sfModifiedNode)
            {
                XRPL_ASSERT(
                    curNode && origNode,
                    "xrpl::detail::ApplyStateTable::apply : valid nodes for "
                    "modification");

                if (curNode->isThreadedType(to.rules()))
                {  // thread transaction to node
                   // item modified
                    threadItem(meta, curNode);
                }

                STObject prevs(sfPreviousFields);
                for (auto const& obj : *origNode)
                {
                    // search the original node for values saved on modify
                    if (obj.getFName().shouldMeta(SField::kSmdChangeOrig) &&
                        !curNode->hasMatchingEntry(obj))
                        prevs.emplaceBack(obj);
                }

                if (!prevs.empty())
                    meta.getAffectedNode(item.first).emplaceBack(std::move(prevs));

                STObject finals(sfFinalFields);
                for (auto const& obj : *curNode)
                {
                    // search the final node for values saved always
                    if (obj.getFName().shouldMeta(SField::kSmdAlways | SField::kSmdChangeNew))
                        finals.emplaceBack(obj);
                }

                if (!finals.empty())
                    meta.getAffectedNode(item.first).emplaceBack(std::move(finals));
            }
            else if (type == &sfCreatedNode)  // if created, thread to owner(s)
            {
                XRPL_ASSERT(
                    curNode && !origNode,
                    "xrpl::detail::ApplyStateTable::apply : valid nodes for "
                    "creation");
                threadOwners(to, meta, curNode, newMod, j);

                if (curNode->isThreadedType(to.rules()))  // always thread to self
                    threadItem(meta, curNode);

                STObject news(sfNewFields);
                for (auto const& obj : *curNode)
                {
                    // save non-default values
                    if (!obj.isDefault() &&
                        obj.getFName().shouldMeta(SField::kSmdCreate | SField::kSmdAlways))
                        news.emplaceBack(obj);
                }

                if (!news.empty())
                    meta.getAffectedNode(item.first).emplaceBack(std::move(news));
            }
            else
            {
                // LCOV_EXCL_START
                UNREACHABLE(
                    "xrpl::detail::ApplyStateTable::apply : unsupported "
                    "operation type");
                // LCOV_EXCL_STOP
            }
        }

        if (!isDryRun)
        {
            // add any new modified nodes to the modification set
            for (auto const& mod : newMod)
                to.rawReplace(mod.second);
        }

        sMeta = std::make_shared<Serializer>();
        meta.addRaw(*sMeta, ter, to.txCount());

        // VFALCO For diagnostics do we want to show
        //        metadata even when the base view is open?
        JLOG(j.trace()) << "metadata " << meta.getJson(JsonOptions::Values::None);

        metadata = meta;
    }

    if (!isDryRun)
    {
        to.rawTxInsert(tx.getTransactionID(), sTx, sMeta);
        apply(to);
    }
    return metadata;
}

//---

bool
ApplyStateTable::exists(ReadView const& base, Keylet const& k) const
{
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.exists(k);
    auto const& item = iter->second;
    auto const& sle = item.second;
    switch (item.first)
    {
        case Action::Erase:
            return false;
        case Action::Cache:
        case Action::Insert:
        case Action::Modify:
            break;
    }
    return k.check(*sle);
}

auto
ApplyStateTable::succ(
    ReadView const& base,
    key_type const& key,
    std::optional<key_type> const& last) const -> std::optional<key_type>
{
    std::optional<key_type> next = key;
    items_t::const_iterator iter;
    // Find base successor that is
    // not also deleted in our list
    do
    {
        next = base.succ(*next, last);
        if (!next)
            break;
        iter = items_.find(*next);
    } while (iter != items_.end() && iter->second.first == Action::Erase);
    // Find non-deleted successor in our list
    for (iter = items_.upper_bound(key); iter != items_.end(); ++iter)
    {
        if (iter->second.first != Action::Erase)
        {
            // Found both, return the lower key
            if (!next || next > iter->first)
                next = iter->first;
            break;
        }
    }
    // Nothing in our list, return
    // what we got from the parent.
    if (last && next >= last)
        return std::nullopt;
    return next;
}

SLE::const_pointer
ApplyStateTable::read(ReadView const& base, Keylet const& k) const
{
    auto const iter = items_.find(k.key);
    if (iter == items_.end())
        return base.read(k);
    auto const& item = iter->second;
    auto const& sle = item.second;
    switch (item.first)
    {
        case Action::Erase:
            return nullptr;
        case Action::Cache:
        case Action::Insert:
        case Action::Modify:
            break;
    };
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

SLE::pointer
ApplyStateTable::peek(ReadView const& base, Keylet const& k)
{
    auto iter = items_.lower_bound(k.key);
    if (iter == items_.end() || iter->first != k.key)
    {
        auto const sle = base.read(k);
        if (!sle)
            return nullptr;
        // Make our own copy
        using namespace std;
        iter = items_.emplace_hint(
            iter,
            piecewise_construct,
            forward_as_tuple(sle->key()),
            forward_as_tuple(Action::Cache, make_shared<SLE>(*sle)));
        return iter->second.second;
    }
    auto const& item = iter->second;
    auto const& sle = item.second;
    switch (item.first)
    {
        case Action::Erase:
            return nullptr;
        case Action::Cache:
        case Action::Insert:
        case Action::Modify:
            break;
    };
    if (!k.check(*sle))
        return nullptr;
    return sle;
}

void
ApplyStateTable::erase(ReadView const& base, SLE::ref sle)
{
    auto const iter = items_.find(sle->key());
    if (iter == items_.end())
        Throw<std::logic_error>("ApplyStateTable::erase: missing key");
    auto& item = iter->second;
    if (item.second != sle)
        Throw<std::logic_error>("ApplyStateTable::erase: unknown SLE");
    switch (item.first)
    {
        case Action::Erase:
            Throw<std::logic_error>("ApplyStateTable::erase: double erase");
            break;
        case Action::Insert:
            items_.erase(iter);
            break;
        case Action::Cache:
        case Action::Modify:
            item.first = Action::Erase;
            break;
    }
}

void
ApplyStateTable::rawErase(ReadView const& base, SLE::ref sle)
{
    using namespace std;
    auto const result = items_.emplace(
        piecewise_construct, forward_as_tuple(sle->key()), forward_as_tuple(Action::Erase, sle));
    if (result.second)
        return;
    auto& item = result.first->second;
    switch (item.first)
    {
        case Action::Erase:
            Throw<std::logic_error>("ApplyStateTable::rawErase: double erase");
            break;
        case Action::Insert:
            items_.erase(result.first);
            break;
        case Action::Cache:
        case Action::Modify:
            item.first = Action::Erase;
            item.second = sle;
            break;
    }
}

void
ApplyStateTable::insert(ReadView const& base, SLE::ref sle)
{
    auto const iter = items_.lower_bound(sle->key());
    if (iter == items_.end() || iter->first != sle->key())
    {
        using namespace std;
        items_.emplace_hint(
            iter,
            piecewise_construct,
            forward_as_tuple(sle->key()),
            forward_as_tuple(Action::Insert, sle));
        return;
    }
    auto& item = iter->second;
    switch (item.first)
    {
        case Action::Cache:
            Throw<std::logic_error>("ApplyStateTable::insert: already cached");
        case Action::Insert:
            Throw<std::logic_error>("ApplyStateTable::insert: already inserted");
        case Action::Modify:
            Throw<std::logic_error>("ApplyStateTable::insert: already modified");
        case Action::Erase:
            break;
    }
    item.first = Action::Modify;
    item.second = sle;
}

void
ApplyStateTable::replace(ReadView const& base, SLE::ref sle)
{
    auto const iter = items_.lower_bound(sle->key());
    if (iter == items_.end() || iter->first != sle->key())
    {
        using namespace std;
        items_.emplace_hint(
            iter,
            piecewise_construct,
            forward_as_tuple(sle->key()),
            forward_as_tuple(Action::Modify, sle));
        return;
    }
    auto& item = iter->second;
    switch (item.first)
    {
        case Action::Erase:
            Throw<std::logic_error>("ApplyStateTable::replace: already erased");
        case Action::Cache:
            item.first = Action::Modify;
            break;
        case Action::Insert:
        case Action::Modify:
            break;
    }
    item.second = sle;
}

void
ApplyStateTable::update(ReadView const& base, SLE::ref sle)
{
    auto const iter = items_.find(sle->key());
    if (iter == items_.end())
        Throw<std::logic_error>("ApplyStateTable::update: missing key");
    auto& item = iter->second;
    if (item.second != sle)
        Throw<std::logic_error>("ApplyStateTable::update: unknown SLE");
    switch (item.first)
    {
        case Action::Erase:
            Throw<std::logic_error>("ApplyStateTable::update: erased");
            break;
        case Action::Cache:
            item.first = Action::Modify;
            break;
        case Action::Insert:
        case Action::Modify:
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
ApplyStateTable::threadItem(TxMeta& meta, SLE::ref sle)
{
    key_type prevTxID;
    LedgerIndex prevLgrID = 0;

    if (!sle->thread(meta.getTxID(), meta.getLgrSeq(), prevTxID, prevLgrID))
        return;

    if (!prevTxID.isZero())
    {
        auto& node = meta.getAffectedNode(sle, sfModifiedNode);

        if (node.getFieldIndex(sfPreviousTxnID) == -1)
        {
            XRPL_ASSERT(
                node.getFieldIndex(sfPreviousTxnLgrSeq) == -1,
                "xrpl::ApplyStateTable::threadItem : previous ledger is not "
                "set");
            node.setFieldH256(sfPreviousTxnID, prevTxID);
            node.setFieldU32(sfPreviousTxnLgrSeq, prevLgrID);
        }

        XRPL_ASSERT(
            node.getFieldH256(sfPreviousTxnID) == prevTxID,
            "xrpl::ApplyStateTable::threadItem : previous transaction is a "
            "match");
        XRPL_ASSERT(
            node.getFieldU32(sfPreviousTxnLgrSeq) == prevLgrID,
            "xrpl::ApplyStateTable::threadItem : previous ledger is a match");
    }
}

SLE::pointer
ApplyStateTable::getForMod(ReadView const& base, key_type const& key, Mods& mods, beast::Journal j)
{
    {
        auto miter = mods.find(key);
        if (miter != mods.end())
        {
            XRPL_ASSERT(miter->second, "xrpl::ApplyStateTable::getForMod : non-null result");
            return miter->second;
        }
    }
    {
        auto iter = items_.find(key);
        if (iter != items_.end())
        {
            auto const& item = iter->second;
            if (item.first == Action::Erase)
            {
                // The Destination of an Escrow or a PayChannel may have been
                // deleted.  In that case the account we're threading to will
                // not be found and it is appropriate to return a nullptr.
                JLOG(j.warn()) << "Trying to thread to deleted node";
                return nullptr;
            }
            if (item.first != Action::Cache)
                return item.second;

            // If it's only cached, then the node is being modified only by
            // metadata; fall through and track it in the mods table.
        }
    }
    auto c = base.read(keylet::unchecked(key));
    if (!c)
    {
        // The Destination of an Escrow or a PayChannel may have been
        // deleted.  In that case the account we're threading to will
        // not be found and it is appropriate to return a nullptr.
        JLOG(j.warn()) << "ApplyStateTable::getForMod: key not found";
        return nullptr;
    }
    auto sle = std::make_shared<SLE>(*c);
    mods.emplace(key, sle);
    return sle;
}

void
ApplyStateTable::threadTx(
    ReadView const& base,
    TxMeta& meta,
    AccountID const& to,
    Mods& mods,
    beast::Journal j)
{
    auto const sle = getForMod(base, keylet::account(to).key, mods, j);
    if (!sle)
    {
        // The Destination of an Escrow or PayChannel may have been deleted.
        // In that case the account we are threading to will not be found.
        // So this logging is just a warning.
        JLOG(j.warn()) << "Threading to non-existent account: " << toBase58(to);
        return;
    }
    // threadItem only applied to AccountRoot
    XRPL_ASSERT(
        sle->isThreadedType(base.rules()), "xrpl::ApplyStateTable::threadTx : SLE is threaded");
    threadItem(meta, sle);
}

void
ApplyStateTable::threadOwners(
    ReadView const& base,
    TxMeta& meta,
    SLE::const_ref sle,
    Mods& mods,
    beast::Journal j)
{
    LedgerEntryType const ledgerType{sle->getType()};
    switch (ledgerType)
    {
        case ltACCOUNT_ROOT: {
            // Nothing to do
            break;
        }
        case ltRIPPLE_STATE: {
            threadTx(base, meta, (*sle)[sfLowLimit].getIssuer(), mods, j);
            threadTx(base, meta, (*sle)[sfHighLimit].getIssuer(), mods, j);
            break;
        }
        default: {
            // If sfAccount is present, thread to that account
            if (auto const optSleAcct{(*sle)[~sfAccount]})
                threadTx(base, meta, *optSleAcct, mods, j);

            // If sfDestination is present, thread to that account
            if (auto const optSleDest{(*sle)[~sfDestination]})
                threadTx(base, meta, *optSleDest, mods, j);
        }
    }
}

}  // namespace xrpl::detail
