# Overview and shared commands

The Overview is a presentation of state that other modules already own. `OverviewStateComposer` accepts snapshots of module state, coalesces bursts for 120 ms, and emits a `WorkspaceOverviewSnapshot`. It has no repository, process, Git, or database dependencies, so recomposition cannot start backend work.

Each overview section carries a summary, update time, loading flag, partial error, and command IDs. `OverviewPage` renders Git, services, ports, logs, database, and notes cards. The grid uses three columns on wide windows, two on medium windows, and one on narrow windows. Data older than five minutes is labeled stale.

`CommandRegistry` is the common source for command metadata, availability, QAction instances, and execution handlers. The shell navigation, workspace header, overflow menu, Overview buttons, and Ctrl+K palette route through those commands. Availability is recalculated when the active workspace changes and unavailable commands expose a reason.

The command palette fuzzy-matches command titles, descriptions, and keywords, favors exact and prefix matches, and ranks up to 30 recently used commands. Search providers are queried one at a time on the UI event loop; a newer query invalidates the previous provider sequence. Workspace results use the workspace list already loaded by the shell. Full file-content indexing remains deferred.

The current Overview Git card links to the Git module rather than querying Git itself. The ports card reports cached results and can explicitly request a refresh. The logs card summarizes service-output bytes observed during this session; the Logs module remains responsible for parsing and displaying log lines.
