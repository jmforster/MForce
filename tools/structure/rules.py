"""The rules, evaluated in one place: which includes are outside the map,
and what exceeds a limit. Takes measurements in, returns findings."""


def include_violations(path, module, incs, module_map):
    """Includes in `path` (owned by `module`) that the map does not allow."""
    out = []
    for inc in incs:
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
