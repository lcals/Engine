import { Resources, ResourceHandle, Animator, CharacterMotor, Debug, Gameplay, MeshRenderer, Scene, Transform, Time } from "engine";
import { CharacterControls } from "../character_controls.js";
import { locomotionMachine, locomotionActions } from "../animation/locomotion_machine.js";

// 源 walk 采样的足底最低约 -0.0372 m；仅补偿样例模型的放置高度，不执行足部 IK。
const avatar_ground_offset = 0.05;

export default class Game extends Gameplay {
    private readonly resources: ResourceHandle[] = [];
    scene: Scene | null = null;
    private readonly input = new CharacterControls();
    private animator: Animator | null = null;
    private motor: CharacterMotor | null = null;
    private readonly peers: Animator[] = [];
    private elapsed = 0;


    override OnInitialize(): boolean {
        for (const path of ["scenes/playground.scene", "prefabs/characters/player.prefab", "materials/character.material", "models/locomotions.fbx/animations/walk", "models/locomotions.fbx/animations/run"]) {
            const resource = Resources.Load(path);
            if (!resource) { Debug.LogError(`Cannot load ${path}: ${Resources.lastError}`); return false; }
            this.resources.push(resource);
        }

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
            !animator.SetStateMachine(locomotionMachine, locomotionActions)
        )
            return false;
        this.animator = animator;
        this.motor = player.GetComponent(CharacterMotor);
        if (!this.motor) return false;
        // Independent parameters and phases on eight actors using the same definition.
        for (let index = 0; index < 7; ++index) {
            const peer = scene.InstantiatePrefab("prefabs/characters/player.prefab", `Peer ${index}`);
            const pose = peer?.GetComponent(Transform);
            const mesh = peer?.FindChild("root/GENERIC MAN new");
            const peerAnimator = mesh?.AddComponent(Animator);
            // Keep every peer in front of the initial camera and leave the player's sightline clear.
            const x = (index < 3 ? index - 3 : index - 2) * 1.5;
            if (!pose || !peerAnimator || !pose.SetPosition(x, avatar_ground_offset, -3.5) ||
                !peerAnimator.SetStateMachine(locomotionMachine, locomotionActions)) return false;
            this.peers.push(peerAnimator);
        }
        Debug.Log(
            "Animation playground ready: WASD move, Shift sprint, Ctrl crouch, Space jump, right mouse look, Tab camera.",
        );
        return this.input.Bind(scene);
    }

    override Update(): void {
        this.input.Update();
        const velocity = this.motor?.GetVelocity();
        const speed = velocity ? Math.hypot(velocity.x, velocity.z) : 0;
        if (!this.animator?.SetParameters({speed})) throw new Error("Cannot submit player speed");
        this.elapsed += Time.deltaTime;
        for (let index = 0; index < this.peers.length; ++index) {
            const phase = (this.elapsed + index * 0.8) % 9;
            const peerSpeed = phase < 3 ? 0 : phase < 6 ? 1.2 + index * .1 : 4;
            if (!this.peers[index].SetParameters({speed: peerSpeed}))
                throw new Error("Cannot submit peer locomotion parameters");
        }
    }

    override OnShutdown(): void {
        for (const resource of this.resources.splice(0)) {
            if (!Resources.Release(resource)) Debug.LogError("Cannot release showcase resource");
        }

        this.input.Shutdown();
        this.animator?.ClearStateMachine();
        for (const peer of this.peers) peer.ClearStateMachine();
        this.peers.length = 0;
        this.animator = null;
        this.motor = null;
        this.elapsed = 0;
        this.scene = null;
    }
}
