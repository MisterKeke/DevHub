# Bounded workspace detection

Workspace scans start only after authentication, when a workspace becomes active. They run through
`TaskScheduler`; changing or clearing the active workspace cancels the scan. Progress is throttled,
and results from a no-longer-active workspace are discarded.

The default scan limits are depth 12, 100,000 inspected files, and a 5 second wall-time budget.
The wall-time limit can be changed with the `detection/wallTimeBudgetMs` application setting and is
clamped to 100–60,000 ms. Detection does not run project tools or interpreters. It skips symlinks and
junctions, ignores dependency/build/cache directories, detects `.git` markers before pruning Git
contents, and never opens candidate log files. SQLite header checks are optional and disabled by
default (`detection/validateSQLiteHeader`).

Detectors run in stable priority order. Evidence paths are workspace-relative and duplicate evidence
is merged into a result for its containing directory. A completed result set and its detector version
are written in one database transaction. A version change invalidates older evidence before scanning;
cancelled or missing-root scans do not replace the last completed result set.
