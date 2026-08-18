#!/usr/bin/env python3
"""Build the feature-interaction dependency graph for rippled.

Constructs feature nodes (transactors, amendments), resource nodes (base
forks, invariants, shared SFields), and their edges from the .macro files and a
libclang parse of Transactor.cpp, then enumerates the pairwise interaction set.
Outputs graph.json and interactions.json. See DESIGN.md.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import yaml
from jsonschema import Draft202012Validator

from common_fields import parse_common_fields
from fork_extractor import ForkResult, extract_forks
from graph import (
    CORE,
    EDGE_CONSUMER,
    EDGE_MEDIATOR,
    EDGE_WRAPPER,
    FEATURE_AMENDMENT,
    FEATURE_TRANSACTOR,
    LOC_IMPL,
    LOC_STATE_ENUM,
    RESOURCE_FORK,
    RESOURCE_INVARIANT,
    RESOURCE_SFIELD,
    SIGNAL_HIGH,
    Edge,
    GraphBuilder,
    ResourceNode,
    feature_id,
    resource_id,
)
from interactions import enumerate_interactions, write_interactions
from macro_extractor import (
    attach_impl_locations,
    extract_macros,
    parse_sfield_rows,
)
from privilege_extractor import extract_privileges

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]

# Input paths (relative to repo root).
TRANSACTIONS_MACRO = "include/xrpl/protocol/detail/transactions.macro"
FEATURES_MACRO = "include/xrpl/protocol/detail/features.macro"
SFIELDS_MACRO = "include/xrpl/protocol/detail/sfields.macro"
TXFORMATS_CPP = "src/libxrpl/protocol/TxFormats.cpp"
PRIVILEGE_HEADER = "include/xrpl/tx/invariants/InvariantCheckPrivilege.h"
TX_FLAGS_HEADER = "include/xrpl/protocol/TxFlags.h"

# Transaction-flag constants, for the lever vocabulary. Derived from the header
# rather than from the config table: the table holds only the flags that are
# fork levers today, so using its keys made pr_map blind to every other tf*.
_TF_FLAG_RE = re.compile(r"\btf[A-Z]\w*")


def find_build_dir(repo_root: Path) -> Path | None:
    for name in (".build", "build"):
        candidate = repo_root / name
        if (candidate / "compile_commands.json").exists():
            return candidate
    return None


def load_table(path: Path) -> dict[str, str]:
    data = yaml.safe_load(path.read_text()) or {}
    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected a mapping, got {type(data).__name__}")
    return data


def load_list(path: Path) -> list[str]:
    data = yaml.safe_load(path.read_text()) or []
    if not isinstance(data, list):
        raise ValueError(f"{path}: expected a list, got {type(data).__name__}")
    return data


def load_impl_overrides(path: Path) -> dict[str, list[str]]:
    """The overrides file maps a transactor to a *list* of paths.

    A scalar value would otherwise be iterated character by character, failing
    with "lists 's', which does not exist" — a confusing error for the obvious
    YAML mistake of writing one path without a list.
    """
    data = load_table(path)
    for key, value in data.items():
        if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
            raise ValueError(f"{path}: {key!r} must be a list of paths, got {value!r}")
    return data


def _mediator_edge(builder: GraphBuilder, amendment: str, dst: str, via: str) -> None:
    fid = feature_id(FEATURE_AMENDMENT, amendment)
    if fid not in builder.features:
        raise ValueError(
            f"Mediator table maps {via!r} to {amendment!r}, which is not an "
            f"active amendment (dangling edge)."
        )
    builder.add_edge(Edge(kind=EDGE_MEDIATOR, src=fid, dst=dst, via=via))


def merge_forks(
    builder: GraphBuilder,
    forks: list[ForkResult],
    amendment_globals: dict[str, str],
    field_table: dict[str, str],
    flag_table: dict[str, str],
    gate_allowlist: set[str] | None = None,
) -> None:
    """Create fork resources and their mediator/consumer/wrapper edges.

    An amendment-gate global that resolves to no active amendment and is not in
    `gate_allowlist` fails the build (same recall guarantee as lever fields and
    flags): a renamed/typo'd gate reference must not silently drop its edge.
    """
    gate_allowlist = gate_allowlist or set()

    # Upfront dangling check on every non-core table value.
    for table, label in ((field_table, "field"), (flag_table, "flag")):
        for key, amendment in table.items():
            if (
                amendment != CORE
                and feature_id(FEATURE_AMENDMENT, amendment) not in builder.features
            ):
                raise ValueError(
                    f"{label}_to_amendment[{key!r}] = {amendment!r} is not an "
                    f"active amendment (dangling)."
                )

    transactors = [n for n in builder.features.values() if n.kind == FEATURE_TRANSACTOR]

    for fork in forks:
        # Recall guardrail: every discovered lever must have a table entry.
        missing_fields = sorted(fork.lever_fields - set(field_table))
        if missing_fields:
            raise ValueError(
                f"Fork {fork.name!r} has lever fields with no "
                f"field_to_amendment entry: {missing_fields}"
            )
        missing_flags = sorted(fork.lever_flags - set(flag_table))
        if missing_flags:
            raise ValueError(
                f"Fork {fork.name!r} has lever flags with no "
                f"flag_to_amendment entry: {missing_flags}"
            )

        unresolved = sorted(
            g
            for g in fork.gate_globals
            if g not in amendment_globals and g not in gate_allowlist
        )
        if unresolved:
            raise ValueError(
                f"Fork {fork.name!r} references amendment gates that resolve to "
                f"no active amendment: {unresolved}. Add the amendment to "
                f"features.macro, or list the token in config/gate_allowlist.yml "
                f"if it is a legitimately retired reference."
            )
        gate_names = sorted(
            {amendment_globals[g] for g in fork.gate_globals if g in amendment_globals}
        )

        rid = resource_id(RESOURCE_FORK, fork.name)
        builder.add_resource(
            ResourceNode(
                id=rid,
                kind=RESOURCE_FORK,
                name=fork.name,
                signal=SIGNAL_HIGH,
                lever_fields=sorted(fork.lever_fields),
                lever_flags=sorted(fork.lever_flags),
                amendment_gates=gate_names,
                state_space=fork.state_space,
                locations=sorted(fork.spans),
            )
        )

        # Mediator edges: lever fields, lever flags, amendment gates.
        for fname in sorted(fork.lever_fields):
            gov = field_table[fname]
            if gov != CORE:
                _mediator_edge(builder, gov, rid, fname)
        for flag in sorted(fork.lever_flags):
            gov = flag_table[flag]
            if gov != CORE:
                _mediator_edge(builder, gov, rid, flag)
        for gate in gate_names:
            _mediator_edge(builder, gate, rid, gate)

        # Consumer edges: every transactor flows through every base fork.
        # Wrapper edges: a wrapper transactor mediates every base fork.
        for tx in transactors:
            builder.add_edge(Edge(kind=EDGE_CONSUMER, src=tx.id, dst=rid, via="base"))
            if tx.wrapper:
                builder.add_edge(
                    Edge(kind=EDGE_WRAPPER, src=tx.id, dst=rid, via="wrapper")
                )


def validate_graph(builder: GraphBuilder, schema_path: Path) -> None:
    schema = json.loads(schema_path.read_text())
    doc = builder.to_dict()
    errors = sorted(
        Draft202012Validator(schema).iter_errors(doc),
        key=lambda e: [str(p) for p in e.path],
    )
    if errors:
        msgs = "\n".join(f"  {list(e.path)}: {e.message}" for e in errors[:10])
        raise ValueError(f"graph.json failed schema validation:\n{msgs}")

    feature_ids = set(builder.features)
    resource_ids = set(builder.resources)
    for edge in builder.edges:
        if edge.src not in feature_ids:
            raise ValueError(f"Edge from unknown feature {edge.src!r}")
        if edge.dst not in resource_ids:
            raise ValueError(f"Edge to unknown resource {edge.dst!r}")


def validate_locations(builder: GraphBuilder, repo_root: Path) -> None:
    """Every node must be locatable, and every span must exist in the checkout.

    A node with no location is invisible to the PR mapper — the diff can never
    reach it — so an unlocatable node is a silent recall hole, not a cosmetic
    gap. A span past the end of its file means a parser is off by more than a
    rounding error and every span it produced is suspect.
    """
    missing = sorted(n.id for n in builder.all_nodes() if not n.locations)
    if missing:
        raise ValueError(
            f"node(s) with no source location, unreachable from any diff: {missing}"
        )

    file_lines: dict[str, list[str]] = {}
    for node in builder.all_nodes():
        for loc in node.locations:
            if loc.file not in file_lines:
                path = Path(repo_root) / loc.file
                if not path.is_file():
                    raise ValueError(f"{node.id}: location file {loc.file!r} not found")
                file_lines[loc.file] = path.read_text().splitlines()
            lines = file_lines[loc.file]
            if loc.end_line > max(1, len(lines)):
                raise ValueError(
                    f"{node.id}: span {loc.file}:{loc.start_line}-{loc.end_line} "
                    f"runs past end of file ({len(lines)} lines)"
                )
            # A span that declares, defines, or precisely references the node
            # must contain its name.
            # This is the cheap check that catches an off-by-N span: a line
            # number can drift silently, but drifting off the name cannot.
            # `impl` spans cover a whole file, and a `state_enum` span is a
            # different declaration entirely (the enum naming a fork's boundary
            # states), so neither is expected to spell the node's own name.
            if loc.role in (LOC_IMPL, LOC_STATE_ENUM):
                continue
            span_text = "\n".join(lines[loc.start_line - 1 : loc.end_line])
            if node.name not in span_text:
                raise ValueError(
                    f"{node.id}: {loc.role} span "
                    f"{loc.file}:{loc.start_line}-{loc.end_line} does not "
                    f"contain {node.name!r}; the span is misaligned"
                )


def resolve_out_paths(out: str) -> tuple[Path, Path]:
    out_path = Path(out)
    if out_path.suffix == ".json":
        out_path.parent.mkdir(parents=True, exist_ok=True)
        return out_path, out_path.parent / "interactions.json"
    out_path.mkdir(parents=True, exist_ok=True)
    return out_path / "graph.json", out_path / "interactions.json"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        help="Build directory with compile_commands.json (default: auto-detect .build/build)",
    )
    parser.add_argument(
        "--out",
        default=str(HERE / "out"),
        help="Output directory, or a *.json path for the graph (interactions.json is written alongside)",
    )
    parser.add_argument(
        "--repo-root", default=str(REPO_ROOT), help="rippled checkout root"
    )
    parser.add_argument(
        "--libclang", help="Path to libclang dylib/so (default: brew LLVM location)"
    )
    args = parser.parse_args(argv)

    repo_root = Path(args.repo_root).resolve()
    build_dir = (
        Path(args.build_dir).resolve() if args.build_dir else find_build_dir(repo_root)
    )
    if build_dir is None or not (build_dir / "compile_commands.json").exists():
        parser.error("no build directory with compile_commands.json found")

    common_fields = parse_common_fields(repo_root / TXFORMATS_CPP)

    builder = GraphBuilder()
    amendment_globals = extract_macros(
        builder,
        repo_root,
        repo_root / TRANSACTIONS_MACRO,
        repo_root / FEATURES_MACRO,
        repo_root / SFIELDS_MACRO,
        common_fields,
    )
    attach_impl_locations(
        builder,
        repo_root,
        load_impl_overrides(HERE / "config" / "transactor_impl_overrides.yml"),
    )
    extract_privileges(builder, repo_root, repo_root / PRIVILEGE_HEADER)

    forks = extract_forks(build_dir, common_fields, args.libclang, repo_root)

    field_table = load_table(HERE / "config" / "field_to_amendment.yml")
    flag_table = load_table(HERE / "config" / "flag_to_amendment.yml")
    gate_allowlist = set(load_list(HERE / "config" / "gate_allowlist.yml"))
    merge_forks(
        builder, forks, amendment_globals, field_table, flag_table, gate_allowlist
    )

    # Record the lever vocabulary the graph was derived from, so pr_map can tell
    # a real lever from a local variable that merely looks like one.
    builder.vocabulary = {
        "common_fields": sorted(common_fields),
        "sfields": sorted(parse_sfield_rows(repo_root / SFIELDS_MACRO)),
        "flags": sorted(
            set(_TF_FLAG_RE.findall((repo_root / TX_FLAGS_HEADER).read_text()))
        ),
        "amendment_globals": sorted(amendment_globals),
    }

    validate_graph(builder, HERE / "graph.schema.json")
    validate_locations(builder, repo_root)

    graph_path, interactions_path = resolve_out_paths(args.out)
    builder.write(graph_path)
    interactions = enumerate_interactions(builder)
    write_interactions(interactions, interactions_path)

    n_tx = sum(1 for n in builder.features.values() if n.kind == FEATURE_TRANSACTOR)
    n_am = sum(1 for n in builder.features.values() if n.kind == FEATURE_AMENDMENT)
    n_fork = sum(1 for r in builder.resources.values() if r.kind == RESOURCE_FORK)
    print(f"features: {n_tx} transactors, {n_am} amendments")
    print(
        f"resources: {n_fork} forks, "
        f"{sum(1 for r in builder.resources.values() if r.kind == RESOURCE_INVARIANT)} invariants, "
        f"{sum(1 for r in builder.resources.values() if r.kind == RESOURCE_SFIELD)} shared sfields"
    )
    print(f"edges: {len(builder.edges)}")
    print(f"interactions: {len(interactions)}")
    print(f"wrote {graph_path} and {interactions_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
