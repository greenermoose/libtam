# File bundle primitives (libtam 0.5.0)

- **Date:** 2026-11-09
- **Stage:** Develop: the `libtam` additions the approved `tam-bundle`
  design names, before `tam-bundle` itself.
- **CLI Tool:** Claude Code 2.1.321
- **Model:** Claude Opus 5.5 (`claude-opus-5-5`)
- **Transcript:** Retained privately by the author.
- **Commits:** the commit that adds this record.
- **Authorship:**
  - Fred approved the `tam-bundle` design and asked for these additions.
  - Claude wrote the code, the tests and this record.
- **Design:** [`tam-bundle` design](https://github.com/greenermoose/tamlinux-tools/blob/develop/docs/plans/tam-bundle.md),
  section "libtam additions".

## Guiding prompts

> yes, start the libtam additions

## What changed

- **`tam_sha256`:** SHA-256 from FIPS 180-4, written for this library;
  incremental, whole file (`tam_sha256_fd`, `tam_sha256_file`, which
  refuses a final symbolic link) and hex.
- **`tam_fs_walk`:** a descriptor-based walk; entries sorted bytewise per
  directory, a directory before its contents, links reported and never
  followed, optional refusal to enter another filesystem, and a check that
  a directory was not replaced between its stat and its open.
- **`tam_rec`:** a strict line-record reader (a `format N` first record,
  a required final newline so truncation is caught, no control characters
  or NUL) and a writer that refuses any field it could not read back.
- **`tam_fs_tree`:**
  - `tam_fs_exchange`, through the raw `renameat2` system call so it also
    builds against C libraries without the wrapper;
  - `tam_fs_clone_or_copy`: `FICLONE`, then `copy_file_range`, then read
    and write;
  - `tam_fs_remove_tree`: no link following, stops with `EXDEV` at another
    filesystem;
  - `tam_fs_lock` and `tam_fs_unlock`: `flock` with a monotonic timeout.
- **`tam_fs`:** `tam_fs_write_atomic` now also syncs the directory, checks
  `fchmod`, `fsync` and `close` results, retries interrupted writes and
  accepts empty contents; new `tam_fs_sync_dir` and `tam_fs_sync_dirat`.
- **`tam_path_is_safe_rel`:** the relative paths a record may name.
- Version 0.4.0 to 0.5.0 (new features).

## Verification

- `make test` passes, with the new `test_tam_files` built with address and
  undefined-behaviour sanitizers.
- `test_tam_files` passed on tmpfs, on Btrfs (files cloned) and on ext4
  (files copied), using `TAM_TEST_DIR`.
- SHA-256 matches the FIPS 180-2 example vectors, including one million
  `a` fed in uneven pieces, and byte-at-a-time hashing matches one-shot
  hashing for every length up to 200.
- `tam` and `tamlinux-tools` at their current commits build and pass their
  tests against this version, installed to a temporary prefix.
- Not tested: `tam_fs_remove_tree` meeting a mount point or Btrfs
  subvolume (needs a mount or a subvolume the test could not clean up
  without privilege).
