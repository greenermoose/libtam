# Acknowledgements — libtam

Researched 2026-10-09. Thank you to the people whose software, designs, maintenance, testing and public reports make this work possible.

Names are ordered alphabetically by the displayed public name (case and accents ignored for sorting). A self-published profile name is used when available; otherwise the public handle or name in an upstream credit is retained. No private identities, locations, phone numbers or commit-email harvesting are included. Affiliations below are self-reported public profile fields or explicitly attributed project roles; they are not independently verified employment records. Contact links and emails are only those publicly offered by the person or their project.

This repository describes its code as GPLv3; consult [LICENSE](LICENSE) and individual source headers for version/option details. Copyright notices are attributed to works and their stated holders, not inferred from contributor counts. The Free Software Foundation copyright on a GPL/LGPL license document is not treated as ownership of the software. These thanks supplement, and do not replace, required license and source notices.

Each named entry identifies an authored component, a documented design influence, a specific change or public report, or responsibility for a foundation used by this repository. Contributor-roster membership alone is not enough for a named entry. Wider communities are credited collectively below.

No separately authored code clone or named third-party design source was established in the inspected repository. C runtime and kernel credits reflect foundations; the musl target is documented, not a claim of verified compatibility.

## People

| Public name and brief background / contribution | Public affiliation and contact |
| --- | --- |
| **Doug Lea** — Contributor to the GNU C Library’s memory allocator. The system C runtime provides interfaces used by libtam and tam; no source copying into either component is asserted. [Work, license and copyright](#glibc). [Evidence](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) | GNU C Library project [Public profile / project contact](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) |
| **Fred Horch (`greenermoose`)** — Tamlinux creator and maintainer. Directed this repository’s design, implementation, testing and upstream integration, including the separately documented AI-assisted work. [Evidence](https://github.com/greenermoose/tamlinux) | No affiliation stated in the inspected public profile/credit. [Public profile / project contact](https://github.com/greenermoose) |
| **Linus Torvalds (`torvalds`)** — Creator of Linux. Kernel drivers, /proc and /sys underpin the running workstation and hardware telemetry. [Work, license and copyright](#linux). [Evidence](https://www.kernel.org/) | Linux Foundation [Public profile / project contact](https://github.com/torvalds) |
| **Paul Eggert** — Contributor of mktime, fixes and GNU C Library stewardship. The system C runtime provides interfaces used by libtam and tam; no source copying into either component is asserted. [Work, license and copyright](#glibc). [Evidence](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) | GNU C Library project [Public profile / project contact](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) |
| **Per Bothner** — Contributor of libio, which implements stdio functions. The system C runtime provides interfaces used by libtam and tam; no source copying into either component is asserted. [Work, license and copyright](#glibc). [Evidence](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) | GNU C Library project [Public profile / project contact](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) |
| **Rich Felker (`richfelker`)** — Main author and maintainer of musl. libtam names musl as a lightweight C-runtime compatibility target. [Work, license and copyright](#musl). [Evidence](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT) | No affiliation stated in the inspected public profile/credit. [Public profile / project contact](https://github.com/richfelker) |
| **Richard Stallman** — GNU founder and original GCC author. The compiler toolchain turns libtam and tam’s C source into runnable programs. [Work, license and copyright](#gcc). [Evidence](https://gcc.gnu.org/onlinedocs/gcc/Contributors.html) | GNU project [Public profile / project contact](https://www.stallman.org/) |
| **Roland McGrath** — Original GNU C Library author and contributor. The system C runtime provides interfaces used by libtam and tam; no source copying into either component is asserted. [Work, license and copyright](#glibc). [Evidence](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) | GNU C Library project [Public profile / project contact](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) |
| **Torbjörn Granlund** — Contributor of optimized string functions. The system C runtime provides interfaces used by libtam and tam; no source copying into either component is asserted. [Work, license and copyright](#glibc). [Evidence](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) | GNU C Library project [Public profile / project contact](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) |
| **Ulrich Drepper** — Contributor across the GNU C Library, including threading, locale, string and formatted-I/O support. The system C runtime provides interfaces used by libtam and tam; no source copying into either component is asserted. [Work, license and copyright](#glibc). [Evidence](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) | GNU C Library project [Public profile / project contact](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) |
| **Wolfram Gloger** — Contributor to the GNU C Library’s memory allocator. The system C runtime provides interfaces used by libtam and tam; no source copying into either component is asserted. [Work, license and copyright](#glibc). [Evidence](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) | GNU C Library project [Public profile / project contact](https://sourceware.org/glibc/manual/latest/html_node/Contributors.html) |

## Works, licenses and stated copyright notices

The source links below are the authority for complete notices and exceptions. A short notice here is a reference, not a replacement license text. Names in a copyright notice are reproduced as the holder wrote them even when the person now uses a different public display name.

<a id="glibc"></a>
### GNU C Library

- **Connection:** System C runtime used when building/running libtam on the current workstation.
- **License:** LGPL-2.1-or-later and per-file notices. [License/source notices](https://sourceware.org/glibc/).
- **Stated copyright / limits:** Free Software Foundation and other source-file holders.
- **Source:** [Upstream project](https://sourceware.org/glibc/).

<a id="gcc"></a>
### GNU Compiler Collection

- **Connection:** C compiler toolchain used to build libtam and tam on the current workstation.
- **License:** GPL-3.0-or-later for compiler code; GCC Runtime Library Exception and per-file terms apply to runtime libraries. [License/source notices](https://gcc.gnu.org/onlinedocs/gcc/Copying.html).
- **Stated copyright / limits:** Free Software Foundation and other source-file holders; compiler and runtime-library terms are distinct.
- **Source:** [Upstream project](https://gcc.gnu.org/onlinedocs/gcc/Copying.html).

<a id="linux"></a>
### Linux

- **Connection:** Kernel interfaces and drivers underpin the workstation and telemetry probes.
- **License:** GPL-2.0-only with Linux-syscall-note for relevant UAPI headers; individual files may differ. [License/source notices](https://www.kernel.org/doc/html/latest/process/license-rules.html).
- **Stated copyright / limits:** Many individual and organizational holders; consult each file, not a single blanket ownership claim.
- **Source:** [Upstream project](https://www.kernel.org/doc/html/latest/process/license-rules.html).

<a id="musl"></a>
### musl

- **Connection:** Small C-library target named in libtam documentation; compatibility is a stated target, not certified by these credits.
- **License:** MIT; per-file and third-party exceptions are recorded upstream. [License/source notices](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT).
- **Stated copyright / limits:** Rich Felker and other musl authors; see upstream COPYRIGHT.
- **Source:** [Upstream project](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT).

## Community credit and coverage

We also thank the wider upstream communities: reviewers, translators, documentation writers, package maintainers, testers, issue reporters and accessibility contributors. The project/community links above recognize their wider work. This researched list emphasizes identifiable connections to this repository; it is not a complete census of every transitive dependency or a claim of endorsement. Public profiles and affiliations can change; the date above identifies this review.

Thanks also to the [GNU C Library contributors](https://sourceware.org/glibc/), [musl contributors](https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT), compiler/toolchain maintainers, and the standards authors behind the C/POSIX interfaces. These foundational credits do not assert that their source was copied into this repository.

The broader alphabetical list for the public Tamlinux ecosystem, including design, data, typeface, toolchain and planned-base contributions, is in [Tamlinux’s acknowledgements](https://github.com/greenermoose/tamlinux/blob/main/ACKNOWLEDGEMENTS.md).

To correct a name, attribution, affiliation or contact preference, please open an issue in this repository or contact [Fred’s public account](https://github.com/greenermoose). Only evidence-backed additions should be made; do not infer identities behind pseudonyms.

Research and compilation were AI-assisted by Codex under Fred’s direction. AI systems and provider organizations are not listed as humans; existing AI provenance records, where present, describe their separate role.
