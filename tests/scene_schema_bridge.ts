import * as flatbuffers from "flatbuffers";
import {
    CameraDataT,
    ComponentDataT,
    ComponentPayload,
    FloatValueT,
    LightDataT,
    MaterialValue,
    MeshKind,
    MeshRendererDataT,
    NodeDataT,
    SceneData,
    TransformDataT,
    Vec2ValueT,
    Vec3ValueT,
    Vec4ValueT,
} from "./engine/schema";

declare function require(name: string): {
    readFileSync(path: string): Uint8Array;
    writeFileSync(path: string, bytes: Uint8Array): void;
};
declare const process: { argv: string[] };
const fs = require("node:fs");

function expect(condition: unknown, message: string): asserts condition {
    if (!condition) {
        throw new Error(message);
    }
}

const buffer = new flatbuffers.ByteBuffer(new Uint8Array(fs.readFileSync(process.argv[2])));
expect(SceneData.bufferHasIdentifier(buffer), "C++ buffer has ESCN identifier");
const scene = SceneData.getRootAsSceneData(buffer).unpack();
expect(scene.info && scene.info.name === "Shared scene model", "read C++ scene metadata");
expect(scene.nodes.length === 4, "read all four runtime-created nodes");

function node(name: string): NodeDataT {
    const found = scene.nodes.find((value) => value.info?.name === name);
    expect(found && found.info, `find node ${name}`);
    return found;
}

function component(value: NodeDataT, type: ComponentPayload): ComponentDataT {
    const found = value.components.find((item) => item.payloadType === type);
    expect(found && found.payload, `find generated union ${ComponentPayload[type]}`);
    return found;
}

const root = node("Root");
const camera = node("Camera");
const light = node("Light");
const cube = node("Cube");
expect(root.info && camera.info && light.info && cube.info, "node metadata is present");
const originalIds = scene.nodes.map((value) => {
    expect(value.info, "each node has metadata");
    expect(typeof value.info.id === "bigint" && value.info.id > 9007199254740991n,
        "ulong identity uses bigint beyond JavaScript Number precision");
    return { id: value.info.id, parent: value.info.parentId };
});
expect(root.info.parentId === 0n, "root has no parent");
expect(camera.info.parentId === root.info.id && light.info.parentId === root.info.id &&
    cube.info.parentId === root.info.id, "read parent relationships without narrowing integers");
expect(scene.info.activeCameraNode === camera.info.id, "read active camera bigint reference");

const rootTransform = component(root, ComponentPayload.TransformData).payload;
const transform = component(cube, ComponentPayload.TransformData).payload;
const cameraData = component(camera, ComponentPayload.CameraData).payload;
const lightData = component(light, ComponentPayload.LightData).payload;
const meshComponent = component(cube, ComponentPayload.MeshRendererData);
const mesh = meshComponent.payload;
expect(rootTransform instanceof TransformDataT && rootTransform.scale &&
    rootTransform.scale.x === 1 && rootTransform.scale.y === 1 && rootTransform.scale.z === 1,
    "read C++ native default scale through schema");
expect(transform instanceof TransformDataT && transform.position && transform.rotation &&
    transform.scale, "unpack generated transform object");
expect(transform.position.x === 1.5 && transform.position.y === 2.5 &&
    transform.position.z === -3.5 && transform.rotation.z === 30 &&
    transform.scale.x === 2 && transform.scale.y === 3 && transform.scale.z === 4,
    "read vector and transform values from C++");
expect(cameraData instanceof CameraDataT && cameraData.fovDegrees === 75 &&
    Math.abs(cameraData.nearClip - 0.2) < 1e-6 && cameraData.farClip === 200,
    "read generated camera values");
expect(lightData instanceof LightDataT && lightData.color &&
    lightData.intensity === 4.5 && lightData.color.g === 0.5, "read light union payload");
expect(mesh instanceof MeshRendererDataT && mesh.material &&
    mesh.mesh === MeshKind.cube && !meshComponent.enabled &&
    mesh.material.typePath === "project/materials/tests/typed" &&
    mesh.material.shaderPath === "project/shaders/tests/typed" && mesh.material.parameters.length === 5,
    "read typed C++ snapshot material and component state");
const baseColor = mesh.material.parameters.find((value) => value.name === "baseColor");
const metallic = mesh.material.parameters.find((value) => value.name === "metallic");
const roughness = mesh.material.parameters.find((value) => value.name === "roughness");
const position = mesh.material.parameters.find((value) => value.name === "pos");
const foo = mesh.material.parameters.find((value) => value.name === "foo");
expect(baseColor && baseColor.valueType === MaterialValue.Vec4Value &&
    baseColor.value instanceof Vec4ValueT && baseColor.value.value &&
    baseColor.value.value.x === 0.75 && baseColor.value.value.y === 0.25 &&
    baseColor.value.value.z === 0.125 && baseColor.value.value.w === 1,
    "read named Vec4 material parameter through generated union");
expect(metallic && metallic.valueType === MaterialValue.FloatValue &&
    metallic.value instanceof FloatValueT && metallic.value.value === 0.625,
    "read named float parameter from independent runtime snapshot");
expect(roughness && roughness.valueType === MaterialValue.FloatValue &&
    roughness.value instanceof FloatValueT && roughness.value.value === 0.25,
    "read named roughness parameter");

expect(position && position.valueType === MaterialValue.Vec2Value &&
    position.value instanceof Vec2ValueT && position.value.value &&
    position.value.value.x === 1.25 && position.value.value.y === 2.5,
    "read custom shader Vec2 parameter through generated union");
expect(foo && foo.valueType === MaterialValue.Vec3Value &&
    foo.value instanceof Vec3ValueT && foo.value.value &&
    foo.value.value.x === 3 && foo.value.value.y === 4 && foo.value.value.z === 5,
    "read custom shader Vec3 parameter through generated union");

scene.info.name = "TypeScript 场景";
transform.position.x = 42.5;
cameraData.fovDegrees = 55;
lightData.intensity = 7.5;
meshComponent.enabled = true;
metallic.value.value = 0.125;
roughness.value.value = 0.75;
baseColor.value.value.y = 0.5;
position.value.value.x = 11.5;
position.value.value.y = 12.5;
foo.value.value.x = 21;
foo.value.value.y = 22;
foo.value.value.z = 23;

const builder = new flatbuffers.Builder();
SceneData.finishSceneDataBuffer(builder, scene.pack(builder));
const changed = builder.asUint8Array();
const reread = SceneData.getRootAsSceneData(new flatbuffers.ByteBuffer(changed)).unpack();
expect(reread.info?.name === "TypeScript 场景", "generated pack preserves UTF8 edits");
reread.nodes.forEach((value, index) => {
    expect(value.info && value.info.id === originalIds[index].id &&
        value.info.parentId === originalIds[index].parent,
        "generated TypeScript pack preserves bigint identities exactly");
});
fs.writeFileSync(process.argv[3], changed);
