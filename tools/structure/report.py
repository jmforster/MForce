"""Render measurements as Markdown, and compare two snapshots."""
import json


def snapshot(files, limits, dup_blocks, violations):
    """Build the JSON-able snapshot the meter records."""
    return {
        "summary": {
            "files": len(files),
            "lines": sum(f["lines"] for f in files),
            "files_over_limit": sum(f["lines"] > limits["file_lines"] for f in files),
            "functions_over_limit": sum(
                1 for f in files for (_, _, n) in f["functions"] if n > limits["function_lines"]),
            "file_level_mutable_vars": sum(f["mutable_vars"] for f in files),
            "const_cast": sum(f["flags"]["const_cast"] for f in files),
            "dynamic_cast": sum(f["flags"]["dynamic_cast"] for f in files),
            "changelog_comments": sum(f["flags"]["changelog_comments"] for f in files),
            "duplicate_blocks": len(dup_blocks),
            "include_violations": len(violations),
            "unmapped_files": sum(f["module"] is None for f in files),
        },
        "files": {f["path"]: {
            "module": f["module"], "lines": f["lines"],
            "longest_function": max([(n, name) for (name, _, n) in f["functions"]] or [(0, "")]),
            "mutable_vars": f["mutable_vars"], **f["flags"],
            "types": f["types"],
        } for f in files},
        "include_violations": violations,
        "duplicate_blocks": [(n, places[:4]) for n, places in dup_blocks[:60]],
    }


def render(snap, limits):
    s = snap["summary"]
    out = ["# Structure meter", "",
           "| Measure | Value | Limit for new code |", "|---|---|---|",
           f"| Files | {s['files']} | |",
           f"| Lines | {s['lines']:,} | |",
           f"| Files over the file limit | {s['files_over_limit']} | {limits['file_lines']} lines |",
           f"| Functions over the function limit | {s['functions_over_limit']} | {limits['function_lines']} lines |",
           f"| File-level mutable variables | {s['file_level_mutable_vars']} | {limits['file_level_mutable_vars']} |",
           f"| `const_cast` lines | {s['const_cast']} | 0 new |",
           f"| `dynamic_cast` lines | {s['dynamic_cast']} | 0 new in engine |",
           f"| Changelog comment lines | {s['changelog_comments']} | 0 new |",
           f"| Duplicated blocks (token comparison) | {s['duplicate_blocks']} | 0 new |",
           f"| Include edges outside the map | {s['include_violations']} | 0 |",
           f"| Files on no module | {s['unmapped_files']} | 0 |", ""]
    big = sorted(((v["lines"], p) for p, v in snap["files"].items()), reverse=True)
    out += ["## Largest files", "", "| Lines | File | Longest function |", "|---|---|---|"]
    for n, p in big[:12]:
        lf = snap["files"][p]["longest_function"]
        out.append(f"| {n:,} | `{p}` | {lf[1]} ({lf[0]}) |")
    out += ["", "## Include edges outside the map", ""]
    out += [f"- `{v['file']}` ({v['module']}) includes `{v['include']}` ({v['target']})" for v in snap["include_violations"]] or ["- none"]
    out += ["", "## Largest duplicated blocks", ""]
    for n, places in snap["duplicate_blocks"][:15]:
        out.append(f"- {n} lines: " + ", ".join(f"`{p}:{s}-{e}`" for p, s, e in places))
    return "\n".join(out) + "\n"


def delta(old, new):
    """Markdown describing what changed between two snapshots."""
    out = ["# Structure delta", "", "| Measure | Before | After | Change |", "|---|---|---|---|"]
    for k in new["summary"]:
        a, b = old["summary"].get(k), new["summary"][k]
        if a is None:
            continue
        out.append(f"| {k} | {a:,} | {b:,} | {b - a:+,} |")
    of, nf = old["files"], new["files"]
    added = sorted(set(nf) - set(of))
    removed = sorted(set(of) - set(nf))
    out += ["", "## Files added", ""] + ([f"- `{p}` ({nf[p]['lines']} lines)" for p in added] or ["- none"])
    out += ["", "## Files removed", ""] + ([f"- `{p}`" for p in removed] or ["- none"])
    out += ["", "## Types added or removed", ""]
    rows = []
    for p in sorted(set(of) | set(nf)):
        a = set(of.get(p, {}).get("types", []))
        b = set(nf.get(p, {}).get("types", []))
        for t in sorted(b - a):
            rows.append(f"- NEW `{t}` in `{p}`")
        for t in sorted(a - b):
            rows.append(f"- REMOVED `{t}` from `{p}`")
    out += rows or ["- none"]
    out += ["", "## Files that grew or shrank", ""]
    rows = []
    for p in sorted(set(of) & set(nf)):
        d = nf[p]["lines"] - of[p]["lines"]
        if d:
            rows.append(f"- `{p}`: {of[p]['lines']} -> {nf[p]['lines']} ({d:+})")
    out += rows or ["- none"]
    return "\n".join(out) + "\n"


def dump(snap):
    return json.dumps(snap, indent=1, sort_keys=True)
