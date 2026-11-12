#pragma once

// 엔진이 동작하는 데 반드시 필요한 파일 경로. 모두 Resources 아래에 있어야 한다.
// Resources = 엔진·에디터 내장 파일(셰이더, 폰트, 아이콘, 스플래시, 기본 하늘). 스캔하지 않고 여기 적힌 경로로만 불러온다.
// Assets    = 사용자 콘텐츠(모델, 텍스처, 씬). 시작할 때 통째로 스캔하고 Content Drawer에 보인다.
// 에디터 전용 경로는 Editor/HitoriEd/EditorResourcePaths.h에 둔다.
namespace ResourcePaths
{
	constexpr const char* SplashImage = "Resources/Splash/Splash.png";

	// 에디터 UUID 표시 등 엔진이 직접 쓰는 기본 폰트
	constexpr const char* DefaultFont = "Resources/Fonts/Pretendard.json";
	constexpr const char* DefaultFontAtlas = "Resources/Fonts/Pretendard.png";

	constexpr const char* DefaultSkyPanorama = "Resources/SkySphere/Sky.hdr";
	constexpr const char* DefaultParticleTexture = "Resources/SubUV/StarParticle.png";

	// 라이트·안개·데칼 액터의 에디터 빌보드 아이콘
	constexpr const char* DirectionalLightIcon = "Resources/Icons/Directional.png";
	constexpr const char* PointLightIcon = "Resources/Icons/Point.png";
	constexpr const char* SpotLightIcon = "Resources/Icons/Spot.png";
	constexpr const char* RectLightIcon = "Resources/Icons/Rect.png";
	constexpr const char* SkyLightIcon = "Resources/Icons/Sky.png";
	constexpr const char* FogIcon = "Resources/Icons/Fog.png";
	constexpr const char* DecalIcon = "Resources/Icons/Decal.png";
}
