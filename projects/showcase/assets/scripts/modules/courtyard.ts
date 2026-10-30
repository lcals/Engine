import { Animator, Debug, Gameplay, MeshRenderer, Render, Scene, Transform } from "engine";

// 源 walk 采样的足底最低约 -0.0372 m；仅补偿样例模型的放置高度，不执行足部 IK。
const avatar_ground_offset = 0.05;

export default class Game extends Gameplay {
    scene: Scene | null = null;

    override OnInitialize(): boolean {
        if (
            !Render.SetAmbient(0.82, 0.87, 1.0, 0.28) ||
            !Render.SetExposure(1.05) ||
            !Render.SetFilmicTonemap(true) ||
            !Render.SetDirectionalShadows(true, 4096, 0.0015) ||
            !Render.SetClearColor(0x668ca6ff)
        )
            return false;
        this.scene = Scene.Load("project/scenes/playground.scene");
        const scene = this.scene;
        if (!scene) return false;
        const avatar = scene.InstantiateModel("project/models/skeleton.fbx", "Avatar");
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
            !renderer.material.SetType("project/materials/character.material") ||
            !animator.SetAnimation("project/models/locomotions.fbx/animations/walk") ||
            !animator.Play()
        )
            return false;
        Debug.Log(
            "Animation playground ready: WASD move, Space jump, right mouse look, Tab camera.",
        );
        return true;
    }

    override OnShutdown(): void {
        this.scene = null;
    }
}
