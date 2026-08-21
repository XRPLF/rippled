#!/usr/bin/env python3
"""Assert the C++ millisecond ladder agrees with the collector's spanmetrics ladder.

The two are specified to match so a span-derived latency panel and a native
histogram panel can be read on the same scale. They *were* identical when first
shipped. Then the collector ladder alone was extended -- sub-millisecond edges
below 1ms and second-scale edges up to 30s -- and nothing checked the other
side, so the C++ ladder stayed capped at 5s. Every quantile above 5s then read
back as a flat 5000, because Prometheus returns the second-highest edge for a
quantile landing in the `+Inf` bucket. That looks like a measurement rather
than an error, which is why it survived for eleven phases.

The rule is containment, not equality:

  * every representable collector edge MUST appear in the C++ ladder, so the
    shared range reads identically;
  * the C++ ladder MAY carry extra edges ABOVE the collector's highest edge,
    because jobs outlive spans -- the updatepaths job type was measured
    averaging ~60s, which no span approaches. Demanding equality would force a
    ceiling that censors it, reintroducing the bug this guards against;
  * collector edges below 1ms are expected to be ABSENT rather than missing:
    beast::insight::Event rounds every duration up to a whole millisecond
    before it reaches the histogram, so those edges could never collect a
    sample.

Exit 0 when the ladders agree, 1 with a diff when they do not.
"""

import re
import sys
from pathlib import Path

HEADER = Path("include/xrpl/telemetry/HistogramBuckets.h")
COLLECTOR = Path("docker/telemetry/otel-collector-config.yaml")

# beast::insight::Event applies ceil<milliseconds>, so anything below 1ms
# collapses onto the 1ms edge.
REPRESENTABLE_FLOOR_MS = 1.0

UNIT_TO_MS = {"ms": 1.0, "s": 1000.0}


def collector_edges_ms():
    """Parse the spanmetrics bucket list, normalising each edge to milliseconds."""
    text = COLLECTOR.read_text()
    match = re.search(r"buckets:\s*\[(.*?)\]", text, re.S)
    if not match:
        sys.exit(f"{COLLECTOR}: no 'buckets:' list found")

    edges = []
    for raw in match.group(1).split(","):
        token = raw.strip()
        if not token:
            continue
        parsed = re.fullmatch(r"([0-9.]+)(ms|s)", token)
        if not parsed:
            sys.exit(f"{COLLECTOR}: cannot parse bucket edge {token!r}")
        edges.append(float(parsed.group(1)) * UNIT_TO_MS[parsed.group(2)])
    return edges


def cpp_edges_ms():
    """Parse kMillisecondBuckets out of the header that owns every ladder."""
    text = HEADER.read_text()
    match = re.search(r"kMillisecondBuckets\{(.*?)\};", text, re.S)
    if not match:
        sys.exit(f"{HEADER}: kMillisecondBuckets not found")
    return [
        float(token.strip().replace("'", ""))
        for token in match.group(1).split(",")
        if token.strip()
    ]


def main():
    collector = collector_edges_ms()
    cpp = cpp_edges_ms()
    required = [edge for edge in collector if edge >= REPRESENTABLE_FLOOR_MS]
    if not required:
        sys.exit(f"{COLLECTOR}: no edges at or above {REPRESENTABLE_FLOOR_MS} ms")
    collector_top = max(required)

    missing = [edge for edge in required if edge not in cpp]
    # An extra C++ edge inside the collector's range means the two scales
    # disagree where they overlap. Above the collector's top it is a deliberate
    # extension.
    inside_range = [e for e in cpp if e not in required and e < collector_top]

    if not missing and not inside_range:
        extensions = [e for e in cpp if e > collector_top]
        summary = f"OK: all {len(required)} representable collector edges present"
        if extensions:
            pretty = ", ".join(f"{e:g}" for e in extensions)
            summary += (
                f"; {len(extensions)} extension edge(s) above "
                f"{collector_top:g} ms: [{pretty}]"
            )
        print(summary)
        return 0

    print("Bucket ladder parity violated.", file=sys.stderr)
    print(
        f"  collector (>= {REPRESENTABLE_FLOOR_MS:g} ms): "
        f"{[f'{e:g}' for e in required]}",
        file=sys.stderr,
    )
    print(
        f"  HistogramBuckets.h            : {[f'{e:g}' for e in cpp]}", file=sys.stderr
    )
    for edge in missing:
        print(f"  MISSING from the C++ ladder: {edge:g} ms", file=sys.stderr)
    for edge in inside_range:
        print(
            f"  C++ edge {edge:g} ms lies inside the collector's range but is not "
            "a collector edge -- add it to the collector or drop it here",
            file=sys.stderr,
        )
    print(
        "\nThe two ladders must agree over their shared range. Extra C++ edges are\n"
        "permitted only ABOVE the collector's highest edge. Change both sides, or\n"
        "change the spec in OpenTelemetryPlan/Phase7_taskList.md.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
