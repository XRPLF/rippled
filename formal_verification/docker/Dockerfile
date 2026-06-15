# CI image for the formal-verification jobs: xrpld CI base + the pinned Lean
# toolchain, a primed .lake (oleans + compiled objects), and a Conan cache with
# lean4. Build from the repo root:
#     docker build -f formal_verification/docker/Dockerfile .
ARG BASE_IMAGE=ghcr.io/xrplf/xrpld/nix-ubuntu:sha-63ffdc3

# Toolchain stage (shared). Caches live under /opt because CI overrides HOME.
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

FROM toolchain AS builder

# Fetch the mathlib cache and elaborate (oleans; check-proofs reuses them).
COPY formal_verification /opt/lean-cache
RUN cd /opt/lean-cache \
    && rm -rf .lake \
    && lake exe cache get \
    && lake build

# Compile dependency objects (build-test links them); the :static archive fails
# on ARG_MAX but the .o files remain. taskset caps to ~1 job/2 GB to avoid OOM.
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

# Prime the Conan cache with lean4 (os/arch-keyed, so the ci profile reuses it).
COPY external/lean4 /opt/lean-recipe
RUN conan profile detect --exist-ok \
    && lean_ver="$(sed 's|^leanprover/lean4:v||' /tmp/lean-toolchain)" \
    && conan export --version "${lean_ver}" /opt/lean-recipe \
    && conan install --requires="lean4/${lean_ver}" --build=missing \
    && conan cache clean "*"

# Drop dependency git history (keep the objects + oleans).
RUN cd /opt/lean-cache && rm -rf .lake/packages/*/.git

# Final image: toolchain + the baked /opt caches that the jobs restore.
FROM toolchain

COPY --from=builder /opt/lean-cache /opt/lean-cache
COPY --from=builder /opt/conan /opt/conan
COPY --from=builder /opt/xdg-cache /opt/xdg-cache
