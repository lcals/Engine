import { Animator, Debug, Gameplay, MeshRenderer, Scene, Transform } from "engine";
import { CharacterControls } from "../character_controls.js";

// 源 walk 采样的足底最低约 -0.0372 m；仅补偿样例模型的放置高度，不执行足部 IK。
const avatar_ground_offset = 0.05;

export default class Game extends Gameplay {
    scene: Scene | null = null;
    private readonly input = new CharacterControls();

    override OnInitialize(): boolean {
        this.scene = Scene.Load("scenes/playground.scene");
        const scene = this.scene;
        if (!scene) return false;
        const avatar = scene.InstantiatePrefab("prefabs/characters/player.prefab", "Avatar");
        const player = scene.FindNode("Player");
        const body = scene.FindNode("GENERIC MAN new");
        if (!avatar || !player || !body || !avatar.SetParent(player)) return false;
        const avatar_transform = avatar.GetComponent(Transform);
        if (
            !avatar_transform ||
            !avatar_transform.SetPosition(0, avatar_ground_offset, 0) ||
            !avatar_transform.SetRotation(0, 180, 0)
        )
            return false;
        const renderer = body.GetComponent(MeshRenderer);
        const animator = body.AddComponent(Animator);
        if (
            !renderer ||
            !animator ||
            !renderer.material.SetType("materials/character.material") ||
            !animator.SetAnimation("models/locomotions.fbx/animations/walk") ||
            !animator.Play()
        )
            return false;
        Debug.Log(
            "Animation playground ready: WASD move, Space jump, right mouse look, Tab camera.",
        );
        return this.input.Bind(scene);
    }

    override Update(): void { this.input.Update(); }

    override OnShutdown(): void {
        this.input.Shutdown();
        this.scene = null;
    }
}
