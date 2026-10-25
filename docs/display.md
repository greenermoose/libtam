# Experimental Linux display inspection

`tam_display.h` / `libtam-display.a` API 0.1.0 starts the native console
monitor feasibility work. It reads an authorized DRM primary descriptor,
enumerates connectors, CRTCs, planes and their property IDs/values, and
analyzes the routing needed to retain one active output. It does not acquire
devices or perform modesets. There is no runtime dependency beyond libc.

```sh
make display
make test-display
make install-display PREFIX=/your/development/prefix
make uninstall-display PREFIX=/your/development/prefix
```

Build-time Linux DRM UAPI headers are discovered through
`pkg-config --cflags-only-I libdrm`; `DISPLAY_CFLAGS` can supply another
installed-header location. No libdrm functions are linked. Core-only
`make core install-core` also avoids the portal and D-Bus development files.
Normal `make` and `make install` retain the existing core/portal contract;
display installation is explicit during feasibility.

`tam_display_inspect_fd` borrows the caller's descriptor and enables atomic
and universal-plane visibility on that file description. The caller must
provide legitimate device/session access and account for this capability
change. The library never opens a card, calls SET_MASTER, changes permissions,
or steals a seat controller. In particular, opening a primary device can
implicitly acquire master; device acquisition needs its own reviewed adapter.
The library does not establish foreground-VT ownership.

Object and property arrays have fixed limits of 128. Kernel count changes
and interrupted ioctls have four-attempt limits. Connector reads supply one
mode slot to avoid the count-zero forced probe. Hardware-supplied strings
are bounded and sanitized. Allocation is returned only after all queries
succeed; free it with `free()`. Errors include a result code, saved errno
and bounded explanation. Legacy drivers lacking atomic client support fail
explicitly. The observation is not atomic and must never be replayed.

`tam_display_plan_keep` is pure analysis on a single device observation.
It rejects ambiguous/disconnected/inactive selections, inconsistent routing
and invalid counts. It detaches other routed connectors while leaving a
shared retained CRTC and its planes alone. Distinct CRTCs that happen to
share a framebuffer remain distinct. Single-output observations produce
no disable operations. The result is not a kernel-validated request.

Queries expose mode dimensions and property values, but retain neither mode
blobs nor framebuffer ownership. This is **not a restorable snapshot**.
Remaining work includes VT/session acquisition, owned framebuffer/blob
references, atomic TEST_ONLY and commit/compensation, bounded independent
recovery, hotplug/session lifetime and physical fbcon/standby proof.
No write API is exposed before those gates are addressed.

`make test-display` instruments the display objects themselves with ASan
and UBSan. Fake ioctls exercise routing, malformed counts, changing resources,
properties and planes, unsupported atomic capability, device disappearance,
bounded interruption and permission failure at every query. Tests reject
any ioctl outside the inspection allowlist. These tests do not prove the
target GPU, physical standby or recovery.
