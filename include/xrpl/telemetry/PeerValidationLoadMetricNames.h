#pragma once

/**
 * Metric name, description, label keys and label values of the
 * `peer_validation_load` gauge.
 *
 * Every label value is one of the constants below, so the gauge exports the
 * same 10 series per node whatever the peer count: five `metric` values for
 * each of the two `trust` values. No label value comes from a peer.
 *
 * @code
 *   PeerValidationLoadMetricNames.h
 *          |
 *          +--> xrpld/telemetry/PeerValidationLoad.h  (creates the gauge and
 *                                                      observes its points)
 * @endcode
 *
 * Example usage -- creating the gauge:
 * @code
 * auto const gauge =
 *     meter.CreateDoubleObservableGauge(metric::kPeerValidationLoad, kPeerValidationLoadDesc);
 * @endcode
 *
 * Example usage -- the series one point becomes, as a dashboard queries it:
 * @code
 *   peer_validation_load{metric="top_share", trust="untrusted"}
 * @endcode
 *
 * Example usage -- edge case: the busiest peer's id is runtime data, so it has
 * no constant here and never becomes a label. It goes to a throttled warning
 * line instead:
 * @code
 * JLOG(journal.warn()) << formatValidationLoadWarning(warning, publicKey);
 * @endcode
 *
 * Only the instrument name sits in `namespace metric`; the description and the
 * label constants stay outside it.
 *
 * @note These are `constexpr char[]`, not `std::string_view`: the OTel API
 * takes its own `nostd::string_view`, which converts from `char const*` but
 * not from `std::string_view`. Same convention as GetObjectMetricNames.h.
 * @note Header-only constants with no state, so safe to use from any thread.
 */

namespace xrpl::telemetry {

// ===== Metric name ===========================================================

namespace metric {

/**
 * Observable gauge: the validation rates of the busiest peers and the busiest
 * peer's share, split by the `metric` and `trust` labels.
 */
inline constexpr char kPeerValidationLoad[] = "peer_validation_load";

}  // namespace metric

/**
 * Description of metric::kPeerValidationLoad.
 */
inline constexpr char kPeerValidationLoadDesc[] =
    "Validations per second from the busiest peers, and the busiest peer's share, by signer trust";

// ===== Label keys ============================================================

/**
 * Label key naming which value of the gauge a point carries.
 */
inline constexpr char kLabelMetric[] = "metric";

/**
 * Label key naming whether the validations counted come from trusted signers.
 */
inline constexpr char kLabelTrust[] = "trust";

// ===== Label values ==========================================================

/**
 * `kLabelTrust` value: validations whose signer is on this node's UNL.
 */
inline constexpr char kTrustTrusted[] = "trusted";

/**
 * `kLabelTrust` value: validations from every other signer.
 */
inline constexpr char kTrustUntrusted[] = "untrusted";

/**
 * `kLabelMetric` value: validations per second from the busiest peer.
 */
inline constexpr char kLoadTop1Rate[] = "top1_rate";

/**
 * `kLabelMetric` value: validations per second from the second busiest peer.
 */
inline constexpr char kLoadTop2Rate[] = "top2_rate";

/**
 * `kLabelMetric` value: validations per second from the third busiest peer.
 */
inline constexpr char kLoadTop3Rate[] = "top3_rate";

/**
 * `kLabelMetric` value: the busiest peer's share of all validations of that
 * trust class, from 0 to 1.
 */
inline constexpr char kLoadTopShare[] = "top_share";

/**
 * `kLabelMetric` value: how many peers sent more validations per second than
 * the limit.
 */
inline constexpr char kLoadPeersOverLimit[] = "peers_over_limit";

}  // namespace xrpl::telemetry
