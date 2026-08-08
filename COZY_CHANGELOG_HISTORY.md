# Cozy changelog

## 0.0.3 — the namespace law

### Added
- **Design entry 7: records are the module system.** No module kind, no
  import syntax — package namespaces are records plus the reflection
  pair, with dist.nu as the production precedent. The entry records the
  interpreter-verified law that shapes it: globals resolve at call time,
  so pack-then-prune breaks packages (helpers stay global; hide them by
  convention), while the same late binding grants free mutual recursion
  through the record's own name. Neutrino itself stays flat-plus-
  subrecords by explicit anti-decision (transcript churn buys nothing).

## 0.0.2 — the re-sync

### Added
- **Everything Neutrino learned since the fork.** Cozy 0.0.1 froze at
  Neutrino 2.13.1; this release re-syncs to 2.19.2, inheriting by the
  conformance contract: the symb parser and textbook printer with
  tofun/ffun/dfun (string in, function out), the demo tour package,
  elseif/eval/names/input/pause (the five owner-sanctioned additions —
  eval("r." + name) is the reflection wall's first door), the lit svg
  legend, the vignette plates and og-card, the two-workstation deploy
  (merge -s ours), and the PLAYBOOK's verdict law. 905 goldens, 159
  builtins, 113 + 99 + 318 verified transcripts — all green under the
  Cozy name before any Cozy-original code exists.
- **strfind remains the recorded string gap** (indexing and slicing were
  inherited all along); the reflection pair getfield/setfield remains
  design entry 5, with eval as the interim read-half.

## 0.0.1 — the fork

### Added
- **Cozy exists.** The full Neutrino 2.13.1 tree forked under the Cozy
  name, per the charter's founding task: binary renamed (cozy), REPL
  prompt (cozy> ), a new banner (the mug — the machine that handles the
  particles gently), --version, history file (.cozy_history), and the
  workbench page retitled. Version 0.0.1.
- **The founding documents merged into the repository**: CHARTER.md at
  the root, design/DESIGN_NOTES.md (six designs: sparse, LAPACK backend
  tiers, optimization, the jet family, record reflection, the type
  inventory — every one carrying a Neutrino friction receipt), and
  heritage/ (PLAYBOOK, LESSONS, KNOWN_LIMITATIONS, the Neutrino README).
- **Conformance inherited whole**: the complete golden suite (895),
  the three verified books (105 + 92 + 304 transcripts), doclint, the
  generators, ASan — all green under the Cozy name before any new code.
  Deliberately unrenamed: NEUTRINO_* environment variables and the
  version.h macro names (lift compatibility; a recorded task), and every
  heritage document verbatim.
