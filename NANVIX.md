# SQLite Port for Nanvix

> **TL;DR:** This is a port of the SQLite database engine for the Nanvix operating system. Jump to [Quick Start](#quick-start) to get started immediately.

---

## Overview

This document describes the port of [SQLite](https://sqlite.org/) database engine for the [Nanvix](https://github.com/nanvix/nanvix) operating system. This port enables SQLite to run on Nanvix, a POSIX-compatible educational operating system.

| Property | Value |
|----------|-------|
| **Base Version** | SQLite 3.49.0 |
| **Target Platform** | Nanvix (i686) |
| **Build System** | GNU Make (wrapping autoconf) |

**What's included:**
- ✅ Cross-compilation support for Nanvix
- ✅ Static library build (`libsqlite3.a`)
- ✅ CLI shell executable (`sqlite3.elf`)
- ✅ Build helper scripts
- ✅ CI/CD integration

**Dependencies:**
- zlib (compression support)

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Prerequisites](#prerequisites)
3. [Building](#building)
4. [Testing](#testing)
5. [Changes Summary](#changes-summary)
6. [Known Limitations](#known-limitations)
7. [CI/CD](#cicd)

---

## Quick Start

For experienced users who want to build quickly:

```bash
# 1. Pull the Docker image
docker pull nanvix/toolchain:latest-minimal

# 2. Download Nanvix sysroot and extract Nanvix commit SHA
curl -fsSL https://raw.githubusercontent.com/nanvix/nanvix/refs/heads/dev/scripts/get-nanvix.sh | bash -s -- nanvix-artifacts
tar -xjf nanvix-artifacts/*microvm*single*.tar.bz2 -C nanvix-artifacts
export NANVIX_HOME=$(find nanvix-artifacts -maxdepth 2 -type d -name "bin" -exec dirname {} \; | head -1)
# Extract Nanvix SHA from artifact filename for version matching
export NANVIX_SHA=$(ls nanvix-artifacts/*microvm*single*.tar.bz2 | sed -E 's/.*-([a-f0-9]{40})\.tar\.bz2$/\1/')

# 3. Download zlib dependency (must match Nanvix version)
# Find zlib release built with same Nanvix SHA, or use latest as fallback
curl -fsSL -o zlib-release.tar.bz2 "https://github.com/nanvix/zlib/releases/latest/download/zlib-microvm-single-process.tar.bz2"
tar -xjf zlib-release.tar.bz2
cp -f zlib-*/lib/libz.a "$NANVIX_HOME/lib/"
cp -f zlib-*/include/*.h "$NANVIX_HOME/include/"

# 4. Build (Docker is used automatically if native toolchain is not found)
make -f Makefile.nanvix CONFIG_NANVIX=y NANVIX_HOME="$NANVIX_HOME"

# 5. Run tests
make -f Makefile.nanvix CONFIG_NANVIX=y NANVIX_HOME="$NANVIX_HOME" test
```

Continue reading for detailed instructions.

---

## Prerequisites

You need the following components to build SQLite for Nanvix:

| Component | Description | Default Location |
|-----------|-------------|------------------|
| **Nanvix Toolchain** | i686-nanvix cross-compiler | `$HOME/toolchain` |
| **Nanvix Sysroot** | System libraries and linker script | `$HOME/nanvix` |
| **zlib** | Compression library (must match Nanvix version) | Installed in sysroot |

> **Important:** The zlib library must be built against the same Nanvix version you are using. The CI workflow automatically finds and downloads matching zlib releases based on the Nanvix commit SHA.

### Available Platform Configurations

| Platform | Process Mode | Artifact Pattern |
|----------|--------------|------------------|
| hyperlight | multi-process | `hyperlight.*multi-process` |
| hyperlight | single-process | `hyperlight.*single-process` |
| microvm | single-process | `microvm.*single-process` |
| microvm | multi-process | `microvm.*multi-process` |

### Downloading Nanvix

```bash
curl -fsSL https://raw.githubusercontent.com/nanvix/nanvix/refs/heads/dev/scripts/get-nanvix.sh | bash -s -- nanvix-artifacts
```

The script downloads all release artifacts. Extract the one matching your target platform (see [Quick Start](#quick-start) for a complete example).

### Downloading zlib Dependency

SQLite requires zlib for compression support. **You must use a zlib release that was built against the same Nanvix version.** The Nanvix commit SHA is embedded in the artifact filename (e.g., `nanvix-microvm-single-process-release-<SHA>.tar.bz2`).

To find matching zlib releases:

```bash
# 1. Extract Nanvix SHA from artifact filename
NANVIX_SHA=$(ls nanvix-artifacts/*microvm*single*.tar.bz2 | sed -E 's/.*-([a-f0-9]{40})\.tar\.bz2$/\1/')

# 2. Check zlib releases for one built with matching Nanvix SHA
# The zlib release notes contain the Nanvix commit SHA they were built against

# 3. Download matching zlib (replace PLATFORM and PROCESS_MODE)
curl -fsSL -o zlib-release.tar.bz2 \
  "https://github.com/nanvix/zlib/releases/latest/download/zlib-PLATFORM-PROCESS_MODE.tar.bz2"
tar -xjf zlib-release.tar.bz2
cp -f zlib-*/lib/libz.a "$NANVIX_HOME/lib/"
cp -f zlib-*/include/*.h "$NANVIX_HOME/include/"
```

> **Note:** The CI workflow automatically finds and downloads zlib releases that match the Nanvix version by checking release notes for the Nanvix commit SHA.

---

## Building

### Using Docker (Recommended)

The Makefile supports automatic Docker fallback when the native toolchain is not available:

```bash
# Pull the Nanvix toolchain Docker image
docker pull nanvix/toolchain:latest-minimal

# Build (Docker is used automatically if native toolchain is not found)
make -f Makefile.nanvix CONFIG_NANVIX=y NANVIX_HOME=/path/to/nanvix/sysroot-debug
```

> **Note:** The sysroot (`NANVIX_HOME`) must contain `lib/libposix.a`, `lib/libz.a`, and `lib/user.ld` from a Nanvix build.

**Docker Fallback Behavior:**
- If `NANVIX_TOOLCHAIN` points to a valid toolchain, it uses the native compiler
- If the native toolchain is not found, it automatically uses Docker if available
- Use `CONFIG_NANVIX_DOCKER=y` to force Docker usage even when native toolchain exists
- Use `NANVIX_DOCKER_IMAGE` to specify a custom Docker image (default: `nanvix/toolchain:latest-minimal`)

### Using Native Toolchain

```bash
export NANVIX_TOOLCHAIN=/path/to/toolchain  # Contains: bin/i686-nanvix-gcc
export NANVIX_HOME=/path/to/nanvix          # Contains: lib/user.ld, lib/libposix.a, lib/libz.a
make -f Makefile.nanvix CONFIG_NANVIX=y all
```

### Build Outputs

After a successful build, you will have:

| File | Description |
|------|-------------|
| `libsqlite3.a` | SQLite static library |
| `sqlite3.elf` | SQLite CLI shell executable |

---

## Testing

> **Important:** Tests must be run through the Nanvix daemon (`nanvixd.elf`).

### Running the Test Suite

```bash
# Run all tests
make -f Makefile.nanvix CONFIG_NANVIX=y NANVIX_HOME=/path/to/nanvix test
```

### Running Individual Tests

To run sqlite3 shell manually:

```bash
cd "$NANVIX_HOME" && echo "SELECT 'Hello, Nanvix!';" | ./bin/nanvixd.elf -- /path/to/sqlite3.elf
```

### Available Test Executables

| Executable | Description |
|------------|-------------|
| `sqlite3.elf` | SQLite command-line interface shell |

---

## Changes Summary

The following changes were made to support Nanvix.

### Build System Changes

| Change | Description |
|--------|-------------|
| New Makefile | Added `Makefile.nanvix` for Nanvix cross-compilation |
| Cross-compilation | Uses `CONFIG_NANVIX=y` option to enable Nanvix build |
| Docker support | Automatic Docker fallback when native toolchain not available |
| Configure wrapper | Wraps standard `./configure` with Nanvix cross-compilation settings |
| Linker flags | Added Nanvix-specific flags (`-T user.ld -static`) |
| Shared libraries | Disabled (not supported on Nanvix) |
| Test target | Modified to run via `nanvixd.elf` |

### Configuration Options

| Option | Description |
|--------|-------------|
| `SQLITE_OMIT_WAL` | WAL mode disabled (not supported on Nanvix) |
| `--disable-threadsafe` | Threading disabled (single-threaded mode) |
| `--disable-tcl` | TCL bindings disabled |
| `--disable-shared` | Shared libraries disabled |

### New Files

| File | Purpose |
|------|---------|
| `Makefile.nanvix` | Standalone Makefile for Nanvix cross-compilation |
| `NANVIX.md` | This documentation file |
| `.github/workflows/nanvix-ci.yml` | CI workflow for automated builds |

---

## Known Limitations

| Limitation | Impact |
|------------|--------|
| **No WAL mode** | Write-Ahead Logging disabled (`SQLITE_OMIT_WAL`) |
| **No shared libraries** | Only static library (`libsqlite3.a`) is built |
| **No threading** | Single-threaded mode only |
| **No TCL bindings** | TCL interface not available |
| **Static linking only** | All executables are statically linked |

---

## CI/CD

The GitHub Actions workflow at `.github/workflows/nanvix-ci.yml` automates building and testing on every change.

### Trigger Events

| Event | Description |
|-------|-------------|
| Push to `nanvix/**` | Any push to Nanvix branches |
| PR to `nanvix/**` | Pull requests targeting Nanvix branches |
| Daily schedule | Runs at midnight UTC |
| Manual dispatch | Can be triggered manually |
| Repository dispatch | Triggered by `nanvix-release` events |

### Build Matrix

The CI runs on 4 different platform/process-mode configurations:

| Platform | Process Mode | Runner |
|----------|--------------|--------|
| hyperlight | multi-process | `self-hosted-hyperlight-multi` |
| hyperlight | single-process | `self-hosted-hyperlight-single` |
| microvm | single-process | `self-hosted-microvm-single` |
| microvm | multi-process | `self-hosted-microvm-multi` |

All configurations run in parallel with `fail-fast: false`, ensuring that all platforms are tested even if one fails.

### Dependency Management

The CI workflow automatically downloads the matching zlib release from `nanvix/zlib` before building SQLite, ensuring the correct platform-specific zlib library is used.

---
