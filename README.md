![Roughness Test](roughness_row.png)
![Cornell Box](cornell_box.png)
# Integrating Kulla–Conty BRDF for Real-Time PBR in Unreal Engine 5

> Improving energy conservation and multi-scattering for rough materials in UE5

This project integrates a **Kulla–Conty BRDF** into Unreal Engine 5 to improve **specular energy preservation** and **multi-scattering approximation** for rough surfaces in real-time rendering.

It demonstrates my experience in **physically based rendering, Monte-Carlo integration, BRDF modeling, LUT precomputation, and Unreal Engine shader system customization.**

---

## 📌 Overview

Unreal Engine 5’s default GGX-based BRDF models primarily **single-scattering** and applies limited energy compensation.  
At **medium-to-high roughness**, noticeable **specular energy loss** still occurs, resulting in dim highlights and less physically plausible materials.

This project implements a **Kulla–Conty BRDF model** to approximate:

- Multi-bounce microfacet scattering  
- Improved energy conservation  
- More physically realistic rough surface reflections  

The model is evaluated in real time using **precomputed lookup tables (LUTs)** and a **custom HLSL material node** inside UE5.

---

## ✨ Key Features

- Monte-Carlo integration of Kulla–Conty BRDF  
- Split-sum approximation for real-time evaluation  
- Multi-scattering energy compensation  
- Offline precomputed LUT pipeline  
- Custom HLSL integration into UE5 material system  
- Visual comparison under controlled lighting  

---

## 🎯 Motivation

Standard GGX BRDF only models **single scattering**, which leads to:

- Specular energy loss  
- Darkened highlights on rough materials  
- Non-physical appearance under strong lighting  

The **Kulla–Conty BRDF** approximates **multiple microfacet bounces**, redistributing lost energy back into the specular response.

This project explores:

> How much visual improvement can physically-based multi-scattering bring to real-time UE5 materials?

---

## 🧮 Theory Summary

We follow the **Kulla–Conty microfacet framework** with:

### 1. Split-Sum Approximation

The specular BRDF integral is decomposed into:

- **A-term:** non-Fresnel component  
- **B-term:** Fresnel-weighted component  

Stored in a 2D LUT:  
`E_mu(N·V, roughness)`

---

### 2. Multi-Scattering Compensation

We additionally compute:

- **E_avg:** average energy loss of single scattering  

This allows reconstructing the **multi-bounce contribution** at runtime.

Final evaluation requires only:

- 2 LUT samples  
- Fresnel evaluation  
- Simple normalization math  

No runtime Monte-Carlo sampling is required.

---

## 🛠 Offline LUT Generation

Two 256×256 textures are precomputed on CPU:

| LUT   | Description |
|-------|-------------|
| E_mu  | Split-sum A/B terms |
| E_avg | Multi-scattering average energy |

### Method

- Hammersley low-discrepancy sampling  
- GGX importance sampling  
- 1024 samples per texel  
- Smith masking-shadowing  
- Schlick Fresnel approximation  

### Parameterization

- **X:** N·V  
- **Y:** Roughness  

The generated LUTs are stored as linear PNG textures and imported into Unreal Engine.

---

## 🎮 Unreal Engine 5 Integration

### Texture Setup

- sRGB: OFF  
- Compression: VectorDisplacement  
- Filter: Bilinear / Trilinear  

### Material Pipeline

1. Compute `N·V` using `CameraVectorWS · PixelNormalWS`  
2. Use `(N·V, Roughness)` as LUT UVs  
3. Sample:
   - `E_mu` → A & B terms  
   - `E_avg` → average energy  
4. Evaluate final BRDF using a **Custom HLSL Node**

The output replaces UE5’s default specular BRDF response.

---

## 🧪 Experiments

### Roughness Row Test

- Metallic spheres from roughness 0 → 1  
- Directional light  
- Top: UE5 default BRDF  
- Bottom: Kulla–Conty BRDF  

Results:

- Low roughness: nearly identical  
- Medium roughness: stronger, more stable highlights  
- High roughness: better energy preservation  

---

### Cornell Box Test

- Controlled lighting environment  
- Identical materials  
- Side-by-side comparison  

Results:

- Improved brightness consistency  
- Better grazing-angle response  
- Clearer energy retention at mid roughness  

---

## 📊 Conclusion

- UE5’s default BRDF already applies partial compensation  
- Full Kulla–Conty BRDF provides **more physically accurate energy behavior**  
- Improvements are most noticeable at **medium roughness**  
- Suitable for:
  - Rendering research  
  - Shader and material system development  
  - Physically-based lighting studies  

---

## ⚠ Limitations

- Evaluated only on simple test scenes  
- Performance overhead not deeply profiled  
- Visual differences are subtle in low-complexity materials  
- More complex layered materials may benefit more

---

## 🧑‍💻 My Contribution

- Designed LUT precomputation pipeline  
- Implemented Monte-Carlo GGX integrator  
- Generated E_mu and E_avg textures  
- Integrated custom BRDF into UE5 material system  
- Built evaluation scenes and comparison tests  

---

## 📚 References

- Kulla, C. & Conty, A.  
  *Revisiting Physically Based Shading at Imageworks*  
- Epic Games — Unreal Engine 5 Rendering Documentation  
- UE5 Fab Parametric Cornell Box

---

## 👤 Author

**Zihan Wang (王滋涵)**  
Computer Graphics / Rendering / XR  

GitHub: https://github.com/THANKSHANK  
Portfolio: https://zihanwg.github.io/portfolio.github.io

---

## 📎 Related Article

Technical write-up:  
https://zihanwportfolio.wordpress.com/2025/05/06/integrating-kulla-conty-brdf-for-real-time-pbr-in-ue5/
