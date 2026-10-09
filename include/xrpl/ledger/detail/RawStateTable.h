#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl::detail {

// Helper class that buffers raw modifications
class RawStateTable
{
public:
    using Key = ReadView::Key;
    // Initial size for the monotonic_buffer_resource used for allocations
    // The size was chosen from the old `qalloc` code (which this replaces).
    // It is unclear how the size initially chosen in qalloc.
    static constexpr size_t kInitialBufferSize = kilobytes(256);

    RawStateTable()
        : monotonicResource_{std::make_unique<boost::container::pmr::monotonic_buffer_resource>(
              kInitialBufferSize)}
        , items_{monotonicResource_.get()} {};

    RawStateTable(RawStateTable const& rhs)
        : monotonicResource_{std::make_unique<boost::container::pmr::monotonic_buffer_resource>(
              kInitialBufferSize)}
        , items_{rhs.items_, monotonicResource_.get()}
        , dropsDestroyed_{rhs.dropsDestroyed_} {};

    RawStateTable(RawStateTable&&) = default;

    RawStateTable&
    operator=(RawStateTable&&) = delete;
    RawStateTable&
    operator=(RawStateTable const&) = delete;

    void
    apply(RawView& to) const;

    [[nodiscard]] bool
    exists(ReadView const& base, Keylet const& k) const;

    [[nodiscard]] std::optional<Key>
    succ(ReadView const& base, Key const& key, std::optional<Key> const& last) const;

    void
    erase(SLE::Ref sle);

    void
    insert(SLE::Ref sle);

    void
    replace(SLE::Ref sle);

    [[nodiscard]] SLE::ConstPointer
    read(ReadView const& base, Keylet const& k) const;

    void
    destroyXRP(XRPAmount const& fee);

    [[nodiscard]] std::unique_ptr<ReadView::SlesType::IterBase>
    slesBegin(ReadView const& base) const;

    [[nodiscard]] std::unique_ptr<ReadView::SlesType::IterBase>
    slesEnd(ReadView const& base) const;

    [[nodiscard]] std::unique_ptr<ReadView::SlesType::IterBase>
    slesUpperBound(ReadView const& base, UInt256 const& key) const;

private:
    enum class Action {
        Erase,
        Insert,
        Replace,
    };

    class SlesIterImpl;

    struct SleAction
    {
        Action action;
        SLE::Pointer sle;

        // Constructor needed for emplacement in std::map
        SleAction(Action action, SLE::Pointer sle) : action(action), sle(std::move(sle))
        {
        }
    };

    // Use boost::pmr functionality instead of the std::pmr
    // functions b/c clang does not support pmr yet (as-of 9/2020)
    using ItemsT = std::map<
        Key,
        SleAction,
        std::less<>,
        boost::container::pmr::polymorphic_allocator<std::pair<Key const, SleAction>>>;
    // monotonic_resource_ must outlive `items_`. Make a pointer so it may be
    // easily moved.
    std::unique_ptr<boost::container::pmr::monotonic_buffer_resource> monotonicResource_;
    ItemsT items_;

    XRPAmount dropsDestroyed_{0};
};

}  // namespace xrpl::detail
