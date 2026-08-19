#ifndef KULLA_CONTY_BRDF_INCLUDED
#define KULLA_CONTY_BRDF_INCLUDED

static const float KC_PI = 3.14159265358979323846;

float KC_Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 KC_FresnelSchlick(float VoH, float3 F0)
{
    return F0 + (1.0 - F0) * KC_Pow5(1.0 - saturate(VoH));
}

float3 KC_AverageFresnelSchlick(float3 F0)
{
    // 2 * integral_0^1 SchlickFresnel(mu) * mu dmu.
    return F0 + (1.0 - F0) / 21.0;
}

float KC_SmithG1GGX(float NoX, float alpha)
{
    NoX = saturate(NoX);
    float alpha2 = alpha * alpha;
    float root = sqrt(alpha2 + (1.0 - alpha2) * NoX * NoX);
    return (2.0 * NoX) / max(NoX + root, 1.0e-6);
}

float KC_DistributionGGX(float NoH, float alpha)
{
    float alpha2 = alpha * alpha;
    float denominator = NoH * NoH * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(KC_PI * denominator * denominator, 1.0e-7);
}

float3 KC_GGXSingleScatter(
    float NoV,
    float NoL,
    float NoH,
    float VoH,
    float perceptualRoughness,
    float3 F0)
{
    NoV = saturate(NoV);
    NoL = saturate(NoL);
    if (NoV <= 0.0 || NoL <= 0.0)
    {
        return 0.0;
    }

    float roughness = max(perceptualRoughness, 0.002);
    float alpha = roughness * roughness;
    float D = KC_DistributionGGX(saturate(NoH), alpha);
    float G = KC_SmithG1GGX(NoV, alpha) * KC_SmithG1GGX(NoL, alpha);
    float3 F = KC_FresnelSchlick(VoH, F0);
    return D * G * F / max(4.0 * NoV * NoL, 1.0e-6);
}

float3 KC_MultiScatterFromDirectionalAlbedo(
    float E_NoV,
    float E_NoL,
    float E_Avg,
    float3 F0)
{
    E_NoV = saturate(E_NoV);
    E_NoL = saturate(E_NoL);
    E_Avg = saturate(E_Avg);

    float3 FAvg = KC_AverageFresnelSchlick(saturate(F0));

    // Kulla-Conty Fresnel factor from the corrected 2017 Imageworks slides.
    // FAvg * EAvg is the first term of the multiple-bounce geometric series.
    float3 fresnelDenominator = max(1.0 - FAvg * (1.0 - E_Avg), 1.0e-5);
    float3 FMs = FAvg * E_Avg / fresnelDenominator;

    float energyDenominator = KC_PI * max(1.0 - E_Avg, 1.0e-5);
    return FMs * (1.0 - E_NoV) * (1.0 - E_NoL) / energyDenominator;
}

// For a controlled direct-light evaluator, add this term to
// KC_GGXSingleScatter and multiply the sum by incident radiance * NoL.
// E_NoV and E_NoL are sampled from E_mu at (NoV, roughness) and
// (NoL, roughness). E_Avg is sampled from E_avg at (0.5, roughness).
float3 KC_CombinedBRDF(
    float NoV,
    float NoL,
    float NoH,
    float VoH,
    float perceptualRoughness,
    float3 F0,
    float E_NoV,
    float E_NoL,
    float E_Avg)
{
    float3 singleScatter = KC_GGXSingleScatter(
        NoV, NoL, NoH, VoH, perceptualRoughness, F0);
    float3 multiScatter = KC_MultiScatterFromDirectionalAlbedo(
        E_NoV, E_NoL, E_Avg, F0);
    return singleScatter + multiScatter;
}

#endif
