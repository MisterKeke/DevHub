# Local account and password security

The v1 account is a DevHub access boundary. It does not encrypt workspace files, source code,
external databases, logs, or notes. A session remains process-local.

Usernames are trimmed for display and must contain 1 to 64 Unicode code points. Control
characters and `/` or `\` path separators are rejected. The comparison value is Unicode NFKC
normalization followed by Unicode case folding and a second NFKC pass. This makes comparison
case-insensitive while preserving the trimmed spelling for display. Account directories use
the generated account UUID, never the username.

The account value has no password field. The encoded verifier is kept in a move-only
`PasswordVerifier` and is only accessible to the hasher and SQL repository. Repository errors
and hash errors never include password input or the encoded verifier.

Password hashing uses libsodium’s Argon2id string API. The production profile uses
`crypto_pwhash_OPSLIMIT_INTERACTIVE` and `crypto_pwhash_MEMLIMIT_INTERACTIVE` (64 MiB with the
selected libsodium profile); each encoded verifier stores its
algorithm, salt, and cost parameters. A separate scheme version supports future verifier format
changes. Successful verification checks the current profile and creates a replacement verifier
when needed; the repository can persist that replacement and `last_login_at_utc` in one
transaction.

The interactive profile follows [libsodium’s password-hashing guidance](https://doc.libsodium.org/password_hashing/default_phf).
The repository does not claim a timing result for every target PC. Run
`tools/measure-argon2.ps1` with `devhub_account_tests.exe` on representative release hardware
to log the production-profile hash duration. Automated password tests use an isolated 8 MiB,
one-operation profile and do not change the production policy.

`PasswordTaskService` submits hash and verify work to the bounded `TaskScheduler`. The hasher
also rejects calls from the application thread. Duplicate password requests are rejected while
one is active. Password text is converted into a short-lived UTF-8 buffer and controllable
buffers are overwritten after use. Qt string copies cannot all be reliably erased, so this is
best-effort cleanup rather than a promise of complete process-memory erasure.

Authentication exists only in process memory. “Keep me signed in for this session” defaults to
unchecked; when selected, it suppresses inactivity locking until the app exits, but manual Lock
still requires the password. With the option unchecked, the default inactivity timeout is 15
minutes and only local mouse, keyboard, wheel, touch, or tablet input resets the timer. Sign Out
clears the session and returns to the username/password screen. No persistent sign-in token is
created.

New accounts create their UUID-based `notes`, `recovery`, and `cache` folders before the account
row is committed. If database persistence fails, the staged account folder is removed. Password
changes verify the current password off the GUI thread and update the verifier and `updated_at`
inside one database transaction.

Forgotten-password reset has no email recovery. It requires typing `RESET`, archives the existing
database and account directory under a timestamped Local AppData recovery folder, then initializes
a fresh database and returns to Create Account. Workspace directories are outside this operation
and are never moved or deleted. The reset does not import the old profile automatically.
