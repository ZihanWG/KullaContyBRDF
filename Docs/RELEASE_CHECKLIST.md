# Release checklist

Use this checklist before creating a public release or presenting the repository
as a completed engine integration.

## Required release gates

- [ ] Select and add the root project `LICENSE`.
- [x] Retain the stb license and third-party notice.
- [x] Remove generated Unreal, IDE and native build artifacts from Git tracking.
- [x] Keep only the stb files used by the validation tools.
- [ ] Confirm every binary asset in `ASSET_PROVENANCE.md`.
- [ ] Recheck the current Unreal Engine EULA boundary for the public patch.
- [ ] Apply the patch to a clean UE 5.8.1 source checkout.
- [ ] Build `UnrealEditor` and complete the offscreen smoke test.
- [ ] Run at least five paired Fast/Reference GPU trials on named hardware.
- [ ] Publish raw CSV metadata and the generated summary report.
- [ ] Capture separate, pixel-aligned linear images for baseline, Kulla–Conty
      and an independent reference.
- [ ] Publish both error comparisons and their metrics.
- [ ] Reconnect or replace the early prototype materials called out in README.

## Release metadata

- [ ] Record UE commit/version, platform, compiler, GPU, driver and RHI.
- [ ] State unsupported paths: Substrate, mobile, anisotropy and path tracing.
- [ ] Create a versioned changelog entry and tag only the verified commit.
- [ ] Confirm all GitHub Actions workflows are green on that commit.
