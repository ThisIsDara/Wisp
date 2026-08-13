# Code Signing Policy

**Free code signing provided by [SignPath.io](https://signpath.io/),
certificate by SignPath Foundation.**

This document describes what wisp signs, how signed artifacts are produced,
and who may approve a signing request. It applies to every binary published
on the [releases page](https://github.com/ThisIsDara/wisp/releases).

## What is signed

- `wisp.exe` — the application executable
- `wisp-setup.exe` — the per-user NSIS installer

Both are built from this repository by the public CI pipeline
(`.github/workflows/release.yml`, GitHub Actions) and submitted for signing
by SignPath Foundation. The signing policy requires **manual approval by an
approver for every release**.

## How signing works

- The private key is generated and stored on SignPath's Hardware Security
  Module (HSM). **No key material ever exists on a developer machine or in
  this repository.**
- Only artifacts produced by the repository's CI pipeline are submitted for
  signing; SignPath verifies the origin of the build (source repository,
  branch, commit, build job).
- Signed artifacts are downloaded back into the pipeline and published to
  GitHub Releases alongside a `SHA256SUMS` file.

## Metadata

- Product name of all signed binaries: `wisp`
- Product version: the release tag (`v*`), e.g. `0.1.0`

## Team roles

- Committers and reviewers: the repository owners
- Approvers (approve each signing request): the repository owners

Every change proposed to this repository must be approved by a team member
trusted by the entire team before it can be merged and released; all team
members use multi-factor authentication for GitHub and SignPath access.

## Rules

- Only binaries built from this repository's source are signed.
- Only release artifacts are signed — no unsigned test or development
  binaries are distributed from the releases page.
- The signature does not cover third-party components (Qt, VC runtime);
  their licenses are listed in
  [THIRD-PARTY-NOTICES.txt](packaging/THIRD-PARTY-NOTICES.txt).
