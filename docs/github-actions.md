# Learning GitHub Actions with SumTEditor

This repository contains two workflows:

- `.github/workflows/ci.yml`: checks every push to `main` and every pull request.
- `.github/workflows/release.yml`: builds distributable archives when a version tag is pushed.

## Lesson 1: CI Workflow

The CI workflow answers one question: can the project build and pass tests right now?

Important pieces:

- `on`: declares which events start the workflow.
- `jobs`: a workflow can contain one or more independent jobs.
- `runs-on`: chooses the runner operating system.
- `strategy.matrix`: runs the same job with different inputs.
- `steps`: shell commands or reusable actions executed in order.
- `actions/checkout`: downloads your repository into the runner.

The current CI matrix runs:

- `ubuntu-core`: core library and tests only.
- `ubuntu-tui`: full TUI build on Linux.
- `macos-core`: core library and tests on macOS.

Watch CI runs from your machine:

```sh
gh run list --repo zhangchaosd/SumTEditor
gh run watch --repo zhangchaosd/SumTEditor
```

## Lesson 2: Release Workflow

The release workflow answers a different question: can GitHub build packages that users can download?

It runs on version tags:

```yaml
on:
  push:
    tags:
      - "v*.*.*"
```

It also supports manual test runs from the Actions tab through `workflow_dispatch`.
Manual runs upload build artifacts, but they do not create a GitHub Release.

The release workflow has two jobs:

- `build`: builds and tests the project on Linux and macOS, then uploads `.tar.gz` artifacts.
- `publish`: downloads all artifacts and attaches them to a GitHub Release.

The `publish` job only runs for version tags:

```yaml
if: startsWith(github.ref, 'refs/tags/v')
```

## Creating a Release

Create and push a version tag:

```sh
git tag v0.1.0
git push origin v0.1.0
```

Then watch the release workflow:

```sh
gh run list --repo zhangchaosd/SumTEditor
gh run watch --repo zhangchaosd/SumTEditor
```

If the workflow succeeds, GitHub creates a release with files like:

- `sumteditor-v0.1.0-linux-x86_64.tar.gz`
- `sumteditor-v0.1.0-linux-x86_64.tar.gz.sha256`
- `sumteditor-v0.1.0-macos.tar.gz`
- `sumteditor-v0.1.0-macos.tar.gz.sha256`

## Useful Debugging Commands

List recent runs:

```sh
gh run list --repo zhangchaosd/SumTEditor --limit 10
```

View a run:

```sh
gh run view RUN_ID --repo zhangchaosd/SumTEditor
```

View failed logs:

```sh
gh run view RUN_ID --repo zhangchaosd/SumTEditor --log-failed
```

Rerun a failed workflow:

```sh
gh run rerun RUN_ID --repo zhangchaosd/SumTEditor
```

## What to Learn Next

- Add branch protection so PRs require CI before merging.
- Add CMake presets to make local and CI builds share the same names.
- Add release notes conventions.
- Add a nightly workflow on a schedule.
- Add binary smoke tests for packaged archives.
