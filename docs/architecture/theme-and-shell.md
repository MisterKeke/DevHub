# Theme and application shell

The desktop UI uses semantic values from `src/presentation/theme/ThemeTokens.h` and
`ThemePalette.h`. Widgets select a token rather than embedding a color literal. Settings offers
dark rose and dark blue palettes; changing the palette takes effect after restarting the app.
Logical-pixel sizing follows the UI direction in the master development plan.

`ThemeManager` is parented to `QApplication`. It applies the global palette, Fusion-based
focus treatment, and small application stylesheet layer. It persists the reduced-motion
preference. Current shell components use no continuous or timed animation.

`MainWindow` keeps the standard operating-system window frame and arranges workspace,
module navigation, and page content in three regions. Saved normal geometry is restored only
when it remains visible on an attached screen; otherwise the window is centered on the primary
screen. General settings can disable restoring the previous page or window geometry. The window
minimum is 1180 by 720 logical pixels.

The shell hosts the Overview, Git, Services, Ports, Logs, Database, Notes, Files, and Settings
pages. The folder action opens an existing workspace or registers a selected folder. The
terminal action opens the selected terminal profile at the active workspace; the Command Prompt
profile starts in a separate console window. Shared fields, buttons, cards,
badges, empty states, error details, confirmation, loading, and authentication-background
widgets live in `ReusableWidgets`.

To capture a quick shell image at multiple Qt scale factors, run
`tools/capture-shell-scales.ps1` with the built executable and an output directory. It sets
`QT_SCALE_FACTOR` in each child process and captures 100%, 125%, 150%, and 200% images.
Captures are a manual visual aid; they are not part of CTest.
