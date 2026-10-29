import { Debug, Gameplay, Camera, Light, MeshRenderer, Transform, Scene, Time } from "engine";

export default class Game extends Gameplay {
    private cube: Transform | null = null;
    private angle = 0;

    override OnInitialize(): boolean {
        const scene = Scene.Create("PBR cube");
        if (!scene) return false;
        const cube = scene.CreateNode("Cube");
        const cameraNode = scene.CreateNode("Camera");
        const lightNode = scene.CreateNode("Sun");
        if (!cube || !cameraNode || !lightNode) return false;
        const mesh = cube.AddComponent(MeshRenderer);
        const camera = cameraNode.AddComponent(Camera);
        const light = lightNode.AddComponent(Light);
        const cubeTransform = cube.GetComponent(Transform);
        const cameraTransform = cameraNode.GetComponent(Transform);
        const lightTransform = lightNode.GetComponent(Transform);
        if (!mesh || !camera || !light || !cubeTransform || !cameraTransform || !lightTransform)
            return false;
        const material = mesh.material;
        if (!material.SetType("builtin/materials/default-pbr")
            || !material.SetV4("baseColor", 0.72, 0.23, 0.08, 1)
            || !material.SetFloat("metallic", 0.35)
            || !material.SetFloat("roughness", 0.3) || !cubeTransform.SetRotation(18, 25, 0)
            || !cameraTransform.SetPosition(0, 0, 5)
            || !camera.SetPerspective(50, 0.1, 100)
            || !scene.SetActiveCamera(camera)
            || !lightTransform.SetRotation(-35, -30, 0)
            || !light.SetColor(1, 0.96, 0.9)
            || !light.SetIntensity(4)) return false;
        this.cube = cubeTransform;
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
