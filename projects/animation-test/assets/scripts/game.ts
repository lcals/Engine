import { Animator, Debug, Gameplay, MeshRenderer, Scene, Transform } from "engine";

export default class Game extends Gameplay {
    override OnInitialize(): boolean {
        const scene = Scene.Load("project/scenes/playground.scene");
        if (!scene) return false;
        const avatar = scene.InstantiateModel("project/models/skeleton.fbx", "Avatar");
        const player = scene.FindNode("Player");
        const body = scene.FindNode("GENERIC MAN new");
        if (!avatar || !player || !body || !avatar.SetParent(player)) return false;
        if (!avatar.GetComponent(Transform)?.SetRotation(0, 180, 0)) return false;
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
}
