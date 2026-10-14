# ADR 0001: Use Qt Widgets

- Status: Accepted
- Date: 2026-09-28

## Context

DevHub is a Windows desktop tool with dense tables, trees, keyboard workflows, and native desktop accessibility needs. The initial master plan selected Qt 6.8 and required a Qt Community-compatible dependency set.

## Decision

Build the UI with Qt Widgets and target the Qt 6.11 API baseline. Require Qt 6.11.0 or newer for local development; CI stays pinned to Qt 6.11.2. Use the open-source Qt release and modules Core, Gui, Widgets, Sql, Concurrent, and Test as needed. Do not add Qt WebEngine. The baseline moved after the Qt 6.8.0–6.8.3 Markdown importer advisory (CVE-2025-3512); later advisories remain subject to API-level applicability review.

## Consequences

Qt model/view, widgets, and standard desktop behavior are available from one framework. UI screens remain C++ and custom appearance must use Widgets painting, styles, and semantic theme tokens.

## Alternatives considered

Qt Quick/QML was rejected because the product is data-dense and the plan prioritizes Qt Widgets model/view. A custom rendering stack would add maintenance cost without a product need.
