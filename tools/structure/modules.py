"""The module map: which module owns a file, and what it may include."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP_PATH = Path(__file__).resolve().parent / "modules.json"


class ModuleMap:
    def __init__(self, data):
        self.third_party = data["third_party"]
        self.modules = data["modules"]
        self.by_name = {m["name"]: m for m in self.modules}

    @classmethod
    def load(cls, path=MAP_PATH):
        return cls(json.loads(Path(path).read_text(encoding="utf-8")))

    def owner(self, rel_path):
        """Module name owning a repo-relative path, or None."""
        rel = rel_path.replace("\\", "/")
        best = None
        for m in self.modules:
            for p in m["paths"]:
                if rel == p or (p.endswith("/") and rel.startswith(p)):
                    if best is None or len(p) > best[1]:
                        best = (m["name"], len(p))
        return best[0] if best else None

    def include_target(self, inc):
        """Module or third-party family an #include refers to; None if it
        is a system or unknown header."""
        if inc.startswith("mforce/"):
            parts = inc.split("/")
            return parts[1] if len(parts) > 2 else None
        low = inc.lower()
        for fam, needles in self.third_party.items():
            if any(n in low for n in needles):
                return fam
        return None

    def may_include(self, module, target):
        if module is None or target is None or module == target:
            return True
        m = self.by_name.get(module)
        return m is not None and target in m["may_include"]

    def render_markdown(self):
        out = ["# Modules", "",
               "Generated from `tools/structure/modules.json` by `tools/structure/check.py`.",
               "Phase 1: describes today's layout. Do not edit by hand.", "",
               "| Module | Purpose | Paths | May include |", "|---|---|---|---|"]
        for m in self.modules:
            out.append(f"| {m['name']} | {m['purpose']} | {', '.join('`' + p + '`' for p in m['paths'])} | "
                       f"{', '.join(m['may_include']) or 'nothing'} |")
        out += ["", "Third-party families: " +
                ", ".join(f"`{k}`" for k in self.third_party), ""]
        return "\n".join(out)
