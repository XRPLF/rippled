#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>

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

    using items_t = std::map<key_type, std::pair<Action, SLE::pointer>>;

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

    [[nodiscard]] SLE::const_pointer
    read(ReadView const& base, Keylet const& k) const;

    SLE::pointer
    peek(ReadView const& base, Keylet const& k);

    [[nodiscard]] std::size_t
    size() const;

    void
    visit(
        ReadView const& base,
        std::function<void(
            uint256 const& key,
            bool isDelete,
            SLE::const_ref before,
            SLE::const_ref after)> const& func) const;

    void
    erase(ReadView const& base, SLE::ref sle);

    void
    rawErase(ReadView const& base, SLE::ref sle);

    void
    insert(ReadView const& base, SLE::ref sle);

    void
    update(ReadView const& base, SLE::ref sle);

    void
    replace(ReadView const& base, SLE::ref sle);

    void
    destroyXRP(XRPAmount const& fee);

    // For debugging
    [[nodiscard]] XRPAmount const&
    dropsDestroyed() const
    {
        return dropsDestroyed_;
    }

private:
    using Mods = hash_map<key_type, SLE::pointer>;

    static void
    threadItem(TxMeta& meta, SLE::ref to);

    SLE::pointer
    getForMod(ReadView const& base, key_type const& key, Mods& mods, beast::Journal j);

    void
    threadTx(ReadView const& base, TxMeta& meta, AccountID const& to, Mods& mods, beast::Journal j);

    void
    threadOwners(
        ReadView const& base,
        TxMeta& meta,
        SLE::const_ref sle,
        Mods& mods,
        beast::Journal j);
};

}  // namespace xrpl::detail
