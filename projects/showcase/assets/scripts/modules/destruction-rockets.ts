import { EffectComponent, MeshRenderer, Physics, Scene, Transform, Vector3 } from "engine";
import { range_config as tuning } from "../models/destruction_range_config.js";

interface Rocket {
    readonly transform: Transform;
    readonly renderer: MeshRenderer;
    readonly trail: EffectComponent;
    position: Vector3;
    direction: Vector3;
    lifetime: number;
    active: boolean;
}

// Fixed project-owned projectile pool. Every flight segment uses the same Jolt sweep.
export class DestructionRockets {
    private readonly rockets: Rocket[] = [];
    private player: Transform | null = null;
    private launcher: Transform | null = null;
    fired = 0;
    hits = 0;
    missed = 0;
    blocked = 0;

    Bind(scene: Scene): boolean {
        this.player = scene.FindNode("Player")?.GetComponent(Transform) ?? null;
        this.launcher = scene.FindNode("Rocket launcher")?.GetComponent(Transform) ?? null;
        if (!this.player || !this.launcher) return false;
        for (let index = 1; index <= tuning.rocketPool; ++index) {
            const node = scene.FindNode(`Rocket ${index}`);
            const transform = node?.GetComponent(Transform);
            const renderer = node?.GetComponent(MeshRenderer);
            const trail = node?.GetComponent(EffectComponent);
            if (!transform || !renderer || !trail || !renderer.SetEnabled(false) ||
                !trail.Stop()) return false;
            this.rockets.push({transform, renderer, trail, active: false, lifetime: 0,
                position: {x:0,y:0,z:0}, direction: {x:0,y:0,z:-1}});
        }
        return true;
    }

    Fire(target: Vector3): boolean {
        const slot = this.rockets.find(rocket => !rocket.active);
        const player = this.player?.GetWorldPosition();
        if (!slot || !player) { ++this.blocked; return false; }
        const muzzle = {x: player.x + 0.48, y: player.y + 1.48, z: player.z};
        const delta = {x: target.x - muzzle.x, y: target.y - muzzle.y, z: target.z - muzzle.z};
        const length = Math.hypot(delta.x, delta.y, delta.z);
        if (length < 1) return false;
        const direction = {x: delta.x / length, y: delta.y / length, z: delta.z / length};
        const pitch = Math.asin(direction.y) * 180 / Math.PI;
        const yaw = Math.atan2(-direction.x, -direction.z) * 180 / Math.PI;
        const position = muzzle;
        if (!slot.transform.SetPosition(position.x, position.y, position.z) ||
            !slot.transform.SetRotation(pitch, yaw, 0) || !slot.renderer.SetEnabled(true) ||
            !slot.trail.Play() || !this.launcher?.SetPosition(muzzle.x, muzzle.y, muzzle.z) ||
            !this.launcher.SetRotation(pitch, yaw, 0)) {
            slot.renderer.SetEnabled(false);
            slot.trail.Stop();
            return false;
        }
        slot.position = position;
        slot.direction = direction;
        slot.lifetime = tuning.rocketLifetime;
        slot.active = true;
        ++this.fired;
        return true;
    }

    Tick(dt: number): Vector3[] {
        const impacts: Vector3[] = [];
        const player = this.player?.GetWorldPosition();
        if (player && !this.launcher?.SetPosition(player.x + 0.48, player.y + 1.48, player.z))
            throw new Error("Cannot follow the launcher owner");
        for (const rocket of this.rockets) {
            if (!rocket.active) continue;
            const distance = tuning.rocketSpeed * Math.min(dt, rocket.lifetime);
            const delta = {x: rocket.direction.x * distance, y: rocket.direction.y * distance,
                z: rocket.direction.z * distance};
            const p = rocket.position;
            const hit = Physics.CastSphere(p.x, p.y, p.z, delta.x, delta.y, delta.z, tuning.rocketRadius);
            if (hit === null) throw new Error("Rocket collision sweep failed");
            rocket.position = {x: p.x + delta.x * hit, y: p.y + delta.y * hit, z: p.z + delta.z * hit};
            rocket.lifetime -= dt;
            if (hit < 1 || rocket.lifetime <= 0) {
                if (!rocket.renderer.SetEnabled(false) || !rocket.trail.Stop())
                    throw new Error("Cannot release the rocket pool slot");
                rocket.active = false;
                if (hit < 1) { impacts.push(rocket.position); ++this.hits; }
                else ++this.missed;
            } else if (!rocket.transform.SetPosition(rocket.position.x, rocket.position.y, rocket.position.z)) {
                throw new Error("Cannot move a live projectile");
            }
        }
        return impacts;
    }

    get active(): number { return this.rockets.filter(value => value.active).length; }

    Reset(): boolean {
        let ok = true;
        for (const rocket of this.rockets) {
            if (!rocket.renderer.SetEnabled(false) || !rocket.trail.Stop()) ok = false;
            rocket.active = false;
        }
        this.fired = this.hits = this.missed = this.blocked = 0;
        return ok;
    }

    Shutdown(): void {
        if (!this.Reset()) throw new Error("Cannot stop the rocket range");
        this.rockets.length = 0;
        this.player = this.launcher = null;
    }
}
