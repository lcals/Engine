import {
    Animator, AudioListener, AudioSource, Camera, CharacterMotor, Collider, Debug,
    DestructionComponent, EffectComponent, Gameplay, MeshRenderer, Physics,
    PhysicsImpact, Scene, TerrainComponent, Time, Transform, UiComponent, Vector3,
} from "engine";
import { DestructionIntegrationController } from "../controllers/destruction_integration_controller.js";
import { DestructionIntegrationModel, IntegrationAction } from "../models/destruction_integration_model.js";

import { DestructionRockets } from "./destruction-rockets.js";
import { DestructionNavigation } from "./destruction-navigation.js";
import { range_config as tuning } from "../models/destruction_range_config.js";
import { CharacterControls } from "../character_controls.js";

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
    private readonly input = new CharacterControls();
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
    private readonly rockets = new DestructionRockets();
    private readonly navigation = new DestructionNavigation();
    private barrage_left = 0;
    private rate_index = 1;
    private next_shot = 0;
    private next_navigation = 0;
    private next_stress = 0;
    private weak_stress = false;
    private stress_broken = 0;
    private maximum_frame_ms = 0;
    private average_frame_ms = 0;
    private previous_wall_ms = 0;
    private reporting = false;
    private next_report_ms = 0;
    private barrage_start_ms = 0;
    private barrage_start_fired = 0;
    private barrage_last_shot_ms = 0;
    private readonly pending_explosions: Vector3[] = [];
    private explosion_active = false;
    private explosion_generation = 0;

    override OnInitialize(): boolean {
        this.scene = Scene.Load("scenes/destruction-integration.scene");
        const scene = this.scene;
        if (!scene) return false;
        this.terrain = scene.FindNode("Terrain")?.GetComponent(TerrainComponent) ?? null;
        const player = scene.FindNode("Player");
        this.motor = player?.GetComponent(CharacterMotor) ?? null;
        const view = scene.FindNode("Integration UI")?.GetComponent(UiComponent);
        if (!this.terrain || !player || !this.motor || !view || !this.motor.SetEnabled(false))
            return false;
        for (const name of ["Tower", "Wall", "Column", "Stress beam"]) {
            const component = scene.FindNode(name)?.GetComponent(DestructionComponent);
            if (!component) return false;
            this.specimens.push({ name, component, next_impact: 0 });
        }
        for (const [prefix, count, pool] of [
            ["Impact", 8, this.dust], ["Blast", 6, this.bursts],
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
        const avatar = scene.InstantiateModel("models/skeleton.fbx", "Integration avatar");
        const body = scene.FindNode("GENERIC MAN new");
        const transform = avatar?.GetComponent(Transform);
        const renderer = body?.GetComponent(MeshRenderer);
        this.animator = body?.AddComponent(Animator) ?? null;
        if (!avatar || !transform || !renderer || !this.animator || !avatar.SetParent(player) ||
            !transform.SetPosition(0, 0.05, 0) || !transform.SetRotation(0, 180, 0) ||
            !renderer.material.SetType("materials/character.material") ||
            !this.animator.SetAnimation("models/locomotions.fbx/animations/walk") ||
            !this.animator.SetSpeed(0) || !this.animator.Play()) return false;
        if (!this.rockets.Bind(scene) || !this.navigation.Bind(scene)) return false;
        this.controller = new DestructionIntegrationController(view, this.model);
        if (!this.controller.Bind()) return false;
        Debug.Log("Combined destruction preparing. Use the buttons to excavate, blast and reset.");
        return this.input.Bind(scene);
    }

    override Update(): void {
        this.input.Update();
        const dt = Math.max(0, Time.deltaTime);
        const wall_ms = Date.now();
        const interval_ms = this.previous_wall_ms > 0 ? Math.max(0, wall_ms - this.previous_wall_ms) : 0;
        this.previous_wall_ms = wall_ms;
        this.clock += dt;
        if (this.started) {
            // Gameplay delta is capped by the host. Never use it to hide long stalls.
            this.maximum_frame_ms = Math.max(this.maximum_frame_ms, interval_ms);
            this.average_frame_ms += (interval_ms - this.average_frame_ms) * 0.08;
        }
        const terrain = this.terrain;
        if (!terrain?.IsValid()) return;
        const ready = terrain.ready && this.specimens.every(value =>
            value.component.IsValid() && value.component.ready);
        let changed = this.model.ready !== ready;
        this.model.ready = ready;
        if (ready && !this.started) {
            if (!this.prepare_ground() || !this.refresh_support() || !this.navigation.Build() || !this.motor?.SetEnabled(true))
                throw new Error("Cannot start the integration scene support and character");
            this.started = true;
            this.model.status = "Rocket range ready. Fire at structures or ground, or launch a 60-round barrage. Every explosion follows a real collision sweep.";
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
        }
        if (ready && this.started) {
            if (this.barrage_left > 0 && this.clock >= this.next_shot) {
                // Do not accumulate a catch-up queue after synchronous work stalls a frame.
                const target = tuning.targets[this.rockets.fired % tuning.targets.length];
                const offset = Math.sin(this.rockets.fired * 2.39996) * 1.1;
                if (this.rockets.Fire({x:target.x+offset,y:target.y,z:target.z})) {
                    --this.barrage_left;
                    this.barrage_last_shot_ms = wall_ms;
                }
                this.next_shot = this.clock + 1 / tuning.rates[this.rate_index];
                changed = true;
            }
            const hits = this.rockets.Tick(dt);
            for (const hit of hits) {
                if (!this.explode(hit)) Debug.LogWarning(this.model.status);
            }
            changed = hits.length > 0 || changed;
            if (!this.explosion_active && hits.length === 0 && action === null)
                changed = this.apply_impacts(impacts) || changed;
            if (!this.explosion_active && this.clock >= this.next_stress) {
                this.next_stress = this.clock + tuning.stressInterval;
                for (const item of this.specimens) {
                    const beam = item.name === "Stress beam";
                    const weak = this.weak_stress && beam;
                    const broken = item.component.ApplyStress(0,-9.81,0,
                        weak ? 10 : beam ? 1e8 : tuning.stressElastic,
                        weak ? 20 : beam ? 2e8 : tuning.stressFatal,
                        tuning.stressIterations);
                    if (broken === null) {
                        this.model.status = item.component.lastError || "Stress calculation failed";
                        Debug.LogWarning(this.model.status);
                    } else this.stress_broken += broken;
                }
            }
            if (this.clock >= this.next_navigation) {
                this.next_navigation = this.clock + 0.25;
                if (!this.navigation.Flush()) Debug.LogWarning(this.navigation.status);
            }
            if (!this.explosion_active) this.start_explosion();
        }
        const velocity = this.motor?.GetVelocity();
        const speed = velocity ? Math.min(2, Math.hypot(velocity.x, velocity.z) / 3) : 0;
        if (Math.abs(speed - this.animation_speed) > 0.02) {
            if (!this.animator?.SetSpeed(speed)) throw new Error("Cannot update the integration avatar");
            this.animation_speed = speed;
        }
        const groups = this.specimens.reduce((sum, item) => sum + (item.component.groupCount ?? 0), 0);
        const dynamic = this.specimens.reduce((sum, item) => sum + (item.component.dynamicGroups ?? 0), 0);
        const statistics = `${groups} building groups / ${dynamic} dynamic / ${terrain.fragmentCount} soil fragments / ${this.stress_broken} stress breaks`;
        const contacts = `${this.impact_contacts} qualifying contacts / ${this.secondary_damage} of ${max_secondary_damage} secondary damage events`;
        changed = changed || statistics !== this.model.statistics || contacts !== this.model.impacts;
        this.model.statistics = statistics;
        this.model.impacts = contacts;
        const pending = this.pending_explosions.length + (this.explosion_active ? 1 : 0);
        const combat = `${tuning.rates[this.rate_index]} shots/s requested / ${this.barrage_left} remaining\n${this.rockets.fired} fired / ${this.rockets.hits} hits / ${this.rockets.active} flying / ${this.rockets.missed} misses / ${pending} edits pending\nWall frame interval ${this.average_frame_ms.toFixed(1)} ms smoothed / ${this.maximum_frame_ms.toFixed(1)} ms max (includes frame cap)`;
        changed = changed || combat !== this.model.combat || this.navigation.status !== this.model.navigation;
        this.model.combat = combat;
        this.model.navigation = this.navigation.status;
        if (this.reporting && (wall_ms >= this.next_report_ms ||
            (this.barrage_left === 0 && this.rockets.active === 0 && pending === 0))) {
            const count = this.rockets.fired - this.barrage_start_fired;
            const seconds = (this.barrage_last_shot_ms - this.barrage_start_ms) / 1000;
            const complete = this.barrage_left === 0 && this.rockets.active === 0 && pending === 0;
            Debug.Log(`Rocket range sample ${JSON.stringify({ complete,
                requestedRate: tuning.rates[this.rate_index], fired: this.rockets.fired,
                hits: this.rockets.hits, flying: this.rockets.active, missed: this.rockets.missed,
                remaining: this.barrage_left, blocked: this.rockets.blocked, pendingEdits: pending,
                actualRate: seconds > 0 ? (count - 1) / seconds : 0,
                groups, dynamic, soil: terrain.fragmentCount, stressBreaks: this.stress_broken,
                wallFrameSmoothedMs: this.average_frame_ms, wallFrameMaxMs: this.maximum_frame_ms,
                route: this.navigation.status })}`);
            this.next_report_ms = wall_ms + 1000;
            if (complete) this.reporting = false;
        }
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
                if (!specimen.component.OwnsFragment(impact.a.nodeId) &&
                    !specimen.component.OwnsFragment(impact.b.nodeId)) continue;
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
            if (!Physics.IsObjectValid(impact.a) || !Physics.IsObjectValid(impact.b)) continue;
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
            const support = specimen.name === "Stress beam"
                ? this.scene?.FindNode("Stress foundation")?.GetComponent(Collider) : collider;
            if (!support || !specimen.component.RefreshSupport(support, 0.3, 0.5)) return false;
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
                if (this.explosion_active || this.pending_explosions.length > 0) return false;
                if (!terrain.DigSphere(0, -1.5, -2, 6.6) || !this.refresh_support()) return false;
                this.navigation.Dirty({x:0,y:-1.5,z:-2}, 8, false);
                // Dust emerges near the visible rim of this excavation, outside the solid tower.
                if (!this.present(false, 0, 0.6, 2.4)) return false;
                this.model.status = "Foundation excavated. No direct damage: missing ground releases support anchors; landing impacts may fracture the tower.";
                return true;
            case "tower":
                return this.fire({x:0.5,y:5,z:0.6});
            case "wall":
                return this.fire({x:-11,y:4,z:1.1});
            case "ground":
                return this.fire({x:3,y:0,z:5});
            case "rock":
                return this.fire({x:-18,y:1.7,z:7});
            case "fire": {
                const position = this.scene?.FindNode("Player camera")?.GetComponent(Transform)?.GetWorldPosition();
                const forward = this.scene?.FindNode("Player camera")?.GetComponent(Transform)?.GetWorldForward();
                if (!position || !forward) return false;
                return this.fire({x:position.x+forward.x*70,y:position.y+forward.y*70,z:position.z+forward.z*70});
            }
            case "barrage":
                this.barrage_left = tuning.barrageShots;
                this.next_shot = this.clock;
                this.reporting = true;
                this.next_report_ms = this.barrage_start_ms = this.barrage_last_shot_ms = Date.now();
                this.barrage_start_fired = this.rockets.fired;
                this.maximum_frame_ms = this.average_frame_ms = 0;
                this.model.status = "Continuous fire: fixed sequence across ground and buildings. Stop cancels new shots; in-flight rockets still hit.";
                return true;
            case "stop":
                this.barrage_left = 0;
                this.model.status = "Cease fire. In-flight rockets finish their physical trajectories.";
                return true;
            case "rate":
                this.rate_index = (this.rate_index + 1) % tuning.rates.length;
                return true;
            case "stress":
                this.weak_stress = !this.weak_stress;
                this.model.status = this.weak_stress ? "Weak cantilever material: official Blast stress may break overloaded bonds."
                    : "Normal structural strength restored; broken bonds stay broken until Reset.";
                this.next_stress = this.clock;
                return true;
            case "route":
                return this.navigation.Route();
            case "fill":
                if (this.explosion_active || this.pending_explosions.length > 0) return false;
                if (!terrain.FillSphere(3, -2, 5, 4) || !this.refresh_support()) return false;
                this.navigation.Dirty({x:3,y:0,z:5}, 6, false);
                this.model.status = "Soil filled. Broken building anchors remain disconnected; Reset all restores the initial structure.";
                return true;
            case "reset":
                ++this.explosion_generation;
                this.pending_explosions.length = 0;
                this.explosion_active = false;
                this.barrage_left = 0;
                this.reporting = false;
                this.weak_stress = false;
                this.stress_broken = 0;
                this.maximum_frame_ms = this.average_frame_ms = 0;
                if (!this.rockets.Reset() || !this.stop_emitters() || !terrain.Reset() || !this.prepare_ground()) return false;
                for (const specimen of this.specimens) {
                    if (!specimen.component.Reset()) return false;
                    specimen.next_impact = this.clock + 0.65;
                }
                this.secondary_damage = 0;
                this.impact_contacts = 0;
                this.support_checks = 0;
                if (!this.refresh_support() || !this.navigation.Build() || !this.respawn()) return false;
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

    private prepare_ground(): boolean {
        // A narrow pedestal and an overhanging cap provide repeatable unsupported-soil tests.
        return !!this.terrain && this.terrain.FillSphere(-18, 1.3, 7, 1.8) &&
            this.terrain.FillSphere(-18, 6.9, 7, 4.4);
    }

    private fire(target: Vector3): boolean {
        if (!this.rockets.Fire(target)) return false;
        this.model.status = "Rocket launched from the character. Collision determines the explosion point.";
        return true;
    }

    private explode(point: Vector3): boolean {
        if (this.pending_explosions.length >= tuning.rocketPool) {
            this.model.status = "Explosion queue is full; this hit was not submitted.";
            return false;
        }
        this.pending_explosions.push(point);
        return this.present(true, point.x, point.y, point.z) &&
            this.present(false, point.x, point.y, point.z);
    }

    private start_explosion(): void {
        const terrain = this.terrain;
        const point = this.pending_explosions.shift();
        if (!terrain || !point) return;
        const {x,y,z} = point;
        const generation = this.explosion_generation;
        const before = terrain.fragmentCount ?? 0;
        this.explosion_active = true;
        let remaining = this.specimens.length + 1;
        let ok = true;
        let terrain_ready = false;
        const completed = (success: boolean): void => {
            if (generation !== this.explosion_generation || !this.scene?.IsValid()) return;
            ok = success && ok;
            if (--remaining !== 0) return;
            // All workers have published before synchronous support or soil edits change revisions.
            if (terrain_ready) {
                if (!terrain.CollapseDetached(tuning.minSoilSamples, tuning.maxSoilFragments,
                    tuning.soilFragmentCells, tuning.soilDensity)) ok = false;
            }
            if (!this.refresh_support()) ok = false;
            if (terrain_ready)
                this.navigation.Dirty(point, tuning.terrainRadius + 2,
                    (terrain.fragmentCount ?? 0) !== before);
            this.explosion_active = false;
            this.next_stress = this.clock;
            this.model.status = ok
                ? `Rocket hit (${x.toFixed(1)}, ${y.toFixed(1)}, ${z.toFixed(1)}). Background edits published; support and navigation refreshed.`
                : terrain.lastError || this.specimens.find(item => item.component.lastError)?.component.lastError ||
                    "An explosion edit was rejected or cancelled.";
            if (!ok) Debug.LogWarning(this.model.status);
            if (!this.controller?.Refresh()) throw new Error("Cannot refresh completed explosion");
        };
        for (const specimen of this.specimens) {
            if (!specimen.component.DamageSphereAsync(x, y, z, tuning.damageRadius,
                tuning.damage, tuning.impulse, completed)) completed(false);
            specimen.next_impact = this.clock + 0.2;
        }
        if (!terrain.DigSphereAsync(x, y, z, tuning.terrainRadius, success => {
            terrain_ready = success;
            completed(success);
        })) completed(false);
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
        ++this.explosion_generation;
        this.pending_explosions.length = 0;
        this.explosion_active = false;
        this.input.Shutdown();
        this.barrage_left = 0;
        this.rockets.Shutdown();
        this.navigation.Shutdown();
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
