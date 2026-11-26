# Code taken from other projects

This file lists every piece of code libtam takes from another project.
An idea taken without its code goes in `ideas/`, not here.

## tam-view document source, layout and renderers

- **Project:** tam-view (https://github.com/greenermoose/tam-view)
- **License:** GNU General Public License, version 3 or later
- **Source:** `src/source.c`, `src/layout.c`, `src/render.c`,
  `src/render_md.c`, `src/render_html.c`, `src/render_mermaid.c`,
  `include/render_internal.h`, the renderer part of `include/view.h`, and
  `tests/test-layout.c` and `tests/test-render.c`, at commit `3444bb1`
- **Here:** `src/render/`, `include/tam_render.h`, `tests/test_render.c`
  and `tests/test_render_layout.c`
- **Changes:** Moved, not copied: tam-view and tam-knox now link
  `libtam-render.a`. Public names gained the `tam_source_`,
  `tam_position_` and `tam_render_` prefixes; internal ones `tam__`.
- **Taken:** 2026-11-26

## How to record a piece

One section per piece, newest last:

    ## <what it is>

    - **Project:** name and URL
    - **License:** the project's license
    - **Source:** file path in the project, and the commit it came from
    - **Here:** the file or files it now lives in
    - **Changes:** how it was rewritten or reshaped, and why
    - **Taken:** YYYY-MM-DD

Add the project's copyright notice to `COPYRIGHT` in the same commit.
When upstream fixes a bug in code we took, check whether our copy has
the same bug, and record what we did in `bugs/`.
