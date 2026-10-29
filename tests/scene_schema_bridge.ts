import * as flatbuffers from "flatbuffers";
import {
    CameraDataT,
    ComponentDataT,
    ComponentPayload,
    FloatValueT,
    LightDataT,
    MaterialValue,
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
expect(mesh instanceof MeshRendererDataT && !meshComponent.enabled &&
    mesh.meshGuid === "00000000-0000-4000-8000-000000000005" &&
    mesh.materialGuid === "00000000-0000-4000-8000-000000000003",
    "read external resource GUID references without embedding material definitions");

scene.info.name = "TypeScript 场景";
transform.position.x = 42.5;
cameraData.fovDegrees = 55;
lightData.intensity = 7.5;
meshComponent.enabled = true;
mesh.materialGuid = "00000000-0000-4000-8000-000000000004";

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
