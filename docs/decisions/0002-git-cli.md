# ADR 0002: Use the installed Git CLI

- Status: Accepted
- Date: 2026-09-28

## Context

DevHub must work with user Git configuration, credentials, SSH, hooks, filters, LFS, and signing.

## Decision

Invoke installed Git 2.40 or newer with QProcess. Pass arguments separately, use stable porcelain or explicit formats, disable color and paging, put `--` before user-controlled paths, disable interactive credential prompts for background work, and cap captured output per the master plan.

## Consequences

The user’s Git setup remains authoritative. The app must locate/check Git and parse only documented machine-readable output; it does not bundle Git.

## Alternatives considered

An embedded Git library was rejected because it would not reliably honor all installed Git configuration and integrations.
