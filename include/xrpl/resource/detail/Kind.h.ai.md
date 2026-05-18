# `include/xrpl/resource/detail/Kind.h`

## Role in the Resource Management System

This file defines the foundational `Kind` enum used throughout the `xrpl::Resource` subsystem to classify every network connection that the XRPL node tracks for resource-consumption purposes. It is a pure vocabulary header — three enumeration values, no logic — but the distinction it encodes drives the entire policy layer of how the node manages load and enforces rate limits.

## The Three Consumer Kinds

`Kind` partitions all tracked endpoints into three behaviorally distinct categories:

**`kindInbound`** represents a connection initiated by a remote peer toward this node. In `Logic.h`, inbound entries are keyed with the port number stripped (`address.at_port(0)`), because many connections from the same IP on different ephemeral ports represent the same logical peer. These consumers have full resource accounting and can be warned, penalized, or disconnected when their load balance exceeds configured thresholds.

**`kindOutbound`** represents a connection this node initiated to a remote peer. The full address including port is preserved in the key (`address` as-is), since this node itself chose and controls the destination. Outbound peers are also subject to resource limits, but their billing semantics differ from inbound ones because the node trusts them to some degree by virtue of having explicitly connected.

**`kindUnlimited`** represents a specially privileged inbound connection — typically an administrative or trusted local client — that is exempt from normal resource-consumption limits. The key uses port 1 (`address.at_port(1)`) to separate it from ordinary inbound entries for the same address. While load metering is bypassed, `Entry::isUnlimited()` (in `Entry.h`) reads this flag and the comment is careful to note that administrative RPC restrictions (such as the `stop` command) may still apply based on `Role`, not `Kind`. This separation of concerns is intentional: resource throttling and command authorization are distinct policy axes.

## Why a Plain `enum` Rather Than `enum class`

The values are used as unscoped identifiers throughout the detail layer (e.g., `kindInbound`, `kindOutbound`, `kindUnlimited` appear directly in `Logic.h` switch cases and constructor arguments without qualification). An unscoped `enum` keeps the call sites concise inside the `Resource` namespace where these names are always unambiguous.

## Integration Points

`Kind` is a member of `Key` (defined in `Key.h`), which pairs it with a `beast::IP::Endpoint` to form the composite lookup key for the consumer table. The `Key::key_equal` comparator checks both fields, meaning the same IP address registered as `kindInbound` and `kindUnlimited` produces two separate, independent table entries — the correct behavior because they represent different trust relationships despite sharing an address. In `Logic.h`, each of the three `Kind` values maps to a dedicated `beast::intrusive_list` (`inbound_`, `outbound_`, `admin_`), enabling the sweeper to age out entries using the appropriate list without a runtime type check on the full entry.