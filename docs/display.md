# Experimental Linux console display control

`tam_display.h` / `libtam-display.a` API 0.4.0 provides DRM inspection,
connector routing analysis, console framebuffer mapping, owned restoration references,
targeted atomic blank/wake transactions and state queries. `tam_vt.h` verifies the caller's foreground text VT
and exposes TIOCLINUX blank/wake and console idle control.
`tam_display_session.h` / `libtam-display-session.a` is a separate optional
session/seat adapter. The display archive needs only libc; the adapter needs
libsystemd. Neither uses the portal archive or a libdrm runtime.

This is a software-tested feasibility implementation. It has not established
sustained fbcon rendering, physical monitor blanking/standby or reliable
hardware fallback after process/device loss. The approved
[console monitor plan](https://github.com/greenermoose/tamlinux/blob/develop/docs/plans/console-monitor-control.md)
keeps those acceptance gates open.

## Build and development package

```sh
# No systemd or portal development files needed for this archive.
make display test-display
make install-display PREFIX=/your/development/prefix

# Optional systemd session adapter; does not build the portal.
make session test-session
make install-session PREFIX=/your/development/prefix
make uninstall-session uninstall-display PREFIX=/your/development/prefix
```

Build-time DRM UAPI headers are found with
`pkg-config --cflags-only-I libdrm`; `DISPLAY_CFLAGS` can supply another
installed-header location. Normal `make` and `make install` preserve the
existing core/portal contract. Core-only builds/install remain available.
The optional installs include their headers, static archives and pkg-config
metadata. Compiler flags and header dependencies invalidate objects; clients
must also depend on the static archives. Symbols and debug information are
retained in development builds and installs.

## Acquisition and inspection

`tam_vt_open` resolves `/dev/tty` to an owned real Linux VT. It rejects PTYs,
foreign ownership, graphical mode, inactive VTs and background process groups.
`tam_vt_check` can use the foreground group captured before a recovery fork.
It never changes KD mode, keyboard mode, terminal settings or activates
another VT. The recovery worker stays in the controlling terminal's session
but has a separate process group. `tam_vt_claim` accepts only `VT_AUTO`, then
owns `VT_PROCESS` handoff with blocked SIGUSR1/SIGUSR2. Restore displays and
drop master before `tam_vt_release` acknowledges a pending switch and restores
the saved `VT_AUTO` mode. Recovery permits the shell to regain foreground
after UI death while requiring the same owned active text VT/session.

`tam_display_session_open` identifies the calling process's login session,
checks local/active/type `tty` and the same VT, and verifies the selected primary
DRM card's udev seat against the session. It opens only `/dev/dri/cardN`, with
existing permissions and no symlink following, verifies the opened node's
identity, and requests DRM master. SET_MASTER cannot evict an existing master;
occupied or unauthorized devices fail explicitly. No permissions are changed.

Logind is an observer, not the device/controller provider. Its session state,
removal and bus-owner changes terminate the selection. Method queries have
two-second timeouts. `TakeControl` was evaluated and rejected for ordinary text
VTs: systemd's [controller setup](https://github.com/systemd/systemd/blob/v262/src/login/logind-session.c#L1493)
enters KD_GRAPHICS and mutes the keyboard, and its
[VT reset](https://github.com/systemd/systemd/blob/v262/src/basic/terminal-util.c#L1654)
changes VT ownership on release. The adapter never invokes TakeControl,
TakeDevice, ReleaseControl or a privileged helper. Hosts without direct seat
permissions report permission denial; this prototype does not broaden access.

`tam_display_inspect_fd` remains a borrowed-descriptor observation API. It
only enables atomic/universal-plane visibility and queries state. Object
and property counts are capped at 128, changing counts/EINTR have bounded
retries, connector reads avoid an implicit forced probe, and strings are
sanitized. `free()` the returned observation. It is not a restorable snapshot.
`tam_display_plan_keep` remains pure analysis; a shared retained CRTC and its
planes stay enabled even when another connector is detached.

## Owned references and writes

`tam_display_capture_fd` duplicates an authorized primary descriptor, copies
atomic writable properties and retains the resources needed by restoration:

- Blob contents are copied into client-owned blobs, capped at 1 MiB each,
  4 MiB total and 256 unique references.
- `GETFB2` obtains GEM handles; `ADDFB2` registers owned framebuffer objects
  backed by the same buffers. Shared framebuffer references are deduplicated,
  and each unique returned GEM handle is closed once.
- Fence descriptors/pointers are not replayed. Unsupported transient or
  unknown object references refuse capture. Requests are capped at 4096
  property assignments.
- Non-destructive `CLOSEFB` support is proved with a never-attached clone
  before any display commit. Only that unattached probe may use `RMFB` for
  failure cleanup. Releasing real restoration references always uses
  `CLOSEFB`, so it cannot disable restored scanout planes.

The complete request using owned blob/framebuffer IDs must pass atomic
`TEST_ONLY` before capture succeeds. The caller must install an authorization
callback with `tam_display_state_set_access_check` and arm independent recovery
before a real write. Writes without that callback fail. The callback runs
immediately before every real commit, including restoration.

`tam_display_map_console_fb` inspects `/sys/class/vtconsole`, `/dev/fb*` via
`FBIOGET_CON2FBMAP`, and `/sys/class/graphics/fb*` to discover the bound `fbcon`
device, its geometry/stride, and its backing primary DRM card (`cardN`).
`tam_display_state_verify_console_fb` validates that the mapped geometry and stride
match the captured active DRM framebuffer reference on the primary card.

`tam_display_state_keep` re-enumerates, builds one complete single-device
request, tests it with `TEST_ONLY | ALLOW_MODESET`, rechecks the observation,
and commits synchronously. It only detaches the excluded connectors and
turns off their otherwise unused CRTCs/planes. Calling `tam_display_state_keep`
on an already modified state safely transitions the active output.
`tam_display_state_blank` detaches a specified list of active connectors while
preserving remaining routes. `tam_display_state_wake` restores a previously
detached connector to its captured route in a single atomic transaction.
`tam_display_state_is_detached` queries whether a connector is currently detached.
Unsupported selections do not fall back to whole-console blanking. Writes are never
retried on `EINTR`, which can follow an applied transaction. Restore after
any uncertain commit; a failed atomic test issues no display commit.

`tam_display_state_restore` re-enumerates the same retained device, checks
object/property/monitor identity and expected state, tests the owned restore
request and commits it. Recovery permits captured connectors to disconnect or
vanish, including EDID disappearance and narrow kernel detachment/disable
transitions on the lost output's unused pipeline. It restores all surviving
captured console outputs using their owned modes/framebuffers, detaches lost
outputs and disables only CRTCs/planes without a surviving route. Vanished
connector IDs are omitted from the request; shared CRTCs stay enabled.

New connectors, changed nonzero EDID identities, unknown connection state,
foreign routing and replacement CRTCs/planes still refuse stale replay. No
new monitor modes are invented. If no captured working output survives,
restoration fails without a modeset; the owner must release control and report
loss. After TEST_ONLY, a second enumeration must match the tested observation,
then authorization is checked immediately before the sole synchronous commit.
Further hotplug cancels recovery rather than retrying indefinitely. A successful
restore consumes the observation: capture afresh for another selection.
`tam_display_state_free` releases references and its duplicate descriptor;
it does not implicitly restore or claim successful recovery.

## Remaining gates and tests

The bounded command prototype uses a separate recovery process that owns its
own observation connection, VT handoff and snapshot. The UI owns no DRM fd. It uses a private
readiness handshake, deadline, signals, device events and kernel hotplug
notifications. UI death requests recovery; killing the recovery process is
reported as failure. No signal handler or fixture test establishes the
kernel's fallback if that process dies. Kernel ioctl hangs cannot be bounded
by a userspace timer alone.

Hotplug ends a trial and the software-tested recovery restores surviving
captured outputs, including after retained-output loss. Drivers that also
replace CRTCs/planes, introduce connectors or alter unrelated routing still
fail closed; physical recovery remains unproved. Actual console framebuffer mapping,
targeted blanking, per-output wake, persistent `keep --shell`, terminal UI parity
and resource measurements are implemented and software-tested in 0.5.5.
Physical multi-monitor hardware trial and physical fbcon/VT/desktop handoff remain open.
Do not install this prototype into a live session outside the recorded package Test route.

`make test-display test-session` instruments the library objects with
ASan/UBSan. Fake ioctl tests cover reference ownership/cleanup, shared CRTCs
and framebuffers, kernel test/write rejection, stale state, interrupted
commits, missing authorization, failed restore, limits and query failures.
Unplug fixtures cover all three retained choices and every disconnected/removed
connector subset, shared-pipeline preservation, no remaining output, changed
identity, topology races and authorization loss between test and write.
Fake VT/session tests verify foreground rejection, existing-permission acquisition,
seat mismatches, existing master, session loss and cleanup. They contact no display or real bus.
