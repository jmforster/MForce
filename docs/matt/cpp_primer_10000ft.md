# C++ at 10,000 feet, for a Java/C# designer

Written 2026-10-04. Purpose: let you read MForce's C++ and judge its design,
not write it. Every example is from this repo, with the line to look at.

The short version: the design vocabulary you already have (inheritance,
composition, interfaces, separation of concerns) carries over unchanged.
What changes is (1) who owns an object and when it dies, (2) how files and
the build work, and (3) how little the language catches for you.

---

## 1. Ownership: the one real difference

In Java and C# every object lives on the heap, every variable is a
reference, and the garbage collector decides when things die. In C++ a
variable **is** the object. `Pitch a = b;` copies the whole object, the
way a C# `struct` does. An object dies at a known moment: when its scope
ends, or when its owner dies. There is no garbage collector.

So every time one object holds another, the code has to say how:

| Written as | Meaning | Nearest thing you know |
|---|---|---|
| `Foo f;` (a member) | The Foo lives inside me and dies with me. | A C# `struct` field |
| `std::unique_ptr<Foo>` | Heap object, I am its only owner, it dies with me. | A private reference nobody else holds |
| `std::shared_ptr<Foo>` | Shared owner, reference-counted; dies when the last holder lets go. | An ordinary Java/C# reference |
| `Foo&` or `Foo*` | I am borrowing it. Someone else owns it. | No equivalent: a reference that can outlive its target |

In MForce every pin is a `std::shared_ptr<ValueSource>`
([dsp_wave_source.h:101](../../engine/include/mforce/core/dsp_wave_source.h)).
That is the right choice for the graph: one LFO can feed many pins, so no
single consumer owns it.

The cost: reference counting has no cycle collector. Two nodes that hold
each other never die. The review claims feedback-loop patches leak for
this reason (finding F263, not yet verified).

**When reviewing:** for every pointer or reference member, the report
should be able to say who owns the thing it points at.

## 2. Destructors (RAII): `using`, built into every object

A C++ object's destructor runs at the exact moment it dies, and that is
where resources are released. It is C#'s `using` / `IDisposable` or Java's
try-with-resources, except automatic and for every object.

Why it matters here: freeing memory takes time and may take a lock, so it
must not happen on the audio thread. That is why the UI hands dead voices
to a clean-up step on the UI thread instead of letting them die in the
audio callback.

## 3. Classes, inheritance, interfaces: same ideas, different defaults

- `struct` and `class` are the same thing. The only difference is the
  default visibility (`struct` starts public). MForce uses `struct`
  almost everywhere.
- There is no `interface` keyword. An interface is a class whose methods
  are all "pure virtual": `virtual float next() = 0;`. The `= 0` means
  abstract.
- `virtual` is opt-in, as in C# and unlike Java. `override` means what it
  means in C#. `final` is `sealed`.

**Exhibit A: ValueSource**
([dsp_value_source.h:75](../../engine/include/mforce/core/dsp_value_source.h)).
An abstract base with three hot methods (`prepare`, `next`, `current`)
and a set of optional self-description methods that have default bodies.

**WaveSource**
([dsp_wave_source.h:9](../../engine/include/mforce/core/dsp_wave_source.h))
shows both tools in one class:

- *Inheritance, as the Template Method pattern.* `next()` (lines 30-63)
  does the work every oscillator shares: advance the pins, run the phase
  accumulator. It then calls `compute_wave_value()` (line 98), which is
  `protected` and pure virtual. Each oscillator supplies only that one
  function.
- *Composition.* A WaveSource **is a** ValueSource and **has** three
  ValueSources: amplitude, frequency and phase (lines 101-103).

Which to use where is a design decision, exactly as in Java or C#. The
language does not make it for you.

**Multiple inheritance** exists. MForce uses it only in the "base class
plus interface" form: `struct Partials : ValueSource, IPartials`
([partials.h:266](../../engine/include/mforce/source/additive/partials.h)),
which is `class Partials : ValueSource, IPartials` in C#.

Two traps that Java and C# do not have:

- **Virtual destructor.** A base class needs `virtual ~Base()` (line 76 of
  dsp_value_source.h). Without it, destroying a derived object through a
  base pointer is a bug.
- **Slicing.** Assigning a derived object to a base *value* copies only
  the base part. This is why polymorphic objects are always held through
  a pointer.

**Cost.** A virtual call is one extra indirection. That is irrelevant in a
UI and measurable at 48,000 calls per second per node. Look at lines
33-47 of dsp_wave_source.h: each pin costs two virtual calls per sample
(`next()`, then `current()`). That is a reason to know the cost, not a
reason to avoid inheritance.

## 4. Two tools C++ adds to that choice

**Templates** look like generics but are resolved entirely at compile
time: the compiler writes a separate copy of the code for each type, with
no virtual call. C# generics over structs are the closest thing; Java
generics are not. Use them when the variation is known at compile time
and the call sits in a hot loop. The cost is that template code lives in
headers, compiles slowly and produces bad error messages.

**`std::variant<A, B, C>`** is a value that holds exactly one of a fixed
list of types. The closest thing is a Java sealed interface with a
pattern `switch`. MForce uses it for `Articulation` and `Ornament`
([basics.h:51 and 92](../../engine/include/mforce/music/basics.h)).

The design rule for choosing between a virtual base and a variant:

- **Open set of types, fixed set of operations: virtual base.**
  ValueSource: anyone can add a node type; every node answers the same
  few calls.
- **Closed set of types, growing set of operations: variant.** Ornaments:
  the list is fixed, and the conductor, the serializer and the printer
  each handle every case. The compiler flags a missed one.

## 5. `const`: a promise the compiler checks

Java's `final` and C#'s `readonly` protect the variable. C++ `const`
protects the object.

- `float current() const` means calling it cannot change the object.
- `const RenderContext& ctx` means "borrowed, read-only, not copied".

`const_cast` removes the promise. The review found five in
`Composer::compose()`: the signature says "I will not touch your
template" and the body writes to it. In a review, every `const_cast` is a
finding until explained.

## 6. Files and the build: where C++ gives you nothing

This section is the one that explains `main.cpp`.

- There are no packages, no assemblies, no `internal`, and no
  one-public-class-per-file rule.
- `#include` pastes the text of another file. Each `.cpp` is compiled
  alone (a "translation unit") and the results are linked.
- A header (`.h`) is what other files may see; a source file (`.cpp`)
  holds the bodies. MForce's engine keeps most bodies in headers. That is
  legal and convenient, but every file that includes a header recompiles
  it. The review measured about 95 seconds to compile the CLI's one
  source file.
- `namespace mforce` is naming only. It controls no access.
- `static` on a variable at file level means "a global, private to this
  file". The UI file has 349 of them.

**Consequence.** Nothing in the language objects to a 14,658-line file.
Java would have forced dozens of files; a C# solution would have had
projects and `internal`. In C++ the only boundary the toolchain enforces
is the **library target**. MForce has exactly one: `mforce_engine` is a
library with its own public include directory
([engine/CMakeLists.txt:1 and 17](../../engine/CMakeLists.txt)), and the
tools link against it. That boundary held: I checked today, and no engine
file includes anything from the tools or from the UI libraries. The UI is
one executable with one source file, so it has no internal boundary of
any kind.

Structure in C++ is therefore convention plus tooling. That is why the
protocol uses a budget script, and why the UI's modules may become
separate library targets, which is the closest C++ gets to assemblies.

## 7. What the language will not catch

The JVM and the CLR throw on these. C++ does not.

| Mistake | Java/C# | C++ | In the review |
|---|---|---|---|
| Index out of bounds | Exception | Reads or writes whatever memory is there | F041: DelayFilter can index a negative position |
| Uninitialised field | Zero or null | Garbage value | F049 |
| Signed integer overflow | Wraps | Undefined | The chord-voicing loop that spins to overflow |
| Null or dead pointer | Exception | Crash, or silent corruption | The audition buffer resize (F388) |

The term is "undefined behaviour": the compiler is allowed to assume it
never happens. So code that appears to work proves less in C++ than you
are used to. Tests, warnings-as-errors and sanitizer runs do the job the
runtime does for you in Java and C#.

## 8. The real-time rule in C++ terms

The audio callback runs about every 10 ms. Anything with unbounded time
inside it is a dropout. In C++ that means none of these on a `next()`
path:

- heap allocation (`new`, `make_shared`, a `vector` growing);
- a mutex;
- `throw`.

Line 38 of dsp_wave_source.h is a `throw` inside `next()`. It compiles,
and with no handler on the audio thread it ends the process. The compiler
MForce uses cannot check "this function must not allocate", which is why
campaign 1 adds a test that counts allocations.

`std::atomic<float>` is the lock-free way to pass a value between threads
(C#'s `Interlocked` / `volatile`). It is how the mod wheel and pressure
reach the audio thread.

## 9. Vocabulary you will meet

| C++ | C# / Java | Example |
|---|---|---|
| `std::vector<T>` | `List<T>` / `ArrayList<T>` | everywhere |
| `std::span<const T>` | `ReadOnlySpan<T>` | descriptor tables, dsp_wave_source.h:75 |
| `std::string_view` | `ReadOnlySpan<char>` | pin names in `set_param` |
| `std::optional<T>` | `Nullable<T>` / `Optional<T>` | template fields |
| `std::unordered_map<K,V>` | `Dictionary` / `HashMap` | registries |
| `auto` | `var` | |
| `enum class` | `enum` | `SourceCategory`, dsp_value_source.h:15 |
| `std::make_shared<T>(...)` | `new T(...)` | dsp_wave_source.h:12 |
| `std::move(x)` | none: "hand over the contents instead of copying" | dsp_wave_source.h:16 |
| `static constexpr` | `const` / `static readonly` | dsp_wave_source.h:76 |
| `explicit` constructor | "no silent conversion to this type" | dsp_wave_source.h:10 |
| `freq_` (trailing underscore) | private-field naming convention | |
| `#pragma once` | "include this header only once" | top of every header |
| no reflection | hence explicit registries | `SourceRegistry` |

## 10. Judging a C++ class in five minutes

1. **Read only the header.** Can you say what the class is for in one
   sentence? If not, it has more than one job.
2. **Read the `#include` lines.** They are its dependencies. A UI class
   that includes audio-driver and JSON headers is mixing concerns.
3. **Read the members.** For each pointer or reference: who owns it?
4. **Find the `virtual` functions.** They are the extension points. Are
   they the ones you would have chosen?
5. **Public versus private.** Can outside code reach the state directly?
6. **Size.** A header over a few hundred lines is doing too much.
7. **Red-flag words.** `const_cast`, `dynamic_cast`, file-level `static`
   variables, `throw` on a render path. Each one is a question to ask.
