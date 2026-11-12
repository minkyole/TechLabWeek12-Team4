@echo off
chcp 65001 > nul
rem 실행 시 자동 생성되는 캐시를 지운다. 다음 실행 때 다시 만들어진다.
cd /d "%~dp0"

echo [1/2] 셰이더 컴파일 캐시 (Intermediate\Shaders)
if exist "Intermediate\Shaders" rmdir /s /q "Intermediate\Shaders"

echo [2/2] OBJ Bake 캐시 (*.obj.bin)
del /s /q "Assets\*.obj.bin" > nul 2>&1
del /s /q "Resources\*.obj.bin" > nul 2>&1

echo 완료
PAUSE
