import { AnimationState, AnimationStateContext, AnimationStateMachine } from "engine";
import { walk_animation, walk_reference_speed } from "../character_animations.js";

class MovingState extends AnimationState {
    constructor(name: string, alias: string, private readonly referenceSpeed: number) {
        super(name, alias);
    }
    override OnUpdate(context: AnimationStateContext): void {
        const speed = context.GetFloat("speed");
        if (speed === null || !context.SetSpeed(Math.min(4, speed / this.referenceSpeed)))
            throw new Error("Cannot update locomotion playback speed");
    }
}

// One immutable definition is explicitly shared by every actor in this module's Run.
// Idle temporarily retains the first walk pose until an authored idle clip is added.
class LocomotionMachine extends AnimationStateMachine {
    constructor() {
        super("Idle");
        this.AddParameter("speed", "float", 0);
        this.AddState(new AnimationState("Idle", "idle", {speed: 0}));
        this.AddState(new MovingState("Walk", "walk", walk_reference_speed));
        this.AddState(new MovingState("Run", "run", 3.4));
        for (const from of ["Walk", "Run"])
            this.AddTransition(from, "Idle", c => (c.GetFloat("speed") ?? 0) < 0.1,
                {seconds: 0.15, priority: 10, interrupt: true});
        for (const from of ["Idle", "Walk"])
            this.AddTransition(from, "Run", c => (c.GetFloat("speed") ?? 0) >= 3.5,
                {seconds: 0.2, priority: 5, interrupt: true});
        this.AddTransition("Idle", "Walk", c => (c.GetFloat("speed") ?? 0) >= 0.1,
            {seconds: 0.2, interrupt: true});
        this.AddTransition("Run", "Walk", c => (c.GetFloat("speed") ?? 0) < 3.2,
            {seconds: 0.2, interrupt: true});
    }
}
export const locomotionMachine = new LocomotionMachine();
export const locomotionActions = Object.freeze({
    idle: walk_animation,
    walk: walk_animation,
    run: "models/locomotions.fbx/animations/run",
});
