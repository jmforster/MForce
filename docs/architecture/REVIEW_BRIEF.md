# The independent reviewer's brief

The fixed brief of the refactor protocol (gates spec section 3.8). At the
end of each checkpoint a reviewer with a fresh context, which did not
write the code, is given the diff, the module map (`MODULES.md`) and the
signed design section, and told to refute. For each item below it
answers PASS, FAIL or NOT APPLICABLE, with a file and line as evidence
for every FAIL and for every PASS it could have failed. Its output goes
into the architecture report verbatim, followed by what was done about
each FAIL.

1. **Separation of concerns.** Does every new or changed file do one job?
   Does any file now mix drawing, audio, persistence and model?
2. **Single responsibility.** Can each class be described in one sentence
   without "and"?
3. **Dependency direction.** Any include against the map? Any cycle?
4. **Duplication.** Does anything in the diff repeat code that already
   exists anywhere in the tree?
5. **Encapsulation.** Public mutable state? Callers reaching into
   internals?
6. **Ownership.** Is the owner of every pointer and reference member
   clear? Any ownership cycle?
7. **Abstraction fit.** Does the code match the design Matt signed? Is
   each inheritance, composition, variant or template choice the one the
   design gave a reason for?
8. **Real-time rule.** Any allocation, lock, throw or unbounded work on a
   `next()` path?
9. **Names and comments.** Do names say what the thing is? Any changelog
   comment?
10. **Tests.** Is the behaviour in the diff covered by a test?

The reviewer refutes: it looks for the way each item fails, not for the
way it passes. A PASS without a named place it checked is not a PASS.
