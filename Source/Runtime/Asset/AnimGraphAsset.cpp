#include "EnginePCH.h"
#include "AssetManager.h"
#include "Animation/AnimGraph.h"
#include "Core/Misc/FileHelper.h"

namespace
{
bool GraphPath(const FString& Path, fs::path& Native, FString& Key, FString& Error)
{
    std::error_code EC;
    Native=fs::weakly_canonical(FFileHelper::ToNativePath(Path),EC);
    if(EC) { Error="Invalid graph path"; return false; }
    auto Root=fs::weakly_canonical("Assets",EC);
    auto Relative=Native.lexically_relative(Root);
    if(EC || Relative.empty() || Relative.is_absolute() || *Relative.begin()==".." || _stricmp(Native.extension().string().c_str(),".animgraph"))
    { Error="Choose an .animgraph file inside Assets"; return false; }
    Key=FFileHelper::ToUtf8Path(Relative); return true;
}
}
UAnimGraphAsset* UAssetManager::LoadAnimGraph(const FString& Path, FString& Error)
{
    Error.clear(); fs::path Native; FString Key, Text;
    if(!GraphPath(Path,Native,Key,Error)) return nullptr;
    if(AssetMap.Contains(Key) && !GetAssetByPath<UAnimGraphAsset>(Key)) { Error="Asset key belongs to a different type"; return nullptr; }
    if(!FFileHelper::LoadFileToString(Text,FFileHelper::ToUtf8Path(Native))) { Error="Cannot read AnimGraph"; return nullptr; }
    try
    {
        auto J=json::parse(Text);
        if(J.at("Version")!=1 || J.at("Type")!="AnimGraph") { Error="Unsupported AnimGraph version/type"; return nullptr; }
        FAnimGraphAssetData D; D.MeshKey=J.at("Mesh"); D.UpdateScript=J.value("UpdateScript",FString{}); D.Graph=ReadAnimPoseGraph(J.at("Graph"));
        for(auto& V:J.at("Parameters")) { FAnimParameter P; P.Name=FName(V.at("Name").get<FString>().c_str()); P.Type=(EAnimParameterType)V.at("Type").get<int>(); P.BoolValue=V.at("Bool"); P.FloatValue=V.at("Float"); D.Parameters.push_back(P); }
        if(!D.Validate(Error)) return nullptr;
        auto* Asset=GetAssetByPath<UAnimGraphAsset>(Key);
        if(!Asset) { Asset=NewObject<UAnimGraphAsset>(); RegisterAsset(Key,Asset); }
        Asset->Data=std::move(D); AssetPathMap.Add(Key,FFileHelper::ToUtf8Path(Native)); return Asset;
    }
    catch(const std::exception& E) { Error=FString("Invalid AnimGraph: ")+E.what(); return nullptr; }
}
bool UAssetManager::SaveAnimGraph(const FString& Path, const FAnimGraphAssetData& D, FString& Error, bool CreateOnly)
{
    Error.clear(); fs::path Native; FString Key;
    if(!GraphPath(Path,Native,Key,Error) || !D.Validate(Error)) return false;
    if(AssetMap.Contains(Key) && !GetAssetByPath<UAnimGraphAsset>(Key)) { Error="Asset key belongs to a different type"; return false; }
    std::error_code EC;
    if(CreateOnly && fs::exists(Native,EC)) { Error="File already exists"; return false; }
    json J={{"Version",1},{"Type","AnimGraph"},{"Mesh",D.MeshKey},{"UpdateScript",D.UpdateScript},{"Graph",AnimPoseGraphJson(D.Graph)},{"Parameters",json::array()}};
    for(auto& P:D.Parameters) J["Parameters"].push_back({{"Name",P.Name.ToString()},{"Type",(int)P.Type},{"Bool",P.BoolValue},{"Float",P.FloatValue}});
    wchar_t Temp[MAX_PATH];
    if(!GetTempFileNameW(Native.parent_path().c_str(),L"agr",0,Temp)) { Error="Cannot create graph save file"; return false; }
    bool OK=false;
    try { OK=FFileHelper::SaveStringToFile(J.dump(2),FFileHelper::ToUtf8Path(Temp)); } catch(const std::exception& E) { Error=E.what(); }
    if(OK) OK=MoveFileExW(Temp,Native.c_str(),MOVEFILE_WRITE_THROUGH|(CreateOnly?0:MOVEFILE_REPLACE_EXISTING))!=FALSE;
    if(!OK) { fs::remove(Temp,EC); Error="Cannot save AnimGraph"; return false; }
    auto* Asset=GetAssetByPath<UAnimGraphAsset>(Key);
    if(!Asset) { Asset=NewObject<UAnimGraphAsset>(); RegisterAsset(Key,Asset); }
    Asset->Data=D; AssetPathMap.Add(Key,FFileHelper::ToUtf8Path(Native)); return true;
}
