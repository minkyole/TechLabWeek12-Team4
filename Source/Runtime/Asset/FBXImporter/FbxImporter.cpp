#include "EnginePCH.h"
#include "FbxImporter.h"
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace
{
	FMatrix ToEngineMatrix(const FbxAMatrix& Matrix)
	{
		// SDK로 원점과 축을 변환해 엔진의 행벡터 행렬로 옮긴다. 스케일과 전단도 보존한다.
		const FbxVector4 Origin = Matrix.MultT(FbxVector4(0.0, 0.0, 0.0, 1.0));
		const FbxVector4 X = Matrix.MultT(FbxVector4(1.0, 0.0, 0.0, 1.0)) - Origin;
		const FbxVector4 Y = Matrix.MultT(FbxVector4(0.0, 1.0, 0.0, 1.0)) - Origin;
		const FbxVector4 Z = Matrix.MultT(FbxVector4(0.0, 0.0, 1.0, 1.0)) - Origin;
		return FMatrix(
			float(X[0]), float(X[1]), float(X[2]), 0.0f,
			float(Y[0]), float(Y[1]), float(Y[2]), 0.0f,
			float(Z[0]), float(Z[1]), float(Z[2]), 0.0f,
			float(Origin[0]), float(Origin[1]), float(Origin[2]), 1.0f);
	}

	std::filesystem::path MakeUTF8Path(const FString& Path)
	{
		return std::filesystem::path(std::u8string(Path.begin(), Path.end()));
	}

	FStaticMeshData MakeStaticMeshData(const FSkeletalMesh& Part)
	{
		FStaticMeshData Data;
		Data.Vertices.Reserve(Part.Vertices.Num());
		Data.AABB = FBox{FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX)};
		for (const FSkeletalVertex& SourceVertex : Part.Vertices)
		{
			const FVertex& Vertex = SourceVertex.Vertex;
			Data.Vertices.Add(Vertex);
			Data.AABB.Expand(FBox{Vertex.Position, Vertex.Position});
		}

		Data.Indices = Part.Indices;
		for (const FSkeletalMeshSection& Section : Part.Sections)
			Data.Sections.Add(FStaticMeshSection{Section.StartIndex, Section.IndexCount, Section.MaterialSlotIndex});
		for (const FSkeletalMaterialSlot& Slot : Part.MaterialSlots)
			Data.MaterialSlots.Add(FStaticMaterialSlot{Slot.Name, Slot.BaseColor, Slot.BaseColorTexturePath, Slot.NormalTexturePath});
		return Data;
	}

	FString ResolveTexturePath(FbxFileTexture* Texture, const FString& FbxPath)
	{
		if (!Texture) return {};

		std::error_code Error;
		const auto FbxDirectory = std::filesystem::absolute(MakeUTF8Path(FbxPath), Error).parent_path();
		if (Error)
		{
			HTR_LOG(Warning, "[FBX] Cannot resolve FBX directory: {}", FbxPath);
			return {};
		}

		const auto FilePath = MakeUTF8Path(Texture->GetFileName());
		const auto RelativePath = MakeUTF8Path(Texture->GetRelativeFileName());
		auto FindFile = [&Error](const std::filesystem::path& Candidate) -> FString
		{
			if (Candidate.empty() || !Candidate.is_absolute()) return {};
			Error.clear();
			if (!std::filesystem::is_regular_file(Candidate, Error) || Error) return {};
			const auto UTF8Path = Candidate.lexically_normal().generic_u8string();
			return FString(UTF8Path.begin(), UTF8Path.end());
		};

		// 원래 절대경로를 우선 사용하고, 파일이 이동했다면 FBX 폴더 기준으로 찾는다.
		FString Resolved = FindFile(FilePath);
		if (Resolved.empty() && !RelativePath.empty() && RelativePath.is_relative())
			Resolved = FindFile(FbxDirectory / RelativePath);
		if (Resolved.empty() && !FilePath.empty() && FilePath.is_relative())
			Resolved = FindFile(FbxDirectory / FilePath);
		if (Resolved.empty() && !FilePath.filename().empty())
			Resolved = FindFile(FbxDirectory / FilePath.filename());
		if (Resolved.empty() && !RelativePath.filename().empty())
			Resolved = FindFile(FbxDirectory / RelativePath.filename());
		if (Resolved.empty())
			HTR_LOG(Warning, "[FBX] Texture file not found: '{}' (relative: '{}', FBX: '{}')",
				Texture->GetFileName(), Texture->GetRelativeFileName(), FbxPath);
		return Resolved;
	}
}

struct FBoneInfluence
{
	TArray<int32> BoneIndices;
	TArray<float> BoneWeights;
};


FFbxImporter::FFbxImporter()
{
	pFbxManager = FbxManager::Create();	// FbxManager 생성
	if (!pFbxManager)
	{
		HTR_LOG(Error, "[FBX] Manager Create Failed");
		return;
	}

	FbxIOSettings* FBXSetting =  FbxIOSettings::Create(pFbxManager, IOSROOT);	// 설정 Root 이름 세팅 후 IOSetting 객체 생성
	if (!FBXSetting)
	{
		HTR_LOG(Error, "[FBX] IOSettings Create Failed");
		pFbxManager->Destroy();
		pFbxManager = nullptr;
		return;
	}

	pFbxManager->SetIOSettings(FBXSetting);	// FbxManager에 IOSetting 설정
}

FFbxImporter::~FFbxImporter()
{
	if (pFbxManager)
	{
		pFbxManager->Destroy();
	}
}

FFbxImportResult FFbxImporter::ImportFBX(const FString& Path, const FString& SceneName)
{
	FFbxImportResult FbxResult;
	if (!pFbxManager)	// Manager가 초기에 제대로 생성되었는지 여부 체크
	{
		HTR_LOG(Error, "[FBX] Manager Is Not Ready: {}", Path);
		return FbxResult;
	}

	FbxIOSettings* pFbxIOSetting = pFbxManager->GetIOSettings();	// FbxManager의 IO Setting 객체 생성
	if (!pFbxIOSetting)
	{
		HTR_LOG(Error, "[FBX] IOSettings Is Not Ready: {}", Path);
		return FbxResult;
	}

	FbxImporter* pFbxImporter = FbxImporter::Create(pFbxManager, "");	// FbxImporter 객체 생성
	if (!pFbxImporter)
	{
		HTR_LOG(Error, "[FBX] Importer Create Failed: {}", Path);
		return FbxResult;
	}

	bool bInitialized = pFbxImporter->Initialize(Path.c_str(), -1, pFbxIOSetting); // FbxImporter 초기 Initialize
	if (!bInitialized)
	{
		HTR_LOG(Error, "[FBX] Importer Initialize Failed: {}", pFbxImporter->GetStatus().GetErrorString());
		pFbxImporter->Destroy();
		return FbxResult;
	}                          

	FbxGeometryConverter Converter(pFbxManager);	// Polygon 삼각형화 하기 위한 Geometry Converter 생성
	FbxScene* pFbxScene = FbxScene::Create(pFbxManager, SceneName.c_str()); // FbxScene 객체 생성
	if (!pFbxScene)
	{
		HTR_LOG(Error, "[FBX] Scene Create Failed: {}", Path);
		pFbxImporter->Destroy();
		return FbxResult;
	}

	bool bImport = pFbxImporter->Import(pFbxScene);  // FbxImporter Initialize의 파일에서 Scene 정보 가져와 pbxScene에 채움                    
	if (!bImport)
	{
		HTR_LOG(Error, "[FBX] Import Failed: {}", pFbxImporter->GetStatus().GetErrorString());
		pFbxImporter->Destroy();
		pFbxScene->Destroy();
		return FbxResult;
	}      

	// FBX 축 문자열은 오른쪽, 위쪽, 앞쪽 순서다. 엔진은 각각 +Y, +Z, +X를 사용한다.
	FbxAxisSystem EngineAxes;
	if (!FbxAxisSystem::ParseAxisSystem("yzx", EngineAxes))
	{
		HTR_LOG(Error, "[FBX] Invalid engine axis system");
		pFbxImporter->Destroy();
		pFbxScene->Destroy();
		return FbxResult;
	}

	// 메시, 클러스터, 바인드 포즈를 추출하기 전에 씬 전체를 엔진의 축과 단위로 변환한다.
	if (pFbxScene->GetGlobalSettings().GetAxisSystem() != EngineAxes)
	{
		EngineAxes.DeepConvertScene(pFbxScene);
	}
	if (pFbxScene->GetGlobalSettings().GetSystemUnit() != FbxSystemUnit::m)
	{
		FbxSystemUnit::m.ConvertScene(pFbxScene);
	}

	bool bTriangulated = Converter.Triangulate(pFbxScene, true);	// 삼각형화 진행
	if (!bTriangulated)
	{
		HTR_LOG(Error, "[FBX] Triangulate Failed");
		pFbxImporter->Destroy();
		pFbxScene->Destroy();
		return FbxResult;
	}

	pFbxImporter->Destroy();	// 다 채웠으므로 Importer는 Destroy

	TMap<FbxNode*, int32> BoneIndexByNode;
	FbxNode* pFbxRootNode = pFbxScene->GetRootNode();

	try
	{
		if (pFbxRootNode)
		{
			// 전체 Tree에 대해 SkeletonNode부터 전체 순회하여 BoneIndexByNode를 완성
			for (int i = 0; i < pFbxRootNode->GetChildCount(); i++)
			{
				ProcessSkeletonNode(pFbxRootNode->GetChild(i), BoneIndexByNode, FbxResult);
			}

			// 모든 본의 번호가 준비된 후 MeshNode 순회 진행
			for (int i = 0; i < pFbxRootNode->GetChildCount(); i++)
			{
				ProcessMeshNode(pFbxRootNode->GetChild(i), BoneIndexByNode, FbxResult, Path);
			}

			// 축·단위 변환이 끝난 뒤, 씬을 지우기 전에 구워야 메시와 같은 규약이 된다.
			ProcessAnimations(pFbxScene, BoneIndexByNode, FbxResult);
		}
	}
	catch (const std::runtime_error& Exception)
	{
		HTR_LOG(Error, "[FBX] Reference pose import failed: {}", Exception.what());
		pFbxScene->Destroy();
		return FFbxImportResult{};
	}
	pFbxScene->Destroy();
	FbxResult.bSuccess = true;
	return FbxResult;
}

void FFbxImporter::ProcessSkeletonNode(FbxNode* Node, TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult)
{
	if (!Node) return;

	for (int i = 0; i < Node->GetNodeAttributeCount(); i++) // 한 Node에 여러 Attribute가 있을 수 있으므로 전체를 순회
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri && NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eSkeleton)
		{
			FSkeletalBone Bone = ProcessSkeleton(Node, BoneIndexByNode);
			BoneIndexByNode.Add(Node, OutResult.Bones.Num());
			OutResult.Bones.Add(Bone);
		}
	}
	for (int i = 0; i < Node->GetChildCount(); ++i) // 연결된 자식에 대해서 재귀적으로 순회
	{
		ProcessSkeletonNode(Node->GetChild(i), BoneIndexByNode, OutResult);
	}
}

void FFbxImporter::ProcessMeshNode(FbxNode* Node, const TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult, const FString& FbxPath)
{
	if (!Node) return;

	// LOD 0만 사용 나중에 Skeletal LOD 구현 시 수정 필요
	for (int i = 0; i < Node->GetNodeAttributeCount(); i++)
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri && NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eLODGroup)
		{
			if (Node->GetChildCount() > 0)
			{
				ProcessMeshNode(Node->GetChild(0), BoneIndexByNode, OutResult, FbxPath);
			}
			return;
		}
	}

	for (int i = 0; i < Node->GetNodeAttributeCount(); i++)	// 한 Node에 여러 Attribute가 있을 수 있으므로 전체를 순회
	{
		FbxNodeAttribute* NodeAttri = Node->GetNodeAttributeByIndex(i);
		if (NodeAttri && NodeAttri->GetAttributeType() == FbxNodeAttribute::EType::eMesh)
		{
			FbxMesh* Mesh = static_cast<FbxMesh*>(NodeAttri);
			FSkeletalMesh Part = ProcessMesh(Mesh, BoneIndexByNode, FbxPath);
			if (Part.Indices.IsEmpty()) continue;
			const int32 SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);

			FbxAMatrix MeshTransform = Node->EvaluateGlobalTransform();
			bool bFoundBindTransform = false;
			for (int SkinIndex = 0; SkinIndex < SkinCount && !bFoundBindTransform; ++SkinIndex)
			{
				auto* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
				if (!Skin) continue;
				for (int ClusterIndex = 0; ClusterIndex < Skin->GetClusterCount(); ++ClusterIndex)
				{
					FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
					if (!Cluster) continue;
					Cluster->GetTransformMatrix(MeshTransform);
					bFoundBindTransform = true;
					break;
				}
			}

			// Control Point에 없는 Geometric Transform도 메시 Bind 변환에 포함한다.
			FbxAMatrix Geometry;
			Geometry.SetTRS(Node->GetGeometricTranslation(FbxNode::eSourcePivot),
				Node->GetGeometricRotation(FbxNode::eSourcePivot),
				Node->GetGeometricScaling(FbxNode::eSourcePivot));
			Part.MeshTransform = ToEngineMatrix(MeshTransform * Geometry);

			if (SkinCount == 0)
			{
				const int32* AttachmentBone = nullptr;
				for (FbxNode* Ancestor = Node; Ancestor && !AttachmentBone; Ancestor = Ancestor->GetParent())
					AttachmentBone = BoneIndexByNode.Find(Ancestor);

				if (!AttachmentBone)
				{
					OutResult.StaticMeshes.Add(FFbxStaticMeshPart{
						Node->GetName(), Part.MeshTransform, MakeStaticMeshData(Part)});
					continue;
				}

				// 본에 붙은 비스키닝 파트는 그 본의 가중치를 1로 설정해 함께 움직이게 한다.
				for (FSkeletalVertex& Vertex : Part.Vertices)
				{
					Vertex.BoneIndices[0] = static_cast<uint32>(*AttachmentBone);
					Vertex.BoneWeights[0] = 1.0f;
				}
			}

			OutResult.Meshes.Add(std::move(Part));
		}
	}
	for (int i = 0; i < Node->GetChildCount(); ++i) // 연결된 자식에 대해서 재귀적으로 순회
	{
		ProcessMeshNode(Node->GetChild(i), BoneIndexByNode, OutResult, FbxPath);
	}
}

void FFbxImporter::BuildInfluenceMap(FbxMesh* Mesh, const TMap<FbxNode*, int32>& BoneIndexByNode, TMap<int32, FBoneInfluence>& InfluenceByControlPoint)
{
	if (!Mesh) return;

	const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
	for (int SkinIndex = 0; SkinIndex < SkinCount; ++SkinIndex)	// FbxSkin만큼 순회
	{
		FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIndex, FbxDeformer::eSkin));
		if (!Skin) continue;

		const int ClusterCount = Skin->GetClusterCount();
		for (int ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)	// 한 FbxSkin의 전체 Cluster만큼 순회
		{
			FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
			if (!Cluster) continue;

			FbxNode* BoneNode = Cluster->GetLink();	// Cluster와 연결된 BoneNode 찾기
			if (!BoneNode) continue;
			const int32* BoneIndex = BoneIndexByNode.Find(BoneNode);	// 해당 Node의 BoneIndex 찾기
			if (!BoneIndex)
			{
				HTR_LOG(Warning, "[FBX] Bone is not registered: {}", BoneNode->GetName());
				continue;
			}

			const int Count = Cluster->GetControlPointIndicesCount();
			const int* ControlPointIndices = Cluster->GetControlPointIndices();
			const double* Weights = Cluster->GetControlPointWeights();
			if (!ControlPointIndices || !Weights) continue;

			for (int EntryIndex = 0; EntryIndex < Count; ++EntryIndex)	// Cluster 내부의 한 Bone이 영향을 주는 정점 배열, Weight 배열 순회
			{
				const int ControlPointIndex = ControlPointIndices[EntryIndex];	// Cluster의 전체 CPIndices를 순회
				const float Weight = static_cast<float>(Weights[EntryIndex]);	// Cluster의 전체 Weight를 순회

				if (ControlPointIndex < 0 || ControlPointIndex >= Mesh->GetControlPointsCount()) continue;
				if (Weight <= 0.0f) continue;

				FBoneInfluence& Influence = InfluenceByControlPoint[ControlPointIndex];	// Cluster 내부의 CPIndex를 바탕으로 Influence 정보를 추가
				Influence.BoneIndices.Add(*BoneIndex);
				Influence.BoneWeights.Add(Weight);
			}
		}
	}

}

FSkeletalMesh FFbxImporter::ProcessMesh(FbxMesh* Mesh, const TMap<FbxNode*, int32>& BoneIndexByNode, const FString& FbxPath)
{
	FSkeletalMesh SkeletalMesh;
	TMap<int32, FBoneInfluence> InfluenceByControlPoint;
	BuildInfluenceMap(Mesh, BoneIndexByNode, InfluenceByControlPoint);	// Control Point - Influence간 연결 Map을 완성
	BuildMaterials(Mesh, SkeletalMesh, FbxPath);	// Mesh 안의 Material 정보 Slot에 저장
	BuildMeshVerticesAndIndices(Mesh, InfluenceByControlPoint, SkeletalMesh);	// 이후 정점 추가 과정에서 ControlPoint에 맞춰 Vertex 정보 추가하면서 같이 추가

	return SkeletalMesh;
}

namespace
{
	// 바인드 포즈와 애니메이션 키가 같은 식을 쓰도록 '본 계층 부모 기준 로컬'을 한곳에서 만든다.
	FbxAMatrix EvaluateBoneLocal(FbxNode* BoneNode, FbxNode* ParentBoneNode, const FbxTime& Time)
	{
		FbxAMatrix Local = BoneNode->EvaluateGlobalTransform(Time);
		if (ParentBoneNode)
		{
			// FBX 행렬은 열벡터 규약이라 부모 역행렬을 왼쪽에 곱한다.
			Local = ParentBoneNode->EvaluateGlobalTransform(Time).Inverse() * Local;
		}
		return Local;
	}

	// SRT의 X, Y, Z에 커브가 하나라도 있으면 true
	bool HasTransformCurve(FbxNode* Node, FbxAnimLayer* Layer)
	{
		auto HasCurve = [Layer](FbxPropertyT<FbxDouble3>& Property)
		{
			return Property.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_X)
				|| Property.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Y)
				|| Property.GetCurve(Layer, FBXSDK_CURVENODE_COMPONENT_Z);
		};
		return HasCurve(Node->LclTranslation) || HasCurve(Node->LclRotation) || HasCurve(Node->LclScaling);
	}

	// 본 자신뿐 아니라 부모 본까지 사이에 낀 일반 노드(Armature 등)가 움직여도 이 본의 로컬이 바뀐다.
	bool IsBoneAnimated(FbxNode* BoneNode, FbxNode* ParentBoneNode, FbxAnimLayer* Layer)
	{
		if (!Layer) return false;
		for (FbxNode* Node = BoneNode; Node && Node != ParentBoneNode; Node = Node->GetParent())
		{
			if (HasTransformCurve(Node, Layer)) return true;
		}
		return false;
	}
}

FSkeletalBone FFbxImporter::ProcessSkeleton(FbxNode* Node, const TMap<FbxNode*, int32>& BoneIndexByNode)
{
	if (!Node || !Node->GetScene())
	{
		throw std::runtime_error("Bone node or scene is missing");
	}

	FSkeletalBone SkeletalBone;
	SkeletalBone.Name = Node->GetName();
	// 중간의 일반 노드를 건너뛰어 실제 본 계층의 부모를 찾는다.
	FbxNode* ParentBoneNode = Node->GetParent();
	while (ParentBoneNode && !BoneIndexByNode.Find(ParentBoneNode))
	{
		ParentBoneNode = ParentBoneNode->GetParent();
	}
	const int32* ParentIndex = ParentBoneNode ? BoneIndexByNode.Find(ParentBoneNode) : nullptr;
	SkeletalBone.ParentIndex = ParentIndex ? *ParentIndex : -1;

	int ScenePoseCount = Node->GetScene()->GetPoseCount();
	for (int i = 0; i < ScenePoseCount; i++)	// Scene 내부의 전체 Pose에 대해서 순회
	{
		FbxPose* CurrentPose = Node->GetScene()->GetPose(i);
		if (CurrentPose && CurrentPose->IsBindPose())
		{
			int NodePoseIndex = CurrentPose->Find(Node);
			if (NodePoseIndex < 0) continue;
			FbxMatrix RefTransform = CurrentPose->GetMatrix(NodePoseIndex);	// CurrentPose에서 Node를 찾았을때만 Matrix 호출
			if (ParentBoneNode)
			{
				int ParentPoseIndex = CurrentPose->Find(ParentBoneNode);	
				if (ParentPoseIndex < 0) continue;
				FbxMatrix RefParentTransform = CurrentPose->GetMatrix(ParentPoseIndex);	// CurrentPose에서 부모 Node를 찾았을때만 Matrix 호출 
				FbxMatrix ParentInverse = RefParentTransform.Inverse();
				FbxMatrix Identity;
				Identity.SetIdentity();
				if (RefParentTransform * ParentInverse != Identity) continue;
				RefTransform = ParentInverse * RefTransform; // 전역 행렬에서 부모 변환을 제거해 로컬 변환 계산
			}
			else
			{
				// 루트 본은 부모 역행렬을 적용하지 않고 전역 Bind Pose 유지
			}

			FbxVector4 T, S, Shear;
			FbxQuaternion Q;
			double Sign;
			RefTransform.GetElements(T, Q, Shear, S, Sign);
			SkeletalBone.RefPoseTransform = FTransform(FVector(float(T[0]), float(T[1]), float(T[2])),
				FVector(float(S[0]), float(S[1]), float(S[2])), 
				FQuat(float(Q[0]), float(Q[1]), float(Q[2]), float(Q[3])).Normalized().ToFRotator());
			return SkeletalBone;
		}
	}

	// 애니메이션 전용 FBX는 BindPose가 없는 경우가 많아, 애니메이션을 뺀 노드 기본값을 바인드 포즈로 쓴다.
	const FbxAMatrix Local = EvaluateBoneLocal(Node, ParentBoneNode, FBXSDK_TIME_INFINITE);
	const FbxVector4 T = Local.GetT();
	const FbxVector4 S = Local.GetS();
	const FbxQuaternion Q = Local.GetQ();
	SkeletalBone.RefPoseTransform = FTransform(FVector(float(T[0]), float(T[1]), float(T[2])),
		FVector(float(S[0]), float(S[1]), float(S[2])),
		FQuat(float(Q[0]), float(Q[1]), float(Q[2]), float(Q[3])).Normalized().ToFRotator());
	return SkeletalBone;
}

void FFbxImporter::ProcessAnimations(FbxScene* Scene, const TMap<FbxNode*, int32>& BoneIndexByNode, FFbxImportResult& OutResult)
{
	const int32 BoneCount = OutResult.Bones.Num();
	if (!Scene || BoneCount == 0) return;

	// 트랙 순서를 Bones[] 순서와 같게 하려고 본 인덱스로 노드를 거꾸로 찾는다.
	TArray<FbxNode*> BoneNodes;
	BoneNodes.SetNum(BoneCount);
	for (const auto& [Node, BoneIndex] : BoneIndexByNode)
	{
		BoneNodes[BoneIndex] = Node;
	}

	// 씬의 시간 모드를 따르고, 정보가 없으면 30fps로 굽는다.
	const FbxGlobalSettings& Settings = Scene->GetGlobalSettings();
	const FbxTime::EMode TimeMode = Settings.GetTimeMode();
	double Fps = TimeMode == FbxTime::eCustom ? Settings.GetCustomFrameRate() : FbxTime::GetFrameRate(TimeMode);
	if (Fps <= 0.0) Fps = 30.0;
	// 29.97 같은 소수 FPS도 담으려고 분모를 1000으로 둔다.
	const FFrameRate FrameRate(static_cast<int32>(std::lround(Fps * 1000.0)), 1000);

	const int StackCount = Scene->GetSrcObjectCount<FbxAnimStack>();
	for (int StackIndex = 0; StackIndex < StackCount; ++StackIndex)
	{
		FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		if (!Stack) continue;

		// 평가기는 '현재 스택'만 보므로, 클립마다 바꿔 줘야 각자 다른 동작이 나온다.
		Scene->SetCurrentAnimationStack(Stack);

		FbxTimeSpan Span = Stack->GetLocalTimeSpan();
		// 일부 익스포터는 스택 구간을 비워 두고 TakeInfo에만 기록한다.
		if (Span.GetDuration().GetSecondDouble() <= 0.0)
		{
			if (FbxTakeInfo* Take = Scene->GetTakeInfo(Stack->GetName()))
				Span = Take->mLocalTimeSpan;
		}
		const double DurationSeconds = Span.GetDuration().GetSecondDouble();

		FFbxAnimClip Clip;
		Clip.Name = Stack->GetName();
		Clip.FrameRate = FrameRate;

		// 버림하면 1.0 * 30 = 29.999 같은 오차로 마지막 프레임이 사라져서 반올림한다.
		Clip.NumFrames = FMath::Max(0, static_cast<int32>(std::lround(DurationSeconds * Clip.FrameRate.AsDecimal())));

		// 레이어 합성은 평가기가 해 주므로, 베이스 레이어는 '움직이는 본인가' 판정에만 쓴다.
		FbxAnimLayer* BaseLayer = Stack->GetMember<FbxAnimLayer>(0);

		Clip.Tracks.Reserve(BoneCount);
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const FSkeletalBone& Bone = OutResult.Bones[BoneIndex];
			// 바인드 포즈와 같은 기준을 쓰려고 FBX 노드 부모가 아니라 본 계층 부모를 넘긴다.
			FbxNode* ParentBoneNode = Bone.ParentIndex >= 0 ? BoneNodes[Bone.ParentIndex] : nullptr;

			FBoneAnimationTrack Track;
			Track.Name = FName(Bone.Name);
			SampleBoneTrack(BoneNodes[BoneIndex], ParentBoneNode, BaseLayer, Span.GetStart(), Clip, Bone, Track.InternalTrack);
			Clip.Tracks.Add(std::move(Track));
		}
		OutResult.Anims.Add(std::move(Clip));
	}
}

void FFbxImporter::SampleBoneTrack(FbxNode* BoneNode, FbxNode* ParentBoneNode, FbxAnimLayer* Layer, const FbxTime& StartTime,
	const FFbxAnimClip& Clip, const FSkeletalBone& Bone, FRawAnimSequenceTrack& OutTrack)
{
	// 안 움직이는 본은 노드 기본값이 아니라 바인드 포즈 1키로 둔다. 둘이 다르면 스킨이 틀어지기 때문이다.
	if (!BoneNode || !IsBoneAnimated(BoneNode, ParentBoneNode, Layer))
	{
		OutTrack.PosKeys.Add(Bone.RefPoseTransform.Location);
		OutTrack.RotKeys.Add(Bone.RefPoseTransform.GetOrientation());
		OutTrack.ScaleKeys.Add(Bone.RefPoseTransform.Scale);
		return;
	}

	const int32 NumKeys = Clip.NumFrames + 1;
	OutTrack.PosKeys.Reserve(NumKeys);
	OutTrack.RotKeys.Reserve(NumKeys);
	OutTrack.ScaleKeys.Reserve(NumKeys);

	// float 간격을 누적하면 오차가 쌓여서, 매 키를 시작 시각 + i * 간격으로 새로 계산한다.
	const double Interval = static_cast<double>(Clip.FrameRate.Denominator) / Clip.FrameRate.Numerator;
	const double StartSeconds = StartTime.GetSecondDouble();
	for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
	{
		FbxTime Time;
		Time.SetSecondDouble(StartSeconds + KeyIndex * Interval);

		// 평가기가 레이어 합성, 오일러→회전, Pre/Post Rotation, 피벗을 모두 반영한 결과를 준다.
		const FbxAMatrix Local = EvaluateBoneLocal(BoneNode, ParentBoneNode, Time);
		const FbxVector4 T = Local.GetT();
		const FbxVector4 S = Local.GetS();
		const FbxQuaternion Q = Local.GetQ();

		OutTrack.PosKeys.Add(FVector(float(T[0]), float(T[1]), float(T[2])));
		OutTrack.RotKeys.Add(FQuat(float(Q[0]), float(Q[1]), float(Q[2]), float(Q[3])).Normalized());
		OutTrack.ScaleKeys.Add(FVector(float(S[0]), float(S[1]), float(S[2])));
	}
}

void FFbxImporter::Cook(FFbxImportResult&& ImportResult, FFbxCookedData& Cooked)
{
	Cooked.StaticMeshes = std::move(ImportResult.StaticMeshes);
	Cooked.Anims = std::move(ImportResult.Anims);
	FSkeletalMeshData& OutData = Cooked.SkeletalMesh;
	OutData.Meshes = std::move(ImportResult.Meshes);
	if (OutData.Meshes.IsEmpty()) return;
	OutData.Bones = std::move(ImportResult.Bones);

	TArray<FMatrix> GlobalRefMatrix; // 부모 기준 Transform 저장하기 위한 임시배열
	GlobalRefMatrix.Reserve(OutData.Bones.Num());

	for (int i = 0; i < OutData.Bones.Num(); i++)
	{
		FMatrix RefMatrix = OutData.Bones[i].RefPoseTransform.GetLocalMatrix();
		if (OutData.Bones[i].ParentIndex != -1)
		{
			RefMatrix = RefMatrix * GlobalRefMatrix[OutData.Bones[i].ParentIndex]; // 부모 기준 Bone Transform을 전역 기준 BindPose Transform으로 이동, 부모기준 Transform을 곱함
		}
		GlobalRefMatrix.Add(RefMatrix);	// 다음 계산시 부모 Transform으로 활용하기 위해 저장
	}

	TArray<FMatrix> InverseGlobalRefMatrix;
	InverseGlobalRefMatrix.Reserve(GlobalRefMatrix.Num());
	for (const FMatrix& RefMatrix : GlobalRefMatrix)
	{
		InverseGlobalRefMatrix.Add(RefMatrix.Inverse());
	}
	for (FSkeletalMesh& Mesh : OutData.Meshes)
	{
		Mesh.InverseBindPoseMat.Reset();
		Mesh.InverseBindPoseMat.Reserve(OutData.Bones.Num());
		for (const FMatrix& InverseRefMatrix : InverseGlobalRefMatrix)
		{
			// 행벡터: 메시 로컬 -> FBX 전역 -> 본 바인드 로컬.
			Mesh.InverseBindPoseMat.Add(Mesh.MeshTransform * InverseRefMatrix);
		}
	}
}

void FFbxImporter::BuildMeshVerticesAndIndices(FbxMesh* Mesh,const TMap<int32, FBoneInfluence>& InfluenceByControlPoint,FSkeletalMesh& SkeletalMesh)
{
	if (!Mesh) return;

	FbxStringList UVSetNames;
	Mesh->GetUVSetNames(UVSetNames); // UV 추출을 위한 UVSet 이름 넣어두기
	TMap<FFbxVertexKey, int32, FFbxVertexKeyHash> VertexCache;

	if (SkeletalMesh.MaterialSlots.IsEmpty())
	{
		FSkeletalMaterialSlot DefaultSlot;
		DefaultSlot.Name = "Default";
		SkeletalMesh.MaterialSlots.Add(DefaultSlot);
	}
	FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterial(0);	// Polygon - Material 간의 연결 정보
	const auto MappingMode = MaterialElement ? MaterialElement->GetMappingMode() : FbxGeometryElement::eNone;
	bool bWarnedInvalidMaterial = false;

	for (int i = 0; i < Mesh->GetPolygonCount(); i++) // 전체 Polygon 순회
	{
		if (Mesh->GetPolygonSize(i) == 3)
		{
			int MaterialIndex = 0;
			bool bValidMaterial = false;
			if (MaterialElement && (MappingMode == FbxGeometryElement::eAllSame || MappingMode == FbxGeometryElement::eByPolygon))
			{
				const int MaterialMappingIndex = MappingMode == FbxGeometryElement::eAllSame ? 0 : i; // eAllSame일 경우 Polygon-Material 매핑 배열에서 첫번째만 읽음, eByPolygon이면 면의 index를 따라감
				if (MaterialMappingIndex < MaterialElement->GetIndexArray().GetCount())
				{
					const int MappedIndex = MaterialElement->GetIndexArray().GetAt(MaterialMappingIndex);	// 현재 계산하고 있는 Polygon이 사용하는 Material의 index 읽음
					if (MappedIndex >= 0 && MappedIndex < SkeletalMesh.MaterialSlots.Num())
					{
						MaterialIndex = MappedIndex;
						bValidMaterial = true;
					}
				}
			}
			if (MaterialElement && !bValidMaterial && !bWarnedInvalidMaterial)
			{
				HTR_LOG(Warning, "[FBX] Invalid or unsupported material mapping on mesh '{}'; using slot 0", Mesh->GetName());
				bWarnedInvalidMaterial = true;
			}
			int SectionNum = SkeletalMesh.Sections.Num();
			if (SectionNum == 0 || SkeletalMesh.Sections[SectionNum - 1].MaterialSlotIndex != MaterialIndex)	// Section이 현재 존재하지 않거나, 이전 Section과 Material이 달라졌을경우 Section 추가
			{
				FSkeletalMeshSection Section;
				Section.StartIndex = static_cast<uint32>(SkeletalMesh.Indices.Num());
				Section.MaterialSlotIndex = MaterialIndex;
				Section.IndexCount = 3;
				SkeletalMesh.Sections.Add(Section);
			}
			else SkeletalMesh.Sections[SectionNum - 1].IndexCount += 3;	// Section이 그대로일때는 삼각형만큼 index 추가

			for (int j = 0; j < 3; j++)	// Polygon 내부의 점 순회, Polygon은 삼각형화에 의해 삼각형으로 고정됨
			{
				int ControlPointIndex = Mesh->GetPolygonVertex(i, j);
				FbxVector4 FbxVertex = Mesh->GetControlPointAt(ControlPointIndex);
				FSkeletalVertex SkeletalVertex;
				FVertex& Vertex = SkeletalVertex.Vertex;
				Vertex.Position = FVector(static_cast<float>(FbxVertex[0]), static_cast<float>(FbxVertex[1]), static_cast<float>(FbxVertex[2]));

				FbxVector4 Normal;
				bool bNormal = Mesh->GetPolygonVertexNormal(i, j, Normal);
				if (bNormal) Vertex.Normal = FVector(static_cast<float>(Normal[0]), static_cast<float>(Normal[1]), static_cast<float>(Normal[2]));

				if (UVSetNames.GetCount() > 0) // UVSet이 존재할 때 체크
				{
					FbxVector2 UV;
					bool bUnmapped = false;
					bool bSuccess = Mesh->GetPolygonVertexUV(i, j, UVSetNames.GetStringAt(0), UV, bUnmapped);
					if (bSuccess && !bUnmapped)
					{
						Vertex.UV = FVector2(static_cast<float>(UV[0]), 1.0f - static_cast<float>(UV[1]));
					}
				}

				FFbxVertexKey Key{ ControlPointIndex, Vertex.Normal.X, Vertex.Normal.Y, Vertex.Normal.Z, Vertex.UV.X, Vertex.UV.Y };
				if (const int32* Found = VertexCache.Find(Key))
				{
					SkeletalMesh.Indices.Add(static_cast<uint32>(*Found));	// 같은 정점이면 기존 정점의 인덱스만 추가
				}
				else // 다른 정점이면 본 정보를 채우고 새 정점으로 추가
				{
					if (const FBoneInfluence* Influence = InfluenceByControlPoint.Find(ControlPointIndex))
					{
						// 큰 가중치부터 최대 4개만 유지한다.
						for (int InfluenceIndex = 0; InfluenceIndex < Influence->BoneIndices.Num(); ++InfluenceIndex)
						{
							const float Weight = Influence->BoneWeights[InfluenceIndex];
							for (int Slot = 0; Slot < 4; ++Slot)
							{
								if (Weight > SkeletalVertex.BoneWeights[Slot])
								{
									for (int Shift = 3; Shift > Slot; --Shift)
									{
										SkeletalVertex.BoneIndices[Shift] = SkeletalVertex.BoneIndices[Shift - 1];
										SkeletalVertex.BoneWeights[Shift] = SkeletalVertex.BoneWeights[Shift - 1];
									}
									SkeletalVertex.BoneIndices[Slot] = static_cast<uint32>(Influence->BoneIndices[InfluenceIndex]);
									SkeletalVertex.BoneWeights[Slot] = Weight;
									break;
								}
							}
						}

						float TotalWeight = 0.0f;
						for (float Weight : SkeletalVertex.BoneWeights) TotalWeight += Weight;
						if (TotalWeight > 0.0f)
						{
							for (float& Weight : SkeletalVertex.BoneWeights) Weight /= TotalWeight;
						}
					}

					uint32 NewVertexIndex = static_cast<uint32>(SkeletalMesh.Vertices.Num());
					SkeletalMesh.Vertices.Add(SkeletalVertex);
					SkeletalMesh.Indices.Add(NewVertexIndex);
					VertexCache.Add(Key, static_cast<int32>(NewVertexIndex));
				}
			}
		}
	}
}

void FFbxImporter::BuildMaterials(FbxMesh* Mesh, FSkeletalMesh& SkeletalMesh, const FString& FbxPath)
{
	FbxNode* Node = Mesh ? Mesh->GetNode() : nullptr;
	const int MaterialCount = Node ? Node->GetMaterialCount() : 0;
	for (int i = 0; i < MaterialCount; i++)	// 한 Node 내에 존재하는 전체 Material 순회
	{
		FbxSurfaceMaterial* FbxMat = Node->GetMaterial(i);
		FSkeletalMaterialSlot Slot;
		if (FbxMat != nullptr)
		{
			Slot.Name = FbxMat->GetName();
			FbxProperty DiffuseProperty = FbxMat->FindProperty(FbxSurfaceMaterial::sDiffuse);	// Diffuse Property를 가져옴
			FbxProperty NormalProperty = FbxMat->FindProperty(FbxSurfaceMaterial::sNormalMap); // Normal Property를 가져옴
			if (DiffuseProperty.IsValid())
			{
				FbxDouble3 Color = DiffuseProperty.Get<FbxDouble3>();	// Property 존재시 Color 정보 가져와 Slot에 저장
				Slot.BaseColor = FVector4(Color[0], Color[1], Color[2], 1.0f);
				if (DiffuseProperty.GetSrcObjectCount<FbxFileTexture>() > 0)
				{
					FbxFileTexture* Texture = DiffuseProperty.GetSrcObject<FbxFileTexture>(0);
					if (Texture)
					{
						Slot.BaseColorTexturePath = ResolveTexturePath(Texture, FbxPath);
					}
				}
			}
			if (NormalProperty.IsValid())
			{
				if (NormalProperty.GetSrcObjectCount<FbxFileTexture>() > 0)
				{
					FbxFileTexture* Texture = NormalProperty.GetSrcObject<FbxFileTexture>(0);
					if (Texture)
					{
						Slot.NormalTexturePath = ResolveTexturePath(Texture, FbxPath);
					}
				}
			}
		}
		SkeletalMesh.MaterialSlots.Add(Slot);
	}
	if (SkeletalMesh.MaterialSlots.IsEmpty())
	{
		FSkeletalMaterialSlot DefaultSlot;
		DefaultSlot.Name = "Default";
		SkeletalMesh.MaterialSlots.Add(DefaultSlot);
	}
}
