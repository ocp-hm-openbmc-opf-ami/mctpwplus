# MCTP Wrapper C++ Library Coverage Report

## Prerequisites:

- Install Docker
- Configure SSH described in docs: http://openbmc-docs.jf.intel.com/getting_started/
- Create working directory
  ```bash
  mkdir ci_test_area && cd ci_test_area
  ```
- Download the CI Image: https://github.com/openbmc/openbmc-build-scripts
  ```bash
  git clone git@github.com:openbmc/openbmc-build-scripts.git
  ```
- Clone MCTP Wrapper C++ Library repository with rename to mctpwplus
  ```bash
  git clone git@github.com:intel-collab/firmware.bmc.openbmc.libraries.mctpwplus.git mctpwplus
  ```

### Virtual environment

It is recommended to use Python virtual environment.

```bash
virtualenv --python=python3 venv
```

```bash
. ./venv/bin/activate
```

```bash
pip install -r mctpwplus/coverage_upstream/requirements.txt
```

## Running the code coverage using Upstream Docker

Methods to run Upstream Docker are described below.

### 1. Unit tests with all analyses

```bash
WORKSPACE=$(pwd) UNIT_TEST_PKG=mctpwplus \
NO_FORMAT_CODE=1 \
./openbmc-build-scripts/run-unit-test-docker.sh
```

> [!NOTE]
> `NO_FORMAT_CODE=1` is passed to skip the code formatting.
>
> Report location: `ci_test_area\mctpwplus\build\meson-logs\coveragereport`

### 2. Tests only

```bash
WORKSPACE=$(pwd) UNIT_TEST_PKG=mctpwplus \
NO_FORMAT_CODE=1 \
TEST_ONLY=1 \
./openbmc-build-scripts/run-unit-test-docker.sh
```

> [!NOTE]
> `TEST_ONLY=1` is passed to skip all analyzes.
>
> There will be no coverage report.

### 3. UT with code coverage - Interactive session

Pass `INTERACTIVE=1` flag to use an interactive session.

```bash
WORKSPACE=$(pwd) UNIT_TEST_PKG=mctpwplus \
NO_FORMAT_CODE=1 \
INTERACTIVE=1 \
./openbmc-build-scripts/run-unit-test-docker.sh
```

After docker launches, use commands:

```bash
cd mctpwplus
```

```bash
meson setup build -Dtests=enabled -Db_coverage=true --optimization=0
```

```bash
meson test -C build -v
```

```bash
ninja -C build coverage-html
```

> [!NOTE]
> Report location: `ci_test_area\mctpwplus\build\meson-logs\coveragereport`

## Comments

> [!TIP]
> To remove all unused containers, networks, images use:
>
> ```bash
> docker system prune
> ```
