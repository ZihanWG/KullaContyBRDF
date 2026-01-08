📘 Integrating Kulla–Conty BRDF for Real-Time PBR in UE5

Real-time multi-scattering energy compensation for GGX in Unreal Engine 5 using precomputed LUTs.

Author: Zihan Wang, Shengyan Xu
Engine: Unreal Engine 5
Keywords: PBR, BRDF, GGX, Multi-Scattering, HLSL, UE5, Rendering

🔍 Overview

Modern real-time engines commonly use a GGX microfacet BRDF with single-scattering approximation.
While efficient, it loses significant specular energy on rough surfaces, resulting in:

Dim highlights

Over-darkened mid/high roughness materials

Violation of energy conservation

This project integrates the Kulla–Conty BRDF model into Unreal Engine 5 to approximate multi-scattering reflection and recover lost energy using precomputed lookup tables (LUTs).

We implemented:

Offline Monte Carlo integration of BRDF terms

Split-sum LUT for specular response

Average energy LUT for multi-bounce correction

Custom HLSL BRDF inside UE5 material system

✨ Features

✅ Multi-scattering energy compensation

✅ Physically-based, roughness-aware specular correction

✅ Offline LUT generation (CPU)

✅ Real-time material integration in UE5

✅ Side-by-side comparison with UE5 default BRDF

📐 Theory Recap (Kulla–Conty BRDF)

Traditional GGX assumes only single-bounce microfacet reflection:

energy is lost when light scatters between microfacets.

Kulla–Conty extends this by modeling:

average lost energy

multi-bounce redistribution

roughness-dependent compensation

We approximate this with two LUTs:

E(μ) – Split-sum terms (A/B)

E_avg – Average multi-scatter energy

Indexed by:

u = N · V  
v = Roughness


So runtime shading becomes:

2 texture fetches + small HLSL function


instead of thousands of samples per pixel.

🧮 Offline LUT Generation

Resolution: 256 × 256
Samples per pixel: 1024 (Hammersley + GGX importance sampling)

We compute:

1️⃣ Split-sum LUT (Eμ)

Stores:

R → A term

G → B term

From:

A += (1 − F) · Gvis  
B += F · Gvis

2️⃣ Multi-scatter LUT (Eavg)

Stores average lost energy across microfacets.

Sampling pipeline

Hammersley low-discrepancy sequence

ImportanceSampleGGX

Schlick Fresnel

Smith geometry

All integrals are evaluated offline and saved as PNG LUTs.

🎮 Unreal Engine 5 Integration
Texture Setup

Import LUTs into UE5 and set:

sRGB → ❌ Off

Compression → VectorDisplacementMap

Filter → Bilinear / Trilinear

UV Construction
u = saturate(dot(NormalWS, ViewDirWS))  
v = Roughness


Use:

CameraVectorWS

PixelNormalWS

Dot

AppendVector

Custom HLSL Node

Core KC shading logic is implemented inside a Custom Material Expression:

Main steps:

Schlick Fresnel

Blend Eμ LUT channels

Blend Eavg LUT channels

Multi-scatter compensation

Clamp and stabilize

Output feeds the specular term.

🧪 Results
Row Roughness Test

0.0 → 1.0 roughness sweep

Top: UE5 default BRDF

Bottom: KC BRDF

Observations:

Low roughness: nearly identical

Medium roughness:

brighter highlights

less energy loss

stronger grazing response

High roughness: subtle but more physically plausible brightness

Cornell Box Test

Controlled lighting comparison:

KC BRDF shows:

improved brightness preservation

stronger indirect response

better behavior under rough materials

📊 Conclusion

UE5 default BRDF already applies partial energy compensation

KC BRDF improves physical accuracy, especially at mid-roughness

Differences are subtle but measurable

Particularly useful for:

film-quality real-time rendering

HDR lighting

layered / rough materials

⚠️ Limitations

Tested mainly on analytic objects (spheres)

No GPU performance profiling yet

Static lighting scenarios only

Future improvements:

performance benchmarking

complex asset validation

integration at engine shading model level

🛠 Tech Stack

C++ (offline integration tool)

Monte Carlo integration

Unreal Engine 5

HLSL custom material nodes

Physically Based Rendering

📎 Reference

Kulla, C., Conty, A. Revisiting Physically Based Shading at Imageworks

Epic Games PBR shading model

UE5 material system

👤 Author

Zihan Wang
Computer Graphics / Rendering / XR
Rochester Institute of Technology
