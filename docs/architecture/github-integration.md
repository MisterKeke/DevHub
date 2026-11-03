# GitHub Pull Request and CI integration

DevHub uses its native Qt networking stack for GitHub. It does not call `gh`, run GitHub API shell commands, start a local callback server, or ask the user to paste a token. The GitHub integration is read-only.

## Register the GitHub App

1. Create a [GitHub App](https://docs.github.com/en/apps/creating-github-apps/registering-a-github-app) in the account or organization that will own the integration.
2. Enable **Device Flow** in the app's optional features. Leave expiring user access tokens enabled, as GitHub recommends. DevHub requests `offline_access` so GitHub returns an expiring access token and refresh token.
3. Set the repository permissions to **Pull requests: Read-only**, **Issues: Read-only**, **Checks: Read-only**, and **Actions: Read-only**. Metadata read access is implicit. Do not grant write permissions. Pull request, review, comment, check-run, Actions, and job-log endpoints each enforce the corresponding permissions; see GitHub's [permissions reference](https://docs.github.com/en/rest/authentication/permissions-required-for-github-apps), [Pull Requests API](https://docs.github.com/en/rest/pulls/pulls), [Checks API](https://docs.github.com/en/rest/checks/runs), [workflow runs API](https://docs.github.com/en/rest/actions/workflow-runs), and [workflow jobs and logs API](https://docs.github.com/en/rest/actions/workflow-jobs).
4. Install the App on the account or organization and choose **Only select repositories** when you want to limit access. The user access token is still constrained by both the App's permissions and the signed-in user's own repository access.
5. Copy the **Client ID** from the GitHub App's General settings. This is not the App ID. Device Flow and refresh use the public Client ID; do not configure or embed a client secret, App private key, or installation key in DevHub.
6. In DevHub open **Settings → Integrations**, paste the OAuth Client ID, and save. DevHub accepts legacy `Iv1.…` and current `Iv23…` GitHub App Client IDs. Enable Device Flow in the App before connecting.
7. Select **Connect GitHub**. DevHub displays a short code and the official `https://github.com/login/device` page. Enter the code in the browser and authorize the App. DevHub verifies the account with `GET /user` before it reports success. A missing or invalid Client ID remains in a setup state; no sample credentials are built in.

GitHub's [Device Flow guide](https://docs.github.com/en/apps/creating-github-apps/authenticating-with-a-github-app/generating-a-user-access-token-for-a-github-app) documents the device-code and token requests. The OAuth `offline_access` scope and refresh-token rules are in [refreshing user access tokens](https://docs.github.com/en/apps/creating-github-apps/authenticating-with-a-github-app/refreshing-user-access-tokens). DevHub follows the returned polling interval, adds the documented delay after `slow_down`, and keeps access tokens out of URLs.

## Credentials and session boundary

Access and refresh tokens are serialized into one Windows Credential Manager generic credential scoped to the DevHub local account ID. They are not stored in the settings database, QSettings, process arguments, support bundles, or logs. The service verifies restored credentials, refreshes expiring access tokens, persists rotated credentials before replacing its in-memory copy, and asks the user to reconnect when refresh fails. Disconnect removes the local credential; it does not revoke GitHub authorization. Use [GitHub's authorized applications settings](https://github.com/settings/apps/authorizations) to revoke it remotely.

DevHub clears in-memory GitHub credentials and cancels authentication, dashboard, and monitoring requests when the local session locks or signs out. Credentials are restored only after the local session is active. Workspace changes invalidate the selected repository and stale dashboard responses. Monitoring is stopped when GitHub is disconnected or the local session is locked.

## Repository and dashboard behavior

The existing Git service reads configured remotes. DevHub accepts `https://github.com/owner/repository[.git]`, `git@github.com:owner/repository[.git]`, and `ssh://git@github.com/owner/repository[.git]`; it rejects non-GitHub hosts and malformed paths. If a workspace has several GitHub remotes, select one in **Git → Pull Requests**. That choice is saved per workspace and recalculated when Git remotes refresh.

The Pull Requests tab supports open, closed, and all states, search, table sorting, incremental pages of 50, local-branch association, and links to GitHub. Selecting a request loads its overview, up to 100 changed files, up to 100 reviews, up to 100 issue comments, up to 100 check runs, the matching commit's Actions runs, and failed job summaries for up to five failed runs. Large descriptions, patches, comments, API responses, and log excerpts have explicit bounds. Markdown uses DevHub's safe renderer, which blocks HTML and arbitrary resource loading.

The **CI History** tab pages repository workflow runs by 50 and labels the run's commit. Runs are repository history; DevHub does not imply that a run belongs to the selected pull request unless the commit SHA matches. Selecting a run loads up to 100 jobs and their failed steps. A user can explicitly download a job log. DevHub accepts only GitHub's HTTPS API redirect and known GitHub/Azure log-download hosts, never forwards the bearer token to the download host, keeps the response in memory, caps downloads at 2 MiB, and displays at most 128 Ki characters.

The current build does not submit CI logs to Assistant. Logs stay in the CI details view until that view is replaced or closed.

## Workspace monitoring and notifications

Monitoring is opt-in per workspace in **Settings → Integrations**. The configured interval is bounded from 60 seconds to one hour. DevHub monitors only the selected remote for the active workspace, with one bounded request for the latest 50 Pull Requests and one for the latest 50 Actions runs per poll. The first successful poll records a baseline without generating historical notifications. Later polls notify for observed Pull Request merges and completed Actions runs. Stable event keys are persisted through the notification repository to prevent repeat notifications after restart. Monitoring does not poll while disconnected, locked, or signed out.

The monitor deliberately does not claim to notify on every possible review or CI event. It observes the latest bounded repository pages and can miss events that fall outside those pages between polls. It does not run a webhook server or change PR/Planner task state.

## API limits and permission errors

All API calls use HTTPS, a version header, bearer authorization, timeouts, bounded JSON bodies, and pagination for list views. GitHub rate-limit responses include the reset time when available. A 403 or 404 is reported as an access problem; confirm the App installation includes the repository, that the required read permissions were approved, and that the signed-in user can access it. Organization SSO policy can impose additional authorization steps.

The local build has no real Client ID configured. Device Flow and live GitHub API access therefore remain inactive until the App owner supplies that public configuration and installs the App with the required permissions.
