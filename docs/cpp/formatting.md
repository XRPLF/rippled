# C++ Formatting

All code must conform to `clang-format`, according to the settings in
[`.clang-format`](../../.clang-format), unless the result would be unreasonably
difficult to read or maintain. The `clang-format` version is pinned in
[`.pre-commit-config.yaml`](../../.pre-commit-config.yaml), so the
[`pre-commit`](../../CONTRIBUTING.md#pre-commit-hooks) hook always formats with the same version as
CI. To demarcate lines that should be left as-is, surround them with comments
like this:

```
// clang-format off
...
// clang-format on
```

The easiest way to format your changes is to let the `pre-commit` hook run
automatically on commit, or to run it manually:

```bash
pre-commit run clang-format --all-files
```

You can also format individual files in place by running `clang-format -i <file>...`
from any directory within this project.

> [!NOTE]
> This uses whatever `clang-format` version is installed locally, which may
> differ from the pinned version used by `pre-commit` and CI, so the results
> can vary.

There is a Continuous Integration job that runs clang-format on pull requests. If the code doesn't comply, a patch file that corrects auto-fixable formatting issues is generated.

To download the patch file:

1. Next to `clang-format / check (pull_request) Failing after #s` -> click **Details** to open the details page.
2. Left menu -> click **Summary**
3. Scroll down to near the bottom-right under `Artifacts` -> click **clang-format.patch**
4. Download the zip file and extract it to your local git repository. Run `git apply [patch-file-name]`.
5. Commit and push.
