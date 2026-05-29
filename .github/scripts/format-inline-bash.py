#!/usr/bin/env python3

"""
Format embedded shell snippets using the shfmt hook configured in
.pre-commit-config.yaml.

Two shapes are recognised:

* YAML workflow/action files: literal block-scalar runs (`run: |`) and
  single-line runs (`run: some command`). A single-line run is upgraded to
  a `run: |` block scalar if shfmt's output spans multiple lines.

* Markdown files: ``` ```bash ``` fenced code blocks.

Any block that shfmt cannot parse is skipped with a warning on stderr, so
the file is left untouched and surrounding blocks still get formatted.

For each occurrence the body is dedented, written to a temp .sh file,
formatted via `pre-commit run shfmt --files <temp>` (falling back to
`prek`), then re-indented and written back in place.

When invoked without arguments, every .yml/.yaml under .github/ plus every
.md file in the repo is scanned. When invoked with file arguments (the
pre-commit case), only those files are processed.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Union

REPO = Path(__file__).resolve().parents[2]

_HOOK_RUNNER = next((cmd for cmd in ("pre-commit", "prek") if shutil.which(cmd)), None)
if _HOOK_RUNNER is None:
    sys.exit("error: neither `pre-commit` nor `prek` found on PATH")

RUN_BLOCK_RE = re.compile(r"^(?P<prefix>[ \t]*(?:- )?)run:[ \t]*\|[+-]?[ \t]*$")
RUN_INLINE_RE = re.compile(
    r"^(?P<prefix>[ \t]*(?:- )?)run:[ \t]+" r"(?P<value>(?!\|[+-]?[ \t]*$)\S.*?)[ \t]*$"
)
MD_BASH_OPEN_RE = re.compile(r"^(?P<indent>[ ]{0,3})`{3}bash[ \t]*$")
MD_FENCE_CLOSE_RE = re.compile(r"^[ ]{0,3}`{3,}[ \t]*$")


@dataclass(frozen=True)
class BlockRun:
    """A `run: |` block scalar; `body_start:body_end` slices into `lines`."""

    body_start: int
    body_end: int
    body_indent: int


@dataclass(frozen=True)
class InlineRun:
    """A single-line `run: value` at `line_idx`."""

    line_idx: int
    prefix: str
    value: str


@dataclass(frozen=True)
class MdBashBlock:
    """A markdown ``` ```bash ``` fenced code block.

    `body_start:body_end` slices into the file's lines; `open_line_idx`
    points at the opening fence line.
    """

    open_line_idx: int
    body_start: int
    body_end: int
    body_indent: int


RunItem = Union[BlockRun, InlineRun]


def _scan_block_body(
    lines: list[str], body_start: int, run_col: int
) -> tuple[int | None, int]:
    """Locate the body of a `run: |` block scalar starting at `body_start`.

    Returns `(body_indent, scan_end)`. `scan_end` is the line index where the
    outer scanner should resume. `body_indent` is `None` when no body is
    present (the scalar is empty, or the next non-blank line has indent
    `<= run_col`).
    """
    body_indent: int | None = None
    scan_end = len(lines)
    for idx in range(body_start, len(lines)):
        line = lines[idx]
        if line.strip() == "":
            continue
        indent = len(line) - len(line.lstrip(" "))
        if body_indent is None:
            if indent > run_col:
                body_indent = indent
            else:
                scan_end = idx
                break
        elif indent < body_indent:
            scan_end = idx
            break
    if body_indent is not None:
        while scan_end > body_start and lines[scan_end - 1].strip() == "":
            scan_end -= 1
        if scan_end <= body_start:
            body_indent = None
    return body_indent, scan_end


def find_run_blocks(lines: list[str]) -> list[RunItem]:
    """Return run items in document order."""
    items: list[RunItem] = []
    line_idx = 0
    while line_idx < len(lines):
        line = lines[line_idx]
        if block_match := RUN_BLOCK_RE.match(line):
            run_col = len(block_match.group("prefix"))
            body_start = line_idx + 1
            body_indent, scan_end = _scan_block_body(lines, body_start, run_col)
            if body_indent is not None:
                items.append(
                    BlockRun(
                        body_start=body_start,
                        body_end=scan_end,
                        body_indent=body_indent,
                    )
                )
            line_idx = scan_end
            continue
        if inline_match := RUN_INLINE_RE.match(line):
            items.append(
                InlineRun(
                    line_idx=line_idx,
                    prefix=inline_match.group("prefix"),
                    value=inline_match.group("value"),
                )
            )
        line_idx += 1
    return items


def find_md_bash_blocks(lines: list[str]) -> list[MdBashBlock]:
    """Return ``` ```bash ``` fenced code blocks in document order."""
    blocks: list[MdBashBlock] = []
    line_idx = 0
    while line_idx < len(lines):
        open_match = MD_BASH_OPEN_RE.match(lines[line_idx])
        if not open_match:
            line_idx += 1
            continue
        body_start = line_idx + 1
        close_idx = next(
            (
                j
                for j in range(body_start, len(lines))
                if MD_FENCE_CLOSE_RE.match(lines[j])
            ),
            None,
        )
        if close_idx is None:
            line_idx = body_start
            continue
        body = lines[body_start:close_idx]
        non_blank = [b for b in body if b.strip()]
        body_indent = (
            min(len(b) - len(b.lstrip(" ")) for b in non_blank)
            if non_blank
            else len(open_match.group("indent"))
        )
        blocks.append(
            MdBashBlock(
                open_line_idx=line_idx,
                body_start=body_start,
                body_end=close_idx,
                body_indent=body_indent,
            )
        )
        line_idx = close_idx + 1
    return blocks


def dedent(lines: list[str], n: int) -> list[str]:
    pad = " " * n
    return [
        (
            ""
            if line.strip() == ""
            else (line[n:] if line.startswith(pad) else line.lstrip(" "))
        )
        for line in lines
    ]


def reindent(lines: list[str], n: int) -> list[str]:
    pad = " " * n
    return [pad + line if line else "" for line in lines]


_SHFMT_ERR_RE = re.compile(r"\.sh:\d+:\d+:\s")
_GHA_EXPR_RE = re.compile(r"\$\{\{.*?\}\}", re.DOTALL)
_GHA_PLACEHOLDER_RE = re.compile(r"__GHA_EXPR_(\d+)__")


def _encode_gha_exprs(text: str) -> tuple[str, list[str]]:
    """Replace `${{ ... }}` expressions with bash-safe placeholder identifiers."""
    exprs: list[str] = []

    def repl(match: re.Match[str]) -> str:
        exprs.append(match.group(0))
        return f"__GHA_EXPR_{len(exprs) - 1}__"

    return _GHA_EXPR_RE.sub(repl, text), exprs


def _decode_gha_exprs(text: str, exprs: list[str]) -> str:
    """Restore `${{ ... }}` expressions from placeholder identifiers."""
    return _GHA_PLACEHOLDER_RE.sub(lambda m: exprs[int(m.group(1))], text)


def shfmt_via_hook(tmp_path: Path) -> tuple[bool, str]:
    # `${{ ... }}` is not valid shell, so swap it for a placeholder identifier
    # that shfmt can parse, then restore it after formatting.
    encoded, exprs = _encode_gha_exprs(tmp_path.read_text())
    if exprs:
        tmp_path.write_text(encoded)
    res = subprocess.run(
        [_HOOK_RUNNER, "run", "shfmt", "--files", str(tmp_path)],
        cwd=REPO,
        capture_output=True,
        text=True,
    )
    output = res.stdout + res.stderr
    # shfmt emits parse errors as "<path>:<line>:<col>: <message>".
    parse_err = bool(_SHFMT_ERR_RE.search(output))
    # A non-zero exit that is neither a parse error nor pre-commit's "I had
    # to modify files" signal means the hook itself failed to run (missing
    # binary, install failure, bad config, ...). Surface that loudly rather
    # than silently treating it as a no-op.
    if (
        res.returncode != 0
        and not parse_err
        and "files were modified by this hook" not in output
    ):
        sys.exit(
            f"error: `{_HOOK_RUNNER} run shfmt` failed with exit {res.returncode}:\n{output}"
        )
    if exprs and not parse_err:
        tmp_path.write_text(_decode_gha_exprs(tmp_path.read_text(), exprs))
    return not parse_err, output


def _skip(path: Path, where: int, kind: str, output: str) -> None:
    print(
        f"  shfmt could not parse {kind} at {path}:{where + 1} — skipped",
        file=sys.stderr,
    )
    print(f"    {output.strip()}", file=sys.stderr)


def process_yaml_file(path: Path, tmp_path: Path) -> int:
    text = path.read_text()
    had_nl = text.endswith("\n")
    lines = text.split("\n")
    if had_nl:
        lines = lines[:-1]
    items = find_run_blocks(lines)
    if not items:
        return 0
    changed = 0
    # Process in reverse so earlier indices remain valid as we splice.
    for item in reversed(items):
        if isinstance(item, BlockRun):
            body = lines[item.body_start : item.body_end]
            tmp_path.write_text("\n".join(dedent(body, item.body_indent)) + "\n")
            ok, output = shfmt_via_hook(tmp_path)
            if not ok:
                _skip(path, item.body_start, "block", output)
                continue
            formatted = tmp_path.read_text().rstrip("\n")
            new_body = reindent(formatted.split("\n"), item.body_indent)
            if new_body != body:
                lines[item.body_start : item.body_end] = new_body
                changed += 1
        else:
            tmp_path.write_text(item.value + "\n")
            ok, output = shfmt_via_hook(tmp_path)
            if not ok:
                _skip(path, item.line_idx, "inline run", output)
                continue
            formatted = tmp_path.read_text().rstrip("\n")
            if formatted == item.value:
                continue
            formatted_lines = formatted.split("\n")
            if len(formatted_lines) == 1:
                lines[item.line_idx] = f"{item.prefix}run: {formatted}"
            else:
                body_indent = len(item.prefix) + 2
                lines[item.line_idx : item.line_idx + 1] = [
                    f"{item.prefix}run: |",
                    *reindent(formatted_lines, body_indent),
                ]
            changed += 1
    new_text = "\n".join(lines) + ("\n" if had_nl else "")
    if new_text != text:
        path.write_text(new_text)
    return changed


def process_md_file(path: Path, tmp_path: Path) -> int:
    text = path.read_text()
    had_nl = text.endswith("\n")
    lines = text.split("\n")
    if had_nl:
        lines = lines[:-1]
    blocks = find_md_bash_blocks(lines)
    if not blocks:
        return 0
    changed = 0
    for block in reversed(blocks):
        body = lines[block.body_start : block.body_end]
        tmp_path.write_text("\n".join(dedent(body, block.body_indent)) + "\n")
        ok, output = shfmt_via_hook(tmp_path)
        if not ok:
            _skip(path, block.open_line_idx, "```bash block", output)
            continue
        formatted = tmp_path.read_text().rstrip("\n")
        formatted_lines = formatted.split("\n") if formatted else []
        new_body = reindent(formatted_lines, block.body_indent)
        if new_body != body:
            lines[block.body_start : block.body_end] = new_body
            changed += 1
    new_text = "\n".join(lines) + ("\n" if had_nl else "")
    if new_text != text:
        path.write_text(new_text)
    return changed


def process_file(path: Path, tmp_path: Path) -> int:
    if path.suffix in (".yml", ".yaml"):
        return process_yaml_file(path, tmp_path)
    if path.suffix == ".md":
        return process_md_file(path, tmp_path)
    return 0


def gather_files(argv: list[str]) -> list[Path]:
    """Return YAML workflow/action files and markdown files that we should
    process — either the paths in `argv` or, when `argv` is empty, every
    such file in the repo (skipping `external/`)."""
    if argv:
        candidates: list[Path] = [
            (REPO / a).resolve() if not Path(a).is_absolute() else Path(a) for a in argv
        ]
    else:
        gh = REPO / ".github"
        candidates = [
            *gh.rglob("*.yml"),
            *gh.rglob("*.yaml"),
            *(
                p
                for p in REPO.rglob("*.md")
                if "external" not in p.relative_to(REPO).parts
            ),
        ]
    return sorted(
        p
        for p in candidates
        if p.exists()
        and (
            (p.suffix in (".yml", ".yaml") and ".github" in p.parts)
            or p.suffix == ".md"
        )
    )


def main(argv: list[str]) -> int:
    files = gather_files(argv)
    if not files:
        return 0
    with tempfile.TemporaryDirectory(prefix="format-inline-bash-") as tmpdir:
        tmp_path = Path(tmpdir) / "shfmt.sh"
        total = 0
        for f in files:
            n = process_file(f, tmp_path)
            if n:
                print(f"{f.relative_to(REPO)}: reformatted {n} block(s)")
                total += n
        return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
