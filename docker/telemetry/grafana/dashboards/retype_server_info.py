#!/usr/bin/env python3
"""Change the render type of four Server Info tiles on node-health, and append
the user's live-authored ledger-seq panels. Throwaway helper (not committed).

Transforms (type + query/options only; gridPos kept):
  - Build Version : stat -> piechart, slices = count by (version, xrpl_branch)
  - Server State  : stat -> piechart, slices = node count per state (keep 0-4
                    value/color mappings)
  - Peer Count    : stat -> bargauge (horizontal), one bar per node
  - Uptime        : stat -> bargauge (horizontal), one bar per node

The user added three panels directly on Grafana Cloud (a row + two ledger-seq
stat panels); those are re-appended verbatim from /tmp/nh_added_panels.json so a
repo write does not drop them.
"""

import json
import sys

FILTERS = (
    'service_instance_id=~"$node", deployment_environment=~"$deployment_environment", '
    'xrpl_network_type=~"$xrpl_network_type", service_name=~"$service_name", '
    'xrpl_work_item=~"$xrpl_work_item", xrpl_branch=~"$xrpl_branch", '
    'xrpl_node_role=~"$xrpl_node_role"'
)

ADDED_PANELS = "/tmp/nh_added_panels.json"


def find(panels, title):
    for p in panels:
        if p.get("title") == title:
            return p
        if "panels" in p:
            r = find(p["panels"], title)
            if r:
                return r
    return None


def to_piechart(p, expr, legend):
    """Convert a panel to a piechart with one target + legend."""
    p["type"] = "piechart"
    p["targets"] = [
        {
            "datasource": {"type": "prometheus", "uid": "${DS_PROMETHEUS}"},
            "expr": expr,
            "legendFormat": legend,
            "refId": "A",
        }
    ]
    p["options"] = {
        "legend": {"displayMode": "list", "placement": "right", "values": ["value"]},
        "pieType": "pie",
        "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False},
        "tooltip": {"mode": "single", "sort": "desc"},
    }
    # piechart uses field displayName for slice labels, not the xrpl_ident stat form
    p.setdefault("fieldConfig", {}).setdefault("defaults", {}).pop("displayName", None)


def to_bargauge(p, expr):
    """Convert a panel to a horizontal bar gauge, one bar per node."""
    p["type"] = "bargauge"
    for t in p["targets"]:
        t["expr"] = expr
        t["instant"] = False
        t["legendFormat"] = "{{service_instance_id}}"
    p["options"] = {
        "displayMode": "gradient",
        "orientation": "horizontal",
        "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False},
        "showUnfilled": True,
        "valueMode": "color",
    }
    # bar label is the per-node legend, not the stat xrpl_ident bracket
    p.setdefault("fieldConfig", {}).setdefault("defaults", {})[
        "displayName"
    ] = "{{service_instance_id}}"


def retype(dash):
    panels = dash["panels"]

    # 1) Build Version -> pie by (version, xrpl_branch)
    bv = find(panels, "Build Version")
    to_piechart(
        bv,
        "count by (version, xrpl_branch) (build_info{%s})" % FILTERS,
        "{{version}} {{xrpl_branch}}",
    )

    # 2) Server State -> pie of node count per state. server_state is a metric
    #    VALUE (0-4), not a label, so count_values buckets nodes by that value
    #    into a new "state" label; the nested label_replace chain then renames
    #    0-4 to DISCONNECTED..FULL so the pie slices read as state names. (Value
    #    mappings can't help here -- they map field values, not legend labels.)
    ss = find(panels, "Server State")
    state_expr = (
        'count_values("state", server_info{%s, metric="server_state"})' % FILTERS
    )
    for num, name in (
        ("0", "DISCONNECTED"),
        ("1", "CONNECTED"),
        ("2", "SYNCING"),
        ("3", "TRACKING"),
        ("4", "FULL"),
    ):
        state_expr = 'label_replace(%s, "state", "%s", "state", "%s")' % (
            state_expr,
            name,
            num,
        )
    to_piechart(ss, state_expr, "{{state}}")

    # 3) Peer Count -> bargauge per node
    pc = find(panels, "Peer Count")
    to_bargauge(pc, 'server_info{%s, metric="peers"}' % FILTERS)

    # 4) Uptime -> bargauge per node
    up = find(panels, "Uptime")
    to_bargauge(up, 'server_info{%s, metric="uptime"}' % FILTERS)

    # 5) Re-append the user's live-authored panels if not already present.
    have = {p.get("title") for p in panels}
    added = json.load(open(ADDED_PANELS))
    for p in added:
        if p.get("title") not in have:
            panels.append(p)

    return dash


# --- serializer matching the dashboards' on-disk byte format ---------------
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


def main():
    for path in sys.argv[1:]:
        d = json.load(open(path))
        retype(d)
        out = _dump(d) + "\n"
        json.loads(out)
        open(path, "w").write(out)
        print("%s: retyped 4 panels, +%d user panels" % (path, 3))


if __name__ == "__main__":
    main()
