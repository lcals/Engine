import { Debug, Gameplay, Node, Scene, Time } from "engine";

export default class Game extends Gameplay {
    private cube: Node | null = null;
    private angle = 0;

    override OnInitialize(): boolean {
        const scene = Scene.Create("PBR cube");
        if (!scene) return false;
        const cube = scene.CreateNode("Cube");
        const cameraNode = scene.CreateNode("Camera");
        const lightNode = scene.CreateNode("Sun");
        if (!cube || !cameraNode || !lightNode) return false;
        const mesh = cube.AddMeshRenderer();
        const camera = cameraNode.AddCamera();
        const light = lightNode.AddLight();
        if (!mesh || !camera || !light) return false;
        if (!mesh.SetMaterial({
            baseColor: { r: 0.72, g: 0.23, b: 0.08, a: 1 },
            metallic: 0.35,
            roughness: 0.3,
            unlit: false,
        }) || !cube.SetRotation(18, 25, 0)
            || !cameraNode.SetPosition(0, 0, 5)
            || !camera.SetPerspective(50, 0.1, 100)
            || !scene.SetActiveCamera(camera)
            || !lightNode.SetRotation(-35, -30, 0)
            || !light.SetColor(1, 0.96, 0.9)
            || !light.SetIntensity(4)) return false;
        this.cube = cube;
        Debug.Log("Game.OnInitialize: PBR cube scene ready");
        return true;
    }

    override Update(): void {
        this.angle = (this.angle + Time.deltaTime * 35) % 360;
        if (this.cube && !this.cube.SetRotation(18, 25 + this.angle, 0)) {
            throw new Error("Cube is no longer part of the active scene");
        }
    }

    override OnShutdown(): void {
        this.cube = null;
        Debug.Log("Game.OnShutdown: scene released");
    }
}
