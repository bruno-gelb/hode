# Contributing

Fixes, ports and build improvements are welcome. hode is Gregory Montoir's work, and this fork keeps it open after
his repository was archived (see [README.md](README.md)), so keep changes in the spirit of the original: a faithful
reimplementation of the Heart of Darkness engine.

## Git workflow

- We follow [trunk-based development](https://trunkbaseddevelopment.com/): `main` is the only long-lived branch.
  Branch off it, keep your branch small and short-lived, and open the pull request against `main`.
- `main` is protected: every change goes through a pull request, and force pushes and deleting the branch are
  blocked.
- One pull request per change.

## Commit messages

Follow [How to Write a Git Commit Message](https://cbea.ms/git-commit/) by Chris Beams. In short:

1. Separate the subject from the body with a blank line.
2. Keep the subject to 50 characters or less.
3. Capitalize the subject.
4. Don't end the subject with a period.
5. Write the subject in the imperative mood: "Fix sound panning", not "Fixed sound panning".
6. Wrap the body at 72 characters.
7. Use the body to explain what changed and why, not how.

## License

hode has no license yet: the original code is © Gregory Montoir, with no license granted. Until that changes, keep
in mind that contributions go into a project whose own code can't be relicensed by this fork.
