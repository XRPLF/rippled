# Securing OpenTelemetry Against Trace Context Spoofing

> **Part of**: [OpenTelemetry Implementation Plan](./OpenTelemetryPlan.md) — see also [Design Decisions § Privacy](./02-design-decisions.md#244-privacy--sensitive-data-policy) (what we don't collect) and [Configuration Reference § 5.5](./05-configuration-reference.md#55-opentelemetry-collector-configuration) (collector base config).

Trace context spoofing (or poisoning) occurs when untrusted actors inject tampered or stale trace IDs into your system. If these requests are processed, the spans are appended to historical trace buckets, stretching trace durations, ruining p99 latency metrics, and breaking Grafana dashboards.

This guide outlines two categories of defense: mitigating tampered contexts and locking down the OpenTelemetry (OTel) Collector to trusted clients only.

---

## Part 1: Mitigating Tampered Trace Contexts

### 1. Perimeter Defense: Strip Headers at the API Gateway

The most effective way to prevent spoofing from external sources is to treat your API Gateway (Envoy, NGINX, AWS ALB) as a hard boundary. Strip incoming W3C tracing headers (`traceparent`, `tracestate`) from public traffic so the gateway is forced to generate a fresh, legitimate `trace_id`.

**NGINX Example (Stripping Headers):**

```nginx
server {
    listen 80;

    location / {
        # Clear out untrusted incoming trace headers
        proxy_set_header traceparent "";
        proxy_set_header tracestate "";

        proxy_pass http://backend_service;
    }
}
```

### **2. Timestamp-Anchored Trace IDs and OTTL Filtering**

If you use a custom trace ID generator that embeds a timestamp in the first few bytes (like AWS X-Ray or UUIDv7), you can use the OTel Collector's OpenTelemetry Transform Language (OTTL) to detect anomalies.
**Collector Configuration (Conceptual OTTL Filter):**

```yaml
processors:
  filter/stale_traces:
    error_mode: ignore
    traces:
      span:
        # Example: Drop spans where the start time is significantly different
        # from an expected parameter or embedded timestamp logic.
        # Note: Standard W3C trace IDs do not contain timestamps by default.
        - 'Keep out-of-bounds spans: time.sub(start_time, now()) > duration("1h")'
```

## **Part 2: Restricting Access to the OTel Collector**

Locking down the Collector ensures that only authenticated, trusted clients can submit telemetry data.

### **Approach A: Network Layer Security (Kubernetes Network Policies)**

Ensure your Collector is not exposed to the public internet. If running in Kubernetes, use a NetworkPolicy to restrict ingress traffic to specific namespaces.
**Kubernetes NetworkPolicy Example:**

```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: allow-internal-otel
  namespace: observability
spec:
  podSelector:
    matchLabels:
      app: opentelemetry-collector
  policyTypes:
    - Ingress
  ingress:
    - from:
        - namespaceSelector:
            matchLabels:
              environment: production
      ports:
        - protocol: TCP
          port: 4317 # gRPC
        - protocol: TCP
          port: 4318 # HTTP
```

### **Approach B: Transport Layer Security (Mutual TLS / mTLS)**

Require clients to present a valid cryptographic certificate to connect to the Collector.
**Collector Configuration (mTLS):**

```yaml
receivers:
  otlp:
    protocols:
      grpc:
        endpoint: 0.0.0.0:4317
        tls:
          # Setting client_ca_file makes the collector require and verify a
          # client cert, rejecting connections without a trusted one.
          client_ca_file: /certs/client_ca.pem # CA that signs trusted client certs
          cert_file: /certs/collector.pem
          key_file: /certs/collector.key
```

### **Approach C: Application Layer Authentication (Basic Auth Extension)**

Use the Collector's extension system to require an API key or Basic Auth credentials.
**Collector Configuration (Basic Auth):**

```yaml
extensions:
  basicauth/collector:
    htpasswd:
      inline: |
        # username:trusted-client, password:SecurePassword123
        trusted-client:$apr1$4v8p76o6$DMTX5Wv6uOmrFAZp2X1N1.

receivers:
  otlp:
    protocols:
      grpc:
        endpoint: 0.0.0.0:4317
        auth:
          authenticator: basicauth/collector

processors:
  batch:

exporters:
  otlp:
    endpoint: my-backend-storage:4317

service:
  extensions: [basicauth/collector]
  pipelines:
    traces:
      receivers: [otlp]
      processors: [batch]
      exporters: [otlp]
```

**Client Setup (Environment Variables):**
Developers must pass the authentication header using the standard OTel SDK environment variables:

```bash
# Base64 encoded "trusted-client:SecurePassword123"
export OTEL_EXPORTER_OTLP_HEADERS="Authorization=Basic dHJ1c3RlZC1jbGllbnQ6U2VjdXJlUGFzc3dvcmQxMjM="
```

---

Available routes to build on top of: https://github.com/XRPLF/rippled/pull/6425#discussion_r3234751995

---

# Analysis: Applying the Guide to xrpld

The guide above is written for HTTP-fronted web services. xrpld is a P2P node daemon, so the threat model and the applicable defenses differ. This section captures how each approach maps to xrpld and the chosen direction.

## Threat Model

xrpld has **two distinct attack surfaces**, not one. The original guide conflates them under "trace context spoofing"; for xrpld they need separate defenses.

| Surface                                   | Attacker                                                 | Vector                                                                                                                                                                                     | Defense                                       |
| ----------------------------------------- | -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------- |
| **Collector ingress** (xrpld → collector) | Anyone who can reach `4317`/`4318` on the collector host | Forged OTLP traffic, telemetry exfiltration, DoS on collector                                                                                                                              | mTLS + network policy                         |
| **Peer trace context** (peer → xrpld)     | Malicious peer in the XRPL overlay                       | Crafted `protocol::TraceContext` field inside peer protobuf messages (TMTransaction, consensus, etc.) — used to forge `trace_id`/`span_id`, pollute p99, attach spans to historical traces | Validate + rate-limit at the receive boundary |

**Deployment context:** Across-network. xrpld nodes (potentially run by external operators or in different DCs) ship telemetry to a centrally-hosted collector across an untrusted network. The collector is NOT on the same host or private VPC as every node.

```
                ┌── peer (untrusted) ── TMTransaction{trace_context} ──▶ xrpld
                │                                                            │
                │                                              [validate + rate-limit]
                │                                                            │
                │                                                            ▼
                │                                                     SpanGuard (clean)
                │                                                            │
                │                                                            │ OTLP/gRPC
                │                                                            │ + mTLS
                │                                                            ▼
                └─────────────────────────────────────────  [client_ca_file: verify client cert]
                                                                  OTel Collector
                                                              (in private subnet, NetPol)
```

## Part 1 Applicability — Peer Trace-Context Validation

The guide's NGINX header stripping and OTTL stale-span filtering target HTTP gateways and post-hoc cleanup. Neither fits xrpld directly:

- **NGINX header stripping** — N/A. There is no HTTP gateway between peers and xrpld; trace context arrives inside protobuf peer messages (`protocol::TraceContext`), not as W3C `traceparent` headers. See [src/xrpld/telemetry/PropagationHelpers.h](../src/xrpld/telemetry/PropagationHelpers.h).
- **OTTL stale-span filtering** — Weak fit. Post-hoc cleanup at the collector loses peer identity (you can't tell _which_ peer poisoned the trace). Validation at the receive site is stronger.

**xrpld-specific Part 1 mitigations:**

1. **Validate extracted context at the boundary** in [src/xrpld/telemetry/ConsensusReceiveTracing.h](../src/xrpld/telemetry/ConsensusReceiveTracing.h) and any other peer-message receive site. Reject if `trace_id` is all-zero, wrong length, or fails W3C format checks. Treat invalid context as "no propagated context" — start a fresh span — rather than dropping the message.
2. **Per-peer sample rate limiting** so a hostile peer cannot flood the collector with spans bearing a fabricated `trace_id`. Use probabilistic sampling on the receive path keyed by peer identity.

## Part 2 — Comparison of Collector Hardening Approaches

Evaluated for the across-network deployment shape:

| Approach                        | Across-network fit                                                                                                                                                                                                                                                                                                | Cost                                                                 | Verdict                            |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- | ---------------------------------- |
| **A. NetworkPolicy / firewall** | Necessary baseline (don't expose `4317`/`4318` to the internet), but insufficient on its own when traffic genuinely crosses networks — you cannot NetworkPolicy the public internet.                                                                                                                              | Cheap.                                                               | **Defense-in-depth, not primary.** |
| **B. mTLS**                     | Strongest fit. Every xrpld node holds a client cert; the collector verifies it via `client_ca_file` in the receiver's `tls` block. Encrypts in transit (raw OTLP over the internet leaks transaction patterns and validator identity). Compromised node = revoke one cert, no shared secret to rotate everywhere. | Cert issuance + rotation pipeline.                                   | **Primary.**                       |
| **C. Basic Auth**               | Worst shape for this topology. Single shared password across all xrpld nodes — one leaked node config compromises the whole fleet. Doesn't encrypt; you'd need TLS underneath anyway, at which point you're 80% of the way to mTLS.                                                                               | Cheap to set up, expensive to operate (rotation across N operators). | **Skip.**                          |

## Decision

**Primary defense:** mTLS (Approach B) on the collector's OTLP receivers. The collector requires and verifies each client certificate when `client_ca_file` is set in the receiver's `tls` block (there is no `auth_type` field — setting `client_ca_file` is what enforces client-cert verification).

**Defense-in-depth:** NetworkPolicy / firewall rules (Approach A) so `4317`/`4318` are never reachable from outside the expected operator subnets even if mTLS were misconfigured.

**Skipped:** Basic Auth (Approach C) — wrong shape for an across-network, multi-operator topology.

**Plus xrpld-specific Part 1 work:** trace-context validation and per-peer rate limiting at peer-message receive sites.

## Decisions Made

| Decision             | Choice                           | Rationale                                                                                                                                                                                                                                                     |
| -------------------- | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Cert source for mTLS | **Reuse XRPL node identity key** | One identity per node, no separate PKI to operate. Fits XRPL's existing trust model; requires small CA tooling step to derive/sign the OTel client cert from the node key.                                                                                    |
| Part 1 scope         | **Include in this spec**         | Collector hardening and peer trace-context validation share one threat model. Coherent design doc; can still be split into multiple PRs at implementation.                                                                                                    |
| Dev impact           | **Production-only**              | Local `docker/telemetry/docker-compose.yml` keeps `insecure: true` and no auth for fast iteration. Only production deployment manifests gain mTLS. Accepted risk: minor dev/prod drift, mitigated by integration tests against a TLS-enabled collector in CI. |

## Out of Scope

- NGINX/Envoy header stripping (no HTTP gateway in front of xrpld-to-collector traffic).
- OTTL stale-span filtering at the collector (weaker than source validation; loses peer identity).
- Local development docker-compose hardening.
- Telemetry backend (Tempo) hardening — separate concern, downstream of the collector.

## Next Step

Write this up as a design doc with full sections covering:

1. Threat model & architecture (this section, expanded)
2. Collector hardening — mTLS config, NetworkPolicy
3. Cert pipeline — deriving OTel client cert from XRPL node key
4. Peer trace-context validation — receive-site checks in `ConsensusReceiveTracing.h`
5. Per-peer span rate limiting
6. Testing & rollout
