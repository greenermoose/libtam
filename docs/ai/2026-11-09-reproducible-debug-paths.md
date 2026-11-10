# Build-directory-independent debug information (libtam 0.5.1)

- **Date:** 2026-11-09
- **Stage:** Develop: a reproducibility fix found while building bundles
  with `tam-bundle build`.
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred chose to fix both `libtam` and `tam-bundle build`.
  - Claude wrote the change and this record.
- **Design:** [`tam-bundle` design](https://github.com/greenermoose/tamlinux-tools/blob/develop/docs/plans/tam-bundle.md),
  "a component that fails is fixed rather than excused".

## Guiding prompts

> Option 3, both

## What changed

- **`Makefile`:** `-ffile-prefix-map=$(CURDIR)=.` joins `-g`, so debug
  information names source files relative to the source tree instead of
  the directory it was built in. Before, a program that links `libtam`
  statically carried `libtam`'s build directory, so the same commit built
  in two places gave two different programs.
- **Version:** 0.5.0 to 0.5.1 (a fix).

## Verification

- `make test` passes.
- `libtam.a` and `libtam-portal.a` built from the same tree in two
  different directories are byte-identical, and neither contains the build
  directory.
