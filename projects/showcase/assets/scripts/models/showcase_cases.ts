import type { Gameplay, Scene } from "engine";
import Minimal from "../modules/minimal.js";
import Apartment from "../modules/apartment.js";
import Courtyard from "../modules/courtyard.js";
import FontGallery from "../modules/font-gallery.js";
import Effects from "../modules/effects.js";
import Terrain from "../modules/terrain.js";
import Destruction from "../modules/destruction.js";
import DestructionIntegration from "../modules/destruction-integration.js";
import ShootingRange from "../modules/shooting-range.js";
import NavigationLab from "../modules/navigation.js";
import PhysicsInteractions from "../modules/physics-interactions.js";
import PersistentUi from "../modules/persistent-ui.js";
import EcsManyObjects from "../modules/ecs-many-objects.js";

export type ShowcaseGame = Gameplay & { scene: Scene | null };

export interface ShowcaseCase {
    readonly id: string;
    readonly button: string;
    readonly title: string;
    readonly goal: string;
    readonly scene: string;
    readonly game: new () => ShowcaseGame;
}

// 项目菜单和测试共用这一份功能用例；场景资源、按钮身份与 GUID 保持不变。
export const showcase_cases: readonly ShowcaseCase[] = [
    {
        id: "pbr-material", button: "scene-cube", title: "PBR material and transform",
        goal: "Observe direct lighting and a rotating mesh with a shared PBR material.",
        scene: "scenes/main.scene", game: Minimal,
    },
    {
        id: "authored-scene", button: "scene-apartment", title: "Authored scene and local lights",
        goal: "Reload saved geometry, materials, lights and cameras in the apartment.",
        scene: "scenes/apartment.scene", game: Apartment,
    },
    {
        id: "character-animation", button: "scene-courtyard", title: "Character, animation and camera",
        goal: "Move and jump; switch first/third person while preserving skinning and shadows.",
        scene: "scenes/playground.scene", game: Courtyard,
    },
    {
        id: "vector-text", button: "scene-gallery", title: "3D vector text",
        goal: "Inspect scene text using the same font resources as UI.",
        scene: "scenes/font-gallery.scene", game: FontGallery,
    },
    {
        id: "effects-lifecycle", button: "scene-effects", title: "Particle and model effects",
        goal: "Play, pause and stop independent sprite and model effect instances.",
        scene: "scenes/effects.scene", game: Effects,
    },
    {
        id: "terrain-editing", button: "scene-terrain", title: "Terrain editing and restoration",
        goal: "Dig and fill across chunks; save, restore and walk on the changed collision.",
        scene: "scenes/terrain.scene", game: Terrain,
    },
    {
        id: "fracture-graph", button: "scene-destruction", title: "Fracture bonds and support",
        goal: "Damage supported structures, inspect detached bodies and reset the graph.",
        scene: "scenes/destruction.scene", game: Destruction,
    },
    {
        id: "destruction-integration", button: "scene-integration", title: "Destruction system integration",
        goal: "Link projectile hits, foundations, debris, effects, sound and navigation updates.",
        scene: "scenes/destruction-integration.scene", game: DestructionIntegration,
    },
    {
        id: "hit-queries", button: "scene-shooting", title: "Ray and sphere hit queries",
        goal: "Compare hits, wall obstruction and stale identities; reset target feedback.",
        scene: "scenes/shooting-range.scene", game: ShootingRange,
    },
    {
        id: "navigation-terrain", button: "scene-navigation", title: "Crowd and terrain navigation",
        goal: "Change goals, pause groups, cut routes and restore them with local tile updates.",
        scene: "scenes/navigation.scene", game: NavigationLab,
    },
    {
        id: "physics-interactions", button: "scene-interactions", title: "Physics queries and contact events",
        goal: "Query motors and agents; inspect sphere/box overlaps and enter/stay/exit events.",
        scene: "scenes/physics-interactions.scene", game: PhysicsInteractions,
    },
    {
        id: "persistent-ui", button: "scene-persistent-ui", title: "UI and scene lifetime",
        goal: "Keep text and subscriptions through loading, cancellation, failure and unload.",
        scene: "scenes/persistent-ui.scene", game: PersistentUi,
    },
    {
        id: "ecs-many-objects", button: "scene-ecs", title: "ECS many objects",
        goal: "Create 1,000 / 10,000 / 50,000 real entities; check hierarchy, visibility and stale handles.",
        scene: "scenes/ecs-many-objects.scene", game: EcsManyObjects,
    },
];
