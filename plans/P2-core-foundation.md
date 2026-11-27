# P2: Core foundation (historical)

- **Status:** done
- **Versions:** 0.1.0 to 0.8.0

## Goal

Establish `libtam` as Tamlinux's static C foundation library with zero
dependencies beyond libc in `libtam.a`, separate optional archives for
D-Bus portal and experimental console display control, and shared
terminal/cultivar primitives.

## Versions

- **0.1.0–0.3.0 (done):** Core foundation (`tam_str`, `tam_exec`,
  `tam_fs`, `tam_path`, `tam_ini`, `tam_term`) and separate
  `libtam-portal.a`; switched to static-only archives.
- **0.4.0 (done):** Experimental `libtam-display.a` (DRM/KMS, fbcon,
  `tam_vt`) and `libtam-display-session.a` (libsystemd observer).
- **0.5.0–0.5.1 (done):** `tam_sha256`, `tam_walk`, `tam_rec`,
  `tam_fs_tree`, and reproducible debug paths.
- **0.6.0 (done):** Shared startup guard and repository-local defect
  tracking.
- **0.7.0–0.7.1 (done):** Terminal foundation (`tam_uni`, `tam_key`,
  `tam_tty`), `VERSION` file as source of truth, and plain-text READMEs.
- **0.8.0 (done):** Retired `tam_bundle_guard` and added `tam_cultivar`
  (`tam_install_pending`, `tam_cultivar_read_version`).

## Acceptance

All historical versions shipped and pass `make test` under ASan/UBSan.

## Dependencies

Core `libtam.a` depends only on libc; `libtam-portal.a` depends on
libdbus; optional display archives depend on libdrm/libsystemd.

## Risks

None (completed; recorded retrospectively to pair every roadmap phase
with a plan).

## Open points

None.
