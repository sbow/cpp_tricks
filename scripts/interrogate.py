#!/usr/bin/env python3
"""Compile C++ in stages, run inspection tools, emit a static walkthrough site."""

from __future__ import annotations

import argparse
import html
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path


PROGRAMS_ROOT_DEFAULT = Path("cpp_tricks")
ARTIFACTS_ROOT_DEFAULT = Path("build/interrogate")
SITE_DIR_DEFAULT = Path("build/interrogate/site")


@dataclass
class CommandResult:
    argv: list[str]
    stdout: str
    stderr: str
    returncode: int
    artifact: Path | None = None

    @property
    def ok(self) -> bool:
        return self.returncode == 0


@dataclass
class Stage:
    id: str
    title: str
    description: str
    command: CommandResult
    file_preview: tuple[Path, int] | None = None  # path, max_lines
    inspections: list[tuple[str, CommandResult]] = field(default_factory=list)


@dataclass
class Program:
    name: str
    sources: list[Path]


def run(argv: list[str], *, cwd: Path) -> CommandResult:
    proc = subprocess.run(
        argv,
        cwd=cwd,
        capture_output=True,
        text=True,
        errors="replace",
    )
    return CommandResult(
        argv=argv,
        stdout=proc.stdout,
        stderr=proc.stderr,
        returncode=proc.returncode,
    )


def read_preview(path: Path, max_lines: int) -> tuple[str, bool]:
    if not path.exists():
        return "(file missing)", False
    text = path.read_text(errors="replace")
    lines = text.splitlines()
    if len(lines) <= max_lines:
        return text, False
    head = max_lines * 2 // 3
    tail = max_lines - head
    omitted = len(lines) - head - tail
    snippet = (
        "\n".join(lines[:head])
        + f"\n\n… [{omitted:,} lines omitted] …\n\n"
        + "\n".join(lines[-tail:])
    )
    return snippet, True


def gxx_flags(std: str, debug: bool) -> list[str]:
    flags = ["g++", f"-std={std}", "-Wall", "-Wextra"]
    if debug:
        flags.extend(["-g", "-O0"])
    return flags


def discover_programs(programs_root: Path) -> list[Program]:
    """Find cpp_tricks/<name>/src/*.cpp, matching the Makefile layout."""
    if not programs_root.is_dir():
        return []

    programs: list[Program] = []
    for src_dir in sorted(programs_root.glob("*/src")):
        if not src_dir.is_dir():
            continue
        sources = sorted(src_dir.glob("*.cpp"))
        if not sources:
            continue
        name = src_dir.parent.name
        programs.append(Program(name=name, sources=sources))
    return programs


def pick_primary_source(sources: list[Path]) -> Path:
    for source in sources:
        if source.name == "main.cpp":
            return source
    return sources[0]


def build_pipeline(
    project_root: Path,
    program: Program,
    out_dir: Path,
    std: str,
    debug: bool,
) -> tuple[list[Stage], Path | None]:
    out_dir.mkdir(parents=True, exist_ok=True)
    primary = pick_primary_source(program.sources)
    base = primary.stem
    preprocessed = out_dir / f"{base}.i"
    assembly = out_dir / f"{base}.s"
    object_files = [out_dir / f"{source.stem}.o" for source in program.sources]
    executable = out_dir / program.name
    cxx = gxx_flags(std, debug)
    stages: list[Stage] = []

    primary_rel = primary.relative_to(project_root)

    # 1 — preprocess (primary translation unit)
    cmd = run([*cxx, "-E", str(primary_rel), "-o", str(preprocessed)], cwd=project_root)
    cmd.artifact = preprocessed
    stages.append(
        Stage(
            id="preprocess",
            title="Preprocess (-E)",
            description=(
                f"Runs only the preprocessor on {primary.name}: #include expansion, "
                "macros, comments removed. Output is translation-unit text (.i), often huge."
            ),
            command=cmd,
            file_preview=(preprocessed, 80),
        )
    )

    # 2 — assembly (primary translation unit)
    cmd = run([*cxx, "-S", str(primary_rel), "-o", str(assembly)], cwd=project_root)
    cmd.artifact = assembly
    stages.append(
        Stage(
            id="assemble-source",
            title="Compile to assembly (-S)",
            description=(
                f"Compiles {primary.name} to human-readable assembly (.s) without "
                "assembling or linking."
            ),
            command=cmd,
            file_preview=(assembly, 200),
        )
    )

    # 3 — object (all translation units)
    compile_cmds: list[CommandResult] = []
    for source, object_file in zip(program.sources, object_files, strict=True):
        source_rel = source.relative_to(project_root)
        cmd = run([*cxx, "-c", str(source_rel), "-o", str(object_file)], cwd=project_root)
        cmd.artifact = object_file
        compile_cmds.append(cmd)

    primary_object = out_dir / f"{primary.stem}.o"
    obj_inspections = inspect_object(primary_object, project_root)
    if len(program.sources) == 1:
        object_description = (
            "Produces a relocatable ELF object (.o): machine code plus symbol "
            "table and relocations. Not executable until linked."
        )
        object_command = compile_cmds[0]
    else:
        object_description = (
            f"Compiles each of {len(program.sources)} translation units to .o files. "
            f"Inspection below uses {primary.name}."
        )
        object_command = CommandResult(
            argv=["g++", "-c", f"({len(compile_cmds)} translation units)"],
            stdout="\n\n".join(
                f"$ {' '.join(c.argv)}\n(exit {c.returncode})"
                for c in compile_cmds
            ),
            stderr="\n".join(c.stderr for c in compile_cmds if c.stderr.strip()),
            returncode=max(c.returncode for c in compile_cmds),
            artifact=primary_object,
        )

    stages.append(
        Stage(
            id="object",
            title="Compile to object (-c)",
            description=object_description,
            command=object_command,
            file_preview=None,
            inspections=obj_inspections,
        )
    )

    # 4 — link
    link_argv = [*cxx, *[str(p) for p in object_files], "-o", str(executable)]
    cmd = run(link_argv, cwd=project_root)
    cmd.artifact = executable
    exe_inspections = inspect_executable(executable, project_root)
    stages.append(
        Stage(
            id="executable",
            title="Link (executable)",
            description=(
                "Linker resolves symbols and relocations, pulls in libstdc++ and "
                "libc, and produces a runnable ELF binary."
            ),
            command=cmd,
            file_preview=None,
            inspections=exe_inspections,
        )
    )

    # 5 — run
    if executable.exists():
        run_cmd = run([str(executable)], cwd=project_root)
        stages.append(
            Stage(
                id="run",
                title="Run",
                description="Execute the linked binary and capture stdout/stderr.",
                command=run_cmd,
            )
        )

    exe = executable if executable.exists() else None
    return stages, exe


def inspect_object(path: Path, cwd: Path) -> list[tuple[str, CommandResult]]:
    if not path.exists():
        return []
    return [
        ("file", run(["file", str(path)], cwd=cwd)),
        ("nm -C (demangled symbols)", run(["nm", "-C", str(path)], cwd=cwd)),
        ("readelf -S (sections)", run(["readelf", "-S", str(path)], cwd=cwd)),
        ("readelf -r (relocations)", run(["readelf", "-r", str(path)], cwd=cwd)),
        (
            "objdump -d -S -C (disassembly + source)",
            run(["objdump", "-d", "-S", "-C", str(path)], cwd=cwd),
        ),
    ]


def inspect_executable(path: Path, cwd: Path) -> list[tuple[str, CommandResult]]:
    if not path.exists():
        return []
    inspections: list[tuple[str, CommandResult]] = [
        ("file", run(["file", str(path)], cwd=cwd)),
        ("ldd (shared libraries)", run(["ldd", str(path)], cwd=cwd)),
        ("nm -C | head (symbols)", run(["nm", "-C", str(path)], cwd=cwd)),
        ("readelf -h (ELF header)", run(["readelf", "-h", str(path)], cwd=cwd)),
        ("readelf -l (program headers)", run(["readelf", "-l", str(path)], cwd=cwd)),
        ("readelf -S (sections)", run(["readelf", "-S", str(path)], cwd=cwd)),
        (
            "objdump -d -S -C (disassembly + source)",
            run(["objdump", "-d", "-S", "-C", str(path)], cwd=cwd),
        ),
        ("strings | grep -E (Hello|world|libstd)", run(
            ["bash", "-c", f"strings {path} | grep -E 'Hello|world|libstd' || true"],
            cwd=cwd,
        )),
    ]
    return inspections


def esc(text: str) -> str:
    return html.escape(text)


def cmd_block(result: CommandResult) -> str:
    joined = esc(" ".join(result.argv))
    status = "ok" if result.ok else f"failed (exit {result.returncode})"
    parts = [
        f'<div class="cmd"><code>$ {joined}</code></div>',
        f'<p class="status {"ok" if result.ok else "err"}">{status}</p>',
    ]
    if result.stdout.strip():
        parts.append(f'<pre class="out">{esc(result.stdout)}</pre>')
    if result.stderr.strip():
        parts.append(f'<pre class="err">{esc(result.stderr)}</pre>')
    return "\n".join(parts)


def program_page_name(program_name: str) -> str:
    return f"{program_name}.html"


def render_program_nav(all_programs: list[str], current: str | None) -> str:
    all_cls = ' class="active"' if current is None else ""
    items = [f'<a href="index.html"{all_cls}>All programs</a>']
    for name in all_programs:
        href = program_page_name(name)
        cls = ' class="active"' if name == current else ""
        items.append(f'<a href="{href}"{cls}>{esc(name)}</a>')
    return "\n".join(items)


def render_stage_sections(stages: list[Stage], project_root: Path) -> str:
    stage_html: list[str] = []
    for stage in stages:
        body = [
            f'<section id="{stage.id}" class="stage">',
            f"<h2>{esc(stage.title)}</h2>",
            f'<p class="lead">{esc(stage.description)}</p>',
            cmd_block(stage.command),
        ]
        if stage.file_preview and stage.file_preview[0].exists():
            path, max_lines = stage.file_preview
            preview, truncated = read_preview(path, max_lines)
            rel = path.relative_to(project_root)
            note = (
                f' <span class="trunc">(truncated; open {esc(str(rel))} for full file)</span>'
                if truncated
                else ""
            )
            size = path.stat().st_size
            body.append(
                f'<h3>Artifact: <code>{esc(str(rel))}</code> ({size:,} bytes){note}</h3>'
                f'<pre class="artifact">{esc(preview)}</pre>'
            )
        for label, insp in stage.inspections:
            body.append(f"<h3>{esc(label)}</h3>")
            body.append(cmd_block(insp))
        body.append("</section>")
        stage_html.append("\n".join(body))
    return "\n".join(stage_html)


def render_program_site(
    stages: list[Stage],
    *,
    program: Program,
    project_root: Path,
    out_dir: Path,
    site_dir: Path,
    all_programs: list[str],
    std: str,
    generated_at: str,
) -> Path:
    site_dir.mkdir(parents=True, exist_ok=True)
    primary = pick_primary_source(program.sources)
    page_path = site_dir / program_page_name(program.name)

    nav_items = [('<a href="#overview">Overview</a>', "overview")]
    for stage in stages:
        nav_items.append((f'<a href="#{stage.id}">{esc(stage.title)}</a>', stage.id))

    sources_label = ", ".join(
        str(source.relative_to(project_root)) for source in program.sources
    )
    page_path.write_text(
        HTML_TEMPLATE.format(
            title=esc(f"C++ build walkthrough — {program.name}"),
            program_nav=render_program_nav(all_programs, program.name),
            nav="\n".join(n[0] for n in nav_items),
            stages=render_stage_sections(stages, project_root),
            program=esc(program.name),
            source=esc(sources_label),
            std=esc(std),
            out_dir=esc(str(out_dir.relative_to(project_root))),
            generated_at=generated_at,
        ),
        encoding="utf-8",
    )
    return page_path


def render_index_site(
    *,
    programs: list[Program],
    program_pages: dict[str, Path],
    program_results: dict[str, list[Stage]],
    project_root: Path,
    programs_root: Path,
    artifacts_root: Path,
    site_dir: Path,
    std: str,
    generated_at: str,
) -> Path:
    site_dir.mkdir(parents=True, exist_ok=True)
    cards: list[str] = []
    for program in programs:
        stages = program_results.get(program.name, [])
        failed = [s for s in stages if not s.command.ok]
        if not stages:
            status = ""
            status_text = "not built in this run"
        elif failed:
            status = "err"
            status_text = f"{len(failed)} stage(s) failed"
        else:
            status = "ok"
            status_text = "built"
        primary = pick_primary_source(program.sources)
        artifacts_dir = artifacts_root / program.name
        cards.append(
            f"""<article class="program-card">
  <h2><a href="{esc(program_page_name(program.name))}">{esc(program.name)}</a></h2>
  <p class="status {status}">{esc(status_text)}</p>
  <p class="meta">Source: <code>{esc(str(primary.relative_to(project_root)))}</code></p>
  <p class="meta">Artifacts: <code>{esc(str(artifacts_dir.relative_to(project_root)))}</code></p>
  <p><a class="button" href="{esc(program_page_name(program.name))}">Open walkthrough →</a></p>
</article>"""
        )

    index_path = site_dir / "index.html"
    index_path.write_text(
        INDEX_TEMPLATE.format(
            title="C++ build walkthrough",
            program_nav=render_program_nav([p.name for p in programs], None),
            programs="\n".join(cards),
            programs_root=esc(str(programs_root.relative_to(project_root))),
            std=esc(std),
            artifacts_root=esc(str(artifacts_root.relative_to(project_root))),
            generated_at=generated_at,
        ),
        encoding="utf-8",
    )
    return index_path


HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title}</title>
  <style>
    :root {{
      --bg: #0f1419;
      --surface: #1a2332;
      --border: #2d3a4f;
      --text: #e7ecf3;
      --muted: #8b9cb3;
      --accent: #6eb5ff;
      --ok: #3dd68c;
      --err: #f07178;
      --code-bg: #0a0e14;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      font-family: "Segoe UI", system-ui, sans-serif;
      background: var(--bg);
      color: var(--text);
      line-height: 1.5;
    }}
    .layout {{
      display: grid;
      grid-template-columns: 220px 1fr;
      min-height: 100vh;
    }}
    nav {{
      position: sticky;
      top: 0;
      height: 100vh;
      overflow-y: auto;
      padding: 1.25rem 1rem;
      background: var(--surface);
      border-right: 1px solid var(--border);
    }}
    nav .nav-group {{
      margin-bottom: 1.25rem;
    }}
    nav .nav-label {{
      display: block;
      margin-bottom: 0.5rem;
      font-size: 0.75rem;
      font-weight: 600;
      letter-spacing: 0.04em;
      text-transform: uppercase;
      color: var(--muted);
    }}
    nav a {{
      display: block;
      color: var(--muted);
      text-decoration: none;
      padding: 0.35rem 0.5rem;
      border-radius: 4px;
      font-size: 0.9rem;
    }}
    nav a:hover {{ color: var(--accent); background: var(--code-bg); }}
    nav a.active {{ color: var(--accent); background: var(--code-bg); }}
    main {{ padding: 2rem 2.5rem 4rem; max-width: 1100px; }}
    h1 {{ font-size: 1.75rem; margin-top: 0; }}
    h2 {{ font-size: 1.35rem; margin-top: 2.5rem; border-bottom: 1px solid var(--border); padding-bottom: 0.35rem; }}
    h3 {{ font-size: 1rem; color: var(--accent); margin-top: 1.5rem; }}
    .lead {{ color: var(--muted); }}
    .meta {{ font-size: 0.85rem; color: var(--muted); margin-bottom: 2rem; }}
    .pipeline {{
      display: flex;
      flex-wrap: wrap;
      gap: 0.5rem;
      align-items: center;
      margin: 1rem 0 2rem;
      font-family: ui-monospace, monospace;
      font-size: 0.85rem;
    }}
    .pipeline span {{
      background: var(--surface);
      border: 1px solid var(--border);
      padding: 0.35rem 0.6rem;
      border-radius: 4px;
    }}
    .pipeline .arrow {{ background: none; border: none; color: var(--muted); }}
    .cmd code {{
      display: block;
      background: var(--code-bg);
      border: 1px solid var(--border);
      padding: 0.65rem 0.85rem;
      border-radius: 6px;
      font-family: ui-monospace, "Cascadia Code", monospace;
      font-size: 0.82rem;
      overflow-x: auto;
    }}
    pre {{
      background: var(--code-bg);
      border: 1px solid var(--border);
      padding: 0.85rem 1rem;
      border-radius: 6px;
      overflow-x: auto;
      font-family: ui-monospace, monospace;
      font-size: 0.78rem;
      line-height: 1.45;
      white-space: pre-wrap;
      word-break: break-word;
    }}
    pre.artifact {{ max-height: 28rem; overflow-y: auto; }}
    pre.err {{ border-color: var(--err); }}
    .status {{ font-size: 0.85rem; margin: 0.25rem 0 0.75rem; }}
    .status.ok {{ color: var(--ok); }}
    .status.err {{ color: var(--err); }}
    .trunc {{ color: var(--muted); font-size: 0.85rem; }}
    section.stage {{ margin-bottom: 3rem; }}
    @media (max-width: 768px) {{
      .layout {{ grid-template-columns: 1fr; }}
      nav {{ position: static; height: auto; }}
    }}
  </style>
</head>
<body>
  <div class="layout">
    <nav>
      <div class="nav-group">
        <span class="nav-label">Programs</span>
        {program_nav}
      </div>
      <div class="nav-group">
        <span class="nav-label">Stages</span>
        {nav}
      </div>
    </nav>
    <main>
      <h1>{title}</h1>
      <p class="meta">Program: <code>{program}</code> · Source: <code>{source}</code> · C++ {std} · Artifacts: <code>{out_dir}</code><br>
      Generated {generated_at}</p>

      <section id="overview">
        <h2>Overview</h2>
        <p class="lead">Pipeline from .cpp sources to a runnable binary, with inspection at each stage.</p>
        <div class="pipeline">
          <span>.cpp</span><span class="arrow">→ -E →</span><span>.i</span>
          <span class="arrow">→ -S →</span><span>.s</span>
          <span class="arrow">→ -c →</span><span>.o</span>
          <span class="arrow">→ link →</span><span>executable</span>
          <span class="arrow">→</span><span>run</span>
        </div>
        <p class="lead">Use <strong>g++</strong> (not gcc) so libstdc++ is linked. Undefined <code>std::cout</code> at link time usually means the C++ standard library was not linked.</p>
      </section>

      {stages}
    </main>
  </div>
</body>
</html>
"""

INDEX_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title}</title>
  <style>
    :root {{
      --bg: #0f1419;
      --surface: #1a2332;
      --border: #2d3a4f;
      --text: #e7ecf3;
      --muted: #8b9cb3;
      --accent: #6eb5ff;
      --ok: #3dd68c;
      --err: #f07178;
      --code-bg: #0a0e14;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      font-family: "Segoe UI", system-ui, sans-serif;
      background: var(--bg);
      color: var(--text);
      line-height: 1.5;
    }}
    .layout {{
      display: grid;
      grid-template-columns: 220px 1fr;
      min-height: 100vh;
    }}
    nav {{
      position: sticky;
      top: 0;
      height: 100vh;
      overflow-y: auto;
      padding: 1.25rem 1rem;
      background: var(--surface);
      border-right: 1px solid var(--border);
    }}
    nav .nav-group {{ margin-bottom: 1.25rem; }}
    nav .nav-label {{
      display: block;
      margin-bottom: 0.5rem;
      font-size: 0.75rem;
      font-weight: 600;
      letter-spacing: 0.04em;
      text-transform: uppercase;
      color: var(--muted);
    }}
    nav a {{
      display: block;
      color: var(--muted);
      text-decoration: none;
      padding: 0.35rem 0.5rem;
      border-radius: 4px;
      font-size: 0.9rem;
    }}
    nav a:hover {{ color: var(--accent); background: var(--code-bg); }}
    nav a.active {{ color: var(--accent); background: var(--code-bg); }}
    main {{ padding: 2rem 2.5rem 4rem; max-width: 900px; }}
    h1 {{ font-size: 1.75rem; margin-top: 0; }}
    h2 {{ font-size: 1.2rem; margin: 0 0 0.5rem; }}
    .meta {{ font-size: 0.85rem; color: var(--muted); margin-bottom: 2rem; }}
    .program-grid {{
      display: grid;
      gap: 1rem;
    }}
    .program-card {{
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 1.25rem 1.5rem;
    }}
    .program-card h2 a {{
      color: var(--text);
      text-decoration: none;
    }}
    .program-card h2 a:hover {{ color: var(--accent); }}
    .status {{ font-size: 0.85rem; margin: 0.25rem 0 0.75rem; }}
    .status.ok {{ color: var(--ok); }}
    .status.err {{ color: var(--err); }}
    .button {{
      display: inline-block;
      margin-top: 0.5rem;
      color: var(--accent);
      text-decoration: none;
      font-size: 0.9rem;
    }}
    .button:hover {{ text-decoration: underline; }}
    @media (max-width: 768px) {{
      .layout {{ grid-template-columns: 1fr; }}
      nav {{ position: static; height: auto; }}
    }}
  </style>
</head>
<body>
  <div class="layout">
    <nav>
      <div class="nav-group">
        <span class="nav-label">Programs</span>
        {program_nav}
      </div>
    </nav>
    <main>
      <h1>{title}</h1>
      <p class="meta">Programs root: <code>{programs_root}</code> · C++ {std} · Artifacts: <code>{artifacts_root}</code><br>
      Generated {generated_at}</p>
      <div class="program-grid">
        {programs}
      </div>
    </main>
  </div>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--programs-root",
        type=Path,
        default=PROGRAMS_ROOT_DEFAULT,
        help="Root directory containing one folder per program (default: cpp_tricks)",
    )
    parser.add_argument(
        "--program",
        action="append",
        dest="programs",
        metavar="NAME",
        help="Build only named program(s); default is all discovered programs",
    )
    parser.add_argument(
        "--std",
        default="c++20",
        help="C++ standard flag (default: c++20)",
    )
    parser.add_argument(
        "--artifacts-dir",
        type=Path,
        default=ARTIFACTS_ROOT_DEFAULT,
        help="Root directory for per-program .i, .s, .o, binary artifacts",
    )
    parser.add_argument(
        "--site-dir",
        type=Path,
        default=SITE_DIR_DEFAULT,
        help="Output directory for generated HTML",
    )
    parser.add_argument(
        "--no-debug",
        action="store_true",
        help="Omit -g -O0 (less useful objdump -S output)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove artifacts and site dirs before building",
    )
    args = parser.parse_args()

    project_root = Path.cwd().resolve()
    programs_root = (project_root / args.programs_root).resolve()
    artifacts_root = (project_root / args.artifacts_dir).resolve()
    site_dir = (project_root / args.site_dir).resolve()

    discovered = discover_programs(programs_root)
    if not discovered:
        print(
            f"error: no programs found under {programs_root.relative_to(project_root)}/*/src/*.cpp",
            file=sys.stderr,
        )
        return 1

    if args.programs:
        wanted = set(args.programs)
        unknown = wanted - {p.name for p in discovered}
        if unknown:
            print(
                f"error: unknown program(s): {', '.join(sorted(unknown))}",
                file=sys.stderr,
            )
            return 1
        programs = [p for p in discovered if p.name in wanted]
    else:
        programs = discovered

    if args.clean:
        for d in (artifacts_root, site_dir):
            if d.exists():
                shutil.rmtree(d)

    if not shutil.which("g++"):
        print("error: g++ not found on PATH", file=sys.stderr)
        return 1

    generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    all_program_names = [p.name for p in discovered]
    program_pages: dict[str, Path] = {}
    program_results: dict[str, list[Stage]] = {}
    failed_programs: list[str] = []

    for program in programs:
        out_dir = artifacts_root / program.name
        print(f"Program: {program.name}")
        print(f"  Sources: {', '.join(str(s.relative_to(project_root)) for s in program.sources)}")
        print(f"  Artifacts: {out_dir.relative_to(project_root)}")

        stages, _exe = build_pipeline(
            project_root,
            program,
            out_dir,
            args.std,
            debug=not args.no_debug,
        )
        program_results[program.name] = stages

        page = render_program_site(
            stages,
            program=program,
            project_root=project_root,
            out_dir=out_dir,
            site_dir=site_dir,
            all_programs=all_program_names,
            std=args.std,
            generated_at=generated_at,
        )
        program_pages[program.name] = page
        print(f"  Page: {page.relative_to(project_root)}")

        failed = [s for s in stages if not s.command.ok]
        if failed:
            failed_programs.append(program.name)
            print(
                f"  warning: failed stages: {', '.join(s.title for s in failed)}",
                file=sys.stderr,
            )

    index = render_index_site(
        programs=discovered,
        program_pages=program_pages,
        program_results=program_results,
        project_root=project_root,
        programs_root=programs_root,
        artifacts_root=artifacts_root,
        site_dir=site_dir,
        std=args.std,
        generated_at=generated_at,
    )
    print(f"Index: {index.relative_to(project_root)}")

    meta = {
        "programs_root": str(programs_root.relative_to(project_root)),
        "std": args.std,
        "artifacts_root": str(artifacts_root.relative_to(project_root)),
        "site_dir": str(site_dir.relative_to(project_root)),
        "generated_at": generated_at,
        "programs": [
            {
                "name": program.name,
                "sources": [str(s.relative_to(project_root)) for s in program.sources],
                "page": program_page_name(program.name),
                "artifacts_dir": str((artifacts_root / program.name).relative_to(project_root)),
                "stages": [s.id for s in program_results.get(program.name, [])],
                "ok": program.name not in failed_programs,
            }
            for program in discovered
        ],
    }
    (site_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

    if failed_programs:
        print(
            "warning: some programs had failed stages: "
            + ", ".join(failed_programs),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
