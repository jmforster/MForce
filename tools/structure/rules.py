"""The rules, evaluated in one place: which includes are outside the map,
and what exceeds a limit. Takes measurements in, returns findings."""

import re

# A header is named as mforce/<module>/..., never by the folder that holds it:
# an include spelled through a module's include/ or src/ folder, or into
# third_party/, reaches past the compiler's module boundary (a test program
# has engine/ on its path). "<module>/tests/fixtures.h" is how a test program
# reaches a lower module's shared test helpers, and stays allowed.
LONG_PATH_MARKERS = ("/include/mforce/", "/src/")


def normalise_include(inc):
    """The include text as the long-path rule reads it: forward slashes, no
    `./` segments, no repeated slashes, lower case."""
    s = inc.replace("\\", "/").lower()
    s = re.sub(r"/+", "/", s)
    while True:
        shorter = re.sub(r"(^|/)\./", r"\1", s)
        if shorter == s:
            return s
        s = shorter


def is_long_path(inc, module_names):
    """Whether an include names a header by a folder path: through a
    module's include/ or src/ folder, or into third_party/."""
    s = normalise_include(inc)
    parts = s.split("/")
    if any(m in "/" + s for m in LONG_PATH_MARKERS) or parts[0] == "third_party":
        return True
    return len(parts) > 2 and parts[0] in module_names and parts[1] in ("include", "src")


def include_violations(path, module, incs, module_map):
    """Includes in `path` (owned by `module`) that the map does not allow,
    and includes that name a header by its folder path."""
    out = []
    names = {m.lower() for m in module_map.by_name}
    for inc in incs:
        if is_long_path(inc, names):
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
