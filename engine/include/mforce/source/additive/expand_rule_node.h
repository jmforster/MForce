#pragma once
// ExpandRuleNode: the ExpandRule as a node.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/expand_rule.h"

namespace mforce {

// ExpandRuleNode — the ExpandRule as a node, so a patch can declare one at
// the top level and reference it from a Partials host via { "ref": "..." }.
// A part, like Formant: a parametric building block a host reads, with no
// signal of its own. The loader snapshots its fields into the host with
// to_struct() when the host is built.
struct ExpandRuleNode final : Component {
  // The host reads the pins with to_struct(); nothing pulls them per sample,
  // so all ten are Manual and this node never prepares or advances them.
  Param spacing1{this, "spacing1", 0.5f,  0.0f, 24.0f, "semis",  Pull::Manual};
  Param spacing2{this, "spacing2", 0.5f,  0.0f, 24.0f, "semis",  Pull::Manual};
  Param dt1     {this, "dt1",      0.01f, 0.0f, 1.0f,  "0-1",    Pull::Manual};
  Param dt2     {this, "dt2",      0.01f, 0.0f, 1.0f,  "0-1",    Pull::Manual};
  Param loPct1  {this, "loPct1",   0.1f,  0.0f, 1.0f,  "0-1",    Pull::Manual};
  Param loPct2  {this, "loPct2",   0.1f,  0.0f, 1.0f,  "0-1",    Pull::Manual};
  Param power1  {this, "power1",   1.0f,  0.0f, 10.0f, "",       Pull::Manual};
  Param power2  {this, "power2",   1.0f,  0.0f, 10.0f, "",       Pull::Manual};
  Param po1     {this, "po1",      0.0f,  0.0f, 1.0f,  "cycles", Pull::Manual};
  Param po2     {this, "po2",      0.0f,  0.0f, 1.0f,  "cycles", Pull::Manual};
  Setting<int> count  {this, "count",   2, 0, 16};
  Setting<int> recurse{this, "recurse", 0, 0, 4};

  // Snapshot to a plain ExpandRule for consumption by Partials. Reads the
  // current() of each field — at load time these are ConstantSource values,
  // at start time they could be the current value of a wired envelope.
  ExpandRule to_struct() const {
    ExpandRule r;
    r.count = count.value;
    r.recurse = recurse.value;
    r.spacing1 = spacing1.current();
    r.spacing2 = spacing2.current();
    r.dt1 = dt1.current();
    r.dt2 = dt2.current();
    r.loPct1 = loPct1.current();
    r.loPct2 = loPct2.current();
    r.power1 = power1.current();
    r.power2 = power2.current();
    r.po1 = po1.current();
    r.po2 = po2.current();
    return r;
  }

  const char* type_name() const override { return "ExpandRule"; }
};

} // namespace mforce
