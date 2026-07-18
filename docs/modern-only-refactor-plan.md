# Archived Web-Shell Modern-Only Refactor Plan

Archived: 2026-07-13.

This file used to be the working plan for replacing the classic Qt Widgets
layout with an HTML/CSS/JavaScript shell hosted in Qt WebEngine. That
architecture was an intermediate migration stage and no longer describes the
Windows client.

The completed architectural cutover is a direct Qt Quick/QML product UI with
typed C++ controllers and models. It has no classic compatibility view,
WebChannel, browser snapshot/patch transport, or WebEngine product shell.
WebEngine remains only as a lazy, isolated Qt Quick media-player surface. This
does not claim finished visual or interaction parity.

Do not use historical statements from this file as current architecture or as
implementation instructions. The current source boundary, compatibility
contract, allowed native surfaces, and remaining Windows release gates are
documented in:

- [`modern-client-migration.md`](modern-client-migration.md)
- [`status-and-roadmap.md`](status-and-roadmap.md)
- [`screen-sharing-architecture.md`](screen-sharing-architecture.md)

The former phase-by-phase Web-shell plan remains available in Git history if a
migration archaeology reference is needed.
