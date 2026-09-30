# Port monitor and logs

The first Windows implementation of the Port Monitor reads owner-PID TCP and UDP tables through the IP Helper API. Enumeration and process inspection run on a worker thread. Process handles use query-only rights except for an explicit termination request. A termination request confirms the process, port, and risk in the UI, then rechecks the process creation time before calling `TerminateProcess`; the feature does not request elevation.

Workspace association currently uses exact PIDs for processes launched by DevHub's service coordinator. Uncertain endpoints remain unassociated. Process-name matching is deliberately not used.

File log source metadata is account-scoped through its workspace and stored in the existing `log_sources` table. The viewer follows a selected file using file-system notifications plus polling, handles UTF-8 streams and BOMs, preserves partial lines, and reopens after replacement or truncation. On first open it tails at most the last MiB. Log contents are never written back to the source file.

Each visible source has a bounded ring buffer (100,000 lines and 16 MiB of line text by default). Service stdout and stderr use separate in-memory sources. Pausing affects presentation only; readers continue tracking data within the memory cap. Clearing a view clears DevHub's in-memory copy, not the file.

The current UI provides endpoint search and protocol filters, common severity recognition, substring and regular-expression filters, copy/export, and saved file sources. Advanced cross-module Overview summaries, background cancellation for large regular-expression filters, persistent per-source tuning, and port-to-workspace evidence beyond managed service PIDs remain follow-up work.
