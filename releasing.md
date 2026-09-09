# Releasing Addictol

Addictol has one development branch and channel: `master`. The product version is the explicit
`major.minor.patch` in `Version/resource_version2.h`. GitHub Actions run numbers identify CI builds
without changing the DLL/F4SE numeric version.

## Repository settings

Keep branch protection or rulesets on `master`; the release workflow never pushes a version bump
directly to it. In **Settings > Actions > General**, enable **Allow GitHub Actions to create and
approve pull requests**. The workflows use job-scoped permissions:

- normal builds and reusable checks: `contents: read`
- development and stable publication: `contents: write`
- the next-version branch: `contents: write`
- the next-version pull request and validation dispatches: `pull-requests: write`, `actions: write`

The repository's default workflow-token permission may remain read-only because each write job
requests only the permission it needs. No personal access token or protection bypass is required.

If required checks are configured for `master`, select the MSBuild and xmake check contexts produced
by a normal pull request (the reusable jobs are **MSBuild package** and **xmake validation**).
The bump PR runs those same workflows on its own branch, so the checks attach to its commit.
Confirm the exact rendered context names after the first run before making them required.

A pull request created with `GITHUB_TOKEN` does not trigger another `push` or `pull_request`
workflow. The stable workflow accounts for that: it pushes the bump branch, opens the pull request,
and explicitly dispatches MSBuild and xmake on that branch. Dispatch events are supported with
`GITHUB_TOKEN`; checking out another commit inside the stable workflow would not attach checks to
that commit. Wait for both dispatched checks before reviewing and merging the PR. The merge push
runs normal `master` CI.

## Development prereleases

Every push to `master` builds the tracked product version. For example, product version `1.6.0`
and Actions run 615 produce:

- release/tag/build identity `v1.6.0-dev.615`
- DLL, Windows resource, F4SE, and startup-log version `1.6.0.0`
- MSBuild package manifest `build-info.txt` with the identity and exact source commit

Manual MSBuild dispatches and pull requests validate and upload Actions artifacts but never publish
a release. The xmake workflow continues to validate normal pushes and pull requests.

Development and stable publication share the repository-wide `release-publication` concurrency
group with `cancel-in-progress: false`. The lock covers publication, not the long builds. Once inside
the lock, a development publisher checks again for the matching stable tag or release; if either
exists, it keeps the Actions artifact but suppresses the prerelease. This also stops stale or
in-flight builds from publishing after promotion.

GitHub concurrency permits one running and one pending job per group and replaces an older pending
job when another is queued. If a queued stable publication is replaced before it starts, rerun the
manual stable workflow. A running publication is never cancelled.

## Stable release walkthrough

1. Confirm `master` contains the intended release code and that
   `Version/resource_version2.h` contains the version being released. Do not edit the header to the
   next development version yet.
2. Open **Actions > Release stable > Run workflow**, using the `master` branch.
3. Enter **version** `1.6.0`, **source_ref** `master` (or an exact reachable commit/ref), and
   **next_version** `1.7.0`.
4. The workflow resolves the source to a full commit SHA at the start and verifies that it is
   reachable from `master`. It rejects noncanonical versions, leading zeros, packed-field
   overflows, a source/header mismatch, and a next version that is not greater than both the release
   and current `master`.
5. MSBuild packages that pinned source while xmake builds it, verifies all four F4SE exports, and
   runs `vmm-tests`. The stable publisher then enters the shared publication lock.
6. The publisher refuses a mismatching existing tag or release and rejects a version below the
   highest stable version, preventing **Latest** from regressing. Release notes begin at the
   preceding stable release rather than a development prerelease. The normal GitHub release
   `v1.6.0` points exactly at the pinned commit and is marked **Latest**.
7. After publication, the workflow prepares `release-prepare-v1.7.0` from current `master`, changes
   only the tracked version header, commits and pushes that branch, opens a pull request to `master`,
   and dispatches its MSBuild and xmake checks.
8. Wait for both checks, then review and merge normally. The merge push publishes the first
   `v1.7.0-dev.RUN` prerelease.

Do not derive the next `master` version from the latest stable release. The explicit workflow input
and current version header are authoritative.

## Failure and rerun behavior

Publication is deliberately conservative:

- existing tags must resolve to the pinned source commit
- existing releases must have the expected stable/prerelease state
- an existing asset is preserved and never replaced
- a missing asset may be added to an otherwise exact release
- a recovered stable release is marked **Latest** only after the downgrade guard passes

If stable publication succeeded but branch creation or pull-request creation failed, rerun the
same stable inputs. The workflow verifies the existing release/tag instead of replacing them, then
resumes the missing next-version work. When **source_ref** is `master`, an existing release tag
pins recovery to the original source even if `master` has advanced. An explicit ref or SHA must still
resolve to that original commit.

An existing bump branch is reused only when its tip declares the requested next version and its
last commit changes only `Version/resource_version2.h`. If the bump was already merged and
`master` declares the requested next version, the rerun completes without opening another PR.
If a dispatched bump check fails, inspect and rerun that check; rerunning stable release
orchestration also redispatches both checks for the existing PR.

Permission, network, and API failures are fatal. Only confirmed GitHub `404` responses are treated
as an absent release, tag, branch, or pull request.

## Migration from legacy build-number versions

Older automation used the Actions run number as the numeric DLL patch and published tags such as
`v1.6.614.0`. The first release under this strategy is the explicit stable `v1.6.0`. Its numeric DLL
version is therefore lower than legacy development builds even though the stable release is newer
for distribution purposes. Afterward, development tags use SemVer prerelease suffixes such as
`v1.7.0-dev.616`; the DLL remains `1.7.0.0`.

The historical stable `v1.5.395` remains the release-notes baseline for `v1.6.0`. Weekly cleanup
deletes only old prerelease releases and their matching tags. Stable releases and stable tags are
never selected by cleanup.
