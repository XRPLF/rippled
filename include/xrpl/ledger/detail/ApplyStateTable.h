#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>

#include <memory>
#ifndef NDEBUG
#include <map>
#endif

namespace xrpl::detail {

// Helper class that buffers modifications
class ApplyStateTable
{
public:
    using key_type = ReadView::key_type;

private:
    enum class Action {
        Cache,
        Erase,
        Insert,
        Modify,
    };

    using items_t = std::map<key_type, std::pair<Action, std::shared_ptr<SLE>>>;

    items_t items_;
    XRPAmount dropsDestroyed_{0};

public:
    ApplyStateTable() = default;
    ApplyStateTable(ApplyStateTable&&) = default;

    ApplyStateTable(ApplyStateTable const&) = delete;
    ApplyStateTable&
    operator=(ApplyStateTable&&) = delete;
    ApplyStateTable&
    operator=(ApplyStateTable const&) = delete;

    void
    apply(RawView& to) const;

    std::optional<TxMeta>
    apply(
        OpenView& to,
        STTx const& tx,
        TER ter,
        std::optional<STAmount> const& deliver,
        std::optional<uint256 const> const& parentBatchId,
        bool isDryRun,
        beast::Journal j);

    [[nodiscard]] bool
    exists(ReadView const& base, Keylet const& k) const;

    [[nodiscard]] std::optional<key_type>
    succ(ReadView const& base, key_type const& key, std::optional<key_type> const& last) const;

    [[nodiscard]] std::shared_ptr<SLE const>
    read(ReadView const& base, Keylet const& k) const;

    std::shared_ptr<SLE>
    peek(ReadView const& base, Keylet const& k);

    [[nodiscard]] std::size_t
    size() const;

    void
    visit(
        ReadView const& base,
        std::function<void(
            uint256 const& key,
            bool isDelete,
            std::shared_ptr<SLE const> const& before,
            std::shared_ptr<SLE const> const& after)> const& func) const;

    void
    erase(ReadView const& base, std::shared_ptr<SLE> const& sle);

    void
    rawErase(ReadView const& base, std::shared_ptr<SLE> const& sle);

    void
    insert(ReadView const& base, std::shared_ptr<SLE> const& sle);

    void
    update(ReadView const& base, std::shared_ptr<SLE> const& sle);

    void
    replace(ReadView const& base, std::shared_ptr<SLE> const& sle);

    void
    destroyXRP(XRPAmount const& fee);

    // For debugging
    [[nodiscard]] XRPAmount const&
    dropsDestroyed() const
    {
        return dropsDestroyed_;
    }

#ifndef NDEBUG
    /** Every ledger entry this table has read or written, mapped to its type.

        Populated in DEBUG builds by the access methods below (reads via
        read/exists/peek and writes via insert/update/replace/erase). Directory
        iteration via succ() is deliberately NOT recorded — see AccessSet for
        why directory entries are out of scope for conflict tracking. Used by
        the parallel-apply access-set assertion to verify a transactor's
        declared footprint is a superset of what it actually touched.
    */
    [[nodiscard]] std::map<key_type, LedgerEntryType> const&
    touchedEntries() const
    {
        return touched_;
    }
#endif

private:
#ifndef NDEBUG
    void
    recordTouch(key_type const& key, LedgerEntryType type) const
    {
        // Prefer ltDIR_NODE on collision so directory pages stay identifiable
        // regardless of access order; non-dir objects are never accessed via a
        // directory keylet, so this never mislabels a real object.
        auto const [it, inserted] = touched_.try_emplace(key, type);
        if (!inserted && type == ltDIR_NODE)
            it->second = ltDIR_NODE;
    }

    mutable std::map<key_type, LedgerEntryType> touched_;
#endif

    using Mods = hash_map<key_type, std::shared_ptr<SLE>>;

    static void
    threadItem(TxMeta& meta, std::shared_ptr<SLE> const& to);

    std::shared_ptr<SLE>
    getForMod(ReadView const& base, key_type const& key, Mods& mods, beast::Journal j);

    void
    threadTx(ReadView const& base, TxMeta& meta, AccountID const& to, Mods& mods, beast::Journal j);

    void
    threadOwners(
        ReadView const& base,
        TxMeta& meta,
        std::shared_ptr<SLE const> const& sle,
        Mods& mods,
        beast::Journal j);
};

}  // namespace xrpl::detail
