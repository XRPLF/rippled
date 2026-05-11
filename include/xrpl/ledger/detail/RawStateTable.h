#pragma once

#include <xrpl/ledger/RawView.h>
#include <xrpl/ledger/ReadView.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>

#include <map>
#include <utility>

namespace xrpl::detail {

// Helper class that buffers raw modifications
class RawStateTable
{
public:
    using key_type = ReadView::key_type;
    // Initial size for the monotonic_buffer_resource used for allocations
    // The size was chosen from the old `qalloc` code (which this replaces).
    // It is unclear how the size initially chosen in qalloc.
    static constexpr size_t kInitialBufferSize = kilobytes(256);

    RawStateTable()
        : monotonic_resource_{std::make_unique<boost::container::pmr::monotonic_buffer_resource>(
              kInitialBufferSize)}
        , items_{monotonic_resource_.get()} {};

    RawStateTable(RawStateTable const& rhs)
        : monotonic_resource_{std::make_unique<boost::container::pmr::monotonic_buffer_resource>(
              kInitialBufferSize)}
        , items_{rhs.items_, monotonic_resource_.get()}
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

    [[nodiscard]] std::optional<key_type>
    succ(ReadView const& base, key_type const& key, std::optional<key_type> const& last) const;

    void
    erase(std::shared_ptr<SLE> const& sle);

    void
    insert(std::shared_ptr<SLE> const& sle);

    void
    replace(std::shared_ptr<SLE> const& sle);

    [[nodiscard]] std::shared_ptr<SLE const>
    read(ReadView const& base, Keylet const& k) const;

    void
    destroyXRP(XRPAmount const& fee);

    [[nodiscard]] std::unique_ptr<ReadView::SlesType::iter_base>
    slesBegin(ReadView const& base) const;

    [[nodiscard]] std::unique_ptr<ReadView::SlesType::iter_base>
    slesEnd(ReadView const& base) const;

    [[nodiscard]] std::unique_ptr<ReadView::SlesType::iter_base>
    slesUpperBound(ReadView const& base, uint256 const& key) const;

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
        std::shared_ptr<SLE> sle;

        // Constructor needed for emplacement in std::map
        SleAction(Action action, std::shared_ptr<SLE> const& sle) : action(action), sle(sle)
        {
        }
    };

    // Use boost::pmr functionality instead of the std::pmr
    // functions b/c clang does not support pmr yet (as-of 9/2020)
    using items_t = std::map<
        key_type,
        SleAction,
        std::less<key_type>,
        boost::container::pmr::polymorphic_allocator<std::pair<key_type const, SleAction>>>;
    // monotonic_resource_ must outlive `items_`. Make a pointer so it may be
    // easily moved.
    std::unique_ptr<boost::container::pmr::monotonic_buffer_resource> monotonic_resource_;
    items_t items_;

    XRPAmount dropsDestroyed_{0};
};

}  // namespace xrpl::detail
