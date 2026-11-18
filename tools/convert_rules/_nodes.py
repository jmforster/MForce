"""What the rules share about where a document keeps its nodes and how a
refusal names one."""


def node_container(doc):
    """Patches come in two shapes: instrument patches nest the graph under a
    "graph" key, NodeGraph-mode patches put nodes at top level. Returns the
    dict that OWNS the "nodes" list, so callers can read and replace it, or
    None when the document has neither; each caller decides what absence
    means."""
    g = doc.get("graph")
    if isinstance(g, dict) and isinstance(g.get("nodes"), list):
        return g
    if isinstance(doc.get("nodes"), list):
        return doc
    return None


def walk_node_lists(doc):
    """Every node object in the document's node list, in order, each followed
    by the nodes of any node list nested inside it (an object holding a
    "nodes" list, at any depth under the node), as (the list that holds the
    node, the node) pairs. Nothing when the document has no node list."""
    container = node_container(doc)
    if container is None:
        return []
    found = []

    def nested(o):
        if isinstance(o, dict):
            if isinstance(o.get("nodes"), list):
                walk_list(o["nodes"])
            for k, v in o.items():
                if k != "nodes":
                    nested(v)
        elif isinstance(o, list):
            for v in o:
                nested(v)

    def walk_list(nodes):
        for node in nodes:
            if isinstance(node, dict):
                found.append((nodes, node))
                for v in node.values():
                    nested(v)

    walk_list(container["nodes"])
    return found


def walk_nodes(doc):
    """The nodes of walk_node_lists, in its order."""
    return [node for _, node in walk_node_lists(doc)]


def node_label(node):
    """A node as a refusal or a note names it: its id quoted, or <no id>."""
    nid = node.get("id") if isinstance(node, dict) else None
    return "<no id>" if nid is None else "'%s'" % nid


def is_number(x):
    """A JSON number; a bool is not one."""
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def is_feed(x):
    """A feed object: exactly one key, "ref" or "tap", holding a string."""
    return (isinstance(x, dict) and len(x) == 1
            and next(iter(x)) in ("ref", "tap") and isinstance(next(iter(x.values())), str))
