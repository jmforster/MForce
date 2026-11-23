"""The rules, evaluated in one place: which includes are outside the map,
and what exceeds a limit. Takes measurements in, returns findings."""

# A header is named as mforce/<module>/..., never by the folder that holds it:
# an include spelled through a module's include/ or src/ folder reaches past
# the compiler's module boundary (a test program has engine/ on its path).
LONG_PATH_MARKERS = ("/include/mforce/", "/src/")


def include_violations(path, module, incs, module_map):
    """Includes in `path` (owned by `module`) that the map does not allow,
    and includes that name a header by its folder path."""
    out = []
    for inc in incs:
        if any(m in "/" + inc.replace("\\", "/") for m in LONG_PATH_MARKERS):
            out.append({"file": path, "module": module, "include": inc, "target": "long-path"})
            continue
        target = module_map.include_target(inc)
        if target and not module_map.may_include(module, target, path):
            out.append({"file": path, "module": module, "include": inc, "target": target})
    return out


def over_limits(files, limits):
    """Counts of files and functions over their limits."""
    return {
        "files_over_limit": sum(f["lines"] > limits["file_lines"] for f in files),
        "functions_over_limit": sum(
            1 for f in files for (_, _, n) in f["functions"] if n > limits["function_lines"]),
    }
