# ADR 0005: Target Windows 11 x64 for v1

- Status: Accepted
- Date: 2026-09-28

## Context

The first release is intended for Windows 11 x64 and depends on Windows process, port, installer, and shell integration.

## Decision

Support Windows 11 x64 for v1. Keep platform-specific functionality behind narrow interfaces. Other platforms are outside v1 scope.

## Consequences

The CI and package target focus on Windows x64. Portable domain and application layers should avoid unnecessary Windows coupling.

## Alternatives considered

Cross-platform v1 was rejected in favor of shipping a narrower and testable Windows product.
