#include "ShaderMaterialValidation.h"
#include "BuiltInShaders.h"
#include "../Materials/MaterialAsset.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>//bhjsunxuiibjd
#include <string>
namespace ReplayEngine::Rendering::Validation {
namespace { class Checker { public: void Expect(bool ok,const char* what){++total_; if(ok)return; ++failed_; std::fprintf(stderr,"  [FAIL] %s\n",what);} int Report()const{std::fprintf(stderr,"shader-material %s: %d checks, %d failed\n",failed_?"FAILED":"OK",total_,failed_); return failed_?1300:0;} private:int total_=0,failed_=0;}; }
int RunShaderMaterialValidation(){
 
  Checker c; namespace fs=std::filesystem; const fs::path dir=fs::temp_directory_path()/"replay_shader_material_validation"; std::error_code ec; fs::remove_all(dir,ec); fs::create_directories(dir,ec);
  MaterialAsset source; source.shading_model=2; source.shader_guid=BuiltInShaders::Toon.ToString(); source.base_color={0.2f,0.3f,0.4f,1.0f}; source.roughness=0.72f; source.properties.Set("prop.UnknownFutureValue",Reflection::PropertyValue::MakeString("keep-me"));
  std::string error; const fs::path v4=dir/"v4.replaymaterial"; c.Expect(MaterialAsset::Save(source,v4,error),"v4 save"); MaterialAsset loaded; c.Expect(MaterialAsset::Load(v4,loaded,error),"v4 load"); c.Expect(loaded.shader_guid==BuiltInShaders::Toon.ToString(),"shader guid roundtrip"); c.Expect(loaded.properties.Find("prop.BaseColor")!=nullptr,"legacy BaseColor migrated into properties"); const auto* unknown=loaded.properties.Find("prop.UnknownFutureValue"); c.Expect(unknown && unknown->AsString()=="keep-me","unknown property preserved"); c.Expect(loaded.roughness>0.71f && loaded.roughness<0.73f,"legacy field synchronized from property bag");
  const fs::path v1=dir/"v1.replaymaterial"; { std::ofstream s(v1); s<<"REPLAY_MATERIAL 1\nBASE_COLOR 1 0 0 1\nBASE_COLOR_TEXTURE \"\"\nNORMAL_TEXTURE \"\"\nMETALLIC 0.25\nMETALLIC_TEXTURE \"\"\nROUGHNESS 0.5\nROUGHNESS_TEXTURE \"\"\nEMISSIVE 0 0 0 0\nEMISSIVE_TEXTURE \"\"\nAMBIENT_OCCLUSION 1\nAMBIENT_OCCLUSION_TEXTURE \"\"\nALPHA 0 0.5\nDOUBLE_SIDED 0\nSHADING_MODEL 1\nEND_MATERIAL\n"; }
  MaterialAsset old; c.Expect(MaterialAsset::Load(v1,old,error),"v1 load"); c.Expect(old.shader_guid==BuiltInShaders::Pbr.ToString(),"v1 shading_model to shader guid"); c.Expect(old.properties.Find("prop.Metallic")!=nullptr,"v1 fixed fields migrated"); c.Expect(old.properties.Find("prop.OcclusionMap")!=nullptr,"v1 AO map migrated to prop.OcclusionMap");
 const fs::path migrated=dir/"migrated.replaymaterial"; c.Expect(MaterialAsset::Save(old,migrated,error),"migrated save"); std::ifstream check(migrated); std::string text((std::istreambuf_iterator<char>(check)),{}); c.Expect(text.find("REPLAY_MATERIAL 5")!=std::string::npos,"save always v5");
 fs::remove_all(dir,ec); return c.Report(); }
}
