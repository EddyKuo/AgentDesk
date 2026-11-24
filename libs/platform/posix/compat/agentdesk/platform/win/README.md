# macOS stand-ins for the Windows helper headers

Interim layer for the macOS port (design doc ADR-30). Code shared by both platforms still names
`agentdesk::platform::win` types and helpers (UTF-16 conversion, `PtyProcess`, `Foreground`, ...).
These headers give the parts that make sense on macOS the same names, implemented on the
platform-neutral interfaces in `libs/platform/include/agentdesk/platform/`, so the shared code
builds on macOS with few changes and merges cleanly with Windows-side work.

They go away with steps 1-8 of `docs/design/platform-abstraction.md`, when shared code moves to
the neutral names on both platforms. Nothing Win32-specific (handles, jobs, ConPTY) is emulated.
