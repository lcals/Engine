#include "control_fixture.hpp"
#include "plugins/runtime/script/script_system.hpp"
#include "resource_fixture.hpp"
#include "resource_services.hpp"
#include "runtime/app/app.hpp"
#include "runtime/log/log_system.hpp"
#include "runtime/resource/types/script_resource.hpp"
#include "runtime/schema/control_json_generated.hpp"

using namespace engine;
using namespace test_support;
namespace
{
bool run()
{
    ResourceServices services;
    camera::CameraTypeRegistry types;
    character::CharacterAbilityRegistry abilities;
    WorldSystem world(&services.loader);
    plugins::script::ScriptSystem script(&services.loader, &world);
    if (!services.initialize() || !camera::register_builtin_camera_types(types) ||
        !types.freeze() || !character::register_builtin_character_abilities(abilities) ||
        !abilities.freeze() || !world.initialize({}) ||
        !camera::register_camera_components(world, types) ||
        !character::register_character_components(world, abilities) || !script.initialize({}))
    {
        return false;
    }
    auto definition = camera_definition();
    schema::CameraParameterDataT parameter;
    parameter.name = "zoom";
    parameter.default_value = std::make_unique<schema::CameraParameterValueT>();
    parameter.default_value->name = "zoom";
    definition.parameters.push_back(std::move(parameter));
    schema::CameraTargetDataT target;
    target.name = "subject";
    definition.targets.push_back(target);
    std::string source = "import "
                         "{Gameplay,Scene,Camera,CharacterMotor,CharacterController,"
                         "CameraController,CharacterCamera} from 'engine'; const definition=";
    source += schema::control_json::write(definition, true).dump();
    source += R"js(;
function check(value,message){if(!value)throw Error(message);}
export default class extends Gameplay {
    OnInitialize(){
        this.scene=Scene.Create('Control bindings');
        this.player=this.scene.CreateNode('Player');
        this.motor=this.player.AddComponent(CharacterMotor);
        this.controller=this.player.AddComponent(CharacterController);
        const c=this.controller, settings=c.GetSettings();
        check(typeof settings.cameraNodeId==='bigint' && settings.movement.maxSpeed===4,
              'generated schema shapes and uint64 identity');
        settings.movement.maxSpeed=6;
        check(c.SetSettings(settings) && c.GetSettings().movement.maxSpeed===6,'settings roundtrip');
        check(!c.SetSettings({...settings,moveSpeed:7}) && !c.SetInput(1,0,0,0,false),
              'removed fields and positional input must be rejected');
        check(c.SetInput({move:[1,1],lookDelta:[0,0],lookRate:[0,0],toggleView:false,actions:{}}),
              'current object input');
        check(!c.SetInput({move:[NaN,0],lookDelta:[0,0],lookRate:[0,0],toggleView:false,actions:{}}),
              'nonfinite input rejected');
        check(c.GetStatus().inputSequence===0n && !this.motor.GetStatus().hasSimulated,
              'runtime snapshots');
        const node=this.scene.CreateNode('View');node.AddComponent(Camera);
        this.camera=node.AddComponent(CameraController);
        definition.moduleData.fixed[0].id=18446744073709551615n;
        definition.rigs[0].modules[1].id=18446744073709551615n;
        check(this.camera.SetSettings(definition),'camera complete schema');
        const copy=this.camera.GetSettings();
        check(copy.moduleData.fixed[0].id===18446744073709551615n && copy.machine.conditionData===null &&
              this.camera.SetSettings(copy),'BigInt and nullable generated fields roundtrip');
        check(this.camera.SetParameters({zoom:2}) && !this.camera.SetParameters({zoom:4,missing:1}) &&
              this.camera.GetParameter('zoom')===2,'atomic parameter writes');
        check(this.camera.SetTarget('subject',this.player) &&
              this.camera.GetTarget('subject').id===this.player.id &&
              !this.camera.SetTarget('subject',node),'stable targets and cycle rejection');
        this.adapter=node.AddComponent(CharacterCamera);
        check(this.adapter.GetAngles()===null && !this.adapter.ApplyLook(null,[0,0],.1),
              'unconfigured adapter and invalid vectors reject safely');
        return true;
    }
    Update(){
        check(!this.controller.IsValid() && this.controller.GetStatus()===null &&
              this.controller.GetSettings()===null && !this.controller.ClearInput(),
              'replacement must not revive old controller handle');
        const current=this.player.GetComponent(CharacterController);
        check(current.IsValid() && current.GetSettings().movement.maxSpeed===4,
              'new component has independent defaults');
        check(this.scene.Destroy() && this.camera.GetStatus()===null &&
              this.motor.GetStatus()===null && !this.adapter.ToggleView(), 'scene lifetime');
    }
}
)js";
    schema::AssetEntryT info;
    info.guid = "f0000000-0000-4000-8000-000000000001";
    info.path = info.module_path = "scripts/control.js";
    info.kind = schema::AssetKind::script;
    const auto* bytes = reinterpret_cast<const std::byte*>(source.data());
    std::unique_ptr<Resource> resource =
        std::make_unique<ScriptResource>(std::move(info), ByteBuffer(bytes, bytes + source.size()));
    ResourceHandle handle;
    if (!services.loader.load_memory(resource, {}, handle))
    {
        return check(false, services.loader.last_error().data());
    }
    ResourceReference root;
    if (!root.adopt(&services.loader, handle))
    {
        return false;
    }
    if (!script.prepare_gameplay(root.handle()) || !script.start_prepared_gameplay())
    {
        return check(false, script.status().last_error.c_str());
    }
    auto player = world.scene()->find_node("Player");
    if (!player.remove_component(character::CharacterController::component_type) ||
        !player.add_component<character::CharacterController>() || !script.tick(.016) ||
        !script.status().running)
    {
        return check(false, script.status().last_error.c_str());
    }
    script.stop_gameplay();
    return true;
}
} // namespace
int main()
{
    ResourceFixture files;
    AppConfig app;
    app.application_name = "engine-control-script-test";
    app.assets_path = files.root / "assets";
    app.persistent_data_path = files.root / "persistent";
    app.cache_path = files.root / "cache";
    if (files.root.empty() || !App::initialize(app) || !LogSystem::instance().initialize({}))
    {
        return 1;
    }
    const bool passed = run();
    LogSystem::instance().shutdown();
    App::shutdown();
    return passed ? 0 : 1;
}
