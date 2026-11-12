#include "EnginePCH.h"
#include "AssetManager.h"
#include "Render/Buffer.h"
#include "Render/Material.h"

#include "Render/Vertex.h"
#include "Render/Texture2D.h"
#include "Render/RenderCommand.h"

#include "Render/RenderResourceManager.h"

#include "Asset/ObjImporter/ObjImporter.h"
#include "Asset/FBXImporter/FbxImporter.h"
#include "Asset/FBXImporter/SkeletalMeshBake.h"
#include "Components/SkinnedMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/CommonAnimClips.h"
#include "Animation/AnimNotifyFile.h"
#include "UObject/UObjectGlobals.h"

#include "Render/GeometryGenerator.h"
#include "UObject/UObjectIterator.h"

#include "Render/ImageLoader.h"
#include "Core/ResourcePaths.h"
#include <algorithm>
#include <cctype>
#include "Core/Misc/FileHelper.h"


namespace
{
	FString GetAssetExtension(const FString& Path) {
		const std::u8string UTF8Path{ Path.begin(), Path.end() };
		FString Extension{ std::filesystem::path{ UTF8Path }.extension().string() };
		for (char& Character : Extension) {
			Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		}
		return Extension;
	}

	FString ToUTF8String(const std::filesystem::path& Path)
	{
		const std::u8string UTF8Path = Path.generic_u8string();
		return FString{ UTF8Path.begin(), UTF8Path.end() };
	}

	UStaticMesh* CreateStaticMesh(const FStaticMeshData* MeshData)
	{
		if (!MeshData || !MeshData->Vertices.Num() || !MeshData->Indices.Num())
			return nullptr;

		FStaticMeshData Optimized = *MeshData;

		// Mesh에 MaterialSlot 없을 경우 Default Material 할당
		if (Optimized.MaterialSlots.IsEmpty())
		{
			FStaticMaterialSlot DefaultSlot;
			DefaultSlot.Name = "Default";
			Optimized.MaterialSlots.Add(DefaultSlot);
		}

		// Mesh에 Section이 없을 경우 전체 메쉬을 섹션 하나로 세팅
		if (Optimized.Sections.IsEmpty())
		{
			FStaticMeshSection DefaultSection;
			DefaultSection.StartIndex = 0;
			DefaultSection.IndexCount = static_cast<uint32>(Optimized.Indices.Num());
			DefaultSection.MaterialSlotIndex = 0;

			Optimized.Sections.Add(DefaultSection);
		}

		Optimized.BuildTangentSpace();
		Optimized.OptimizeTriangleOrderForVertexCache();

		TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Optimized.Vertices.GetData(), sizeof(FVertex) * static_cast<uint32>(Optimized.Vertices.size()), sizeof(FVertex));
		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(Optimized.Indices.GetData(), static_cast<uint32>(Optimized.Indices.size()));
		if (VB == nullptr || IB == nullptr) return nullptr;

		UStaticMesh* Mesh = NewObject<UStaticMesh>();
		Mesh->MeshData = std::move(Optimized);

		// Vertex/Index GPU 업로드
		Mesh->VertexBuffer = std::move(VB);
		Mesh->IndexBuffer = std::move(IB);

		UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!DefaultMaterial)
		{
			return nullptr;
		}

		// MaterialSlots를 실제 UMaterial로 변환
		for (const FStaticMaterialSlot& Slot : Mesh->MeshData.MaterialSlots)
		{
			UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);
			if (!Material)
			{
				return nullptr;
			}
			Material->BaseColor = Slot.BaseColor;
			Material->SetBlendState(Material->BaseColor.W < 1.0f ? EBlendState::AlphaBlend : EBlendState::Opaque);

			if (!Slot.DiffuseTexturePath.empty())
			{
				UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.DiffuseTexturePath);

				if (Texture)
				{
					Material->Textures[0] = Texture;
				}
			}

			if (!Slot.NormalTexturePath.empty()) {
				if (UTexture2D* NormalTexture{ UAssetManager::Get().LoadTexture(Slot.NormalTexturePath) }) {
					Material->Textures[1] = NormalTexture;
				}
			}

			Mesh->Materials.Add(Material);
		}

		Mesh->MeshData.BuildTriangleBVH();
		Mesh->RebuildRenderData();

		return Mesh;
	}

	USkeletalMesh* CreateSkeletalMesh(const FSkeletalMeshData* MeshData)
	{
		if (!MeshData || MeshData->Meshes.IsEmpty()) return nullptr;

		TArray<FVertex> RenderVertices;
		TArray<uint32> RenderIndices;
		TArray<FSkeletalMeshSection> RenderSections;

		uint32 BaseMaterial = 0;

		for (int i = 0; i < MeshData->Meshes.Num(); i++) // 전체 Mesh에 대하여 VertexBuffer,IndexBuffer 종합하여 추가
		{
			uint32 BaseVertex = static_cast<uint32>(RenderVertices.Num());
			uint32 BaseIndex = RenderIndices.Num();

			for (int j = 0; j < MeshData->Meshes[i].Vertices.Num(); j++)
			{
				RenderVertices.Add(MeshData->Meshes[i].Vertices[j].Vertex);
			}

			for (int j = 0; j < MeshData->Meshes[i].Indices.Num(); j++)
			{
				RenderIndices.Add(BaseVertex + MeshData->Meshes[i].Indices[j]); // Mesh마다 Index가 0으로 돌아가므로 겹치는것을 방지하기 위해 BaseVertex만큼 Offset 추가
			}
			for (int j = 0; j < MeshData->Meshes[i].Sections.Num(); j++)
			{
				FSkeletalMeshSection Section = MeshData->Meshes[i].Sections[j];
				Section.StartIndex += BaseIndex;	// Mesh마다 Index 시작 겹치므로 Offset 추가
				Section.MaterialSlotIndex += BaseMaterial;	// Mesh마다 Material의 Index 시작 겹치므로 Offset 추가
				RenderSections.Add(Section);
			}
			BaseMaterial += static_cast<uint32>(MeshData->Meshes[i].MaterialSlots.Num());	// 한 Mesh의 MaterialSlot 추가가 끝날때마다 BaseMaterial에 전체 슬롯 개수만큼 더해줌 => 현재의 전체 Material 개수 표시
		}

		if (RenderVertices.IsEmpty() || RenderIndices.IsEmpty()) return nullptr;

		BuildMeshTangentSpace(RenderVertices, RenderIndices);

		// GPU 스키닝용 정점: 탄젠트까지 계산한 FVertex에 본 행렬 버퍼 Index와 가중치를 붙인다.
		// 파트마다 본 행렬 BoneStride칸을 쓰고, 마지막 칸(+ NumBones)은 가중치 없는 정점용 MeshTransform이다.
		const uint32 NumBones = static_cast<uint32>(MeshData->Bones.Num());
		const uint32 BoneStride = NumBones + 1;
		TArray<FSkeletalVertex> GPUVertices;
		GPUVertices.Reserve(RenderVertices.Num());
		for (int i = 0; i < MeshData->Meshes.Num(); i++)
		{
			const uint32 PartOffset = static_cast<uint32>(i) * BoneStride;
			for (const FSkeletalVertex& Source : MeshData->Meshes[i].Vertices)
			{
				FSkeletalVertex& Out = GPUVertices.AddDefaulted_GetRef();
				Out.Vertex = RenderVertices[GPUVertices.Num() - 1];

				// CPU 스키닝처럼 유효한 영향만 남기고 합이 1이 되게 다시 나눈다.
				float TotalWeight = 0.0f;
				int32 Count = 0;
				for (int32 Slot = 0; Slot < 4; ++Slot)
				{
					if (!IsValidBoneInfluence(Source, Slot, NumBones))
					{
						continue;
					}
					Out.BoneIndices[Count] = PartOffset + Source.BoneIndices[Slot];
					Out.BoneWeights[Count] = Source.BoneWeights[Slot];
					TotalWeight += Source.BoneWeights[Slot];
					++Count;
				}

				if (TotalWeight > 0.0f)
				{
					for (int32 Slot = 0; Slot < Count; ++Slot)
						Out.BoneWeights[Slot] /= TotalWeight;
				}
				else
				{
					Out.BoneIndices[0] = PartOffset + NumBones;
					Out.BoneWeights[0] = 1.0f;
				}
			}
		}

		TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(GPUVertices.GetData(), sizeof(FSkeletalVertex) * static_cast<uint32>(GPUVertices.Num()), sizeof(FSkeletalVertex));
		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(RenderIndices.GetData(), static_cast<uint32>(RenderIndices.size()));
		
		if (VB == nullptr || IB == nullptr) return nullptr;

		USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
		Mesh->MeshData = *MeshData;

		// Vertex/Index GPU 업로드
		Mesh->VertexBuffer = std::move(VB);
		Mesh->IndexBuffer = std::move(IB);
		Mesh->Sections = std::move(RenderSections);	// Section 정보 저장
		Mesh->BoneStride = BoneStride;

		UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!DefaultMaterial)
		{
			return nullptr;
		}

		// MaterialSlots를 실제 UMaterial로 변환
		for (int k = 0; k < Mesh->MeshData.Meshes.Num(); k++)
		{
			for (const FSkeletalMaterialSlot& Slot : Mesh->MeshData.Meshes[k].MaterialSlots)
			{
				UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);
				if (!Material)
				{
					return nullptr;
				}
				Material->BaseColor = Slot.BaseColor;
				Material->SetBlendState(Material->BaseColor.W < 1.0f ? EBlendState::AlphaBlend : EBlendState::Opaque);

				if (!Slot.BaseColorTexturePath.empty())
				{
					UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.BaseColorTexturePath);

					if (Texture)
					{
						Material->Textures[0] = Texture;
					}
				}

				if (!Slot.NormalTexturePath.empty())
				{
					if (UTexture2D * NormalTexture{ UAssetManager::Get().LoadTexture(Slot.NormalTexturePath) })
					{
						Material->Textures[1] = NormalTexture;
					}
				}

				Mesh->Materials.Add(Material);
			}
		}
		// 생성한 메시의 Bounds를 계산하고 등록된 Proxy에 변경을 알린다.
		Mesh->RebuildRenderData();
		return Mesh;
	}


	FString MakeAssetKey(const FString& Path)
	{
		return ToUTF8String(fs::relative(FFileHelper::ToNativePath(Path), "Assets"));
	}
}

UAssetManager& UAssetManager::Get()
{
	static UAssetManager* Instance = NewObject<UAssetManager>();
	return *Instance;
}

void UAssetManager::Init(const FAssetLoadProgress& OnProgress)
{
	FGeometryGenerator::CreateDefaultMeshDatas();
	// 머티리얼이 참조하므로 반드시 먼저 만든다
	Get().CreateDefaultTextures();
	Get().CreateDefaultMaterial();
	// Resources는 스캔하지 않으므로 엔진이 쓰는 기본 폰트는 경로로 직접 등록한다.
	Get().LoadFontAtlas(ResourcePaths::DefaultFont, ResourcePaths::DefaultFontAtlas);
	Get().ScanAssets("Assets", OnProgress);
	Get().CreateDefaultMeshes();
	Get().CreateParticleMaterial();
	Get().CreateEditorArrowMaterial();
}

void UAssetManager::ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress)
{
	if (!fs::exists(AssetRoot))
	{
		HTR_LOG(Error, "Asset root not found: {}", AssetRoot.generic_string());
		return;
	}

	TArray<fs::path> Files;
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(AssetRoot))
	{
		if (Entry.is_regular_file())
		{
			Files.Add(Entry.path());
		}
	}

	const int32 Total = Files.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		FString Key = ToUTF8String(fs::relative(Files[i], AssetRoot));
		FString Path = ToUTF8String(Files[i]);
		AssetPathMap.Add(Key, Path);
		if (GetAssetExtension(Path) != ".blendspace" && GetAssetExtension(Path) != ".animsm") LoadAsset(Key, Path);

		if (OnProgress)
		{
			OnProgress(i + 1, Total, Key);
		}
	}
	// Resolve composite assets after all animation sub-assets have been registered.
	for (const fs::path& File : Files)
	{
		if (GetAssetExtension(ToUTF8String(File)) == ".blendspace")
			LoadAsset(ToUTF8String(fs::relative(File, AssetRoot)), ToUTF8String(File));
	}
	for (const fs::path& File : Files)
		if (GetAssetExtension(ToUTF8String(File)) == ".animsm") LoadAsset(ToUTF8String(fs::relative(File, AssetRoot)), ToUTF8String(File));
}

void UAssetManager::LoadAsset(const FString& Key, const FString& Path) {
	const FString Extension{ GetAssetExtension(Path) };
	if (Extension == ".animgraph") { FString Error; if (!LoadAnimGraph(Path, Error)) HTR_LOG(Error, "[AnimGraph] {}: {}", Path, Error); return; }
	if (Extension == ".animsm") { FString Error; if (!LoadAnimStateMachine(Path, Error)) HTR_LOG(Error, "[AnimStateMachine] {}: {}", Path, Error); return; }
	if (Extension == ".blendspace")
	{
		FString Error;
		if (!LoadBlendSpace(Path, Error)) HTR_LOG(Error, "[BlendSpace] {}: {}", Path, Error);
		return;
	}
	if (Extension == ".png" || Extension == ".dds") {
		if (UTexture2D* Texture{ LoadTexture(Path) }) {
			AssetMap[Key] = Texture;
		}
	}
	if (Extension == ".json") {
		const FString TexturePath{ fs::path{ Path }.replace_extension(".png").string() };
		if (UFont* Font{ LoadFontAtlas(Path, TexturePath) }) {
			AssetMap[Key] = Font;
		}
	}
	if (Extension == ".obj") {
		LoadObjStaticMesh(Path);
	}
	if (Extension == ".fbx" || IsFbxBakePath(FFileHelper::ToNativePath(Path)))
	{
		LoadFbxAssets(Path);
	}
}

void UAssetManager::CreateDefaultTextures() {
	// 텍스처가 지정되지 않은 머티리얼이 검게 나오지 않도록 하는 1x1 흰색 텍스처.
	// 셰이더에서 곱해도 결과가 변하지 않으므로 "텍스처 없음"의 기본값으로 쓴다.
	const uint32 WhitePixel{ 0xFFFFFFFF };

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_IMMUTABLE;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	TUniquePtr<FTexture2D> Resource{ RenderCommand::CreateTexture2D(Desc, &WhitePixel) };
	if (!Resource) {
		return;
	}

	UTexture2D* WhiteTexture{ NewObject<UTexture2D>() };
	WhiteTexture->SetResource(std::move(Resource));
	WhiteTexture->SetPath("WhiteTexture");

	AssetMap["WhiteTexture"] = WhiteTexture;

	const FVector4 NormalPixel{ 0.5f, 0.5f, 1.0f, 1.0f };
	Desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	TUniquePtr<FTexture2D> NormalResource{ RenderCommand::CreateTexture2D(Desc, &NormalPixel) };
	if (!NormalResource) {
		return;
	}

	UTexture2D* FlatNormalTexture{ NewObject<UTexture2D>() };
	FlatNormalTexture->SetResource(std::move(NormalResource));
	FlatNormalTexture->SetPath("FlatNormalTexture");
	AssetMap["FlatNormalTexture"] = FlatNormalTexture;
}

void UAssetManager::CreateDefaultMeshes()
{
	// 메시 데이터 업로드
	FStaticMeshData* CubeData = FGeometryGenerator::GetMeshData("Cube");
	RegisterAsset("Cube", CreateStaticMesh(CubeData));

	FStaticMeshData* ConeData = FGeometryGenerator::GetMeshData("Cone");
	RegisterAsset("Cone", CreateStaticMesh(ConeData));

	FStaticMeshData* SphereData = FGeometryGenerator::GetMeshData("Sphere");
	RegisterAsset("Sphere", CreateStaticMesh(SphereData));

	FStaticMeshData* CylinderData = FGeometryGenerator::GetMeshData("Cylinder");
	RegisterAsset("Cylinder", CreateStaticMesh(CylinderData));

	FStaticMeshData* PlaneData = FGeometryGenerator::GetMeshData("Plane");
	RegisterAsset("Plane", CreateStaticMesh(PlaneData));

	FStaticMeshData* ArrowMeshData = FGeometryGenerator::GetMeshData("Arrow");
	RegisterAsset("Arrow", CreateStaticMesh(ArrowMeshData));

	FStaticMeshData* RingMeshData = FGeometryGenerator::GetMeshData("Ring");
	RegisterAsset("Ring", CreateStaticMesh(RingMeshData));

	FStaticMeshData* ScaleBarData = FGeometryGenerator::GetMeshData("ScaleBar");
	RegisterAsset("ScaleBar", CreateStaticMesh(ScaleBarData));

	FStaticMeshData* GizmoSphereData = FGeometryGenerator::GetMeshData("GizmoSphere");
	RegisterAsset("GizmoSphere", CreateStaticMesh(GizmoSphereData));

	FStaticMeshData* OctahedralBoneData = FGeometryGenerator::GetMeshData("OctahedralBone");
	RegisterAsset("OctahedralBone", CreateStaticMesh(OctahedralBoneData));

	const FParticleVertex Vertices[] =
	{
		{ FVector(0.0f, -0.5f,  0.5f), FVector2(0.0f, 0.0f) },
		{ FVector(0.0f, 0.5f,  0.5f), FVector2(1.0f, 0.0f) },
		{ FVector(0.0f, 0.5f, -0.5f), FVector2(1.0f, 1.0f) },
		{ FVector(0.0f, -0.5f, -0.5f), FVector2(0.0f, 1.0f) }
	};

	const uint32 Indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	UStaticMesh* Mesh = NewObject<UStaticMesh>();
	Mesh->VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
		Vertices,
		sizeof(Vertices), sizeof(FParticleVertex));
	Mesh->IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
		Indices,
		ARRAYSIZE(Indices));

	FVertex Vertex{};
	Vertex.Position = FVector(0.0f, -0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, -0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);

	Mesh->MeshData.Indices = { 0, 1, 2, 0, 2, 3, 0,2,1, 0,3,2 };

	Mesh->MeshData.AABB.Min = FVector(-0.001f, -0.5, -0.5);
	Mesh->MeshData.AABB.Max = FVector(0.001f, 0.5, 0.5);
	Mesh->RebuildRenderData();

	RegisterAsset("ParticleQuad", Mesh);
}

void UAssetManager::CreateDefaultMaterial() {
	UMaterial* DefaultMat{ NewObject<UMaterial>() };
	DefaultMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/UberLit.hlsl");
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("FlatNormalTexture"));
	DefaultMat->ParamLayout = EMaterialParamLayout::StaticMesh;
	DefaultMat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
	RegisterAsset("DefaultMaterial", DefaultMat);
}

void UAssetManager::CreateParticleMaterial()
{
	UMaterial* ParticleMat = NewObject<UMaterial>();
	ParticleMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ParticleSubUVShader.hlsl");
	ParticleMat->Textures.Add(LoadTexture(ResourcePaths::DefaultParticleTexture));
	ParticleMat->BlendState = EBlendState::AlphaBlend;
	ParticleMat->DepthStencilState = EDepthStencilState::ReadOnly;
	ParticleMat->ParamLayout = EMaterialParamLayout::ParticleSubUV;
	ParticleMat->ParamBuffer = RenderCommand::CreateConstantBuffer(256);
	RegisterAsset("SubUVMaterial", ParticleMat);
}

void UAssetManager::CreateEditorArrowMaterial() {
	UMaterial* BaseMaterial{ GetAssetByPath<UMaterial>("DefaultMaterial") };
	UMaterial* ArrowMaterial{ UMaterial::CreateInstance(BaseMaterial) };
	ArrowMaterial->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/EditorArrowShader.hlsl");
	ArrowMaterial->BaseColor = FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	RegisterAsset("EditorArrowMaterial", ArrowMaterial);
}

void UAssetManager::Shutdown()
{
	Get().FbxAssets.Empty();
	Get().AssetMap.Empty();
}


void UAssetManager::RegisterAsset(const FString& Key, URenderAsset* Asset)
{
	Asset->SetPath(Key);
	AssetMap[Key] = Asset;
}

void UAssetManager::UnregisterAsset(const FString& Key, const URenderAsset* ExpectedAsset)
{
	// 다른 객체로 교체된 등록의 유지
	URenderAsset** RegisteredAsset = AssetMap.Find(Key);
	if (RegisteredAsset && *RegisteredAsset == ExpectedAsset)
	{
		AssetMap.Remove(Key);
	}
}

UTexture2D* UAssetManager::LoadTexture(const FString& InPath, bool GenerateMips) {
	if (URenderAsset** Found{ AssetMap.Find(InPath) }) {
		return Cast<UTexture2D>(*Found);
	}

	TUniquePtr<FTexture2D> Resource{};
	if (GetAssetExtension(InPath) == ".dds") {
		Resource = RenderCommand::LoadDDSTexture(InPath);
	} else {
		const FImageData Data{ ImageLoader::LoadAuto(InPath) };
		if (!Data.IsValid()) {
			return nullptr;
		}

		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = Data.Width;
		Desc.Height = Data.Height;
		Desc.ArraySize = 1;
		Desc.Format = Data.Format;
		Desc.SampleDesc.Count = 1;
		Desc.MipLevels = GenerateMips ? 0 : 1;
		Desc.Usage = GenerateMips ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (GenerateMips ? D3D11_BIND_RENDER_TARGET : 0);
		Desc.MiscFlags = GenerateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
		Resource = RenderCommand::CreateTexture2D(Desc, Data);
	}

	if (!Resource || !Resource->GetSRV()) {
		return nullptr;
	}

	UTexture2D* Asset{ NewObject<UTexture2D>() };
	Asset->SetResource(std::move(Resource));
	RegisterAsset(InPath, Asset);

	return Asset;
}

UFont* UAssetManager::LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath)
{
	UFont* Font = NewObject<UFont>();

	if (!Font->LoadFontAtlasJson(JsonPath))
	{
		return nullptr;
	}

	Font->AtlasTexture = LoadTexture(AtlasTexturePath, false);
	if (!Font->AtlasTexture)
	{
		return nullptr;
	}

	RegisterAsset(JsonPath, Font);
	return Font;
}

UStaticMesh* UAssetManager::LoadObjStaticMesh(const FString& Path)
{
	const FString Key = MakeAssetKey(Path);
	if (UStaticMesh* Cached = GetAssetByPath<UStaticMesh>(Key))
	{
		return Cached;
	}

	TUniquePtr<FStaticMeshData> Data = FObjImporter::LoadStaticMeshData(Path);
	UStaticMesh* Mesh = CreateStaticMesh(Data.get());   // Data가 nullptr이면 nullptr 반환
	if (!Mesh)
	{
		return nullptr;
	}

	Get().RegisterAsset(Key, Mesh);
	return Mesh;
}

bool UAssetManager::IsFbxBakePath(const fs::path& Path)
{
	return _stricmp(Path.extension().string().c_str(), ".bin") == 0 &&
		_stricmp(Path.stem().extension().string().c_str(), ".fbx") == 0;
}

const FFbxAssetSet* UAssetManager::LoadFbxAssets(const FString& Path)
{
	fs::path SourceFile(std::u8string(Path.begin(), Path.end()));
	const bool bOpenBake = IsFbxBakePath(SourceFile);
	if (bOpenBake) SourceFile.replace_extension();

	const FString SourcePath = bOpenBake ? ToUTF8String(SourceFile) : Path;
	const FString Key = MakeAssetKey(SourcePath);
	if (const FFbxAssetSet* Cached = FbxAssets.Find(Key))
		return Cached;

	std::error_code SourceError;
	const auto SourceTime = fs::last_write_time(SourceFile, SourceError);
	const bool bStandaloneBake = bOpenBake && SourceError == std::errc::no_such_file_or_directory;
	const std::optional<fs::file_time_type> ExpectedSourceTime =
		SourceError ? std::nullopt : std::optional{SourceTime};
	FFbxCookedData Data;
	const bool LoadedFromBake = (!SourceError || bStandaloneBake) &&
		FSkeletalMeshBake::ReadBaked(SourcePath, ExpectedSourceTime, Data);
	if (!LoadedFromBake)
	{
		if (bOpenBake && SourceError)
		{
			HTR_LOG(Error, "[FBX] Cannot load bake without source FBX: {}", Path);
			return nullptr;
		}

		FFbxImporter Importer;
		FFbxImportResult ImportResult = Importer.ImportFBX(SourcePath, Key);
		if (!ImportResult.bSuccess)
		{
			HTR_LOG(Error, "[FBX] Scene import failed: {}", Path);
			return nullptr;
		}
		Importer.Cook(std::move(ImportResult), Data);
	}

	if (Data.SkeletalMesh.Meshes.IsEmpty() && Data.StaticMeshes.IsEmpty())
	{
		HTR_LOG(Warning, "[FBX] No renderable mesh in {}", Path);
		return nullptr;
	}

	FFbxAssetSet Assets;
	if (!Data.SkeletalMesh.Meshes.IsEmpty())
	{
		Assets.SkeletalMesh = GetAssetByPath<USkeletalMesh>(Key);
		if (!Assets.SkeletalMesh)
		{
			Assets.SkeletalMesh = CreateSkeletalMesh(&Data.SkeletalMesh);
			if (!Assets.SkeletalMesh)
			{
				HTR_LOG(Error, "[FBX] Skeletal mesh resource creation failed: {}", Path);
				return nullptr;
			}
			RegisterAsset(Key, Assets.SkeletalMesh);
		}
	}

	// 메시 포인터를 SetSkeletalMesh에 넘겨야 하므로 메시 등록 뒤에 클립을 등록한다.
	for (int32 Index = 0; Index < Data.Anims.Num(); ++Index)
	{
		const FFbxAnimClip& Clip = Data.Anims[Index];
		// "Armature|Idle"처럼 구분자가 섞인 이름도 키 한 칸에 들어가도록 경로 문자를 바꾼다.
		FString ClipName = Clip.Name.empty() ? "Anim_" + std::to_string(Index) : Clip.Name;
		std::replace(ClipName.begin(), ClipName.end(), '/', '_');
		std::replace(ClipName.begin(), ClipName.end(), '\\', '_');
		FString AnimKey = Key + "#Anim/" + ClipName;

		// 이미 재생 노드가 쥐고 있을 수 있는 포인터라서 새로 만들지 않고 그대로 쓴다.
		UAnimSequence* Sequence = GetAssetByPath<UAnimSequence>(AnimKey);
		// 같은 FBX 안에서 이름이 겹치면 두 번째 클립을 버리지 않도록 인덱스를 붙인다.
		if (Sequence && Assets.Animations.Find(Sequence) != INDEX_NONE)
		{
			ClipName += "_" + std::to_string(Index);
			AnimKey = Key + "#Anim/" + ClipName;
			Sequence = GetAssetByPath<UAnimSequence>(AnimKey);
		}
		if (!Sequence)
		{
			Sequence = NewObject<UAnimSequence>();
			Sequence->SetName(FName(ClipName));
			Sequence->BuildFromTracks(Clip.FrameRate, Clip.NumFrames, Clip.Tracks);
			Sequence->SetSkeletalMesh(Assets.SkeletalMesh);
			RegisterAsset(AnimKey, Sequence);
		}
		// 루트 처리 방식은 공유 에셋의 성질이라, 어떤 AnimInstance가 먼저 쓰든 등록 시점에 한 번 정해 둔다
		if (const FCommonAnimClip* Common = CommonAnimClips::FindByAssetKey(AnimKey.c_str()))
		{
			Sequence->SetEnableRootMotion(Common->RootMode == ECommonClipRootMode::RootMotion);
			Sequence->SetForceRootLock(Common->RootMode == ECommonClipRootMode::ForceLock);
		}

		// 재사용한 경우에도 넣어야 인덱스로 접근하는 코드와 순서가 맞는다.
		Assets.Animations.Add(Sequence);
	}

	FAnimNotifyFile::Load(SourcePath, Assets.Animations);

	const FString SourceName = ToUTF8String(SourceFile.stem());
	for (int32 Index = 0; Index < Data.StaticMeshes.Num(); ++Index)
	{
		const FFbxStaticMeshPart& Part = Data.StaticMeshes[Index];
		FString Name = Part.NodeName.empty() ? "Mesh" : Part.NodeName;
		std::replace(Name.begin(), Name.end(), '/', '_');
		std::replace(Name.begin(), Name.end(), '\\', '_');
		const FString StaticPrefix = Key + "#Static/" + std::to_string(Index);
		const FString StaticKey = StaticPrefix + "/" + SourceName + "_" + Name;

		UStaticMesh* Mesh = GetAssetByPath<UStaticMesh>(StaticKey);
		if (!Mesh)
		{
			FString Error;
			if (!Part.MeshData.Validate(Error))
			{
				HTR_LOG(Error, "[FBX] Invalid static mesh {}: {}", StaticKey, Error);
				return nullptr;
			}
			Mesh = CreateStaticMesh(&Part.MeshData);
			if (!Mesh)
			{
				HTR_LOG(Error, "[FBX] Static mesh resource creation failed: {}", StaticKey);
				return nullptr;
			}
			RegisterAsset(StaticKey, Mesh);
		}
		// 이전 레벨 파일이 저장한 인덱스·노드 이름 기반 키도 같은 에셋을 가리키도록 한다.
		AssetMap[StaticPrefix + "_" + Name] = Mesh;
		Assets.StaticMeshes.Add(FFbxStaticAsset{Part.NodeName, Part.NodeTransform, Mesh});
	}

	if (!LoadedFromBake && !SourceError)
		FSkeletalMeshBake::WriteBaked(SourcePath, SourceTime, Data);

	FbxAssets.Add(Key, std::move(Assets));
	return FbxAssets.Find(Key);
}

bool UAssetManager::SaveSkeletalMesh(USkeletalMesh* Mesh)
{
	if (!Mesh || Mesh->MeshData.Meshes.IsEmpty())
	{
		HTR_LOG(Warning, "[FBX] Cannot save empty skeletal mesh");
		return false;
	}

	fs::path SourceFile = Mesh->GetPath();
	if (IsFbxBakePath(SourceFile))
	{
		SourceFile.replace_extension();
	}

	fs::path BinFile = SourceFile;
	BinFile += ".bin";

	if (!fs::exists(SourceFile) && !fs::exists(BinFile))
	{
		SourceFile = fs::path("Assets") / SourceFile;
		BinFile = fs::path("Assets") / BinFile;
	}

	const bool bFbxExists = fs::exists(SourceFile);
	const bool bBinExists = fs::exists(BinFile);

	if (!bFbxExists && !bBinExists)
	{
		HTR_LOG(Error, "[FBX] Cannot save: Neither source FBX nor baked .bin exists for: {}", Mesh->GetPath());
		return false;
	}

	int64 Stamp = 0;
	if (bFbxExists)
	{
		std::error_code Error;
		const auto SourceTime = fs::last_write_time(SourceFile, Error);
		if (!Error)
		{
			Stamp = SourceTime.time_since_epoch().count();
		}
	}

	const FString SourcePath = ToUTF8String(SourceFile);

	// 기존 캐시된 FBX 데이터(동일 FBX 내의 스태틱 메시 등)를 보존하기 위해 기존 .bin 데이터 읽기
	FFbxCookedData CookedData;
	FSkeletalMeshBake::ReadBaked(SourcePath, std::nullopt, CookedData);
	CookedData.SkeletalMesh = Mesh->MeshData;

	// 디스크의 .bin 바이너리 파일로 직렬화하여 영구 저장
	if (!FSkeletalMeshBake::WriteBakedDirect(BinFile, CookedData, Stamp))
	{
		HTR_LOG(Error, "[FBX] Failed to save .bin file: {}", BinFile.string());
		return false;
	}

	// 현재 씬에 배치된 모든 스켈레탈 메시 컴포넌트들을 새로운 RefPose 및 스키닝에 맞춰 갱신
	for (TObjectIterator<USkinnedMeshComponent> Itr; Itr; ++Itr)
	{
		USkinnedMeshComponent* Comp = *Itr;
		if (Comp && Comp->GetSkeletalMesh() == Mesh)
		{
			Comp->ResetPoseToBindPose();
			Comp->MarkRenderStateDirty();
		}
	}

	Mesh->RebuildRenderData();
	HTR_LOG(Info, "[FBX] Skeletal mesh successfully saved: {}", BinFile.string());
	return true;
}

bool UAssetManager::RestoreSkeletalMeshToOriginalFBX(USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		HTR_LOG(Warning, "[FBX] Cannot restore null skeletal mesh");
		return false;
	}

	fs::path SourceFile = Mesh->GetPath();
	if (IsFbxBakePath(SourceFile))
	{
		SourceFile.replace_extension();
	}
	if (!fs::exists(SourceFile))
	{
		SourceFile = fs::path("Assets") / SourceFile;
	}

	if (!fs::exists(SourceFile))
	{
		HTR_LOG(Warning, "[FBX] Cannot restore: Original source FBX file does not exist for: {} (standalone .bin asset)", Mesh->GetPath());
		return false;
	}

	std::error_code Error;
	const auto SourceTime = fs::last_write_time(SourceFile, Error);
	if (Error)
	{
		HTR_LOG(Error, "[FBX] Cannot get last write time for: {}", SourceFile.string());
		return false;
	}

	const FString SourcePath = ToUTF8String(SourceFile);
	const FString Key = MakeAssetKey(SourcePath);

	// FBX Importer로 원본 FBX 파일 순수 재임포트
	FFbxImporter Importer;
	FFbxImportResult ImportResult = Importer.ImportFBX(SourcePath, Key);
	if (!ImportResult.bSuccess)
	{
		HTR_LOG(Error, "[FBX] Failed to import original FBX: {}", SourcePath);
		return false;
	}

	FFbxCookedData CleanData;
	Importer.Cook(std::move(ImportResult), CleanData);
	if (CleanData.SkeletalMesh.Meshes.IsEmpty())
	{
		HTR_LOG(Error, "[FBX] No skeletal mesh found in original FBX: {}", SourcePath);
		return false;
	}

	// 1. 메모리의 MeshData 복원
	Mesh->MeshData = CleanData.SkeletalMesh;
	Mesh->RebuildRenderData();

	// 2. 디스크의 .bin 캐시 파일도 원본 클린 데이터로 다시 Bake하여 저장
	FSkeletalMeshBake::WriteBaked(SourcePath, SourceTime, CleanData);

	// 3. 현재 씬의 컴포넌트들 갱신
	for (TObjectIterator<USkinnedMeshComponent> Itr; Itr; ++Itr)
	{
		USkinnedMeshComponent* Comp = *Itr;
		if (Comp && Comp->GetSkeletalMesh() == Mesh)
		{
			Comp->ResetPoseToBindPose();
			Comp->MarkRenderStateDirty();
		}
	}

	HTR_LOG(Info, "[FBX] Skeletal mesh restored to original FBX pose: {}", SourcePath);
	return true;
}

USkeletalMesh* UAssetManager::SaveSkeletalMeshAsNewBin(USkeletalMesh* Mesh, const FString& NewAssetName, const FSkeletalMeshData& NewData)
{
	if (!Mesh || NewAssetName.empty() || NewData.Meshes.IsEmpty())
	{
		HTR_LOG(Warning, "[FBX] Invalid parameters for SaveSkeletalMeshAsNewBin");
		return nullptr;
	}

	fs::path SourceFile = Mesh->GetPath();
	if (IsFbxBakePath(SourceFile))
	{
		SourceFile.replace_extension();
	}
	if (!fs::exists(SourceFile))
	{
		SourceFile = fs::path("Assets") / SourceFile;
	}

	fs::path ParentDir = SourceFile.parent_path();
	if (ParentDir.empty())
	{
		ParentDir = fs::path("Assets") / "Models";
	}

	FString CleanName = NewAssetName;
	if (CleanName.ends_with(".bin")) CleanName = CleanName.substr(0, CleanName.length() - 4);
	if (CleanName.ends_with(".fbx")) CleanName = CleanName.substr(0, CleanName.length() - 4);
	CleanName += ".fbx.bin";

	fs::path TargetBinPath = ParentDir / CleanName;

	FFbxCookedData NewCookedData;
	const FString SourcePath = ToUTF8String(SourceFile);
	FSkeletalMeshBake::ReadBaked(SourcePath, std::nullopt, NewCookedData);
	NewCookedData.SkeletalMesh = NewData;

	int64 Stamp = 0;
	std::error_code Error;
	if (fs::exists(SourceFile))
	{
		auto ST = fs::last_write_time(SourceFile, Error);
		if (!Error) Stamp = ST.time_since_epoch().count();
	}

	if (!FSkeletalMeshBake::WriteBakedDirect(TargetBinPath, NewCookedData, Stamp))
	{
		HTR_LOG(Error, "[FBX] Failed to write new bin file: {}", TargetBinPath.string());
		return nullptr;
	}

	const FString TargetBinPathStr = ToUTF8String(TargetBinPath);
	const FFbxAssetSet* NewAssetSet = LoadFbxAssets(TargetBinPathStr);
	if (NewAssetSet && NewAssetSet->SkeletalMesh)
	{
		HTR_LOG(Info, "[FBX] Successfully saved and registered new skeletal mesh asset: {}", TargetBinPathStr);
		return NewAssetSet->SkeletalMesh;
	}

	return nullptr;
}


bool UAssetManager::ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate)
{
	if (Mesh.GetLODCount() > 1 || Mesh.MeshData.Vertices.IsEmpty())
		return false;   // LOD가 이미 옛 방향으로 만들어졌으면 서로 어긋난다

	FStaticMeshData& Data = Mesh.MeshData;
	FBox Bounds{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	for (FVertex& Vertex : Data.Vertices)
	{
		Vertex.Position = Rotate(Vertex.Position);
		Vertex.Normal = Rotate(Vertex.Normal);
		Bounds.Min = FVector(std::min(Bounds.Min.X, Vertex.Position.X), std::min(Bounds.Min.Y, Vertex.Position.Y), std::min(Bounds.Min.Z, Vertex.Position.Z));
		Bounds.Max = FVector(std::max(Bounds.Max.X, Vertex.Position.X), std::max(Bounds.Max.Y, Vertex.Position.Y), std::max(Bounds.Max.Z, Vertex.Position.Z));
	}
	Data.AABB = Bounds;
	Data.BuildTangentSpace();

	// 인덱스는 그대로다 (회전은 삼각형 감기 방향을 바꾸지 않는다). 정점 버퍼만 다시 올린다.
	TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Data.Vertices.GetData(),
		sizeof(FVertex) * static_cast<uint32>(Data.Vertices.Num()), sizeof(FVertex));
	if (!VB)
		return false;
	Mesh.VertexBuffer = std::move(VB);

	Data.BuildTriangleBVH();   // 피킹용 삼각형 BVH도 새 좌표로
	return true;
}

void UAssetManager::ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func)
{
	for (auto& [Key, Asset] : Get().AssetMap)
	{
		if (!Asset || !Asset->IsA<UStaticMesh>() || !fs::path(Key).extension().string().ends_with(".obj"))
			continue;
		Func(Key, *Cast<UStaticMesh>(Asset));
	}
}

FLODGenerateResult UAssetManager::GenerateStaticMeshLODs(UStaticMesh& Mesh, const FLODGenerateRequest& Request)
{
	TArray<FStaticMeshData> Generated;
	FLODGenerateResult Result = FStaticMeshLODGenerator::Generate(Mesh.GetMeshData(), Generated);

	if (!Result.bSuccess) return Result;

	if (!(Request.ScreenThresholds[0] >
		Request.ScreenThresholds[1] &&
		Request.ScreenThresholds[1] >
		Request.ScreenThresholds[2] &&
		Request.ScreenThresholds[2] > 0.0f))
	{
		Result.bSuccess = false;
		Result.FailureReason = "Screen thresholds must descend";
		return Result;
	}

	TArray<FStaticMeshLODResource> NewResources;

	for (FStaticMeshData& Data : Generated)
	{
		FStaticMeshLODResource Resource;
		Resource.Data = std::move(Data);

		Resource.Data.BuildTangentSpace();
		Resource.Data.OptimizeTriangleOrderForVertexCache();

		Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
			Resource.Data.Vertices.GetData(),
			sizeof(FVertex) *
			static_cast<uint32>(Resource.Data.Vertices.Num()),
			sizeof(FVertex));

		Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
			Resource.Data.Indices.GetData(),
			static_cast<uint32>(Resource.Data.Indices.Num()));

		if (!Resource.VertexBuffer || !Resource.IndexBuffer)
		{
			Result.bSuccess = false;
			Result.FailureReason = "LOD GPU buffer creation failed";
			return Result;
		}

		NewResources.Add(std::move(Resource));
	}

	// bSaveToAsset이면 이 지점에서 파일 저장.
	// 저장 실패 시 기존 Mesh.AdditionalLODs는 그대로 둔다.

	Mesh.AdditionalLODs = std::move(NewResources);
	Mesh.ScreenThresholds = Request.ScreenThresholds;
	Mesh.RebuildRenderData();
	return Result;
}
