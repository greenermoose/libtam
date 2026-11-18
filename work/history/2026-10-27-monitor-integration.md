# 2026-10-27 — display recovery classification

- **Tool:** Codex CLI `0.164.0`
- **Model:** `gpt-6.1-terra`
- **Transcript:** Retained privately by the author.
- **Guiding request:**

> Integrate the C tam-monitor and matching libraries for Tamlinux 0.5.5. Follow the existing 0.5.5 delivery plan (tamlinux-packages/docs/plans/0.5.5.md) and monitor review (tamlinux/docs/reviews/2026-10-27-tam-monitor.md).
>
> Coordinate source corrections. Verify the disposition of R1–R8. If another agent owns those fixes, keep packaging work in separate claimed paths. You may prepare recipes and development builds now, but do not present the reviewed defective revision as ready for Test. R3 requires checking the actual library error contract, as qualified in the report.
>
> Package matching libraries explicitly. Build and install the core, display and display-session archives, headers and pkg-config metadata. Declare the required build dependencies. Preserve portal separation: core clients must not acquire unnecessary display or portal dependencies.
>
> Build the native command properly. Update the commands derivation’s build/check/install workflow and dependencies. Remove Python from tam-monitor’s runtime requirements while preserving other commands’ needs. Rebuild affected static-library consumers. Preserve symbols and debug information through package fixups.
>
> Deliver the development environment too. Expose the assembly’s matching development files through the developer module. Verify both pkg-config and explicit-prefix builds without accidental /usr/local or ~/.local/dev dependencies. Keep minimal runtime and developer selections separate. Preserve Fred’s active development overrides until the authorized deployment delivers their replacements.
>
> Validate exact inputs. Run command regressions, relevant library sanitizer tests, clean package builds and assembly checks with appropriate stable-tree claims. Verify installed ELF identity, dependencies and debug sections outside the checkouts. Record exact source revisions and remaining acceptance work. Passing fixtures do not establish physical monitor behavior.
>
> Leave final selection and activation explicit. Obtain Fred’s final tam command/corpus selection before freezing the assembly; do not choose moving heads. Check candidate history before allocating 0.5.5. Finish with a concrete Test proposal listing component pins, outputs, review dispositions, rollback and physical/developer acceptance steps.

Development work only. Physical display acceptance and stage movement are separate. See the implementation and review records for verification and remaining gates.
