#pragma once

#include <cstdint>

namespace xrpl::node_store {

/**
 * A snapshot of a backend's write-path behaviour.
 *
 * A backend that serializes its writes behind an internal lock cannot
 * report lock wait time directly, because the lock is private to it. This
 * type reports what an outside caller can measure - how many writers were
 * in flight and how long each write took - from which the queuing time
 * follows.
 *
 * With mean depth L, mean insert time W and arrival rate lambda, Little's
 * Law gives service time S = W / L and queuing time W - S. When S times
 * lambda approaches 1.0 the backend is consuming a whole core-equivalent
 * inside its critical section, which is the signature of a serialized
 * write path.
 *
 *     caller ---+
 *     caller ---+--> [ backend lock ] --> disk
 *     caller ---+
 *               |
 *        concurrentWriters = queue length here
 *        insertTotalUs / insertCount = time in the whole system (W)
 *        depthSum / depthSamples     = mean depth (L)
 *
 * @note All fields except @ref concurrentWriters are cumulative for the
 *       life of the backend, so a reader must difference successive
 *       samples to get a rate. @ref concurrentWriters is instantaneous.
 * @note Sampled without a lock, so fields may be a few operations out of
 *       step with each other. They are diagnostics, not accounting.
 *
 * Example - mean insert time and mean depth:
 * @code
 * if (auto const s = backend->getWriteStats(); s && s->insertCount)
 * {
 *     double const meanUs = double(s->insertTotalUs) / s->insertCount;
 *     double const meanDepth = double(s->depthSum) / s->depthSamples;
 *     double const serviceUs = meanUs / meanDepth;  // Little's Law
 * }
 * @endcode
 *
 * Example - edge case, an idle backend:
 * @code
 * // insertCount is 0, so every derived mean is undefined. Guard on it
 * // rather than publishing a division by zero.
 * @endcode
 */
struct WriteStats
{
    /**
     * Writers inside the backend store call right now.
     */
    std::uint64_t concurrentWriters = 0;

    /**
     * Total completed inserts.
     */
    std::uint64_t insertCount = 0;

    /**
     * Summed wall time of all inserts, in microseconds.
     */
    std::uint64_t insertTotalUs = 0;

    /**
     * Longest single insert seen, in microseconds. A true maximum.
     */
    std::uint64_t insertMaxUs = 0;

    /**
     * Summed writer depth observed at each insert. Divided by
     * @ref insertCount this gives mean depth.
     */
    std::uint64_t depthSum = 0;

    /**
     * Number of depth samples summed into @ref depthSum.
     *
     * Divide @ref depthSum by this, not by @ref insertCount: a sample is
     * taken when an insert starts, while insertCount only rises when one
     * finishes, so the two populations differ while inserts are in flight.
     */
    std::uint64_t depthSamples = 0;
};

}  // namespace xrpl::node_store
