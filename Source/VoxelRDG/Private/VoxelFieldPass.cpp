#include "VoxelFieldPass.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "VoxelNoiseParams.h"
#include "VoxelMaterialGenerationSettings.h"
#include "VoxelEditLayer.h"
#include "MarchingCubes/MarchingCubesDispatch.h"

class FVoxelFieldGenCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FVoxelFieldGenCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelFieldGenCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, EditStampCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelEditStampGPU>, EditStamps)

		SHADER_PARAMETER(FVector3f, ChunkOriginWS)
		SHADER_PARAMETER(float,    StepSizeWS)
		SHADER_PARAMETER(uint32,   CellsPerAxis)
		SHADER_PARAMETER(uint32,   SamplesPerAxis)
		SHADER_PARAMETER(float,    IsoLevel)
		SHADER_PARAMETER(uint32,   ChunkSeed)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelNoiseParams>, NoiseParamsBuf)

		SHADER_PARAMETER(FVector2f, BiomeTempMoistureFreq)
		SHADER_PARAMETER(FVector2f, BiomeTempOffset)
		SHADER_PARAMETER(FVector2f, BiomeMoistureOffset)
		SHADER_PARAMETER(FVector2f, BiomeTempMoistureHeightInfluence)
		SHADER_PARAMETER(float, BiomeDomainWarpFrequency)
		SHADER_PARAMETER(float, BiomeDomainWarpAmplitude)
		SHADER_PARAMETER(float, BiomeSeaLevel)
		SHADER_PARAMETER(float, BiomeColdTemperature)
		SHADER_PARAMETER(float, BiomeHotTemperature)
		SHADER_PARAMETER(float, BiomeDryMoisture)
		SHADER_PARAMETER(float, BiomeWetMoisture)
		SHADER_PARAMETER(uint32, BiomeSeed)

		SHADER_PARAMETER(uint32, DesertSurfaceMaterialId)
		SHADER_PARAMETER(uint32, DesertSubsurfaceMaterialId)
		SHADER_PARAMETER(uint32, DesertDeepMaterialId)
		SHADER_PARAMETER(FVector2f, DesertSubsurfaceZRange)

		SHADER_PARAMETER(uint32, PlainsSurfaceMaterialId)
		SHADER_PARAMETER(uint32, PlainsSubsurfaceMaterialId)
		SHADER_PARAMETER(uint32, PlainsDeepMaterialId)
		SHADER_PARAMETER(FVector2f, PlainsSubsurfaceZRange)

		SHADER_PARAMETER(uint32, TundraSurfaceMaterialId)
		SHADER_PARAMETER(uint32, TundraSubsurfaceMaterialId)
		SHADER_PARAMETER(uint32, TundraDeepMaterialId)
		SHADER_PARAMETER(FVector2f, TundraSubsurfaceZRange)

		SHADER_PARAMETER(uint32, bHasOre)
		SHADER_PARAMETER(uint32, OreMaterialId)
		SHADER_PARAMETER(uint32, OreBaseMaterialId)
		SHADER_PARAMETER(FVector2f, OreDepthZRange)
		SHADER_PARAMETER(float, OreVeinFrequency)
		SHADER_PARAMETER(float, OreVeinThreshold)
		SHADER_PARAMETER(float, OreRarity)
		SHADER_PARAMETER(float, OreNodeSizeWS)
		SHADER_PARAMETER(uint32, OreSeed)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutDensity)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,  OutMaterial)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FVoxelFieldGenCS, "/Plugin/Voxel/Fields/VoxelFieldGenerate.usf", "Main", SF_Compute);

FVoxelFieldPassOutputs FVoxelFieldPass::AddFieldPass(
	FRDGBuilder& GraphBuilder,
	const FMCChunkParamsCPU& ChunkParams,
	const FVoxelNoiseParamsCPU& NoiseParamsCPU,
	const FVoxelMaterialGenerationSettings& MaterialSettings,
	const FVoxelEditParams& EditParams)
{
	FVoxelFieldPassOutputs Out;

	const uint32 SamplesPerAxis = ChunkParams.CellsPerAxis + 1u;
	const uint32 NumSamples = SamplesPerAxis * SamplesPerAxis * SamplesPerAxis;

	FRDGBufferDesc DensityDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), NumSamples);
	Out.DensityField = GraphBuilder.CreateBuffer(DensityDesc, TEXT("Voxel.Field.Density"));

	FRDGBufferDesc MaterialDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), NumSamples);
	Out.MaterialField = GraphBuilder.CreateBuffer(MaterialDesc, TEXT("Voxel.Field.Material"));

	Out.SamplesPerAxis = SamplesPerAxis;
	Out.NumSamples = NumSamples;

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.DensityField), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Out.MaterialField), 0);

	auto* PassParams = GraphBuilder.AllocParameters<FVoxelFieldGenCS::FParameters>();
	PassParams->ChunkOriginWS = FVector3f(ChunkParams.ChunkOriginWS);
	PassParams->StepSizeWS = ChunkParams.StepSizeWS;
	PassParams->CellsPerAxis = ChunkParams.CellsPerAxis;
	PassParams->SamplesPerAxis = SamplesPerAxis;
	PassParams->IsoLevel = ChunkParams.IsoLevel;
	PassParams->ChunkSeed = ChunkParams.ChunkSeed;

	const FVoxelNoiseParams NoiseParamsGPU = MakeVoxelNoiseParams(NoiseParamsCPU);
	FRDGBufferRef NoiseParamsBuffer =
		CreateStructuredBuffer(
			GraphBuilder,
			TEXT("Voxel.NoiseParams.Field"),
			sizeof(FVoxelNoiseParams),
			1,
			&NoiseParamsGPU,
			sizeof(FVoxelNoiseParams));
	PassParams->NoiseParamsBuf = GraphBuilder.CreateSRV(NoiseParamsBuffer);

	PassParams->EditStampCount = EditParams.EditStampCount;
	PassParams->EditStamps = GraphBuilder.CreateSRV(EditParams.EditStamps);

	const FVoxelBiomeMapSettings& BiomeMap = MaterialSettings.BiomeMapSettings;
	const FVoxelBiomeThresholds& BiomeThresholds = MaterialSettings.BiomeThresholds;
	const FVoxelBiomeMaterialRules& BiomeRules = MaterialSettings.BiomeMaterialRules;

	PassParams->BiomeTempMoistureFreq = FVector2f(BiomeMap.TemperatureFrequency, BiomeMap.MoistureFrequency);
	PassParams->BiomeTempOffset = BiomeMap.TemperatureOffset;
	PassParams->BiomeMoistureOffset = BiomeMap.MoistureOffset;
	PassParams->BiomeTempMoistureHeightInfluence = FVector2f(BiomeMap.TemperatureHeightInfluence, BiomeMap.MoistureHeightInfluence);
	PassParams->BiomeDomainWarpFrequency = BiomeMap.DomainWarpFrequency;
	PassParams->BiomeDomainWarpAmplitude = BiomeMap.DomainWarpAmplitude;
	PassParams->BiomeSeaLevel = BiomeThresholds.SeaLevel;
	PassParams->BiomeColdTemperature = BiomeThresholds.ColdTemperature;
	PassParams->BiomeHotTemperature = BiomeThresholds.HotTemperature;
	PassParams->BiomeDryMoisture = BiomeThresholds.DryMoisture;
	PassParams->BiomeWetMoisture = BiomeThresholds.WetMoisture;
	PassParams->BiomeSeed = static_cast<uint32>(ChunkParams.ChunkSeed) ^ static_cast<uint32>(BiomeMap.SeedSalt);

	PassParams->DesertSurfaceMaterialId = BiomeRules.Desert.SurfaceMaterialId;
	PassParams->DesertSubsurfaceMaterialId = BiomeRules.Desert.SubsurfaceMaterialId;
	PassParams->DesertDeepMaterialId = BiomeRules.Desert.DeepMaterialId;
	PassParams->DesertSubsurfaceZRange = FVector2f(BiomeRules.Desert.SubsurfaceMinZ, BiomeRules.Desert.SubsurfaceMaxZ);

	PassParams->PlainsSurfaceMaterialId = BiomeRules.Plains.SurfaceMaterialId;
	PassParams->PlainsSubsurfaceMaterialId = BiomeRules.Plains.SubsurfaceMaterialId;
	PassParams->PlainsDeepMaterialId = BiomeRules.Plains.DeepMaterialId;
	PassParams->PlainsSubsurfaceZRange = FVector2f(BiomeRules.Plains.SubsurfaceMinZ, BiomeRules.Plains.SubsurfaceMaxZ);

	PassParams->TundraSurfaceMaterialId = BiomeRules.Tundra.SurfaceMaterialId;
	PassParams->TundraSubsurfaceMaterialId = BiomeRules.Tundra.SubsurfaceMaterialId;
	PassParams->TundraDeepMaterialId = BiomeRules.Tundra.DeepMaterialId;
	PassParams->TundraSubsurfaceZRange = FVector2f(BiomeRules.Tundra.SubsurfaceMinZ, BiomeRules.Tundra.SubsurfaceMaxZ);

	const bool bHasOre = MaterialSettings.OreSettings.Num() > 0;
	PassParams->bHasOre = bHasOre ? 1u : 0u;
	const FVoxelOreSettings Ore = bHasOre ? MaterialSettings.OreSettings[0] : FVoxelOreSettings();
	PassParams->OreMaterialId = Ore.MaterialId;
	PassParams->OreBaseMaterialId = Ore.BaseMaterialId;
	PassParams->OreDepthZRange = FVector2f(Ore.DepthMinZ, Ore.DepthMaxZ);
	PassParams->OreVeinFrequency = Ore.VeinFrequency;
	PassParams->OreVeinThreshold = Ore.VeinThreshold;
	PassParams->OreRarity = Ore.Rarity;
	PassParams->OreNodeSizeWS = Ore.NodeSizeWS;
	PassParams->OreSeed = static_cast<uint32>(ChunkParams.ChunkSeed) ^ static_cast<uint32>(Ore.SeedSalt);

	PassParams->OutDensity = GraphBuilder.CreateUAV(Out.DensityField);
	PassParams->OutMaterial = GraphBuilder.CreateUAV(Out.MaterialField);

	TShaderMapRef<FVoxelFieldGenCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	const FIntVector Groups(
		FMath::DivideAndRoundUp((int32)SamplesPerAxis, 8),
		FMath::DivideAndRoundUp((int32)SamplesPerAxis, 8),
		FMath::DivideAndRoundUp((int32)SamplesPerAxis, 8));

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Voxel.Field.Generate Samples=%u", NumSamples),
		CS,
		PassParams,
		Groups);

	return Out;
}
