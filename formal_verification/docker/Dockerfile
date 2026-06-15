# CI image for the formal-verification jobs: the public xrpld CI image plus the
# pinned Lean toolchain, a primed .lake (mathlib oleans + compiled objects), and a
# Conan cache with the lean4 toolchain. The Lean library itself builds in-job.
#
# Build from the repo root (formal_verification/.lake must be absent; see .dockerignore):
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

# Prime the lake workspace: fetch the mathlib cache and elaborate the default
# XRPL target (check-proofs reuses these oleans).
COPY formal_verification /opt/lean-cache
RUN cd /opt/lean-cache \
    && rm -rf .lake \
    && lake exe cache get \
    && lake build

# Compile every dependency's native objects (build-test links them in-job). The
# :static archive step fails on ARG_MAX, but the .o files are produced. taskset
# caps parallelism to ~1 job/2 GB so mathlib's compile does not OOM a low-RAM VM.
RUN cd /opt/lean-cache \
    && cap="$(nproc)" \
    && mem_cap="$(awk '/MemTotal/{c=int($2/1024/1024/2); print (c<1)?1:c}' /proc/meminfo)" \
    && if [ "$mem_cap" -lt "$cap" ]; then cap="$mem_cap"; fi \
    && echo "Compiling Lean dependency objects with ${cap} parallel job(s)" \
    && taskset -c "0-$((cap - 1))" lake build \
        ProofWidgets:static ImportGraph:static LeanSearchClient:static \
        Plausible:static Aesop:static Qq:static Cli:static Batteries:static \
        Mathlib:static \
    || true

# Prime the Conan cache for build-test with the lean4 toolchain package (keyed on
# os/arch only, so the ci profile reuses it). No Lean model package: it builds
# in-job from the primed .lake.
COPY external/lean4 /opt/lean-recipe
RUN conan profile detect --exist-ok \
    && lean_ver="$(sed 's|^leanprover/lean4:v||' /tmp/lean-toolchain)" \
    && conan export --version "${lean_ver}" /opt/lean-recipe \
    && conan install --requires="lean4/${lean_ver}" --build=missing \
    && conan cache clean "*"

# Drop dependency git history; keep the compiled objects + oleans (build-test
# links the objects, check-proofs and model elaboration need the oleans).
RUN cd /opt/lean-cache && rm -rf .lake/packages/*/.git

# Final image: toolchain + pruned caches. Jobs restore /opt/lean-cache/.lake;
# build-test resolves lean4 from /opt/conan; /opt/xdg-cache keeps the mathlib
# download so the build is offline.
FROM toolchain

COPY --from=builder /opt/lean-cache /opt/lean-cache
COPY --from=builder /opt/conan /opt/conan
COPY --from=builder /opt/xdg-cache /opt/xdg-cache
