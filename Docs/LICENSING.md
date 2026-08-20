# Licensing boundary

This repository contains four distinct categories with different ownership and
distribution rules.

## Project-authored material

The C++, HLSL, Python and PowerShell implementation written for this project
needs a root `LICENSE` selected by the repository owner. Until that file is
added, the public repository should not claim that reuse, modification or
redistribution rights have been granted.

Recommended choices for an employer-facing sample are:

- **MIT:** short and permissive; easiest for readers to understand and reuse.
- **Apache-2.0:** permissive with an explicit patent grant; longer notice terms.
- **All rights reserved:** public viewing only; weakest choice for outside reuse.

The chosen project license must not be presented as licensing Epic-owned Engine
Code or third-party assets.

## stb

The two retained stb headers are redistributed under stb's MIT alternative.
See `THIRD_PARTY_NOTICES.md` and `LUT/stb-master/LICENSE`.

## Unreal Engine integration

The repository does not provide an Unreal Engine checkout. Users of
`EnginePatch/UE5.8.1` must obtain and use UE 5.8.1 under Epic's terms. Small
Engine Code snippets used as matching anchors or in support of the patch remain
subject to the Unreal Engine EULA. Do not aggregate additional Engine Code into
this public repository.

Epic's current EULA permits limited public Engine Code snippets in connection
with supporting patches and plug-ins, subject to its stated conditions. Review
the current agreement before each public release; this document is a release
boundary, not legal advice.

## Binary content

Maps, meshes, materials and imported source files do not automatically inherit
the code license. Each committed asset must be cleared in
`ASSET_PROVENANCE.md` before a tagged release.
