float2 GGXEnergyLookupEnv(float Roughness, float NoV)
{
	return float2(saturate(Roughness + NoV), 0.0f);
}

#include "../KullaContyEAvg.ush"

float4 MainPS(float4 Position : SV_Position) : SV_Target0
{
	const float Roughness = frac(Position.x * 0.001f);
	return KCAverageGGXDirectionalAlbedoFast(Roughness).xxxx;
}
