# Deployment tiers — filtering telemetry by environment, network, and node

Multiple xrpld instances can send telemetry to per-tier collectors that all
forward to one Grafana stack. Four resource attributes segregate the data so
one dashboard set serves every deployment:

| Dimension   | Attribute                | Set by     | Example values                 |
| ----------- | ------------------------ | ---------- | ------------------------------ |
| Node        | `service.instance.id`    | xrpld cfg  | `alice-laptop`, `ci-runner-7`  |
| Service     | `service.name`           | xrpld cfg  | `xrpld`, `pratik-xrpld`        |
| Network     | `xrpl.network.type`      | xrpld node | `mainnet`, `testnet`, `devnet` |
| Environment | `deployment.environment` | collector  | `local`, `test`, `ci`, `prod`  |

The dashboards expose these as the template variables `$node`,
`$service_name`, `$xrpl_network_type`, and `$deployment_environment`
(each variable name matches its Prometheus label).

## Who owns which attribute

- **Node and service** come from the xrpld config (`[insight]` /
  `[telemetry]` `service_instance_id` and `service_name`). They are unique
  per process.
- **Network** is a property of the chain the node joined. The node derives
  it from `[network_id]` and stamps `xrpl.network.type` on all three signals.
- **Environment** is a property of where the collector runs. Each collector
  serves one environment and stamps it.

## The upsert vs insert rule

The collector's `resource/tier` processor uses two different actions on
purpose:

- `deployment.environment` → **`upsert`** (overwrite). The collector _is_ the
  environment, so it is authoritative.
- `xrpl.network.type` → **`insert`** (fill only if absent). The node already
  knows its real network, so the collector must not overwrite it. `insert`
  only supplies a value when the source did not — e.g. an older xrpld build
  whose metrics predate node-side network stamping. This is what makes a
  local node connected to mainnet report `network=mainnet`, not the
  collector's default.

## Configuring a collector for a tier

Each tier runs its own collector. Edit the `resource/tier` processor in the
collector config (`otel-collector-config.yaml` for local backends, or
`otel-collector-config.grafanacloud.yaml` for Grafana Cloud export) and set
the two values for that tier:

```yaml
processors:
  resource/tier:
    attributes:
      - key: deployment.environment
        value: <tier> # local | test | ci | prod
        action: upsert
      - key: xrpl.network.type
        value: <network> # mainnet | testnet | devnet (fallback only)
        action: insert
```

Suggested values per tier:

| Collector           | `deployment.environment` | `xrpl.network.type` (fallback) |
| ------------------- | ------------------------ | ------------------------------ |
| Developer laptop    | `local`                  | `devnet`                       |
| Test machines       | `test`                   | `testnet`                      |
| CI runs             | `ci`                     | `testnet`                      |
| Production observer | `prod`                   | `mainnet`                      |

The `xrpl.network.type` value is only a fallback: when the xrpld node stamps
its own network (all current builds do), the node's value wins. Set it to the
network the collector most commonly serves so older or misconfigured sources
still land under a sensible label.

## How the labels reach metrics

Resource attributes do not become Prometheus labels automatically. Two config
settings make it work, both already enabled:

- `prometheus.resource_to_telemetry_conversion: enabled: true` promotes
  resource attributes to metric labels on the local scrape surface.
- `spanmetrics.resource_metrics_key_attributes` lists the tier attributes so
  span-derived metric series stay grouped per node and tier.

Traces and logs carry resource attributes natively, so no extra setting is
needed for them. Grafana Cloud ingests all three signals' attributes over
OTLP directly.

## Filtering in Grafana

Pick the dashboard template variables top-down: `$deployment_environment` →
`$xrpl_network_type` → `$service_name` → `$node`. Selecting **All** on any
variable matches every value, including series that lack the label, so mixed
old/new data never disappears.
