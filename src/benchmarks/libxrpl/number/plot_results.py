#!/usr/bin/env python3
"""Render the figures in docs/NumberDecimalBenchmark.md from benchmark JSON.

Usage:
    plot_results.py CORE_JSON KERNELS_JSON OUT_DIR

CORE_JSON is Google Benchmark JSON from xrpl.bench.number filtered to
'^(add|mul|div)/full/'; KERNELS_JSON from '^kernel/' with mpdecimal enabled
(for the accuracy counters). Both need --benchmark_repetitions so medians are
available. Writes self-contained SVG files with light and dark variants
(prefers-color-scheme) and no dependencies beyond the standard library.
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable
from xml.sax.saxutils import escape

# Categorical slots 1-3 of the reference palette (validated all-pairs, light
# and dark). Color encodes the type family; every bar is also labeled.
FAMILIES = {
    "number": ("Number today (19 digits)", "#2a78d6", "#3987e5"),
    "ieee64": ("16-digit types", "#eb6834", "#d95926"),
    "wide": ("34- and 38-digit types", "#1baf7a", "#199e70"),
}

# (benchmark subject, row label, family)
CORE_ROWS = [
    ("Number.Large330", "Number (Large330)", "number"),
    ("BoostDecimal64", "Boost decimal64", "ieee64"),
    ("BoostDecimalFast64", "Boost decimal_fast64", "ieee64"),
    ("IntelBid64", "Intel BID64", "ieee64"),
    ("BoostDecimal128", "Boost decimal128", "wide"),
    ("BoostDecimalFast128", "Boost decimal_fast128", "wide"),
    ("IntelBid128", "Intel BID128", "wide"),
    ("MpDecimal34", "mpdecimal 34", "wide"),
    ("MpDecimal38", "mpdecimal 38", "wide"),
]

KERNEL_ROWS = [
    ("Number.Large330", "Number (Large330)", "number"),
    ("BoostDecimal64", "Boost decimal64", "ieee64"),
    ("IntelBid64", "Intel BID64", "ieee64"),
    ("BoostDecimal128", "Boost decimal128", "wide"),
    ("IntelBid128", "Intel BID128", "wide"),
    ("MpDecimal34", "mpdecimal 34", "wide"),
    ("MpDecimal38", "mpdecimal 38", "wide"),
]

FONT = "system-ui, -apple-system, sans-serif"
ROW_H = 24
BAR_H = 12
LABEL_W = 150
PANEL_W = 200
PANEL_GAP = 28
TOP = 78
BOTTOM = 30


@dataclass
class Panel:
    title: str
    unit: str
    values: dict[str, float]
    fmt: Callable[[float], str]
    # Panels with the same group share an x scale; use it only for panels
    # with the same unit, so bar lengths are comparable.
    group: str = ""


def medians(path: str) -> dict[str, dict]:
    """Map run_name to its median aggregate."""
    data = json.loads(Path(path).read_text())
    return {
        b["run_name"]: b
        for b in data["benchmarks"]
        if b.get("aggregate_name") == "median"
    }


def ns_per_item(b: dict) -> float:
    return 1e9 / b["items_per_second"]


def nice_max(v: float) -> tuple[float, list[float]]:
    """Axis maximum and ticks: 4-5 round steps covering v."""
    for step in [
        1,
        2,
        2.5,
        5,
        10,
        20,
        25,
        50,
        100,
        200,
        250,
        500,
        1000,
        2000,
        2500,
        5000,
        10000,
        20000,
        25000,
        50000,
    ]:
        if v / step <= 5:
            top = step * -(-v // step)
            return top, [step * i for i in range(int(top / step) + 1)]
    return v, [0, v]


def bar_path(x: float, y: float, w: float, h: float, r: float = 4) -> str:
    """Bar anchored at the baseline (left) with a rounded data end (right)."""
    r = min(r, w, h / 2)
    return (
        f"M{x:.1f},{y:.1f} H{x + w - r:.1f} "
        f"Q{x + w:.1f},{y:.1f} {x + w:.1f},{y + r:.1f} "
        f"V{y + h - r:.1f} Q{x + w:.1f},{y + h:.1f} {x + w - r:.1f},{y + h:.1f} "
        f"H{x:.1f} Z"
    )


def render(
    title: str, rows: list[tuple[str, str, str]], panels: list[Panel], out: Path
) -> None:
    """Horizontal bar panels sharing one column of row labels."""
    width = LABEL_W + len(panels) * (PANEL_W + PANEL_GAP)
    height = TOP + len(rows) * ROW_H + BOTTOM
    css_light = "".join(
        f".f-{k}{{fill:{light}}}" for k, (_, light, _) in FAMILIES.items()
    )
    css_dark = "".join(f".f-{k}{{fill:{dark}}}" for k, (_, _, dark) in FAMILIES.items())
    out_lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
        f'width="{width}" height="{height}" role="img" '
        f'aria-label="{escape(title)}">',
        "<style>",
        f"text{{font-family:{FONT};font-size:12px}}",
        ".bg{fill:#fcfcfb}.t1{fill:#0b0b0b}.t2{fill:#52514e}"
        ".grid{stroke:#e4e3df;stroke-width:1}.base{stroke:#b9b8b2;stroke-width:1}"
        + css_light,
        "@media (prefers-color-scheme: dark){"
        ".bg{fill:#1a1a19}.t1{fill:#ffffff}.t2{fill:#c3c2b7}"
        ".grid{stroke:#383835}.base{stroke:#5e5d58}" + css_dark + "}",
        "</style>",
        f'<rect class="bg" width="{width}" height="{height}"/>',
        f'<text class="t1" x="0" y="18" font-size="15" font-weight="600">'
        f"{escape(title)}</text>",
    ]
    # Legend: always present for more than one family.
    lx = 0.0
    for key, (label, _, _) in FAMILIES.items():
        out_lines.append(
            f'<rect class="f-{key}" x="{lx}" y="32" width="12" height="12" rx="3"/>'
        )
        out_lines.append(
            f'<text class="t2" x="{lx + 18}" y="42">{escape(label)}</text>'
        )
        lx += 18 + 7.2 * len(label) + 24

    for i, (_, label, _) in enumerate(rows):
        y = TOP + i * ROW_H + ROW_H / 2 + 4
        out_lines.append(f'<text class="t1" x="0" y="{y:.1f}">{escape(label)}</text>')

    for p_index, panel in enumerate(panels):
        x0 = LABEL_W + p_index * (PANEL_W + PANEL_GAP)
        vmax = max(
            p.values.get(s, 0)
            for p in panels
            if p is panel or (panel.group and p.group == panel.group)
            for s, _, _ in rows
        )
        top, ticks = nice_max(vmax * 1.08)
        scale = (PANEL_W - 40) / top
        out_lines.append(
            f'<text class="t1" x="{x0}" y="{TOP - 14}" font-weight="600">'
            f"{escape(panel.title)}</text>"
        )
        y_end = TOP + len(rows) * ROW_H
        for t in ticks:
            gx = x0 + t * scale
            out_lines.append(
                f'<line class="grid" x1="{gx:.1f}" y1="{TOP}" x2="{gx:.1f}" y2="{y_end}"/>'
            )
            out_lines.append(
                f'<text class="t2" x="{gx:.1f}" y="{y_end + 16}" text-anchor="middle" '
                f'font-size="11">{t:g}</text>'
            )
        out_lines.append(
            f'<text class="t2" x="{x0 + PANEL_W - 40:.1f}" y="{y_end + 28}" '
            f'text-anchor="end" font-size="11">{escape(panel.unit)}</text>'
        )
        for i, (subject, label, family) in enumerate(rows):
            if subject not in panel.values:
                continue
            v = panel.values[subject]
            y = TOP + i * ROW_H + (ROW_H - BAR_H) / 2
            w = max(v * scale, 1.0)
            out_lines.append(
                f'<path class="f-{family}" d="{bar_path(x0, y, w, BAR_H)}">'
                f"<title>{escape(label)}: {panel.fmt(v)} {escape(panel.unit)}</title></path>"
            )
            out_lines.append(
                f'<text class="t2" x="{x0 + w + 5:.1f}" y="{y + BAR_H - 2:.1f}" '
                f'font-size="11">{panel.fmt(v)}</text>'
            )
        out_lines.append(
            f'<line class="base" x1="{x0}" y1="{TOP}" x2="{x0}" y2="{y_end}"/>'
        )
    out_lines.append("</svg>")
    out.write_text("\n".join(out_lines) + "\n")


def fmt_ns(v: float) -> str:
    return f"{v:.0f}" if v >= 10 else f"{v:.1f}"


def fmt_us(v: float) -> str:
    return f"{v:.0f}" if v >= 10 else f"{v:.1f}"


def fmt_digits(v: float) -> str:
    return f"{v:.1f}"


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    core = medians(argv[1])
    kernels = medians(argv[2])
    out_dir = Path(argv[3])
    out_dir.mkdir(parents=True, exist_ok=True)

    core_panels = [
        Panel(
            op_title,
            "ns per operation",
            {
                s: ns_per_item(core[f"{op}/full/{s}"])
                for s, _, _ in CORE_ROWS
                if f"{op}/full/{s}" in core
            },
            fmt_ns,
            group="ns",
        )
        for op, op_title in [("add", "Add"), ("mul", "Multiply"), ("div", "Divide")]
    ]
    render(
        "Cost of one operation (full-width operands, lower is faster)",
        CORE_ROWS,
        core_panels,
        out_dir / "number-core-ops.svg",
    )

    def kernel(name: str) -> dict[str, dict]:
        prefix = f"kernel/{name}/"
        return {k[len(prefix) :]: v for k, v in kernels.items() if k.startswith(prefix)}

    for name, title, unit, divisor, fmt in [
        ("amm_swap_in", "AMM swap-in", "ns per call", 1.0, fmt_ns),
        (
            "loan_amortize360",
            "360-payment amortization",
            "µs per schedule",
            1e3,
            fmt_us,
        ),
    ]:
        k = kernel(name)
        render(
            f"{title}: correct digits (higher is better) and cost (lower is faster)",
            KERNEL_ROWS,
            [
                Panel(
                    "Correct digits, worst case",
                    "significant digits",
                    {s: b["digits_min"] for s, b in k.items()},
                    fmt_digits,
                    group="digits",
                ),
                Panel(
                    "Correct digits, median",
                    "significant digits",
                    {s: b["digits_median"] for s, b in k.items()},
                    fmt_digits,
                    group="digits",
                ),
                Panel(
                    "Cost",
                    unit,
                    {s: ns_per_item(b) / divisor for s, b in k.items()},
                    fmt,
                ),
            ],
            out_dir / f"number-{name.replace('_', '-')}.svg",
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
