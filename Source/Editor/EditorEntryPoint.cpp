#include "EnginePCH.h"
#include "EditorEntryPoint.h"

#include "Launch/EntryPoint.h"

#include "Editor/HitoriEd/EditorEngine.h"
#include "Programs/ObjViewer/ObjViewerApp.h"
#include "Programs/Benchmark/BenchmarkApp.h"
#include "Programs/Executable/ExecutableApp.h"

UClass* GetEngineClass() {
#ifdef OBJ_VIEWER
	return UObjViewerEngine::StaticClass();
#elif BENCHMARK
	return UBenchmarkEngine::StaticClass();
#elif defined(EXECUTABLE)
	return UExecutableEngine::StaticClass();
#else
	return UEditorEngine::StaticClass();
#endif
}
