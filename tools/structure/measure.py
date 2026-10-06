"""Turn source text into numbers. Pure functions over text, plus two that
call lizard (function lengths read a file; token duplicates run lizard as
a subprocess). Every regex here is the definition of the number it feeds."""
import re
import subprocess
import sys

import lizard

_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
# A file-level `static` declaration that is not const and completes on its
# line (`;` or `=` present), so a wrapped function return type is not one.
_STATIC_VAR = re.compile(r'^static\s+(?!const\b|constexpr\b|inline\s+const|void\b)(?=[^(]*[=;])')
# The UI's `g_` / `s_` globals declared at file level without `static`.
_GLOBAL_VAR = re.compile(
    r'^(?!static\b|//|#|using\b|typedef\b|extern\b|return\b|namespace\b|struct\b|'
    r'class\b|enum\b|template\b|inline\b|const\b|constexpr\b|friend\b)'
    r'[A-Za-z_][\w:<>,\*&\s]*?\b(g_|s_)\w+\s*(=|;|\{|\[)')
_CHANGELOG = re.compile(r'//.*(20\d\d-\d\d-\d\d|\bbacklog\s+#?\d|\bREVIEW\s+\d)')
_TYPE_DECL = re.compile(r'^(?:struct|class|enum class|enum)\s+([A-Za-z_]\w*)\b(?!\s*;)')
_DUP_SPAN = re.compile(r'^(.*):(\d+) ~ (\d+)$')


def lines_of(text):
    """The file's lines, counted as `wc -l` counts them: a trailing newline
    ends the last line rather than starting an empty one."""
    text = text.replace("\r\n", "\n")
    if text.endswith("\n"):
        text = text[:-1]
    return text.split("\n")


def includes(text):
    """Every #include target, in order."""
    return [m.group(1) for m in map(_INCLUDE.match, lines_of(text)) if m]


def file_level_mutable_vars(text):
    """Variables declared at file level that are not const: `static int x;`
    and the `g_` / `s_` globals the UI uses. A heuristic, written down so
    its number means one thing. Known misses: a pointer-to-const
    (`static const char* p`) is skipped; an unprefixed non-static global
    is not counted."""
    n = 0
    for line in lines_of(text):
        if _STATIC_VAR.match(line) or _GLOBAL_VAR.match(line):
            n += 1
    return n


def flag_counts(text):
    """Counts of constructs worth watching."""
    ls = lines_of(text)
    return {
        "const_cast": sum("const_cast" in l for l in ls),
        "dynamic_cast": sum("dynamic_cast" in l for l in ls),
        "changelog_comments": sum(bool(_CHANGELOG.search(l)) for l in ls),
    }


def type_declarations(text):
    """Names of struct/class/enum declared at column 0 (not forward
    declarations). Nested types are not listed."""
    return [m.group(1) for m in map(_TYPE_DECL.match, lines_of(text)) if m]


def function_lengths(path):
    """[(name, start_line, length)] via lizard."""
    info = lizard.analyze_file(path)
    return [(f.name, f.start_line, f.length) for f in info.function_list]


def parse_duplicates(lizard_output):
    """Parse lizard's -Eduplicate text into
    [(span_lines, [(path, start, end), ...])], largest first."""
    blocks, cur = [], None
    for line in lizard_output.split("\n"):
        if line.startswith("Duplicate block:"):
            cur = []
            blocks.append(cur)
        elif cur is not None and line.startswith("^^^"):
            cur = None
        elif cur is not None:
            m = _DUP_SPAN.match(line.strip())
            if m:
                cur.append((m.group(1).replace("\\", "/"), int(m.group(2)), int(m.group(3))))
    result = [(b[0][2] - b[0][1] + 1, b) for b in blocks if b]
    result.sort(key=lambda x: (-x[0], x[1]))
    return result


def token_duplicates(paths, root):
    """Duplicate blocks found by lizard's token-based detector over `paths`
    (relative to `root`). Token comparison catches copies that differ only
    by renamed variables, which a line comparison misses."""
    out = subprocess.run([sys.executable, "-m", "lizard", "-Eduplicate"] + list(paths),
                         cwd=root, capture_output=True, text=True).stdout
    return parse_duplicates(out)
