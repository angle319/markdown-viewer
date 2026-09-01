// Commit messages follow Conventional Commits, checked with commitlint.
//
//   npx --yes --package @commitlint/cli --package @commitlint/config-conventional \
//     commitlint --from origin/main --to HEAD
//
// Notes on the choices below:
//   * Merge commits use `chore:` rather than a feature type, so a generated
//     changelog does not list the same work twice.
//   * Subjects start lower case; config-conventional's subject-case rule forbids
//     sentence-case.
module.exports = {
    extends: ['@commitlint/config-conventional'],
};
