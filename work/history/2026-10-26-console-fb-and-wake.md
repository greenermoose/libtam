# Console framebuffer mapping and targeted atomic display blank/wake

Date: 2026-10-26
Tool: Google Antigravity
Model: Gemini 3.8 Flash

## Summary

Implemented console framebuffer discovery, geometry/stride verification against active DRM
framebuffers, individual connector blank and wake state transitions, and global text VT
blank/wake ioctls in `libtam-display` and `libtam-display-session` (display API bumped to `0.4.0`):

1. **Console framebuffer mapping (`tam_display_map_console_fb`)**:
   - Inspects `/sys/class/vtconsole/vtcon*` to find the bound frame buffer console device.
   - Attempts `FBIOGET_CON2FBMAP` ioctl on `/dev/fb*` to map the active VT number to its fb device index.
   - If direct ioctl is permission-restricted without ACLs, falls back to unambiguous single-fbcon sysfs topology under `/sys/class/graphics/fb*` while rejecting ambiguous multi-framebuffer systems.
   - Reads geometry (`width`, `height`, `stride`, `bits_per_pixel`) from sysfs and discovers the backing primary DRM card (`cardN`) via device symlink resolution.
   - Verifies against `DRM_IOCTL_MODE_GETFB2` properties stored on active DRM framebuffers during capture (`tam_display_state_verify_console_fb`).

2. **Targeted atomic blank and wake (`tam_display_state_blank`, `tam_display_state_wake`)**:
   - Extends atomic state transactions to support incremental connector detachment (`tam_display_state_blank`) and wake re-attachment (`tam_display_state_wake`) within an active session.
   - Safe atomic transaction updates preserving retained CRTC modes and plane framebuffers.
   - Added `tam_display_state_is_detached` query.

3. **Global console VT control (`tam_vt_blank`, `tam_vt_wake`, `tam_vt_idle_path`)**:
   - TIOCLINUX subcode 14 (`TIOCL_BLANKSCREEN`) and subcode 4 (`TIOCL_UNBLANKSCREEN`) scoped to the verified foreground Linux text VT.
   - Runtime console idle timer control with sysfs readback verification.

4. **Test coverage**:
   - ASan/UBSan unit test suites (`test_display`, `test_display_owned`, `test_vt`, `test_display_session`) covering console fb verification, incremental blank/wake, unplug during blanking, and TIOCLINUX ioctls.
