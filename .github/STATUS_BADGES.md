# CI Status Badges

Add these badges to your main README.md to show build status:

## Build Status

```markdown
![Build and Test](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Build%20and%20Test/badge.svg?branch=main)
![Quick Check](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Quick%20Build%20Check/badge.svg)
```

Replace `YOUR_USERNAME` with your GitHub username.

## Per-Branch Status

```markdown
<!-- Main branch -->
![Main Build](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Build%20and%20Test/badge.svg?branch=main)

<!-- Develop branch -->
![Develop Build](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Build%20and%20Test/badge.svg?branch=develop)
```

## Badges in README

Example placement in README.md:

```markdown
# Z Audio Kit

![Build and Test](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Build%20and%20Test/badge.svg?branch=main)
![License](https://img.shields.io/badge/license-MIT-blue.svg)

Audio processing framework for Zephyr RTOS...
```

## Custom Badge Colors

You can customize badge appearance with shields.io:

```markdown
![Build](https://img.shields.io/github/actions/workflow/status/YOUR_USERNAME/z_audio_kit/build-test.yml?branch=main&label=build&logo=github)
```

## All Available Badges

| Badge | Markdown |
|-------|----------|
| Build Status | `![Build](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Build%20and%20Test/badge.svg)` |
| Quick Check | `![Quick Check](https://github.com/YOUR_USERNAME/z_audio_kit/workflows/Quick%20Build%20Check/badge.svg)` |
| License | `![License](https://img.shields.io/badge/license-MIT-blue.svg)` |
| Zephyr Version | `![Zephyr](https://img.shields.io/badge/zephyr-v3.6-blue.svg)` |
| Platform | `![Platform](https://img.shields.io/badge/platform-ARM%20%7C%20x86-lightgrey.svg)` |
