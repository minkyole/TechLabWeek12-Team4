@echo off
setlocal
pushd "%~dp0..\.."
where cl >nul 2>nul
if errorlevel 1 (
  echo Run this script from an x64 Visual Studio Developer Command Prompt after building Debug.
  popd
  exit /b 1
)
if not exist Build\BlendSpaceChecks mkdir Build\BlendSpaceChecks
set blend_test_compile=/nologo /std:c++20 /EHsc /MDd /Z7 /utf-8 /D_DEBUG /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUNICODE /D_UNICODE /DENGINE_PLATFORM_WINDOWS /I Source /I Source/Runtime /I Source/ThirdParty/json /I Source/ThirdParty/ImGui /I vcpkg_installed/x64-windows/include /I vcpkg_installed/x64-windows/include/luajit
set blend_test_libraries=Build/Bin/Debug-windows-x86_64/HitoriEngine.lib Build/Bin/Debug-windows-x86_64/ImGui.lib /LIBPATH:vcpkg_installed/x64-windows/debug/lib lua51.lib d3d11.lib dxgi.lib dxguid.lib d3dcompiler.lib user32.lib gdi32.lib shell32.lib ole32.lib
cl %blend_test_compile% Tools/Tests/BlendSpaceAssetTests.cpp /Fo:Build/BlendSpaceChecks/BlendSpaceAssetTests.obj /Fe:Build/Bin/Debug-windows-x86_64/BlendSpaceAssetTests.exe /link /DEBUG /PDB:Build/BlendSpaceChecks/BlendSpaceAssetTests.pdb %blend_test_libraries%
if errorlevel 1 goto failure
Build\Bin\Debug-windows-x86_64\BlendSpaceAssetTests.exe
if errorlevel 1 goto failure
cl %blend_test_compile% Tools/Tests/BlendSpaceEditorTests.cpp Source/Editor/BlendSpaceEditor/BlendSpaceEditorPanel.cpp /Fo:Build/BlendSpaceChecks/ /Fe:Build/Bin/Debug-windows-x86_64/BlendSpaceEditorTests.exe /link /DEBUG /PDB:Build/BlendSpaceChecks/BlendSpaceEditorTests.pdb %blend_test_libraries%
if errorlevel 1 goto failure
Build\Bin\Debug-windows-x86_64\BlendSpaceEditorTests.exe
if errorlevel 1 goto failure
popd
exit /b 0
:failure
popd
exit /b 1
