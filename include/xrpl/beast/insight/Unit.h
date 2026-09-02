#pragma once

#include <cstdint>

namespace beast::insight {

/**
 * @brief What an Event's samples measure.
 *
 * `Event` carries "a millisecond time, or other integral value", so the unit
 * cannot be inferred from the sample. Naming it at creation time is what lets
 * the OTel bridge pick both the instrument unit and the matching bucket
 * ladder:
 *
 *     makeEvent("time", Unit::Millis) --> OTel unit "ms" --> millisecond ladder
 *     makeEvent("size", Unit::Bytes)  --> OTel unit "By" --> byte ladder
 *
 * Without an explicit unit every instrument declares `ms`, so a size metric
 * exports under a `_milliseconds` name and inherits a latency bucket ladder.
 * For RPC response sizes that ladder censors about a quarter of the samples
 * and pins the p95 to a constant.
 *
 * The StatsD backend deliberately ignores this and emits `|ms` for every
 * Event. That path is out of service -- its UDP port is commented out of the
 * compose file and the integration test fails if anything is listening on
 * 8125 -- so changing its wire format would alter an external protocol
 * contract for no local benefit and with no way to verify it.
 *
 * @note Adding a member requires extending otelUnitCode(), which switches
 *       exhaustively so a new member is a compile error rather than a silent
 *       fallthrough to milliseconds.
 */
enum class Unit : std::uint8_t {
    /**
     * Whole milliseconds. The default, and what every duration Event uses.
     */
    Millis,

    /**
     * A byte count, such as a serialized response size.
     */
    Bytes
};

/**
 * @brief The OTel (UCUM) unit code for a Unit.
 *
 * The collector's Prometheus exporter derives the exported metric-name suffix
 * from this code, so `ms` yields `_milliseconds` and `By` yields `_bytes`. It
 * is also the key the histogram views match on, which is how each unit gets
 * its own bucket ladder.
 *
 * @param unit The unit to translate.
 * @return A static, null-terminated UCUM code.
 */
constexpr char const*
otelUnitCode(Unit unit) noexcept
{
    switch (unit)
    {
        case Unit::Bytes:
            return "By";
        case Unit::Millis:
            break;
    }
    return "ms";
}

/**
 * @brief Human-readable description for an instrument of this unit.
 *
 * Exported alongside the metric, so this is the text an operator reads in a
 * metric catalogue. A byte-valued instrument that describes itself as a
 * duration is exactly the confusion this whole type exists to remove, so the
 * description is derived from the unit rather than written out at each
 * instrument site.
 *
 * @param unit The unit to describe.
 * @return A static, null-terminated description.
 */
constexpr char const*
otelUnitDescription(Unit unit) noexcept
{
    switch (unit)
    {
        case Unit::Bytes:
            return "Size in bytes";
        case Unit::Millis:
            break;
    }
    return "Duration in ms";
}

}  // namespace beast::insight
