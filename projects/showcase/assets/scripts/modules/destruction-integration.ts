import {
    Animator, AudioListener, AudioSource, Camera, CharacterMotor, Collider, Debug,
    DestructionComponent, EffectComponent, Gameplay, MeshRenderer, Physics,
    PhysicsImpact, Scene, TerrainComponent, Time, Transform, UiComponent,
} from "engine";
import { DestructionIntegrationController } from "../controllers/destruction_integration_controller.js";
import { DestructionIntegrationModel, IntegrationAction } from "../models/destruction_integration_model.js";

interface Specimen {
    readonly name: string;
    readonly component: DestructionComponent;
    next_impact: number;
}
interface Emitter {
    readonly effect: EffectComponent;
    readonly audio: AudioSource;
    readonly transform: Transform;
    available_at: number;
}
interface DamageRequest {
    readonly specimen: Specimen;
    readonly impact: PhysicsImpact;
}

const camera_names = { overview: "Overview camera", front: "Front camera", walk: "Player camera" };
const max_secondary_damage = 24;

export default class Game extends Gameplay {
    scene: Scene | null = null;
    private terrain: TerrainComponent | null = null;
    private motor: CharacterMotor | null = null;
    private animator: Animator | null = null;
    private controller: DestructionIntegrationController | null = null;
    private readonly model = new DestructionIntegrationModel();
    private readonly specimens: Specimen[] = [];
    private readonly dust: Emitter[] = [];
    private readonly bursts: Emitter[] = [];
    private started = false;
    private clock = 0;
    private animation_speed = 0;
    private last_frame = -1n;
    private impact_contacts = 0;
    private secondary_damage = 0;
    private support_checks = 0;

    override OnInitialize(): boolean {
        this.scene = Scene.Load("project/scenes/destruction-integration.scene");
        const scene = this.scene;
        if (!scene) return false;
        this.terrain = scene.FindNode("Terrain")?.GetComponent(TerrainComponent) ?? null;
        const player = scene.FindNode("Player");
        this.motor = player?.GetComponent(CharacterMotor) ?? null;
        const view = scene.FindNode("Integration UI")?.GetComponent(UiComponent);
        if (!this.terrain || !player || !this.motor || !view || !this.motor.SetEnabled(false))
            return false;
        for (const name of ["Tower", "Wall", "Column"]) {
            const component = scene.FindNode(name)?.GetComponent(DestructionComponent);
            if (!component) return false;
            this.specimens.push({ name, component, next_impact: 0 });
        }
        for (const [prefix, count, pool] of [
            ["Impact", 4, this.dust], ["Blast", 2, this.bursts],
        ] as const) {
            for (let index = 1; index <= count; ++index) {
                const node = scene.FindNode(`${prefix} emitter ${index}`);
                const effect = node?.GetComponent(EffectComponent);
                const audio = node?.GetComponent(AudioSource);
                const transform = node?.GetComponent(Transform);
                if (!effect || !audio || !transform ||
                    !effect.SetPlayOnAwake(false) || !effect.SetLoop(false) ||
                    !audio.SetPlayOnAwake(false) || !audio.SetLoop(false)) return false;
                pool.push({ effect, audio, transform, available_at: 0 });
            }
        }
        const avatar = scene.InstantiateModel("project/models/skeleton.fbx", "Integration avatar");
        const body = scene.FindNode("GENERIC MAN new");
        const transform = avatar?.GetComponent(Transform);
        const renderer = body?.GetComponent(MeshRenderer);
        this.animator = body?.AddComponent(Animator) ?? null;
        if (!avatar || !transform || !renderer || !this.animator || !avatar.SetParent(player) ||
            !transform.SetPosition(0, 0.05, 0) || !transform.SetRotation(0, 180, 0) ||
            !renderer.material.SetType("project/materials/character.material") ||
            !this.animator.SetAnimation("project/models/locomotions.fbx/animations/walk") ||
            !this.animator.SetSpeed(0) || !this.animator.Play()) return false;
        this.controller = new DestructionIntegrationController(view, this.model);
        if (!this.controller.Bind()) return false;
        Debug.Log("Combined destruction preparing. Use the buttons to excavate, blast and reset.");
        return true;
    }

    override Update(): void {
        this.clock += Math.max(0, Time.deltaTime);
        const terrain = this.terrain;
        if (!terrain?.IsValid()) return;
        const ready = terrain.ready && this.specimens.every(value =>
            value.component.IsValid() && value.component.ready);
        let changed = this.model.ready !== ready;
        this.model.ready = ready;
        if (ready && !this.started) {
            if (!this.refresh_support() || !this.motor?.SetEnabled(true))
                throw new Error("Cannot start the integration scene support and character");
            this.started = true;
            this.model.status = "Ready. Dig foundation removes only soil; missing support is checked against the real ground collision.";
            changed = true;
        }
        // Resolve ownership before a UI action or damage can replace generated fragment nodes.
        const impacts = this.collect_impacts();
        const action = this.model.pending;
        if (action !== null) {
            this.model.pending = null;
            if (!ready || !this.perform(action)) {
                this.model.status = terrain.lastError ||
                    this.specimens.find(value => value.component.lastError)?.component.lastError ||
                    `Could not complete ${action}.`;
                Debug.LogWarning(this.model.status);
            }
            changed = true;
        } else if (ready) {
            changed = this.apply_impacts(impacts) || changed;
        }
        const velocity = this.motor?.GetVelocity();
        const speed = velocity ? Math.min(2, Math.hypot(velocity.x, velocity.z) / 3) : 0;
        if (Math.abs(speed - this.animation_speed) > 0.02) {
            if (!this.animator?.SetSpeed(speed)) throw new Error("Cannot update the integration avatar");
            this.animation_speed = speed;
        }
        const groups = this.specimens.reduce((sum, item) => sum + (item.component.groupCount ?? 0), 0);
        const dynamic = this.specimens.reduce((sum, item) => sum + (item.component.dynamicGroups ?? 0), 0);
        const statistics = `${terrain.chunkCount} terrain chunks / ${groups} groups / ${dynamic} dynamic / ${this.support_checks} support checks`;
        const contacts = `${this.impact_contacts} qualifying contacts / ${this.secondary_damage} of ${max_secondary_damage} secondary damage events`;
        changed = changed || statistics !== this.model.statistics || contacts !== this.model.impacts;
        this.model.statistics = statistics;
        this.model.impacts = contacts;
        if (!ready) {
            const failure = terrain.lastError ||
                this.specimens.find(value => value.component.lastError)?.component.lastError;
            if (failure && failure !== this.model.status) {
                this.model.status = failure;
                changed = true;
            }
        }
        if (changed && !this.controller?.Refresh()) throw new Error("Cannot refresh the integration view");
    }

    private collect_impacts(): DamageRequest[] {
        const requests: DamageRequest[] = [];
        const previous = this.last_frame;
        for (const impact of Physics.GetImpacts()) {
            if (impact.frame <= previous) continue;
            if (impact.frame > this.last_frame) this.last_frame = impact.frame;
            if (impact.closingSpeed < 2.5 || impact.estimatedImpulse < 900) continue;
            let owned = false;
            for (const specimen of this.specimens) {
                if (!specimen.component.OwnsFragment(impact.nodeA) &&
                    !specimen.component.OwnsFragment(impact.nodeB)) continue;
                requests.push({ specimen, impact });
                owned = true;
            }
            if (owned) ++this.impact_contacts;
        }
        return requests.sort((a, b) => b.impact.estimatedImpulse - a.impact.estimatedImpulse);
    }

    private apply_impacts(requests: readonly DamageRequest[]): boolean {
        let applied = 0;
        for (const { specimen, impact } of requests) {
            if (applied >= 2 || this.secondary_damage >= max_secondary_damage) break;
            if (this.clock < specimen.next_impact) continue;
            specimen.next_impact = this.clock + 0.65;
            const { x, y, z } = impact.point;
            const radius = Math.min(7, 2 + impact.closingSpeed * 0.35 +
                Math.sqrt(impact.estimatedImpulse) * 0.003);
            const damage = Math.min(8, Math.max(1.5, impact.estimatedImpulse / 6000));
            if (!specimen.component.DamageSphere(x, y, z, radius, damage, 0)) {
                Debug.LogWarning(specimen.component.lastError || "Impact damage failed");
                continue;
            }
            ++applied;
            ++this.secondary_damage;
            if (!this.present(false, x, y, z))
                Debug.LogWarning("Could not play the impact presentation");
            this.model.status = `${specimen.name} impact: ${impact.closingSpeed.toFixed(1)} m/s closing speed, ${Math.round(impact.estimatedImpulse)} N.s estimated impulse. Contact caused secondary damage.`;
        }
        return applied > 0;
    }

    private refresh_support(): boolean {
        const collider = this.scene?.FindNode("Terrain")?.GetComponent(Collider);
        if (!collider) return false;
        for (const specimen of this.specimens) {
            if (!specimen.component.RefreshSupport(collider, 0.3, 0.5)) return false;
        }
        ++this.support_checks;
        return true;
    }

    private present(burst: boolean, x: number, y: number, z: number): boolean {
        const pool = burst ? this.bursts : this.dust;
        const slot = pool.find(value => value.available_at <= this.clock);
        // Exhaustion drops optional presentation only; physics damage still happens.
        if (!slot) return true;
        if (!slot.effect.Stop() || !slot.audio.Stop() ||
            !slot.transform.SetPosition(x, y + 0.15, z) ||
            !slot.effect.Play() || !slot.audio.Play()) return false;
        slot.available_at = this.clock + (burst ? 2.5 : 2.8);
        return true;
    }

    private perform(action: IntegrationAction): boolean {
        const terrain = this.terrain;
        if (!terrain) return false;
        switch (action) {
            case "dig":
                if (!terrain.DigSphere(0, -1.5, -2, 6.6) || !this.refresh_support()) return false;
                // Dust emerges near the visible rim of this excavation, outside the solid tower.
                if (!this.present(false, 0, 0.6, 2.4)) return false;
                this.model.status = "Foundation excavated. No direct damage: missing ground releases support anchors; landing impacts may fracture the tower.";
                return true;
            case "tower":
            case "wall": {
                const tower = action === "tower";
                const x = tower ? 0.7 : -11;
                const z = tower ? 0.5 : 1.6;
                const part = this.specimens[tower ? 0 : 1];
                if (!part.component.DamageSphere(x, 2, z, tower ? 6 : 3.4, 10,
                    tower ? 60000 : 7000)) return false;
                part.next_impact = this.clock + 0.2;
                if (!terrain.DigSphere(x, -1, z, tower ? 4.5 : 3) ||
                    !this.refresh_support() || !this.present(true, x, 1.2, z)) return false;
                this.model.status = "Blast changed structure and ground. Subsequent damage comes only from qualifying physical impacts.";
                return true;
            }
            case "fill":
                if (!terrain.FillSphere(0, -4, -2, 4) || !this.refresh_support()) return false;
                this.model.status = "Soil filled. Broken building anchors remain disconnected; Reset all restores the initial structure.";
                return true;
            case "reset":
                if (!this.stop_emitters() || !terrain.Reset()) return false;
                for (const specimen of this.specimens) {
                    if (!specimen.component.Reset()) return false;
                    specimen.next_impact = this.clock + 0.65;
                }
                this.secondary_damage = 0;
                this.impact_contacts = 0;
                this.support_checks = 0;
                if (!this.refresh_support() || !this.respawn()) return false;
                this.model.status = "Original terrain, support anchors and character restored. Contact counters and presentation cleared.";
                return true;
            case "respawn":
                if (!this.respawn()) return false;
                this.model.status = "Character returned to the safe zone. Select Walk to explore actual collision geometry.";
                return true;
            case "overview":
            case "front":
            case "walk": {
                const camera = this.scene?.FindNode(camera_names[action])?.GetComponent(Camera);
                if (!camera || !this.scene?.SetActiveCamera(camera)) return false;
                for (const [key, name] of Object.entries(camera_names)) {
                    if (!this.scene.FindNode(name)?.GetComponent(AudioListener)?.SetEnabled(key === action))
                        return false;
                }
                this.model.camera = action;
                this.model.status = action === "walk"
                    ? "WASD / Space / right mouse look / Tab first or third person. Walking requires keyboard and mouse."
                    : "Inspect real terrain and detached physical groups. Sound follows the selected camera.";
                return true;
            }
        }
    }

    private respawn(): boolean {
        const transform = this.scene?.FindNode("Player")?.GetComponent(Transform);
        return !!transform && !!this.motor && this.motor.ClearInput() &&
            transform.SetPosition(15, 3, 14) && transform.SetRotation(0, 0, 0);
    }

    private stop_emitters(): boolean {
        let result = true;
        for (const slot of [...this.dust, ...this.bursts]) {
            if (slot.effect.IsValid() && !slot.effect.Stop()) result = false;
            if (slot.audio.IsValid() && !slot.audio.Stop()) result = false;
            slot.available_at = 0;
        }
        return result;
    }

    override OnShutdown(): void {
        this.controller?.Unbind();
        this.controller = null;
        if (!this.stop_emitters()) Debug.LogWarning("Some presentation requests could not stop");
        this.dust.length = 0;
        this.bursts.length = 0;
        this.specimens.length = 0;
        this.model.pending = null;
        this.animator = null;
        this.motor = null;
        this.terrain = null;
        this.scene = null;
    }
}
