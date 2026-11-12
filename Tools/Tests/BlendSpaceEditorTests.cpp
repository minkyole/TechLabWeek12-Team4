#include "EnginePCH.h"
#include "Editor/BlendSpaceEditor/BlendSpaceEditorPanel.h"
#include "Animation/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/FileHelper.h"
#include "Render/Mesh.h"
#include "UObject/UObjectGlobals.h"
#include <memory>
#include <cstdlib>
#include <cmath>

static int Checks = 0;
static void Check(bool Value, const char* Label)
{
 ++Checks;
 if (!Value) { std::cerr << "FAIL: " << Label << '\n'; std::exit(1); }
}

int main()
{
 auto& Manager = UAssetManager::Get();
 std::unique_ptr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>());
 std::unique_ptr<UAnimSequence> Sequence(NewObject<UAnimSequence>());
 Sequence->SetSkeletalMesh(Mesh.get()); Sequence->BuildFromTracks(FFrameRate(30,1),30,{});
 Manager.RegisterAsset("UITests/Character.fbx",Mesh.get());
 Manager.RegisterAsset("UITests/Character.fbx#Anim/Idle",Sequence.get());
 const fs::path Folder = fs::path("Assets") / ("_BlendSpaceUITests_"+std::to_string(GetCurrentProcessId()));
 Check(fs::create_directory(Folder),"create UI test directory");
 const FString Path=FFileHelper::ToUtf8Path(Folder/"UI.blendspace");
 FString Error;
 auto* Asset=Manager.CreateBlendSpace1D(Path,Mesh.get(),Error);
 Check(Asset!=nullptr,"create UI asset");
 Check(Asset->AddSample({Sequence.get(),FVector(300,0,0)}),"create graph sample");
 ImGui::CreateContext();
 auto& IO=ImGui::GetIO(); IO.DisplaySize=ImVec2(1400,900); IO.DeltaTime=1.0f/60; IO.IniFilename=nullptr;
 unsigned char* Pixels; int Width,Height; IO.Fonts->GetTexDataAsRGBA32(&Pixels,&Width,&Height);
 {
  FBlendSpaceEditorPanel Panel; Panel.Init(); Panel.OpenAsset(Path);
  auto Frame=[&]() { ImGui::NewFrame(); Panel.OnRender(); ImGui::Render(); };
  Frame(); Frame();
  Check(ImGui::GetDrawData()->TotalVtxCount>0,"panel renders all sections without a graphics device");
  ImVec2 Center; bool Found=false;
  for (int L=0; L<ImGui::GetDrawData()->CmdListsCount; ++L)
  {
   const auto* List=ImGui::GetDrawData()->CmdLists[L];
   for (int I=0; I+3<List->VtxBuffer.Size; ++I)
   {
    if (List->VtxBuffer[I].col==IM_COL32(106,183,223,255))
    {
     Center=ImVec2((List->VtxBuffer[I].pos.x+List->VtxBuffer[I+2].pos.x)*.5f,(List->VtxBuffer[I].pos.y+List->VtxBuffer[I+2].pos.y)*.5f);
     Found=true; break;
    }
   }
   if (Found) break;
  }
  Check(Found,"graph draws a sample diamond");
  IO.AddMousePosEvent(Center.x,Center.y); Frame();
  IO.AddMouseButtonEvent(0,true); Frame();
  IO.AddMouseButtonEvent(0,false); Frame();
  bool Selected=false;
  for (int L=0;L<ImGui::GetDrawData()->CmdListsCount;++L)
   for (const auto& V:ImGui::GetDrawData()->CmdLists[L]->VtxBuffer)
    Selected|=V.col==IM_COL32(255,172,51,255);
  Check(Selected,"click selects graph sample");
  IO.AddMouseButtonEvent(0,true); Frame();
  IO.AddMousePosEvent(Center.x+50,Center.y); Frame(); Frame();
  IO.AddMouseButtonEvent(0,false); Frame();
  Check(Asset->GetSample(0)->SampleValue.X==300,"drag leaves live asset unchanged before save");
  IO.AddKeyEvent(ImGuiMod_Ctrl,true); IO.AddKeyEvent(ImGuiKey_S,true); Frame();
  IO.AddKeyEvent(ImGuiKey_S,false); IO.AddKeyEvent(ImGuiMod_Ctrl,false); Frame();
  Check(Asset->GetSample(0)->SampleValue.X>300,"focused Ctrl+S saves the dragged sample");
  Check(Manager.LoadBlendSpace(Path,Error)==Asset,"editor save retains registered asset identity");
  IO.AddKeyEvent(ImGuiKey_Delete,true); Frame(); IO.AddKeyEvent(ImGuiKey_Delete,false); Frame();
  Check(Asset->GetNumSamples()==1,"graph deletion affects draft only");
  Panel.SetOpen(false); Frame(); Frame(); Frame();
  Check(Panel.IsOpen(),"menu close preserves dirty editor for confirmation");
  Check(Asset->GetNumSamples()==1,"close confirmation does not commit discarded work");
  auto* Popup=ImGui::FindWindowByName("Unsaved BlendSpace");
  Check(Popup!=nullptr,"unsaved confirmation is visible");
  const auto& Style=ImGui::GetStyle();
  const float ButtonX=Popup->WorkRect.Min.x+ImGui::CalcTextSize("Save").x+2*Style.FramePadding.x+Style.ItemSpacing.x+
   (ImGui::CalcTextSize("Discard").x+2*Style.FramePadding.x)*.5f;
  const float ButtonY=Popup->WorkRect.Min.y+ImGui::GetTextLineHeight()+Style.ItemSpacing.y+ImGui::GetFrameHeight()*.5f;
  IO.AddMousePosEvent(ButtonX,ButtonY); Frame();
  IO.AddMouseButtonEvent(0,true); Frame(); IO.AddMouseButtonEvent(0,false); Frame();
  Check(!Panel.IsOpen(),"discard closes the dirty panel");
  Panel.OpenAsset(Path); Frame(); Frame();
  IO.AddMousePosEvent(Center.x,Center.y); IO.AddMouseButtonEvent(0,true); Frame(); IO.AddMouseButtonEvent(0,false); Frame();
  IO.AddKeyEvent(ImGuiMod_Ctrl,true); IO.AddKeyEvent(ImGuiKey_S,true); Frame();
  IO.AddKeyEvent(ImGuiKey_S,false); IO.AddKeyEvent(ImGuiMod_Ctrl,false); Frame();
  Check(Asset->GetNumSamples()==1,"reopening cannot resurrect a discarded deletion");
 }
 ImGui::DestroyContext();
 Manager.Shutdown(); delete Asset;
 fs::remove(FFileHelper::ToNativePath(Path)); fs::remove(Folder);
 std::cout << "PASS: " << Checks << " BlendSpace editor checks\n";
}
