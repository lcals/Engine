export type ShootingRangeAction = "fire" | "reset" | "ray" | "sphere";

/** Only the example's score and controls; no combat or projectile simulation. */
export class ShootingRangeModel {
    readonly actions: readonly ShootingRangeAction[] = ["fire", "reset", "ray", "sphere"];
    readonly total = 3;
    pending: ShootingRangeAction | null = null;
    shots = 0;
    hits = 0;
    ready = false;
    mode: "ray" | "sphere" = "ray";
    status = "Near target ahead, far target left, covered target right. Move around the blue wall.";

    get complete(): boolean {
        return this.hits === this.total;
    }

    Reset(): void {
        this.shots = 0;
        this.hits = 0;
        this.pending = null;
        this.status = "Targets restored. Hit all three; the blue wall blocks the covered target.";
    }
}
