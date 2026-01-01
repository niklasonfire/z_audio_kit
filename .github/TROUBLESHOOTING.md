# GitHub Actions Troubleshooting Guide

## How to Read Workflow Logs

### Accessing Logs

1. Go to your repository on GitHub
2. Click the **Actions** tab
3. Click on the failed workflow run
4. Click on the failed job (e.g., "build (native_sim, basic_sine)")
5. Click on the failed step to expand the log

### Common Failure Patterns

## 1. `west init` Failures

### Error: "fatal: not a git repository"
```
Error: fatal: not a git repository (or any parent up to mount point /)
```

**Cause:** Git operations failing in container

**Fix:** Already implemented - using `actions/checkout@v4` which handles this

### Error: "west.yml: No such file or directory"
```
Error: Cannot find west.yml
```

**Cause:**
- Workflow checked out to wrong directory
- `working-directory` mismatch

**Check in logs:**
```bash
# Look for the "Checkout" step - should show:
Run actions/checkout@v4
  with:
    path: audio_kit  # If this is set, all commands need working-directory: audio_kit
```

**Fix Applied:**
- Removed `path: audio_kit` from checkout
- Now checks out to root of workspace
- `west init -l .` runs in correct location

### Error: "Invalid manifest path"
```
Error: manifest.self.path is "audio_kit" but should be "."
```

**Cause:** west.yml had `self: path: audio_kit` instead of `self: path: .`

**Fix:** ✅ Already applied in west.yml

## 2. `west update` Failures

### Error: "Failed to clone zephyr"
```
Error: Failed to fetch project zephyr
```

**Cause:**
- Network issues
- Invalid Zephyr version
- Container doesn't have git access

**Check in logs:**
```bash
# Look for "Setup workspace" or "Initialize Zephyr workspace" step
# Should show:
west update --narrow -o=--depth=1
# Then git clone output
```

**Solutions:**
- Use `--narrow -o=--depth=1` for shallow clone (already in workflow)
- Check Zephyr version in west.yml (currently v3.6.0)
- Ensure container has network access

### Error: "name-allowlist: module not found"
```
Error: Project 'xyz' in name-allowlist but not in manifest
```

**Cause:** Typo in module name in west.yml

**Fix:** Double-check module names:
```yaml
name-allowlist:
  - cmsis       # Correct
  - hal_stm32   # Correct (note: hal_ prefix)
```

## 3. Build Failures

### Error: "west build: command not found"
```
bash: west: command not found
```

**Cause:**
- Not using Zephyr CI container
- west not installed

**Fix:** Ensure workflow uses correct container:
```yaml
container:
  image: ghcr.io/zephyrproject-rtos/ci:v0.26.13
  options: --user root
```

### Error: "ZEPHYR_BASE not set"
```
Error: ZEPHYR_BASE environment variable is not set
```

**Cause:** `west zephyr-export` not run

**Check in logs:**
```bash
# Should see this step:
west zephyr-export
# Output: exporting zephyr environment variables
```

**Fix:** Add after `west update`:
```yaml
- name: Setup workspace
  run: |
    west init -l .
    west update --narrow -o=--depth=1
    west zephyr-export  # ← This is essential
```

### Error: "No such file or directory: samples/basic_sine"
```
Error: No such file or directory: samples/basic_sine
```

**Cause:** Running `west build` from wrong directory

**Check in logs:**
```bash
# Look for pwd output in your build step
pwd  # Should show: /github/workspace (or similar)
ls   # Should list: samples/, tests/, CMakeLists.txt, etc.
```

**Fix Applied:**
- Removed `working-directory` from build steps
- Commands now run from repository root

### Error: "No rule to make target 'z_audio_kit'"
```
CMake Error: Cannot find source file: z_audio_kit
```

**Cause:** Module not found by Zephyr build system

**Check in logs:**
```bash
# Look for module discovery output:
west list
# Should show both 'zephyr' and your module

west topdir
# Should show workspace root
```

**Debugging steps:**
1. Check `zephyr/module.yml` exists in your repo
2. Verify Kconfig exists
3. Run `west list` to see if module is discovered

### Error: "CONFIG_AUDIO_FRAMEWORK not defined"
```
warning: CONFIG_AUDIO_FRAMEWORK is not set
```

**Cause:**
- Kconfig not found
- Module not loaded

**Fix:**
1. Ensure `Kconfig` exists in repo root
2. Ensure `zephyr/module.yml` points to it:
   ```yaml
   build:
     kconfig: Kconfig
   ```

## 4. Platform-Specific Errors

### Error: "native_sim: unsupported platform"
```
Error: Board 'native_sim' not found
```

**Possible causes:**
- Old Zephyr version (use v3.4+)
- Wrong board name (should be `native_sim` not `native_posix` in newer versions)

**Check Zephyr version in west.yml:**
```yaml
- name: zephyr
  revision: v3.6.0  # Should be 3.4 or newer
```

### Error: "CMSIS-DSP not found" (qemu_cortex_m3)
```
CMake Error: Could not find CMSIS-DSP
```

**Cause:** CMSIS module not imported

**Fix Applied in west.yml:**
```yaml
name-allowlist:
  - cmsis  # ← Make sure this is in the list
```

**Also check prj.conf:**
```ini
CONFIG_CMSIS_DSP=y
CONFIG_CMSIS_DSP_TRANSFORM=y
```

## 5. Test Execution Failures

### Error: "Permission denied: ./build/zephyr/zephyr.exe"
```
bash: ./build/zephyr/zephyr.exe: Permission denied
```

**Cause:** Executable bit not set

**Fix:**
```yaml
- name: Run test
  run: |
    chmod +x build/zephyr/zephyr.exe
    ./build/zephyr/zephyr.exe
```

### Error: "Timeout waiting for test"
```
Error: The operation was canceled (timeout after 2 minutes)
```

**Cause:**
- Test has infinite loop
- Test doesn't exit

**Check test code:**
- Ensure test calls `exit(0)` or returns from `main()`
- Check for `while(1)` loops

**Increase timeout:**
```yaml
timeout-minutes: 5  # Increase from 2
```

## 6. Artifact Upload Failures

### Error: "No files found to upload"
```
Warning: No files were found with the provided path
```

**Cause:** Build failed or files in wrong location

**Check:**
```yaml
path: build/zephyr/zephyr.*  # ← Verify this path exists
```

**Debug in workflow:**
```yaml
- name: Debug - show build output
  if: always()
  run: |
    ls -la build/
    ls -la build/zephyr/ || echo "zephyr directory not found"
```

## Debugging Workflow

### Add debug steps to your workflow:

```yaml
- name: Debug - Environment
  run: |
    echo "=== Environment ==="
    pwd
    echo "---"
    ls -la
    echo "---"
    echo "ZEPHYR_BASE: $ZEPHYR_BASE"
    which west || echo "west not found"

- name: Debug - West status
  run: |
    echo "=== West Status ==="
    west list || echo "west list failed"
    west topdir || echo "west topdir failed"

- name: Debug - Module status
  run: |
    echo "=== Module Status ==="
    ls -la zephyr/module.yml || echo "module.yml not found"
    cat zephyr/module.yml || echo "cannot read module.yml"
```

## Quick Fixes Applied

The following fixes have been applied to address common issues:

### 1. ✅ Fixed west.yml path
```yaml
# Before:
self:
  path: audio_kit  # ❌ Caused path mismatch

# After:
self:
  path: .  # ✅ Correct for root checkout
```

### 2. ✅ Simplified workflow
- Removed `path: audio_kit` from checkout
- Removed nested `working-directory`
- Added `options: --user root` to container
- Simplified to just samples and tests (V2 examples will be addressed separately)

### 3. ✅ Added explicit ZEPHYR_BASE
```yaml
env:
  ZEPHYR_BASE: ${{ github.workspace }}/../zephyr
```

## How to Diagnose Your Specific Failure

### Step 1: Find the failing step

Look for red ❌ marks in the workflow log. Common failing steps:
- "Setup workspace" → west/git issues
- "Build" → CMake/compilation issues
- "Run test" → Test execution issues

### Step 2: Expand the step

Click the failing step to see full output.

### Step 3: Look for error keywords

Search for:
- `Error:`
- `fatal:`
- `CMake Error`
- `not found`
- `Permission denied`

### Step 4: Check context

Look at the lines before the error:
- What command was running?
- What was the working directory?
- What files were expected?

### Step 5: Match to patterns above

Find the matching error pattern in this guide and apply the fix.

## Testing Fixes Locally

Before pushing to GitHub, test locally:

```bash
# Pull the exact CI container
docker pull ghcr.io/zephyrproject-rtos/ci:v0.26.13

# Run interactive shell
docker run -it --rm \
  -v $(pwd):/workspace \
  -w /workspace \
  --user root \
  ghcr.io/zephyrproject-rtos/ci:v0.26.13 \
  /bin/bash

# Inside container, run the exact same commands as CI:
west init -l .
west update --narrow -o=--depth=1
west zephyr-export
west build -b native_sim samples/basic_sine -p
```

## Next Steps

1. **Check the new simplified workflow** (`build-test-simple.yml`)
2. **Review west.yml changes** (path: . instead of path: audio_kit)
3. **Push and watch the new workflow run**
4. **If still failing, paste the specific error** from the GitHub Actions log

The most common issues have been addressed. If you're still seeing failures, please share the specific error message from the workflow logs.
