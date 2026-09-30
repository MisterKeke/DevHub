# ADR 0001: Use Qt Widgets

- Status: Accepted
- Date: 2026-09-28

## Context

DevHub is a Windows desktop tool with dense tables, trees, keyboard workflows, and native desktop accessibility needs. The master plan selects Qt 6.8 and requires a Qt Community-compatible dependency set.

## Decision

Build the UI with Qt Widgets and target the Qt 6.8 API baseline. Require Qt 6.8.3 or newer for local development so newer Qt Creator kits remain usable; CI stays pinned to Qt 6.8.3. Use the open-source Qt release and modules Core, Gui, Widgets, Sql, Concurrent, and Test as needed. Do not add Qt WebEngine.

## Consequences

Qt model/view, widgets, and standard desktop behavior are available from one framework. UI screens remain C++ and custom appearance must use Widgets painting, styles, and semantic theme tokens.

## Alternatives considered

Qt Quick/QML was rejected because the product is data-dense and the plan prioritizes Qt Widgets model/view. A custom rendering stack would add maintenance cost without a product need.
