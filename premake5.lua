local function Shell(Command)
	return '"' .. Command .. '"'
end

local function FindVcpkg()
	local Root = os.getenv("VCPKG_ROOT")
	if Root and os.isfile(Root .. "/vcpkg.exe") then
		return Root .. "/vcpkg.exe"
	end

	local VsWhere = os.getenv("ProgramFiles(x86)") .. "/Microsoft Visual Studio/Installer/vswhere.exe"
	if os.isfile(VsWhere) then
		local VsPath = os.outputof(Shell('"' .. VsWhere .. '" -latest -prerelease -property installationPath'))
		if VsPath and os.isfile(VsPath .. "/VC/vcpkg/vcpkg.exe") then
			return VsPath .. "/VC/vcpkg/vcpkg.exe"
		end
	end
end

local RepoRoot = _MAIN_SCRIPT_DIR
local FbxSdkRoot = "C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.11"
if _ACTION and _ACTION:match("^vs") and not os.isdir(RepoRoot .. "/vcpkg_installed/x64-windows/lib") then
	local Vcpkg = FindVcpkg()
	if Vcpkg then
		os.execute(Shell('"' .. Vcpkg .. '" install --triplet x64-windows'
			.. ' --x-manifest-root="' .. RepoRoot .. '"'
			.. ' --x-install-root="' .. RepoRoot .. '/vcpkg_installed"'))
	else
		print("vcpkg를 찾지 못했습니다. VCPKG_ROOT를 설정하거나 Visual Studio의 vcpkg 구성 요소를 설치하세요.")
	end
end

workspace "Hitori"
	architecture "x64"
	startproject "HitoriEditor"

	configurations
	{
		"Debug",
		"Release",
		"ObjViewer",
		"Executable",
		"Benchmark",
		"Benchmark_Debug",
	}

	multiprocessorcompile "On"

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
local UhtGeneratedRoot = "Build/Generated/UHT/" .. outputdir

IncludeDir = {}
IncludeDir["ImGui"] = "Source/ThirdParty/ImGui"
IncludeDir["stb"]   = "Source/ThirdParty/stb"
IncludeDir["json"]  = "Source/ThirdParty/json"
IncludeDir["DDSLoader"] = "Source/ThirdParty/DDSLoader"
IncludeDir["miniaudio"] = "Source/ThirdParty/miniaudio"
IncludeDir["Sol2"] = "vcpkg_installed/x64-windows/include"
IncludeDir["LuaJIT"] = "vcpkg_installed/x64-windows/include/luajit"

-- premake의 filter는 project()를 만나면 초기화된다.
-- 두 프로젝트가 같은 런타임(/MDd vs /MD)으로 컴파일되지 않으면 링크가 실패하므로
-- 공통 설정을 함수로 묶어 각 프로젝트에서 호출한다.
function CommonSettings()
	language   "C++"
	cppdialect "C++20"
	staticruntime "off"
	characterset  "Unicode"

	targetdir ("Build/Bin/" .. outputdir)
	objdir    ("Build/Intermediate/" .. outputdir)
	-- 에셋·쉐이더를 상대 경로로 읽으므로 작업 디렉터리는 저장소 루트다.
	debugdir  "%{wks.location}"

	defines
	{
		"WIN32_LEAN_AND_MEAN",
		"NOMINMAX",
		"UNICODE",
		"_UNICODE",
	}

	filter "files:**.hlsl"
		excludefrombuild "On"

	filter "system:windows"
		systemversion "latest"
		buildoptions { "/utf-8" }
		defines { "ENGINE_PLATFORM_WINDOWS" }

	filter "configurations:Debug"
		defines { "ENGINE_DEBUG", "_DEBUG" }
		runtime  "Debug"
		symbols  "on"

	filter "configurations:Release"
		defines  { "ENGINE_RELEASE", "NDEBUG" }
		runtime  "Release"
		optimize "on"
		symbols  "on"

	-- 에디터 없이 OBJ 파일만 열어보는 Viewer 빌드
	filter "configurations:ObjViewer"
		defines  { "ENGINE_RELEASE", "NDEBUG", "OBJ_VIEWER" }
		runtime  "Release"
		optimize "on"
		symbols  "on"

	filter "configurations:Executable"
		defines  { "ENGINE_RELEASE", "NDEBUG", "EXECUTABLE" }
		runtime  "Release"
		optimize "on"
		symbols  "on"

	filter "configurations:Benchmark_Debug"
		defines  { "ENGINE_DEBUG", "_DEBUG", "BENCHMARK" }
		runtime  "Debug"
		symbols  "on"

	filter "configurations:Benchmark"
		defines  { "ENGINE_RELEASE", "NDEBUG", "BENCHMARK" }
		runtime  "Release"
		optimize "speed"
		-- /GL + /LTCG는 끈다. 켠 exe가 Defender ML 탐지(Trojan:Win32/Wacatac)에 오탐으로 걸렸다.
		vectorextensions "AVX2"
		-- /GS-(버퍼 보안 검사 끄기)는 성능 이득이 거의 없고 백신 휴리스틱 오탐을 부르기 쉬워 켜 둔다 (기본값 /GS)
		buildoptions { "/Gw" }
		symbols  "on"



	filter {}
end

-- 헤더툴
project "UHT"
    location "Source/Programs/UHT"
    kind "ConsoleApp"
    CommonSettings()
    enablepch "Off"
    objdir ("Build/Intermediate/" .. outputdir .. "/UHT")

    files
    {
        "Source/Programs/UHT/*.h",
        "Source/Programs/UHT/*.cpp",
    }

-- 엔진과 에디터가 공유하는 리플렉션 코드 생성 단계
project "UHTCodegen"
	location "Intermediate/ProjectFiles"
	kind "Utility"
	CommonSettings()
	objdir ("Build/Intermediate/" .. outputdir .. "/UHTCodegen")

	-- 실행 도구의 선행 빌드를 보장하는 프로젝트 참조
	links { "UHT" }
	fastuptodate "Off"

	prebuildmessage "Generating reflection code"
	prebuildcommands
	{
		Shell(RepoRoot .. "/Build/Bin/" .. outputdir .. "/UHT.exe")
			.. " --root " .. Shell(RepoRoot)
			.. " --output " .. Shell(RepoRoot .. "/" .. UhtGeneratedRoot),
	}

-- 외부 라이브러리. 별도 프로젝트로 두면 에디터의 소스 루트가 Source/Editor 하나로 좁혀져
-- Solution Explorer의 "모든 파일 표시"에서 폴더 구조가 그대로 보인다.
project "ImGui"
	location "Source/ThirdParty/ImGui"
	kind     "StaticLib"
	CommonSettings()

	files
	{
		"%{IncludeDir.ImGui}/*.h",
		"%{IncludeDir.ImGui}/*.cpp",
		"%{IncludeDir.ImGui}/backends/imgui_impl_win32.*",
		"%{IncludeDir.ImGui}/backends/imgui_impl_dx11.*",
		"%{IncludeDir.ImGui}/misc/cpp/imgui_stdlib.h",
		"%{IncludeDir.ImGui}/misc/cpp/imgui_stdlib.cpp",
	}

	includedirs
	{
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.ImGui}/backends",
	}

	links
	{
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
	}


-- 런타임. 에디터를 모른다. include 경로에 Source/Editor가 없는 것이 그 방벽이다.
-- location이 소스 루트와 같아야 "모든 파일 표시"에서 폴더가 보인다.
project "HitoriEngine"
	location "Source/Runtime"
	kind     "StaticLib"
	CommonSettings()
	links { "UHTCodegen" }

	includedirs
	{
		FbxSdkRoot .. "/include",
	}

	defines
	{
		"FBXSDK_SHARED",
	}

		links
	{
		"libfbxsdk",
	}

	filter "configurations:Debug or Benchmark_Debug"
		libdirs
		{
			FbxSdkRoot .. "/lib/x64/debug",
		}

		postbuildcommands
		{
			'{COPYFILE} "' .. FbxSdkRoot
				.. '/lib/x64/debug/libfbxsdk.dll" "%{cfg.targetdir}/libfbxsdk.dll"',
		}

	filter "configurations:Release or Benchmark or ObjViewer or Executable"
		libdirs
		{
			FbxSdkRoot .. "/lib/x64/release",
		}

		postbuildcommands
		{
			'{COPYFILE} "' .. FbxSdkRoot
				.. '/lib/x64/release/libfbxsdk.dll" "%{cfg.targetdir}/libfbxsdk.dll"',
		}

	filter {}

	pchheader "EnginePCH.h"
	pchsource "Source/Runtime/EnginePCH.cpp"

	files
	{
		"Source/Runtime/**.h",
		"Source/Runtime/**.hpp",
		"Source/Runtime/**.cpp",
		UhtGeneratedRoot .. "/Runtime/Runtime.gen.cpp",
		"%{IncludeDir.DDSLoader}/*.h",
		"%{IncludeDir.DDSLoader}/*.cpp",
		"%{IncludeDir.miniaudio}/*.h",
		"%{IncludeDir.miniaudio}/*.cpp",
	}

	includedirs
	{
		"Source/Runtime",
		UhtGeneratedRoot .. "/Runtime",
		"%{IncludeDir.LuaJIT}",
    	"%{IncludeDir.Sol2}",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
		"%{IncludeDir.DDSLoader}",
		"%{IncludeDir.miniaudio}",
	}

	-- D3D 시스템 lib는 exe(HitoriEditor)에서 링크한다. 정적 lib에 넣으면 import lib가 엔진 lib 안에 합쳐져
	-- /WHOLEARCHIVE로 강제 링크할 때 __NULL_IMPORT_DESCRIPTOR가 중복 정의된다.

	filter "files:Source/ThirdParty/DDSLoader/**.cpp"
		enablepch "Off"
	filter "files:Source/ThirdParty/miniaudio/**.cpp"
		enablepch "Off"
	filter "files:Source/Runtime/Scripting/**.cpp"
		buildoptions { "/bigobj" }
	filter {}

-- 에디터 애플리케이션. ObjViewer 구성에서는 같은 exe가 뷰어로 빌드된다.
project "HitoriEditor"
	location "Source/Editor"
	kind     "WindowedApp"
	CommonSettings()

	pchheader "EnginePCH.h"
	pchsource "Source/Editor/EditorPCH.cpp"

	links { "UHTCodegen", "HitoriEngine", "ImGui" }

	-- 엔진은 정적 라이브러리라 exe가 참조하지 않는 .obj는 링크에서 빠지고,
	-- 그 안의 클래스 자동 등록(DECLARE_CLASS의 FAutoRegister)도 실행되지 않는다. 엔진 전체를 강제로 링크한다.
	linkoptions { "/WHOLEARCHIVE:HitoriEngine.lib" }

	files
	{
		"Source/Editor/**.h",
		"Source/Editor/**.cpp",
		UhtGeneratedRoot .. "/Editor/Editor.gen.cpp",
		"Source/ThirdParty/ImNeoSequencer/*.h",
		"Source/ThirdParty/ImNeoSequencer/*.cpp",
		"Source/ThirdParty/ImGuiNodeEditor/*.h",
		"Source/ThirdParty/ImGuiNodeEditor/*.inl",
		"Source/ThirdParty/ImGuiNodeEditor/*.cpp",
		"Source/Programs/**.h",
		"Source/Programs/**.cpp",
		"Source/Editor/**.rc",   -- 창·exe 아이콘
	}
	-- 에디터 빌드 대상에서 독립 헤더 도구 제외
    	removefiles
    	{
        	"Source/Programs/UHT/**",
    	}
	includedirs
	{
		"Source",             -- "Editor/OutputLog/ConsolePanel.h"처럼 Source 기준 include를 쓴다
		"Source/Runtime",
		"Source/Programs",
		UhtGeneratedRoot .. "/Runtime",
		UhtGeneratedRoot .. "/Editor",
		"Source/ThirdParty/ImGuiNodeEditor",
		"%{IncludeDir.LuaJIT}",
    	"%{IncludeDir.Sol2}",
		"%{IncludeDir.ImGui}",
		"%{IncludeDir.ImGui}/backends",
		"%{IncludeDir.stb}",
		"%{IncludeDir.json}",
	}

	links
	{
		"lua51",
		"d3d11.lib",
		"dxgi.lib",
		"dxguid.lib",
		"d3dcompiler.lib",
	}

	filter "files:Source/ThirdParty/ImNeoSequencer/*.cpp"
		flags { "NoPCH" }
	filter "files:Source/ThirdParty/ImGuiNodeEditor/*.cpp"
		flags { "NoPCH" }
	filter {}

	filter "configurations:Debug or Benchmark_Debug"
		libdirs
		{
			"vcpkg_installed/x64-windows/debug/lib",
		}

		postbuildcommands
		{
			"{COPYFILE} %[vcpkg_installed/x64-windows/debug/bin/lua51.dll] %[%{!cfg.targetdir}/lua51.dll]",
		}

	filter "configurations:Release or Benchmark or ObjViewer or Executable"
		libdirs
		{
			"vcpkg_installed/x64-windows/lib",
		}

		postbuildcommands
		{
			"{COPYFILE} %[vcpkg_installed/x64-windows/bin/lua51.dll] %[%{!cfg.targetdir}/lua51.dll]",
		}

	filter {}

	filter "configurations:ObjViewer"
		targetname "ObjViewer"

	filter "configurations:Executable"
		targetname "Executable"

	filter "configurations:Benchmark"
		targetname "Benchmark"

	filter "configurations:Benchmark_Debug"
		targetname "Benchmark_Debug"

	filter {}


-- 셰이더를 VS에서 편집하기 위한 보기 전용 프로젝트 (언리얼의 Shaders와 같은 역할).
-- 에셋(Assets)은 에디터 Content Drawer에서 다루므로 솔루션에 넣지 않는다.
-- 새 셰이더 파일은 SlnGenerator.bat을 다시 돌려야 목록에 반영된다.
project "Shaders"
	location "Intermediate/ProjectFiles"
	kind     "Utility"

	files { "Resources/Shader/**.hlsl" }
	files { "Resources/Shader/**.hlsli" }
	vpaths { ["*"] = "Resources/Shader/**" }

	filter "files:**.hlsl"
		excludefrombuild "On"

	filter "files:**.hlsli"
		excludefrombuild "On"

	filter {}
