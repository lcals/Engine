import { MeshRenderer, Navigation, Node, Scene, Transform, Vector3 } from "engine";

export class DestructionNavigation {
    private terrain: Node | null = null;
    private readonly markers: {renderer: MeshRenderer; transform: Transform}[] = [];
    private dirtyMin: Vector3 | null = null;
    private dirtyMax: Vector3 | null = null;
    status = "Navigation not built";
    private readonly start = {x: -20, y: 0.2, z: 5};
    private readonly goal = {x: 18, y: 0.2, z: 5};

    Bind(scene: Scene): boolean {
        this.terrain = scene.FindNode("Terrain");
        for (let index = 1; index <= 48; ++index) {
            const node = scene.FindNode(`Route marker ${index}`);
            const renderer = node?.GetComponent(MeshRenderer);
            const transform = node?.GetComponent(Transform);
            if (!renderer || !transform) return false;
            this.markers.push({renderer, transform});
        }
        return this.terrain !== null;
    }

    Build(): boolean {
        this.dirtyMin = this.dirtyMax = null;
        if (!this.terrain || !Navigation.Build([this.terrain], {
            minimum: {x:-32,y:-16,z:-32}, maximum: {x:32,y:20,z:32},
            cellSize: 0.5, cellHeight: 0.2, tileSize: 16,
            agentHeight: 1.8, agentRadius: 0.4, agentMaxClimb: 0.35,
        })) { this.status = Navigation.lastError ?? "Navigation build failed"; return false; }
        return this.Route();
    }

    Dirty(center: Vector3, radius: number, collapsed: boolean): void {
        const minimum = collapsed ? {x:-32,y:-16,z:-32} :
            {x:center.x-radius,y:-16,z:center.z-radius};
        const maximum = collapsed ? {x:32,y:20,z:32} :
            {x:center.x+radius,y:20,z:center.z+radius};
        this.dirtyMin = this.dirtyMin ? {x:Math.min(minimum.x,this.dirtyMin.x), y:-16,
            z:Math.min(minimum.z,this.dirtyMin.z)} : minimum;
        this.dirtyMax = this.dirtyMax ? {x:Math.max(maximum.x,this.dirtyMax.x), y:20,
            z:Math.max(maximum.z,this.dirtyMax.z)} : maximum;
        // Do not keep showing a route while submitted ground geometry is stale.
        for (const marker of this.markers) {
            if (!marker.renderer.SetEnabled(false)) throw new Error("Cannot hide stale route");
        }
    }

    Flush(): boolean {
        if (!this.dirtyMin || !this.dirtyMax || !this.terrain) return true;
        const minimum = this.dirtyMin, maximum = this.dirtyMax;
        this.dirtyMin = this.dirtyMax = null;
        if (!Navigation.Replace([this.terrain], minimum, maximum)) {
            this.status = Navigation.lastError ?? "Affected navigation tiles invalidated";
            return false;
        }
        return this.Route();
    }

    Route(): boolean {
        if (this.dirtyMin) return this.Flush();
        for (const marker of this.markers) if (!marker.renderer.SetEnabled(false)) return false;
        const path = Navigation.FindPath(this.start, this.goal);
        if (!path) { this.status = "No complete route. Never follow a stale route across the crater."; return true; }
        let index = 0;
        let length = 0;
        for (let segment = 1; segment < path.length; ++segment) {
            const a = path[segment-1], b = path[segment];
            const distance = Math.hypot(b.x-a.x,b.y-a.y,b.z-a.z);
            length += distance;
            const count = Math.max(1, Math.ceil(distance / 1.0));
            for (let step = 0; step < count && index < this.markers.length; ++step) {
                const marker = this.markers[index++], t = step/count;
                if (!marker.transform.SetPosition(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t+0.12,
                    a.z+(b.z-a.z)*t) || !marker.renderer.SetEnabled(true)) return false;
            }
        }
        this.status = `Ground route ${length.toFixed(1)} m / ${Navigation.rebuiltTiles} tiles rebuilt. Moving debris is not baked into this route.`;
        return true;
    }

    Shutdown(): void {
        Navigation.Clear();
        this.markers.length = 0;
        this.terrain = null;
        this.dirtyMin = this.dirtyMax = null;
    }
}
