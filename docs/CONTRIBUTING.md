# Contributing

Thanks for looking. This is a small project with one maintainer, so the rules are short and mostly
about not wasting your time.

## Before you write code

- **Start from an issue.** The work is tracked at
  [github.com/Enomalyy/dumbphone/issues](https://github.com/Enomalyy/dumbphone/issues) — one issue per
  stage. Read the issue and the spec section it points at (`docs/ui-architecture.md`). If what you want
  to do isn't there, open an issue first so we agree on the shape before you build it.
- **Read `AGENTS.md`.** It says where things live and what the module boundaries are. The codebase is
  deliberately small and opinionated: one page renderer, one key/action vocabulary, one design-token
  layer. A patch that adds a second way of doing something will be sent back.
- **This repository is a mirror.** Development happens in a private tree; what you see here is
  published from it. That has one consequence: **merging a pull request on GitHub does not put your
  change into the project.** Accepted changes are ported in as patches, and the merge commit you see
  is cosmetic. Please don't be surprised when the branch is rewritten.

## Sign-off (required)

Every commit must carry a `Signed-off-by` line, certifying you wrote it or otherwise have the right to
submit it under this project's licence — the same rule as the Linux kernel (the DCO, at
<https://developercertificate.org>). Git does it for you:

```sh
git commit -s -m "your message"
```

This is not bureaucracy. The project is MPL-2.0 and may later be offered under other terms; without a
sign-off from every contributor, that becomes impossible. A PR whose commits lack a sign-off cannot be
accepted, no matter how good the code is.

## Practical expectations

- **Build first.** `just build-desktop` must be warning-free and `just build-s3` must still link. If
  your change touches anything visual, regenerate the golden frames per `screenshots/README.md` and
  expect a reviewer to diff them pixel by pixel.
- **Keep the hot path clean.** No allocations in the draw or input path; the target is a
  microcontroller with a slow panel.
- **Say why, not what.** Commit messages explain the reasoning; the code already says what it does.
- **Don't reformat unrelated code.** Diffs are read by humans and by small local models; noise costs
  real time.

## Licence

By contributing you agree your contribution is licensed under the Mozilla Public License 2.0
(`LICENSE`), and you keep your copyright — you're licensing it, not assigning it.
