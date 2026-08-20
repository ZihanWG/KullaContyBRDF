# Third-party notices

This document identifies third-party software and platform dependencies used
by the repository. A future root project license applies only to material owned
by this project's author; it does not replace the terms below.

## stb

The validation tools redistribute these files from the
[`nothings/stb`](https://github.com/nothings/stb) project:

- `LUT/stb-master/stb_image.h`
- `LUT/stb_image_write.h`

stb offers a choice of the MIT License or public-domain dedication. This
repository redistributes the files under the MIT alternative. The complete
upstream notice is retained in `LUT/stb-master/LICENSE` and in the headers.

Copyright (c) 2017 Sean Barrett

## Unreal Engine

Unreal Engine is a dependency and is not licensed under this project's future
root license. Unreal Engine, Engine Code and related Epic-provided content are
governed by the Unreal Engine End User License Agreement and applicable Epic
content terms.

The `EnginePatch/UE5.8.1` scripts require the user to provide a separately
licensed UE 5.8.1 source checkout. The installer uses small matching anchors to
support the patch; it does not include a complete or buildable Unreal Engine
source tree. Any Epic-owned matching text remains Epic property and subject to
Epic's terms.

- Unreal Engine EULA: https://www.unrealengine.com/eula/unreal
- Unreal Engine source access: https://www.unrealengine.com/ue-on-github

Unreal and Unreal Engine are trademarks or registered trademarks of Epic Games,
Inc. This project is independent and is not endorsed by Epic Games.

## Content assets

Committed binary assets are tracked separately in `ASSET_PROVENANCE.md`.
Assets whose origin or redistribution permission is not confirmed must be
removed or replaced before a tagged public release.
