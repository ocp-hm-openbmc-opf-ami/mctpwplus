# Unit Test Validation (upstream)

## Prerequisites

- Install Docker
- Configure SSH described in docs:
  <http://openbmc-docs.jf.intel.com/getting_started/>
- Make sure you have Python 3 installed with the `sh` module

  ```bash
  pip install sh
  ```

- Create working directory (or use existing one)

  ```bash
  mkdir ci_test_area && cd ci_test_area
  ```

- Clone repository with scripts:
  <https://github.com/openbmc/openbmc-build-scripts>

  ```bash
  git clone git@github.com:openbmc/openbmc-build-scripts.git
  ```

- Checkout the revision you need, e.g.

  ```bash
  cd openbmc-build-scripts && git checkout 52c3aec3fae38765fe9a56270c93c6f11303b974 && cd ..
  ```

- Clone MCTP Wrapper C++ Library repository and rename it to mctpwplus

  ```bash
  git clone git@github.com:intel-collab/firmware.bmc.openbmc.libraries.mctpwplus.git mctpwplus
  ```

Directory tree:

```bash
├── ci_test_area # working directory (the name does not matter)
│   ├── openbmc-build-scripts
│   └── mctpwplus
```

## Running Unit Test Validation using upstream script

Methods to run upstream script are described below.

### 1. Unit tests with all analyses

- source files formatters
- Unit Tests
- Unit Tests with Valgrind
- clang-tidy
- Unit Tests with sanitizers
- Code coverage
- cppcheck

```bash
WORKSPACE=$(pwd) UNIT_TEST_PKG=mctpwplus \
EXTRA_DOCKER_RUN_ARGS="-v /home/${USER}/.gitconfig:/home/${USER}/.gitconfig -v /home/${USER}/.ssh:/home/${USER}/.ssh" \
./openbmc-build-scripts/run-unit-test-docker.sh
```

> [!NOTE]
>
> Logs location (including code-coverage report):
> `ci_test_area\mctpwplus\build\meson-logs`

### 2. Tests only

```bash
WORKSPACE=$(pwd) UNIT_TEST_PKG=mctpwplus \
NO_FORMAT_CODE=1 \
TEST_ONLY=1 \
EXTRA_DOCKER_RUN_ARGS="-v /home/${USER}/.gitconfig:/home/${USER}/.gitconfig -v /home/${USER}/.ssh:/home/${USER}/.ssh" \
./openbmc-build-scripts/run-unit-test-docker.sh
```

> [!NOTE]
>
> `NO_FORMAT_CODE=1` is passed to skip the code formatting
>
> `TEST_ONLY=1` is passed to skip all analyzes.
>
> Logs location: `ci_test_area\mctpwplus\build\meson-logs`
>
> There will be no coverage report.

### 3. UT with code coverage - Interactive session

Pass `INTERACTIVE=1` flag to use an interactive session.

```bash
WORKSPACE=$(pwd) UNIT_TEST_PKG=mctpwplus \
NO_FORMAT_CODE=1 \
INTERACTIVE=1 \
EXTRA_DOCKER_RUN_ARGS="-v /home/${USER}/.gitconfig:/home/${USER}/.gitconfig -v /home/${USER}/.ssh:/home/${USER}/.ssh" \
./openbmc-build-scripts/run-unit-test-docker.sh
```

After docker launches, use commands:

```bash
cd mctpwplus
```

```bash
meson setup build -Dtests=enabled -Db_coverage=true -Dcpp_args='-DBOOST_USE_VALGRIND' --optimization=0
```

```bash
meson test -C build -v
```

```bash
ninja -C build coverage-html
```

> [!NOTE]
>
> Report location: `ci_test_area\mctpwplus\build\meson-logs\coveragereport`

## Comments

If you use HTTPS instead of SSH, change `EXTRA_DOCKER_RUN_ARGS` to:

```bash
EXTRA_DOCKER_RUN_ARGS="-v /home/${USER}/.netrc:/home/${USER}/.netrc"
```

> [!TIP]
>
> To remove all unused containers, networks, images use:
>
> ```bash
> docker system prune -af
> ```

&nbsp;

> [!TIP]
>
> Upstream docker uses gcovr to generate coverage report. To exclude folders
> from the report or rename the report title add `gcovr.cfg` file in the root of
> the project, e.g.
>
> ```bash
> exclude = subprojects/
> exclude = tests/
>
> exclude-unreachable-branches = yes
> exclude-throw-branches = yes
>
> html = yes
> print-summary = yes
>
> html-title = mctpwplus Coverage Report
> html-self-contained = yes
> ```

[Upstream docs](https://github.com/openbmc/docs/blob/master/testing/local-ci-build.md)
