#!/usr/bin/env python3
"""Fix three Server Info panels on node-health (legends + colors), matching the
conventions of the user's reference "Validated Ledger Seq — Current (Stat)"
panel. Throwaway helper (not committed).

Operates on a dashboard JSON that was pulled FRESH from Grafana Cloud, so the
user's live layout and their Validated Ledger Seq / Build Version panels are
preserved verbatim -- only these three panels are edited in place:

  - Server State : pie -> horizontal bargauge, one bar per state (node count),
    each bar coloured by state via value mappings (red/orange/yellow/blue/green).
  - Peer Count   : bargauge -> stat, value_and_name + per-node legend + gradient
    colour (clones the reference stat panel's options/color).
  - Uptime       : bargauge -> stat, same stat treatment, unit seconds.

Build Version and the Validated Ledger Seq * panels are intentionally left
untouched (the user owns those).
"""

import json
import sys

FILTERS = (
    'service_instance_id=~"$node", deployment_environment=~"$deployment_environment", '
    'xrpl_network_type=~"$xrpl_network_type", service_name=~"$service_name", '
    'xrpl_work_item=~"$xrpl_work_item", xrpl_branch=~"$xrpl_branch", '
    'xrpl_node_role=~"$xrpl_node_role"'
)

DS = {"type": "prometheus", "uid": "${DS_PROMETHEUS}"}

# state value -> (name, colour) for the Server State bars.
STATES = [
    ("0", "DISCONNECTED", "red"),
    ("1", "CONNECTED", "orange"),
    ("2", "SYNCING", "yellow"),
    ("3", "TRACKING", "blue"),
    ("4", "FULL", "green"),
]


def find(panels, title):
    for p in panels:
        if p.get("title") == title:
            return p
        if "panels" in p:
            r = find(p["panels"], title)
            if r:
                return r
    return None


def fix_server_state(p):
    """Pie -> bargauge, one bar per state (node count), state-coloured."""
    expr = 'count_values("state", server_info{%s, metric="server_state"})' % FILTERS
    for num, name, _ in STATES:
        expr = 'label_replace(%s, "state", "%s", "state", "%s")' % (expr, name, num)
    p["type"] = "bargauge"
    p["targets"] = [
        {
            "datasource": DS,
            "expr": expr,
            "instant": True,
            "legendFormat": "{{state}}",
            "refId": "A",
        }
    ]
    p["options"] = {
        "displayMode": "gradient",
        "orientation": "horizontal",
        "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False},
        "showUnfilled": True,
        "valueMode": "color",
    }
    # colour each bar by its state name (value mapping sets text colour).
    p["fieldConfig"] = {
        "defaults": {
            "color": {"mode": "fixed"},
            "displayName": "${__field.labels.state}",
            "mappings": [
                {"type": "value", "options": {name: {"index": i, "color": colour}}}
                for i, (_, name, colour) in enumerate(STATES)
            ],
            "unit": "short",
        },
        "overrides": [
            {
                "matcher": {"id": "byName", "options": name},
                "properties": [
                    {"id": "color", "value": {"mode": "fixed", "fixedColor": colour}}
                ],
            }
            for _, name, colour in STATES
        ],
    }


def to_reference_stat(p, expr, unit):
    """bargauge -> stat, cloning the reference 'Current (Stat)' panel: per-node
    legend, value_and_name text, continuous colour."""
    p["type"] = "stat"
    p["targets"] = [
        {
            "datasource": DS,
            "expr": expr,
            "instant": True,
            "legendFormat": "{{service_instance_id}}",
            "refId": "A",
        }
    ]
    p["options"] = {
        "colorMode": "value",
        "graphMode": "none",
        "justifyMode": "center",
        "orientation": "auto",
        "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False},
        "textMode": "value_and_name",
        "wideLayout": True,
    }
    p["fieldConfig"] = {
        "defaults": {
            "color": {"mode": "continuous-RdYlGr"},
            "thresholds": {
                "mode": "absolute",
                "steps": [{"color": "green", "value": 0}],
            },
            "unit": unit,
        },
        "overrides": [],
    }


def fix(dash):
    panels = dash["panels"]
    fix_server_state(find(panels, "Server State"))
    to_reference_stat(
        find(panels, "Peer Count"), 'server_info{%s, metric="peers"}' % FILTERS, "short"
    )
    to_reference_stat(
        find(panels, "Uptime"), 'server_info{%s, metric="uptime"}' % FILTERS, "s"
    )
    return dash


def _dump(obj, lvl=0):
    pad = "  " * lvl
    pad1 = "  " * (lvl + 1)
    if isinstance(obj, dict):
        if not obj:
            return "{}"
        return (
            "{\n"
            + ",\n".join(
                "%s%s: %s" % (pad1, json.dumps(k), _dump(v, lvl + 1))
                for k, v in obj.items()
            )
            + "\n"
            + pad
            + "}"
        )
    if isinstance(obj, list):
        if not obj:
            return "[]"
        if all(isinstance(x, (str, int, float, bool)) or x is None for x in obj):
            return "[" + ", ".join(json.dumps(x) for x in obj) + "]"
        return (
            "[\n"
            + ",\n".join("%s%s" % (pad1, _dump(x, lvl + 1)) for x in obj)
            + "\n"
            + pad
            + "]"
        )
    return json.dumps(obj)


# Runtime-only keys the Grafana API returns that must NOT be written to the
# repo file (they are instance state, not dashboard definition).
_RUNTIME_KEYS = ("id", "version", "liveNow")


def main():
    """argv: <live.json> <dest1.json> [dest2.json ...]

    The live pull is the source of truth for layout + the user's panels. For each
    dest we start from that dest's EXISTING top-level shape (to keep repo-only
    keys like ``links`` and avoid importing runtime keys), then replace only the
    ``panels`` array with the fixed live layout."""
    live = json.load(open(sys.argv[1]))
    fix(live)
    for dest in sys.argv[2:]:
        base = json.load(open(dest))  # keep dest's own top-level keys
        base["panels"] = live["panels"]  # adopt live layout + fixed panels
        for k in _RUNTIME_KEYS:
            base.pop(k, None)
        out = _dump(base) + "\n"
        json.loads(out)
        open(dest, "w").write(out)
        print("wrote", dest, "(%d top-level panels)" % len(base["panels"]))


if __name__ == "__main__":
    main()
