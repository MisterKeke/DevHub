# Retro visual theme

The existing Qt Quick shell and Planner and Qt Widgets module pages share the
navy retro palette through `ThemePalette`. Existing appearance choices remain:
Dark Rose uses pink actions, and Dark Blue uses blue actions with the same navy
surfaces and semantic colors. No dependencies or font files were added.

`ThemeTokens::fontFamily()` checks installed fonts once, in this order:
Cascadia Code, Cascadia Mono, Consolas, then the system fixed-width font.
`ThemeManager.fontFamily` exposes the resolved family to Quick. Widgets use the
same family through the application font and shared QSS. Body text remains 10 pt
(approximately 13 logical pixels); headings retain their existing size hierarchy.
The standalone Planner QML fixture uses the same controller property when present
and Cascadia Code otherwise.

Controls, panels, badges, tabs, and focus frames use square corners. Existing
spacing, size tokens, layout hierarchy, responsive breakpoints, and scrolling are
preserved. Pink selections use dark text; secondary controls use blue borders;
keyboard focus uses cyan. Green, orange, cyan, and red retain their status meanings.
Table, tree, menu, dialog, spin box, checkbox, scrollbar, and tab styles are shared
in `ThemeManager`. Custom editor content and syntax/severity colors remain under
their existing renderers.

## Changed files

- `resources/qml/Theme.qml`
- `resources/qml/MainShell.qml`
- `resources/qml/PlannerPanel.qml`
- `resources/qml/Authentication.qml`
- `resources/qml/ShellButton.qml`
- `resources/qml/PinkButton.qml`
- `resources/qml/DarkField.qml`
- `resources/qml/PasswordField.qml`
- `src/presentation/theme/ThemeTokens.h`
- `src/presentation/theme/ThemeTokens.cpp`
- `src/presentation/theme/ThemePalette.cpp`
- `src/presentation/theme/ThemeManager.h`
- `src/presentation/theme/ThemeManager.cpp`
- `src/presentation/theme/ThemeProxyStyle.cpp`
- `src/presentation/widgets/ReusableWidgets.cpp`
- `src/presentation/widgets/SafeMarkdownBrowser.cpp`
- `src/presentation/workspaces/WorkspaceListDelegate.cpp`
- `src/presentation/authentication/AuthenticationWidgets.cpp`
- `src/presentation/notifications/ToastManager.cpp`
- `src/presentation/overview/OverviewPage.cpp`
- `src/presentation/shell/MainWindow.cpp`
- `src/presentation/logs/LogsPage.cpp`
- `docs/retro-theme.md`

## Visual scope

Existing logos, glyphs, controls, navigation entries, and user-facing text remain.
No animals, scenery, decorative widgets, data substitutions, or functionality were
added. The mockup guides the palette and geometry. Existing responsive card
breakpoints and card heights are retained, so fewer cards fit onscreen at some
window sizes than in the reference image. Normal vertical scrolling remains.

Windows native dialogs and window chrome can follow the operating system theme.
The existing high-contrast handling remains in place. A fallback font can alter
text metrics on machines without Cascadia Code.

## Verification

- Windows developer Debug build succeeded with Qt 6.11.0 and MinGW 13.1.0.
  Existing installed vcpkg dependencies were reused with
  `-DVCPKG_MANIFEST_INSTALL=OFF`; no dependency definitions changed.
- All 27 registered CTest targets passed across the suite and targeted reruns.
  The initial sandbox run blocked Git's MSYS named-object creation, even for a
  standalone local bare-repository push. The two affected Git suites passed
  outside that sandbox. The copied test runtime also needed
  `QML_IMPORT_PATH=D:/Qt/6.11.0/mingw_64/qml`; the Planner layout target then passed
  all 12 cases. These are execution-environment settings, not product changes.
- Assistant UI, Assistant settings, custom cursor, and embedded QML runtime tests
  were rerun successfully after the final focus-style adjustment. Existing
  optional native-account and platform-capability test cases retain their skips.
- A scratch driver linked to the real application objects used an isolated local
  profile under `out/`, without changing the user's profile. It verified adding
  two workspaces, switching between them, workspace search, persisted task
  creation, Planner search and clearing search, Board/List/Calendar switching,
  every module, the settings dialog, and the workspace context menu.
- Native renders were inspected at 1640 × 928 and 1180 × 720, with populated
  Planner content, module pages, settings, and menus. Qt reported Cascadia Code
  as both the requested and resolved application font. Source comparison found
  the original QML controls, layouts, handlers, and translated labels preserved.
- `git diff --check` passed.

Local evidence: `out/retro-build-verified.txt`, `out/retro-ctest.txt`,
`out/retro-ctest-rerun.txt`, `out/retro-ctest-ui-final.txt`, and
`out/retro-live-verification-final/results.txt`. Renders are alongside the last
report; the scratch drivers and verification profiles are ignored build artifacts.
