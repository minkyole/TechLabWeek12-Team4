#include "EnginePCH.h"
#include "ParticleSpriteRenderer.h"

#include "Render/Material.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/RenderPacket.h"

#include <algorithm>
#include <utility>
#include <cstddef>
#include <cstring>

bool FParticleSpriteRenderer::Init()
{
	const FParticleSpriteVertex Vertices[4] = {
		{ FVector2(-0.5f, 0.5f),
		  FVector2(0.0f, 0.0f) },
		{ FVector2(0.5f, 0.5f),
		  FVector2(1.0f, 0.0f) },
		{ FVector2(0.5f, -0.5f),
		  FVector2(1.0f, 1.0f) },
		{ FVector2(-0.5f, -0.5f),
		  FVector2(0.0f, 1.0f) }
	};

	const uint32 Indices[6] = { 0, 1, 2, 0, 2, 3 };

	QuadVB = RenderCommand::CreateStaticVertexBuffer(
	    Vertices,
	    sizeof(Vertices),
	    sizeof(FParticleSpriteVertex));

	QuadIB = RenderCommand::CreateStaticIndexBuffer(
	    Indices,
	    6);

	ViewCB = RenderCommand::CreateConstantBuffer(
	    sizeof(FParticleSpriteViewConstants));

	Shader = FRenderResourceManager::GetShaderProgram(
	    "Resources/Shader/ParticleSpriteShader.hlsl");

	if (!Shader ||
	    !Shader->VertexShader ||
	    !Shader->VertexShader->IsValid() ||
	    !Shader->PixelShader ||
	    !Shader->PixelShader->IsValid())
	{
		return false;
	}

	const FShaderByteCode& VSCode = Shader->VertexShader->GetByteCode();

	if (!VSCode.IsValid())
	{
		return false;
	}

	ID3D11Device* Device = RenderCommand::GetDevice();

	const D3D11_INPUT_ELEMENT_DESC Elements[] = {
		{ "POSITION", 0,
		  DXGI_FORMAT_R32G32_FLOAT,
		  0,
		  static_cast<UINT>(
		      offsetof(FParticleSpriteVertex, Corner)),
		  D3D11_INPUT_PER_VERTEX_DATA,
		  0 },
		{ "TEXCOORD", 0,
		  DXGI_FORMAT_R32G32_FLOAT,
		  0,
		  static_cast<UINT>(
		      offsetof(FParticleSpriteVertex, UV)),
		  D3D11_INPUT_PER_VERTEX_DATA,
		  0 },
		{ "INSTANCE_POSITION", 0,
		  DXGI_FORMAT_R32G32B32_FLOAT,
		  1,
		  static_cast<UINT>(
		      offsetof(
		          FParticleSpriteInstanceVertex,
		          Position)),
		  D3D11_INPUT_PER_INSTANCE_DATA,
		  1 },
		{ "INSTANCE_ROTATION", 0,
		  DXGI_FORMAT_R32_FLOAT,
		  1,
		  static_cast<UINT>(
		      offsetof(
		          FParticleSpriteInstanceVertex,
		          Rotation)),
		  D3D11_INPUT_PER_INSTANCE_DATA,
		  1 },
		{ "INSTANCE_SIZE", 0,
		  DXGI_FORMAT_R32G32_FLOAT,
		  1,
		  static_cast<UINT>(
		      offsetof(
		          FParticleSpriteInstanceVertex,
		          Size)),
		  D3D11_INPUT_PER_INSTANCE_DATA,
		  1 },
		{ "INSTANCE_COLOR", 0,
		  DXGI_FORMAT_R32G32B32A32_FLOAT,
		  1,
		  static_cast<UINT>(
		      offsetof(
		          FParticleSpriteInstanceVertex,
		          Color)),
		  D3D11_INPUT_PER_INSTANCE_DATA,
		  1 },
		{ "INSTANCE_UVRECT", 0,
		  DXGI_FORMAT_R32G32B32A32_FLOAT,
		  1,
		  static_cast<UINT>(
		      offsetof(
		          FParticleSpriteInstanceVertex,
		          UVRect)),
		  D3D11_INPUT_PER_INSTANCE_DATA,
		  1 }
	};

	if (FAILED(Device->CreateInputLayout(
	        Elements,
	        static_cast<UINT>(
	            sizeof(Elements) / sizeof(Elements[0])),
	        VSCode.GetData(),
	        VSCode.GetSize(),
	        InputLayout.GetAddressOf())))
	{
		return false;
	}

	if (!QuadVB || !QuadVB->GetBuffer() ||
	    !QuadIB || !QuadIB->GetBuffer() ||
	    !ViewCB || !ViewCB->GetBuffer())
	{
		return false;
	}

	return EnsureCapacity(256);
}

bool FParticleSpriteRenderer::EnsureCapacity(
    uint32 RequiredCount)
{
	if (RequiredCount <= InstanceCapacity)
	{
		return true;
	}

	// 현재 이미터당 파티클 수 제한에 맞춘 상한.
	constexpr uint32 MaxInstances = 65535;

	if (RequiredCount > MaxInstances)
	{
		return false;
	}

	uint32 NewCapacity = InstanceCapacity > 0 ? InstanceCapacity : 256;

	while (NewCapacity < RequiredCount)
	{
		if (NewCapacity > MaxInstances / 2)
		{
			NewCapacity = MaxInstances;
		}
		else
		{
			NewCapacity *= 2;
		}
	}

	const uint32 ByteSize =
	    NewCapacity *
	    static_cast<uint32>(
	        sizeof(FParticleSpriteInstanceVertex));

	auto NewBuffer =
	    RenderCommand::CreateDynamicVertexBuffer(
	        ByteSize,
	        sizeof(FParticleSpriteInstanceVertex));

	if (!NewBuffer || !NewBuffer->GetBuffer())
	{
		return false;
	}

	InstanceVB = std::move(NewBuffer);
	InstanceCapacity = NewCapacity;

	return true;
}

bool FParticleSpriteRenderer::Draw(
    const TArray<FParticleSpriteInstanceVertex>& Instances,
    UMaterial* Material,
    const FMatrix& ViewProjection,
    const FVector& CameraRight,
    const FVector& CameraUp)
{
	if (Instances.Num() == 0)
	{
		return true;
	}

	if (!Material ||
	    Material->Textures.Num() == 0 ||
	    !Material->Textures[0])
	{
		return false;
	}

	const uint32 InstanceCount =
	    static_cast<uint32>(Instances.Num());

	if (!EnsureCapacity(InstanceCount))
	{
		return false;
	}

	void* Dest =
	    RenderCommand::MapWriteDiscard(InstanceVB.get());

	if (!Dest)
	{
		return false;
	}

	std::memcpy(
	    Dest,
	    Instances.GetData(),
	    static_cast<size_t>(InstanceCount) *
	        sizeof(FParticleSpriteInstanceVertex));

	RenderCommand::Unmap(InstanceVB.get());

	FParticleSpriteViewConstants Constants{};

	Constants.ViewProjection = ViewProjection;
	Constants.CameraRight = CameraRight;
	Constants.CameraUp = CameraUp;

	RenderCommand::UpdateBufferData(
	    ViewCB.get(),
	    &Constants);

	RenderCommand::BindConstantBuffer(
	    3,
	    ViewCB.get(),
	    EShaderBindFlagBits::Vertex);

	ID3D11DeviceContext* Context = RenderCommand::GetContext();

	Context->VSSetShader(
	    Shader->VertexShader->GetShader(),
	    nullptr,
	    0);

	Context->PSSetShader(
	    Shader->PixelShader->GetShader(),
	    nullptr,
	    0);

	Context->IASetInputLayout(InputLayout.Get());

	RenderCommand::SetPrimitiveTopology(
	    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D11Buffer* Buffers[2] = {
		QuadVB->GetBuffer(),
		InstanceVB->GetBuffer()
	};

	const UINT Strides[2] = {
		sizeof(FParticleSpriteVertex),
		sizeof(FParticleSpriteInstanceVertex)
	};

	const UINT Offsets[2] = { 0, 0 };

	Context->IASetVertexBuffers(
	    0,
	    2,
	    Buffers,
	    Strides,
	    Offsets);

	RenderCommand::BindIndexBuffer(QuadIB.get());

	RenderCommand::SetRasterizerState(
	    ERasterizerState::SolidNone);

	RenderCommand::SetBlendState(
		Material->BlendState);

		RenderCommand::SetDepthStencilState(
			Material->DepthStencilState);

		RenderCommand::BindSamplerState(
			0,
			Material->SamplerState,
			EShaderBindFlagBits::Pixel);

		RenderCommand::BindShaderResource(
			0,
			Material->Textures[0],
			EShaderBindFlagBits::Pixel);

		RenderCommand::DrawIndexedInstanced(
			6,
			InstanceCount);

		// 다음 일반 메시 Draw에 인스턴스 입력을 남기지 않는다.
		ID3D11Buffer* NullBuffer = nullptr;
		const UINT Zero = 0;

		Context->IASetVertexBuffers(
			1,
			1,
			&NullBuffer,
			&Zero,
			&Zero);

		return true;
}

void FParticleSpriteRenderer::CollectPackets(
	FRenderQueue& InOutRenderQueue,
	TArray<FRenderPacket>& OutParticlePackets)
{
	OutParticlePackets.Reset();

	const uint32 PacketCount =
		static_cast<uint32>(InOutRenderQueue.Num());

	uint32 WriteIndex = 0;

	for (uint32 ReadIndex = 0;
		ReadIndex < PacketCount;
		++ReadIndex)
	{
		FRenderPacket& Packet =
			InOutRenderQueue[ReadIndex];

		if (Packet.IsSpriteParticle())
		{
			OutParticlePackets.Add(
				std::move(Packet));

			continue;
		}

		// 일반 메시 패킷은 기존 순서를 유지하며 앞으로 모은다.
		if (WriteIndex != ReadIndex)
		{
			InOutRenderQueue[WriteIndex] =
				std::move(Packet);
		}

		++WriteIndex;
	}

	// 패킷 배열만 줄인다.
	// StoreWorldMatrix()가 사용하는 MatrixBlocks는 유지한다.
	InOutRenderQueue.SetNum(
		static_cast<int32>(WriteIndex),
		false);

	// 이미터 배치 간 거리 정렬.
	std::stable_sort(
		OutParticlePackets.begin(),
		OutParticlePackets.end(),
		[](const FRenderPacket& A, const FRenderPacket& B)
		{
			return A.CameraToParticleDistance > B.CameraToParticleDistance;
		});
}

void FParticleSpriteRenderer::OnRender(
	const TArray<FRenderPacket>& ParticlePackets,
	const FMatrix& ViewProjection,
	const FVector& CameraRight,
	const FVector& CameraUp)
{
	if (ParticlePackets.Num() == 0)
	{
		return;
	}

	const FVector Right = CameraRight.Normalized();
	const FVector Up = CameraUp.Normalized();

	for (const FRenderPacket& Packet : ParticlePackets)
	{
		if (!Packet.IsSpriteParticle())
		{
			continue;
		}

		if (!Draw(
			Packet.SpriteInstances,
			Packet.Material,
			ViewProjection,
			Right,
			Up))
		{
			HTR_LOG(
			    Error,
			    "Failed to draw sprite particle batch.");
		}
	}
}
