import {
    Animator, Camera, CharacterMotor, Debug, Gameplay, MeshRenderer,
    Scene, TerrainComponent, Transform, UiComponent,
} from "engine";
import { TerrainController } from "../controllers/terrain_controller.js";
import { TerrainAction, TerrainModel } from "../models/terrain_model.js";
import { CharacterControls } from "../character_controls.js";

const save_slot = "showcase-terrain";
const camera_nodes = { overview: "Overview camera", cave: "Cave camera", walk: "Player camera" };

export default class Game extends Gameplay {
    scene: Scene | null = null;
    private terrain: TerrainComponent | null = null;
    private motor: CharacterMotor | null = null;
    private animator: Animator | null = null;
    private controller: TerrainController | null = null;
    private readonly model = new TerrainModel();
    private readonly input = new CharacterControls();
    private player_started = false;
    private tunnel_step = 0;
    private blast_radius = 4.0;
    private animation_speed = 0;

    override OnInitialize(): boolean {
        this.scene = Scene.Load("scenes/terrain.scene");
        const scene = this.scene;
        if (!scene) return false;
        this.terrain = scene.FindNode("Terrain")?.GetComponent(TerrainComponent) ?? null;
        const player = scene.FindNode("Player");
        this.motor = player?.GetComponent(CharacterMotor) ?? null;
        const view = scene.FindNode("Terrain UI")?.GetComponent(UiComponent);
        if (!this.terrain || !player || !this.motor || !view || !this.motor.SetEnabled(false))
            return false;

        // Reuse the open showcase character; terrain contains no purchased project assets.
        const avatar = scene.InstantiateModel("models/skeleton.fbx", "Terrain avatar");
        const body = scene.FindNode("GENERIC MAN new");
        const transform = avatar?.GetComponent(Transform);
        const renderer = body?.GetComponent(MeshRenderer);
        this.animator = body?.AddComponent(Animator) ?? null;
        if (!avatar || !transform || !renderer || !this.animator || !avatar.SetParent(player) ||
            !transform.SetPosition(0, 0.05, 0) || !transform.SetRotation(0, 180, 0) ||
            !renderer.material.SetType("materials/character.material") ||
            !this.animator.SetAnimation("models/locomotions.fbx/animations/walk") ||
            !this.animator.SetSpeed(0) || !this.animator.Play()) return false;

        this.controller = new TerrainController(view, this.model);
        if (!this.controller.Bind()) return false;
        Debug.Log("Terrain laboratory: fixed brush actions cross chunk seams; WASD, Space and right mouse explore the result.");
        return this.input.Bind(scene);
    }

    override Update(): void {
        this.input.Update();
        const terrain = this.terrain;
        if (!terrain || !terrain.IsValid()) return;
        let changed = false;
        if (this.model.ready !== terrain.ready) {
            this.model.ready = terrain.ready;
            changed = true;
        }
        if (terrain.ready && !this.player_started) {
            if (!this.motor?.SetEnabled(true)) throw new Error("Cannot start the terrain character");
            this.player_started = true;
            this.model.status = "Ready. Dig extends the cave across z = 0 and z = -16 chunk seams.";
            changed = true;
        }
        const request = this.model.pending;
        if (request !== null) {
            this.model.pending = null;
            if (!terrain.ready || !this.perform(request)) {
                this.model.status = terrain.lastError || `Could not complete ${request}. Check the scene and try again.`;
                Debug.LogWarning(this.model.status);
            }
            changed = true;
        }
        if (!terrain.ready && terrain.lastError && this.model.status !== terrain.lastError) {
            this.model.status = terrain.lastError;
            changed = true;
        }
        const statistics = `Revision ${terrain.revision} / ${terrain.chunkCount} chunks / ${terrain.triangleCount} triangles / ${terrain.rebuiltChunks} rebuilt`;
        if (this.model.statistics !== statistics) {
            this.model.statistics = statistics;
            changed = true;
        }
        const velocity = this.motor?.GetVelocity();
        const speed = velocity ? Math.min(2, Math.hypot(velocity.x, velocity.z) / 3) : 0;
        if (Math.abs(speed - this.animation_speed) > 0.02) {
            if (!this.animator?.SetSpeed(speed)) throw new Error("Cannot update the terrain character animation");
            this.animation_speed = speed;
        }
        if (changed && !this.controller?.Refresh()) throw new Error("Cannot refresh the terrain view");
    }

    private perform(action: TerrainAction): boolean {
        const terrain = this.terrain;
        if (!terrain) return false;
        switch (action) {
            case "dig": {
                const z = -3 * this.tunnel_step;
                if (!terrain.DigSphere(-4, -5.5, z, 3.4)) return false;
                this.tunnel_step = (this.tunnel_step + 1) % 8;
                this.model.status = `Tunnel extended at (-4, -5.5, ${z}). Use Cave view to inspect the new walls.`;
                return true;
            }
            case "blast": {
                if (!terrain.DigSphere(12, 0, 0, this.blast_radius)) return false;
                this.model.status = `Crater cut at (12, 0, 0), radius ${this.blast_radius.toFixed(1)} m. Geometry and collision rebuild together.`;
                this.blast_radius = Math.min(9, this.blast_radius + 1);
                return true;
            }
            case "fill":
                if (!terrain.FillSphere(-4, -3, 10, 3.8)) return false;
                this.model.status = "Entrance filled at (-4, -3, 10). Reset restores the original cave.";
                return true;
            case "save":
                if (!terrain.Save(save_slot)) return false;
                this.model.status = "Terrain saved on this device. Changes survive returning to the menu or restarting.";
                return true;
            case "load":
                if (!terrain.Load(save_slot)) return false;
                this.tunnel_step = 0;
                this.blast_radius = 4;
                if (!this.respawn()) return false;
                this.model.status = "Saved terrain restored. The character has returned to the safe start.";
                return true;
            case "reset":
                if (!terrain.Reset()) return false;
                this.tunnel_step = 0;
                this.blast_radius = 4;
                if (!this.respawn()) return false;
                this.model.status = "Original hills and cave restored. The saved slot was not changed.";
                return true;
            case "respawn":
                if (!this.respawn()) return false;
                this.model.status = "Character returned to the safe start at (20, 8, 20).";
                return true;
            case "overview":
            case "cave":
            case "walk": {
                const camera = this.scene?.FindNode(camera_nodes[action])?.GetComponent(Camera);
                if (!camera || !this.scene?.SetActiveCamera(camera)) return false;
                this.model.camera = action;
                this.model.status = action === "walk"
                    ? "WASD move / Space jump / right mouse look / Tab first or third person. Keyboard and mouse required for walking."
                    : action === "cave"
                        ? "Fixed cave view: inspect the ceiling, walls and cross-chunk tunnel."
                        : "Fixed overview: the cyan markers identify the z = 0 chunk boundary.";
                return true;
            }
        }
    }

    private respawn(): boolean {
        const transform = this.scene?.FindNode("Player")?.GetComponent(Transform);
        return !!transform && !!this.motor && this.motor.ClearInput() &&
            transform.SetPosition(20, 8, 20) && transform.SetRotation(0, 0, 0);
    }

    override OnShutdown(): void {
        this.input.Shutdown();
        this.controller?.Unbind();
        this.controller = null;
        this.animator = null;
        this.motor = null;
        this.terrain = null;
        this.scene = null;
    }
}
