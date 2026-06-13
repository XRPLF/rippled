# CI image for the formal-verification jobs: a public xrpld CI image extended
# with the pinned Lean toolchain, a primed lake workspace (for check-proofs),
# and a Conan cache holding lean4 + xrpl-lean4-deps (the slow mathlib objects,
# for build-test). The fast xrpl-lean4 model builds in-job, not here. The image
# tag (see the workflow) hashes the dependency pins and recipes, so it rebuilds
# only on a mathlib/toolchain change, not on model edits.
#
# Build from the repo root; formal_verification/.lake must be absent (see
# .dockerignore):
#     docker build -f formal_verification/docker/Dockerfile .
ARG BASE_IMAGE=ghcr.io/xrplf/xrpld/nix-ubuntu:sha-63ffdc3

# Toolchain stage, shared by builder and final image. CONAN_HOME and
# XDG_CACHE_HOME live under /opt because GitHub Actions overrides HOME in
# container jobs, and the jobs must see the caches this image bakes.
FROM ${BASE_IMAGE} AS toolchain

ENV ELAN_HOME=/opt/elan
ENV PATH=${ELAN_HOME}/bin:${PATH}
ENV CONAN_HOME=/opt/conan
ENV XDG_CACHE_HOME=/opt/xdg-cache

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        curl \
        git \
        libgmp-dev \
        rsync \
    && rm -rf /var/lib/apt/lists/*

COPY formal_verification/lean-toolchain /tmp/lean-toolchain
RUN curl -sSf https://elan.lean-lang.org/elan-init.sh \
        | sh -s -- -y --no-modify-path --default-toolchain none \
    && elan toolchain install "$(cat /tmp/lean-toolchain)" \
    && elan default "$(cat /tmp/lean-toolchain)"

# Builder stage: prime the lake workspace and the Conan cache, then prune.
FROM toolchain AS builder

# Prime the lake workspace for check-proofs: fetch the mathlib cache and
# elaborate the default XRPL target. check-proofs copies this into its checkout.
COPY formal_verification /opt/lean-cache
RUN cd /opt/lean-cache \
    && rm -rf .lake \
    && lake exe cache get \
    && lake build

# Prime the Conan cache for build-test: build lean4 and xrpl-lean4-deps (the
# slow, stable mathlib objects), then keep only the package binaries. The
# xrpl-lean4 model is built in-job, not here.
#
# lake has no parallelism knob and otherwise compiles mathlib's ~16k C files on
# every core, OOM-killing the build on low-RAM-per-core machines. taskset caps
# the visible cores (lake reads CPU affinity) to ~1 job per 2 GB. No-op on CI's
# 4-core/16 GB runners; throttles down on a high-core, low-RAM VM.
COPY external/lean4 /opt/lean-recipe
RUN conan profile detect --exist-ok \
    && conan export --version "$(sed 's|^leanprover/lean4:v||' /tmp/lean-toolchain)" /opt/lean-recipe \
    && conan export /opt/lean-cache/deps \
    && cap="$(nproc)" \
    && mem_cap="$(awk '/MemTotal/{c=int($2/1024/1024/2); print (c<1)?1:c}' /proc/meminfo)" \
    && if [ "$mem_cap" -lt "$cap" ]; then cap="$mem_cap"; fi \
    && echo "Building xrpl-lean4-deps with ${cap} parallel job(s)" \
    && taskset -c "0-$((cap - 1))" \
        conan install --requires=xrpl-lean4-deps/0.1.0 --build=missing \
    && conan cache clean "*"

# Drop what no job reads again: dependency git history and generated C objects.
RUN cd /opt/lean-cache \
    && rm -rf .lake/packages/*/.git .lake/packages/*/.lake/build/ir \
    && find .lake/packages -name "lib*.a" -delete

# Final image: toolchain + the pruned caches. check-proofs uses /opt/lean-cache;
# build-test resolves lean4 + xrpl-lean4-deps from /opt/conan and builds the
# model in-job; /opt/xdg-cache keeps the mathlib download so that build is offline.
FROM toolchain

COPY --from=builder /opt/lean-cache /opt/lean-cache
COPY --from=builder /opt/conan /opt/conan
COPY --from=builder /opt/xdg-cache /opt/xdg-cache
