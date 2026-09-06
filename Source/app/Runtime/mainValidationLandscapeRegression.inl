// Included only by the headless Landscape validation translation unit.
#include "../../../RePlayEngine/Editor/Inspector/InspectorPanel.h"
#include "../../../RePlayEngine/Editor/Inspector/PropertyDrawer.h"
#include "../../../RePlayEngine/Components/Rendering/PrimitiveMeshRendererComponent.h"
#include "../../../RePlayEngine/Components/Rendering/SkinnedMeshRendererComponent.h"
#include "../../../RePlayEngine/Reflection/Registry/PropertyRegistry.h"
#include "../../../RePlayEngine/Landscape/LandscapeMeshGenerator.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "../Editor/framework_landscape_editorInternal.h"

namespace ReplayEngine::Runtime::Detail
{
    bool RunLandscapeEditorRegression()
    {
        using namespace Landscape;
        using namespace Scene::Serialization;
        std::error_code directory_error;
        std::filesystem::create_directories("Saved/Validation/LandscapeRegression", directory_error);
        std::ofstream results("Saved/Validation/LandscapeRegression/results.txt");
        bool passed = true;
        const auto check = [&](bool ok, const char* label)
        {
            results << (ok ? "PASS " : "FAIL ") << label << '\n';
            results.flush();
            passed = passed && ok;
        };
        const auto pick = [&](const LandscapeData& data, float x, float z)
        {
            LandscapeRayHit hit;
            data.Raycast({ x, data.BoundsMax().y + 10, z }, {0,-1,0}, 10000, hit);
            return hit;
        };
        const auto save_fixture = [&](const char* name, const LandscapeData& source)
        {
            Scene::Scene scene(name);
            auto* object = scene.CreateGameObject("Landscape");
            auto* landscape = object->AddComponent<Components::LandscapeComponent>();
            landscape->Data().RestoreGeometry(source.CaptureGeometry());
            object->AddComponent<Components::LandscapeRendererComponent>();
            object->AddComponent<Components::LandscapeColliderComponent>();
            scene.CreateGameObject("Sun")->AddComponent<Components::DirectionalLightComponent>();
            SceneData file;
            CaptureScene(scene,file);
            std::string error;
            check(SceneSerializer::SaveToFile(file,
                std::filesystem::path("Saved/Validation/LandscapeRegression") / (std::string(name)+".replayscene"),error),name);
        };

        LandscapeData flat;
        check(flat.Initialize(65,65,0.5f),"flat initialization");
        save_fixture("01_Normal",flat);
        LandscapeBrush brush;
        brush.radius=2; brush.strength=1; brush.falloff=0.2f;
        for (auto mode : { LandscapeBrushMode::Raise, LandscapeBrushMode::Lower,
            LandscapeBrushMode::Smooth, LandscapeBrushMode::Flatten })
        {
            if (mode==LandscapeBrushMode::Smooth || mode==LandscapeBrushMode::Flatten)
                flat.SetHeight(32,32,0.8f);
            const auto before=flat.SerializeInline();
            LandscapeEditorTool tool;
            check(tool.BeginStroke(flat,mode,brush),"begin sculpt");
            check(tool.ApplyStrokeSample(pick(flat,16,16),0.25f),"sculpt changes geometry");
            auto command=tool.EndStroke();
            check(command!=nullptr,"sculpt has undo");
            if (!command) continue;
            const auto after=flat.SerializeInline();
            command->Undo(flat);
            check(flat.SerializeInline()==before,"sculpt undo exact geometry");
            command->Redo(flat);
            check(flat.SerializeInline()==after,"sculpt redo exact geometry");
        }

        LandscapeData cave;
        LandscapeData layer;
        layer.Initialize(17,17,0.5f);
        auto vertices=layer.Vertices(); auto indices=layer.Indices();
        const auto layer_count=vertices.size();
        for (auto vertex:layer.Vertices()) { vertex.position.y=0.3f; vertices.push_back(vertex); }
        for (auto index:layer.Indices()) indices.push_back(static_cast<std::uint32_t>(index+layer_count));
        check(cave.InitializeMesh(std::move(vertices),std::move(indices),0.5f),"cave initialization");
        save_fixture("03_CaveOverhang",cave);
        LandscapeEditorTool cave_tool;
        cave_tool.BeginStroke(cave,LandscapeBrushMode::Raise,brush);
        cave_tool.ApplyStrokeSample(pick(cave,4,4),0.5f);
        auto cave_command=cave_tool.EndStroke();
        bool lower_unchanged=true;
        for (std::size_t i=0;i<layer_count;++i) lower_unchanged &= cave.VertexPosition(i).y==0;
        check(lower_unchanged && cave_command!=nullptr,"nearby disconnected lower surface is untouched");
        LandscapeData::SurfaceRegion region;
        auto cave_hit=pick(cave,4,4);
        cave.QuerySurface(cave_hit.position,brush.radius,cave_hit.face_index,region);
        LandscapeRayHit projected;
        check(cave.ProjectSurface({4.2f,0.1f,4},cave_hit.normal,2,region,projected) &&
            projected.position.y>0.25f,"preview projection stays on picked connected surface");

        LandscapeData boundary;
        boundary.Initialize(129,129,0.5f);
        for (int z=0;z<129;++z) for(int x=0;x<129;++x)
            boundary.SetVertexPosition(boundary.Index(x,z),{x*0.5f,0.05f*std::sin(x*0.2f),z*0.5f},false);
        boundary.FinalizeGeometryEdit();
        save_fixture("04_ChunkBoundary",boundary);
        for(auto mode:{LandscapeBrushMode::Raise,LandscapeBrushMode::Smooth})
        {
            brush.direction=LandscapeSculptDirection::VertexNormal;
            LandscapeEditorTool tool;
            tool.BeginStroke(boundary,mode,brush);
            tool.ApplyStrokeSample(pick(boundary,32,32),0.3f);
            auto partial=boundary.Vertices();
            boundary.RecalculateNormals();
            float max_error=0;
            for(std::size_t i=0;i<partial.size();++i)
            {
                const auto a=partial[i].normal,b=boundary.Vertices()[i].normal;
                max_error=(std::max)(max_error,std::fabs(a.x-b.x)+std::fabs(a.y-b.y)+std::fabs(a.z-b.z));
            }
            check(max_error<1.0e-5f,"partial shared normals equal full face reference");
            tool.EndStroke();
        }
        brush.direction=LandscapeSculptDirection::LocalY;
        LandscapeData bounds;
        bounds.Initialize(9,9,1);
        bounds.SetHeight(4,4,10);
        bounds.SetHeight(4,4,0);
        bounds.FinishSculpt();
        check(bounds.BoundsMax().y==0,"bounds shrink after lowering");
        const auto topology_before=bounds.TopologyRevision();
        bounds.SetHeight(4,4,1); bounds.FinishSculpt();
        check(bounds.TopologyRevision()==topology_before,"vertical sculpt retains chunk index layout");
        for(const auto& chunk:bounds.Chunks())
        {
            LandscapeMeshData mesh;
            check(LandscapeMeshGenerator::Generate(bounds,chunk,0,mesh) && mesh.indices.size()==chunk.indices.size(),
                "mesh generator honors chunk indices");
        }

        LandscapeData subdivided;
        subdivided.Initialize(33,33,1);
        brush.target_edge_length=0.2f;
        const auto subdivide_before=subdivided.SerializeInline();
        const auto capture_before=SceneCaptureCount();
        LandscapeEditorTool subdivide_tool;
        check(subdivide_tool.BeginStroke(subdivided,LandscapeBrushMode::Subdivide,brush),"subdivide begins dedicated undo");
        for(int i=0;i<3;++i) subdivide_tool.ApplyStrokeSample(pick(subdivided,16,16),1.0f/60);
        auto subdivide_command=subdivide_tool.EndStroke();
        const auto subdivide_after=subdivided.SerializeInline();
        check(subdivide_command && subdivide_before!=subdivide_after,"subdivide changes topology");
        if(subdivide_command)
        {
            subdivide_command->Undo(subdivided);
            check(subdivided.SerializeInline()==subdivide_before,"subdivide undo exact topology");
            subdivide_command->Redo(subdivided);
            check(subdivided.SerializeInline()==subdivide_after,"subdivide redo exact topology");
        }
        check(SceneCaptureCount()==capture_before,"subdivide Scene/Capture = 0");
        save_fixture("02_Subdivided",subdivided);

        // Same world-space density and brush size, increasingly large surrounding landscapes.
        for (int resolution : {73,225,709})
        {
            LandscapeData measured; measured.Initialize(resolution,resolution,1);
            auto hit=pick(measured,16,16);
            LandscapeData::SurfaceRegion query;
            measured.QuerySurface(hit.position,2,hit.face_index,query);
            const auto start=std::chrono::steady_clock::now();
            for(int i=0;i<100;++i) measured.QuerySurface(hit.position,2,hit.face_index,query);
            results<<"SurfaceQuery faces="<<measured.FaceCount()<<" candidates="<<query.faces.size()
                <<" mean_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100<<'\n';
            check(query.faces.size()<100,"small brush candidate count independent of total mesh");
            LandscapeEditorTool measured_tool;
            measured_tool.BeginStroke(measured,LandscapeBrushMode::Raise,brush);
            measured_tool.ApplyStrokeSample(hit,0.001f);
            const auto brush_start=std::chrono::steady_clock::now();
            for(int i=0;i<20;++i) measured_tool.ApplyStrokeSample(pick(measured,16,16),0.001f);
            results<<"Brush faces="<<measured.FaceCount()<<" mean_ms="
                <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-brush_start).count()/20<<'\n';
            measured_tool.EndStroke();
            brush.target_edge_length=0.1f;
            // Warm storage and adjacency before measuring consecutive local subdivisions.
            LandscapeEditorTool subdivision;
            subdivision.BeginStroke(measured,LandscapeBrushMode::Subdivide,brush);
            subdivision.ApplyStrokeSample(pick(measured,16,16),0.01f);
            const auto subdivide_start=std::chrono::steady_clock::now();
            for(int i=0;i<3;++i) subdivision.ApplyStrokeSample(pick(measured,16,16),0.01f);
            results<<"Subdivide total_faces="<<measured.FaceCount()<<" mean_ms="
                <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-subdivide_start).count()/3<<'\n';
            const auto partial=measured.Vertices();
            measured.RecalculateNormals();
            float normal_error=0;
            for(std::size_t i=0;i<partial.size();++i)
            {
                const auto a=partial[i].normal,b=measured.Vertices()[i].normal;
                normal_error=(std::max)(normal_error,std::fabs(a.x-b.x)+std::fabs(a.y-b.y)+std::fabs(a.z-b.z));
            }
            check(normal_error<1.0e-5f,"incremental subdivision normals match full rebuild");
            subdivision.EndStroke();
        }

        std::vector<float> reference;
        for(int fps:{120,60,30,15})
        {
            LandscapeData stroke; stroke.Initialize(129,33,0.25f);
            LandscapeEditorTool tool; brush.radius=1.5f; brush.strength=0.1f;
            tool.BeginStroke(stroke,LandscapeBrushMode::Raise,brush);
            // Prime at t=0 without depositing extra time.
            tool.ApplyStrokeSample(pick(stroke,4,4),1.0e-8f);
            for(int frame=1;frame<=fps;++frame)
                tool.ApplyStrokeSample(pick(stroke,4+20.0f*frame/fps,4),1.0f/fps);
            tool.EndStroke();
            std::vector<float> heights;
            for(int x=16;x<=96;++x) heights.push_back(stroke.HeightAt(x,16));
            if(reference.empty()) reference=heights;
            float difference=0,peak=0;
            for(std::size_t i=0;i<heights.size();++i)
            { difference=(std::max)(difference,std::fabs(heights[i]-reference[i])); peak=(std::max)(peak,reference[i]); }
            results<<"FPS "<<fps<<" max_relative_error "<<difference/(peak+1.0e-9f)<<'\n';
            check(difference/(peak+1.0e-9f)<0.25f,"stroke shape stable across frame rates");
        }

        Scene::Scene heavy("HeavyUndo");
        auto* ground=heavy.CreateGameObject("Landscape");
        auto* geometry=ground->AddComponent<Components::LandscapeComponent>();
        geometry->Data().Initialize(709,709,1); // 502681 vertices
        auto* selected=heavy.CreateGameObject("Model");
        selected->AddComponent<Components::PrimitiveMeshRendererComponent>();
        const auto selected_id=selected->ID();
        const auto ground_id=ground->ID();
        auto shared=geometry->Data().CaptureGeometry();
        SceneData undo_before,undo_after;
        const auto begin=std::chrono::steady_clock::now();
        CaptureScene(heavy,undo_before,SceneCaptureMode::Undo);
        CaptureScene(heavy,undo_after,SceneCaptureMode::Undo);
        results<<"Two warm Undo captures ms "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<'\n';
        check(undo_before.objects[0].components[0].landscape_geometry==shared &&
            undo_after.objects[0].components[0].landscape_geometry==shared &&
            !undo_before.objects[0].components[0].properties.Contains("mesh_data"),"502681 vertices shared without mesh text in Undo");
        Editor::EditorContext history;
        history.AttachScene(&heavy);
        history.BeginEdit("rename"); selected->SetName("Changed"); history.CommitEdit();
        check(history.Undo() && heavy.FindGameObjectByID(selected_id)->Name()=="Model","generic undo restores property");
        check(heavy.FindGameObjectByID(ground_id)->GetComponent<Components::LandscapeComponent>()->Data().CaptureGeometry()==shared,
            "generic undo preserves shared Landscape geometry");
        check(history.Redo() && heavy.FindGameObjectByID(selected_id)->Name()=="Changed","generic redo restores property");

        ImGuiContext* previous_context=ImGui::GetCurrentContext();
        ImGui::CreateContext();
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
        io.DisplaySize={1400,1200}; io.DeltaTime=1.0f/60;
        unsigned char* pixels; int font_width,font_height;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&font_width,&font_height);
        Editor::InspectorPanel inspector;
        auto* second=heavy.CreateGameObject("Second");
        second->AddComponent<Components::PrimitiveMeshRendererComponent>();
        history.Selection().Select(selected_id);
        const auto idle_captures=SceneCaptureCount();
        const auto inspector_start=std::chrono::steady_clock::now();
        for(int frame=0;frame<120;++frame)
        {
            if(frame==60) history.Selection().Select(second->ID(),true);
            io.MousePos={100,100};
            for(int button=0;button<3;++button) io.MouseDown[button]=(frame%8==button+1);
            io.MouseWheel=(frame%8==4)?1.0f:0;
            io.KeysDown['W']=frame%8==5; io.KeysDown['Q']=frame%8==6;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({600,1100});
            ImGui::Begin("Scene View");ImGui::InvisibleButton("Viewport",{580,1000});ImGui::End();
            ImGui::SetNextWindowPos({650,0});ImGui::SetNextWindowSize({700,1100});
            ImGui::Begin("Inspector");inspector.DrawContents(history);ImGui::End();
            ImGui::Render();
        }
        results<<"Inspector 120 frames ms "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-inspector_start).count()<<'\n';
        results<<"Idle/camera single+multi Scene/Capture "<<SceneCaptureCount()-idle_captures<<'\n';
        check(SceneCaptureCount()==idle_captures,"single/multi Inspector camera mouse+keys Scene/Capture = 0");
        // Exercise an actual bundled ImGui checkbox; the hook must observe the old value.
        for(bool& down:io.MouseDown) down=false;
        for(bool& down:io.KeysDown) down=false;
        io.MouseWheel=0;
        history.Selection().Select(selected_id);
        const auto* descriptor=Reflection::PropertyRegistry::Find(
            Components::PrimitiveMeshRendererComponent::StaticTypeID(),"cast_shadow");
        bool observed_before=false, widget_changed=false;
        ImVec2 widget_point{20,60};
        const auto widget_captures=SceneCaptureCount();
        for(int frame=0;frame<6;++frame)
        {
            io.MousePos=widget_point; io.MouseDown[0]=(frame==2);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({400,300});
            ImGui::Begin("Property edit test");
            auto* component=heavy.FindGameObjectByID(selected_id)->GetComponent<Components::PrimitiveMeshRendererComponent>();
            const bool changed=Editor::PropertyDrawer::Draw(*descriptor,*component,nullptr,&heavy,false,[&]()
            {
                observed_before=descriptor->Capture(*component).AsBool();
                history.BeginEdit("checkbox");
            });
            const auto low=ImGui::GetItemRectMin(), high=ImGui::GetItemRectMax();
            widget_point={(low.x+high.x)*0.5f,(low.y+high.y)*0.5f};
            if(changed) {widget_changed=true;history.CommitEdit();}
            ImGui::End();ImGui::Render();
        }
        check(widget_changed && observed_before && SceneCaptureCount()==widget_captures+2,
            "checkbox begins before setter and captures one Undo pair");
        check(history.Undo() && descriptor->Capture(*heavy.FindGameObjectByID(selected_id)->
            GetComponent<Components::PrimitiveMeshRendererComponent>()).AsBool(),"checkbox first change is undoable");
        LandscapeData tiny_wall;
        std::vector<LandscapeVertex> tiny_vertices(3);
        tiny_vertices[0].position={0,0,0}; tiny_vertices[1].position={0.0001f,0,0}; tiny_vertices[2].position={0,0.0001f,0};
        tiny_wall.InitializeMesh(std::move(tiny_vertices),{0,1,2});
        LandscapeRayHit tiny_hit;
        check(tiny_wall.Raycast({0.00002f,0.00002f,1},{0,0,-1},2,tiny_hit),"tiny vertical triangle remains pickable");
        check(std::fabs(tiny_wall.FaceNormal(0).z)>0.999f && std::fabs(tiny_wall.Vertices()[0].normal.z)>0.999f,
            "tiny vertical triangle retains non-Y normal");
        LandscapeData::SurfaceRegion tiny_region;
        tiny_wall.QuerySurface(tiny_hit.position,0.001f,tiny_hit.face_index,tiny_region);
        check(tiny_region.faces.size()==1 && tiny_region.vertices.size()==3,"tiny surface survives candidate query");
        // The user's reproduction: keep subdividing the exact same patch, then hover it.
        LandscapeData dense; dense.Initialize(17,17,1);
        LandscapeBrush fine; fine.radius=3;
        fine.target_edge_length=0.001f;
        for (int sample=0;sample<100;++sample)
        {
            const auto hit=pick(dense,8.1f,8.1f);
            LandscapeEditorTool::ApplySubdivideSample(dense,hit.position,hit.face_index,fine);
        }
        dense.FinishSculpt();
        const auto dense_hit=pick(dense,8.1f,8.1f);
        framework_landscape_editor_detail::TerrainRingCache preview;
        preview.Prepare(dense,dense_hit,3,4);
        check(preview.region.faces.size()>4096,"repeated subdivision creates dense preview patch");
        io.MouseDown[0]=false;
        ImGui::NewFrame();
        auto* draw=ImGui::GetForegroundDrawList();
        const auto& transform=heavy.FindGameObjectByID(ground_id)->GetTransform();
        const auto view=DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(8,20,-8,1),
            DirectX::XMVectorSet(8,0,8,1),DirectX::XMVectorSet(0,1,0,0));
        const auto projection=DirectX::XMMatrixPerspectiveFovLH(1,1280.0f/720,0.1f,1000);
        using namespace framework_landscape_editor_detail;
        DrawBrushFaceInfluence(draw,dense,transform,view,projection,1280,720,0,0,dense_hit.position,3,preview);
        DrawTerrainGridInBrush(draw,dense,transform,view,projection,1280,720,0,0,dense_hit.position,3,preview);
        const int before_ring=draw->VtxBuffer.Size;
        DrawTerrainRing(draw,dense,transform,view,projection,1280,720,0,0,dense_hit.position,3,IM_COL32_WHITE,2,preview,0);
        check(draw->VtxBuffer.Size>before_ring,"dense patch retains brush ring geometry");
        const auto queries=preview.projection_queries;
        preview.Prepare(dense,dense_hit,3,4);
        DrawTerrainRing(draw,dense,transform,view,projection,1280,720,0,0,dense_hit.position,3,IM_COL32_WHITE,2,preview,0);
        check(preview.projection_queries==queries,"idle preview reuses ring projections");
        ImGui::Render();
        const auto* preview_draw=ImGui::GetDrawData();
        const std::uint64_t preview_bytes=static_cast<std::uint64_t>(preview_draw->TotalVtxCount)*sizeof(ImDrawVert)+
            static_cast<std::uint64_t>(preview_draw->TotalIdxCount)*sizeof(ImDrawIdx);
        results<<"Dense preview faces "<<preview.region.faces.size()<<" upload_bytes "<<preview_bytes<<'\n';
        check(preview_bytes<2*1024*1024,"dense preview upload bounded below 2 MiB");
        ImGui::DestroyContext(); ImGui::SetCurrentContext(previous_context);
        for(int kind=0;kind<2;++kind)
        {
            const char* name=kind==0?"05_HeavySkinned":"06_HeavyMaterials";
            Scene::Scene model_scene(name);
            auto* model=model_scene.CreateGameObject("Stocking: 970 bones, 28 primitive slots");
            auto* renderer=model->AddComponent<Components::SkinnedMeshRendererComponent>();
            renderer->mesh_asset="45f0b5f4cb85c9288566693cf4916e0d";
            model_scene.CreateGameObject("Sun")->AddComponent<Components::DirectionalLightComponent>();
            SceneData saved;CaptureScene(model_scene,saved);
            std::string error;
            check(SceneSerializer::SaveToFile(saved,std::filesystem::path("Saved/Validation/LandscapeRegression")/
                (std::string(name)+".replayscene"),error),name);
        }
        results<<"OVERALL "<<(passed?"PASS":"FAIL")<<'\n';
        return passed;
    }
}
