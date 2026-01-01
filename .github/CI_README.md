# Continuous Integration Setup

This repository uses GitHub Actions to automatically build and test the audio framework on every push and pull request.

## CI Workflows

### 1. Quick Build Check (`quick-check.yml`)

**Triggers:** Push to `claude/**` branches, pull requests

**Purpose:** Fast feedback for development branches

**What it does:**
- Builds `basic_sine` sample on `native_sim`
- Verifies code compiles without errors
- Runs in ~2-3 minutes

**Status Badge:**
```markdown
![Quick Check](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Quick%20Build%20Check/badge.svg)
```

### 2. Full Build and Test (`build-test.yml`)

**Triggers:** Push to `main`/`develop`/`claude/**` branches, pull requests, manual dispatch

**Purpose:** Comprehensive testing across platforms

**What it builds:**

#### Samples (2 boards × 2 samples = 4 builds)
- `basic_sine` on `native_sim`
- `basic_sine` on `qemu_cortex_m3`
- `metering_console` on `native_sim`
- `metering_console` on `qemu_cortex_m3`

#### Tests (2 boards × 3 tests = 6 builds)
- `analyzer_logic` on `native_sim` (runs test)
- `analyzer_logic` on `qemu_cortex_m3`
- `cow_integrity` on `native_sim` (runs test)
- `cow_integrity` on `qemu_cortex_m3`
- `memory_check` on `native_sim` (runs test)
- `memory_check` on `qemu_cortex_m3`

#### V2 Examples (2 boards × 4 examples = 8 builds)
- `example_channel_strip` on both boards
- `example_standalone_nodes` on both boards
- `example_spectrum_analyzer` on both boards
- `example_spectrum_analyzer_advanced` on both boards

**Total:** 18 builds per CI run

**Runtime:** ~15-20 minutes

**Status Badge:**
```markdown
![Build and Test](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Build%20and%20Test/badge.svg)
```

## Tested Platforms

### native_sim (x86 Simulation)
- **Purpose:** Fast testing on x86_64 architecture
- **Features:** Native POSIX execution, debugging support
- **Use case:** Quick iteration, unit tests
- **Note:** Uses fallback FFT (no CMSIS-DSP)

### qemu_cortex_m3 (ARM Cortex-M3 Emulation)
- **Purpose:** ARM architecture testing
- **Features:** ARM instruction set, QEMU emulation
- **Use case:** Verify ARM builds, basic ARM testing
- **Note:** Uses CMSIS-DSP FFT (optimized)

## CI Environment

**Container:** `ghcr.io/zephyrproject-rtos/ci:v0.26.13`

**Includes:**
- Zephyr SDK (all architectures)
- CMake, Ninja, GCC
- Python with west tool
- QEMU for ARM/x86 simulation

## Local Testing

To reproduce CI builds locally:

### Using Docker (Recommended)

```bash
# Pull the CI container
docker pull ghcr.io/zephyrproject-rtos/ci:v0.26.13

# Run interactive shell
docker run -it -v $(pwd):/workspace \
  ghcr.io/zephyrproject-rtos/ci:v0.26.13

# Inside container:
cd /workspace
west init -l .
west update --narrow -o=--depth=1
west build -b native_sim samples/basic_sine -p
```

### Using Native Installation

```bash
# Initialize workspace
west init -l .
west update

# Build sample
west build -b native_sim samples/basic_sine -p

# Run on native_sim
./build/zephyr/zephyr.exe

# Build for ARM
west build -b qemu_cortex_m3 samples/basic_sine -p

# Clean
rm -rf build
```

## Build Artifacts

All builds upload artifacts (retained for 5 days):

- `build-{board}-{sample}` - Sample builds
- `test-{board}-{test}` - Test builds
- `example-{board}-{example}` - V2 example builds

**Contents:** `zephyr.elf`, `zephyr.hex`, `zephyr.bin`, etc.

## Workflow Files

### build-test.yml Structure

```yaml
jobs:
  build:           # Build samples
  test:            # Build and run tests
  build-v2-examples: # Build V2 architecture examples
  build-summary:   # Final status check
```

### Matrix Strategy

Uses GitHub Actions matrix to parallelize builds:

```yaml
matrix:
  board: [native_sim, qemu_cortex_m3]
  sample: [basic_sine, metering_console]
```

**Benefit:** All combinations build in parallel (faster CI)

## Troubleshooting

### Build Failures

**"west: command not found"**
- Container issue. Ensure using official Zephyr CI container.

**"Zephyr not found"**
- west update failed. Check network connectivity.
- Try: `west update --narrow -o=--depth=1`

**"CMake Error"**
- Missing dependencies. Check `prj.conf` and `CMakeLists.txt`.
- Ensure `CONFIG_AUDIO_FRAMEWORK=y` is set.

**"CMSIS-DSP not found" (qemu_cortex_m3)**
- Add to `prj.conf`:
  ```
  CONFIG_CMSIS_DSP=y
  CONFIG_CMSIS_DSP_TRANSFORM=y
  ```

### Test Failures

**Test timeout (native_sim)**
- Test didn't exit. Check for infinite loops.
- Increase timeout: `timeout-minutes: 5`

**Test not executable**
- Permission issue: `chmod +x build/zephyr/zephyr.exe`

### Slow CI

**west update is slow**
- Using shallow clone: `--narrow -o=--depth=1`
- Only essential modules imported in `west.yml`

**Too many builds**
- Reduce matrix: Remove boards or samples
- Use `quick-check.yml` for development

## Adding New Builds

### Add New Sample

Edit `.github/workflows/build-test.yml`:

```yaml
matrix:
  sample:
    - basic_sine
    - metering_console
    - your_new_sample  # Add here
```

### Add New Board

```yaml
matrix:
  board:
    - native_sim
    - qemu_cortex_m3
    - your_new_board  # Add here
```

### Add New Test

```yaml
matrix:
  test:
    - analyzer_logic
    - cow_integrity
    - your_new_test  # Add here
```

## Manual Workflow Dispatch

Run CI manually from GitHub UI:

1. Go to **Actions** tab
2. Select **Build and Test** workflow
3. Click **Run workflow**
4. Choose branch
5. Click **Run workflow**

## Status Checks

CI runs as required status check for:
- Pull requests to `main`
- Pull requests to `develop`

**Merging blocked** if CI fails.

## Performance

**Typical runtimes:**
- Quick check: 2-3 minutes
- Full build (18 builds): 15-20 minutes
- Per-build time: ~1-2 minutes
- Test execution: <30 seconds

## Best Practices

1. **Use quick-check for development**
   - Fast feedback on `claude/**` branches
   - Full CI runs on PR to main

2. **Fix CI before merging**
   - All builds must pass
   - Check artifacts for warnings

3. **Monitor build times**
   - If CI becomes slow, reduce matrix
   - Consider splitting workflows

4. **Keep artifacts small**
   - Only upload essential files
   - 5-day retention for builds

5. **Test locally first**
   - Use Docker container
   - Faster iteration than waiting for CI

## CI Configuration Files

```
.github/
├── workflows/
│   ├── build-test.yml       # Full CI (18 builds)
│   └── quick-check.yml      # Fast check (1 build)
└── CI_README.md             # This file

west.yml                      # Workspace manifest
```

## Future Enhancements

Potential improvements:

- [ ] Code coverage reporting (gcov/lcov)
- [ ] Static analysis (cppcheck, clang-tidy)
- [ ] Performance benchmarks
- [ ] Hardware-in-the-loop tests
- [ ] Release automation
- [ ] Documentation builds

## Support

For CI issues:
- Check workflow logs in Actions tab
- Review this documentation
- Open issue with `[CI]` tag
