# Release limitations

- DevHub currently targets Windows 11 x64. Other operating systems and CPU architectures are not release-supported.
- The application uses the Qt SQLite driver and libsodium. The exact Qt modules, linkage, license notices, and any source/relinking obligations must be reviewed for each binary distribution.
- Git is discovered as an external user-installed program and is not bundled.
- DevHub does not automatically restart managed services or replay Git or SQL operations after an unexpected exit.
- A note recovery journal is offered when the affected note is opened. It is a local recovery aid, not a versioned backup system.
- The installer is unsigned unless a release engineer supplies a signing certificate. Unsigned private builds remain supported.
- Clean Windows 11 virtual-machine installation and upgrade checks are release gates and have not been performed by this repository build.
