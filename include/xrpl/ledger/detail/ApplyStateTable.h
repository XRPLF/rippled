#pragma once

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxMeta.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <utility>

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

    using ItemsT = std::map<key_type, std::pair<Action, SLE::pointer>>;

    ItemsT items_;
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
        std::optional<UInt256 const> const& parentBatchId,
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
            UInt256 const& key,
            bool isDelete,
            SLE::ConstRef before,
            SLE::ConstRef after)> const& func) const;

    void
    erase(ReadView const& base, SLE::Ref sle);

    void
    rawErase(ReadView const& base, SLE::Ref sle);

    void
    insert(ReadView const& base, SLE::Ref sle);

    void
    update(ReadView const& base, SLE::Ref sle);

    void
    replace(ReadView const& base, SLE::Ref sle);

    void
    destroyXRP(XRPAmount const& fee);

    // For debugging
    [[nodiscard]] XRPAmount const&
    dropsDestroyed() const
    {
        return dropsDestroyed_;
    }

private:
    using Mods = HashMap<key_type, SLE::pointer>;

    static void
    threadItem(TxMeta& meta, SLE::Ref to);

    SLE::pointer
    getForMod(ReadView const& base, key_type const& key, Mods& mods, beast::Journal j);

    void
    threadTx(ReadView const& base, TxMeta& meta, AccountID const& to, Mods& mods, beast::Journal j);

    void
    threadOwners(
        ReadView const& base,
        TxMeta& meta,
        SLE::ConstRef sle,
        Mods& mods,
        beast::Journal j);
};

}  // namespace xrpl::detail
