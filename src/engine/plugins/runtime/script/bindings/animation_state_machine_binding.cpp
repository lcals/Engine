#include "plugins/runtime/script/bindings/animation_state_machine_binding.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
const animationDefinitions = new WeakMap();
const animationStates = new WeakMap();
const animationContexts = new WeakMap();
const animationAliasSnapshots = new WeakMap();
const animationMachines = new Map();
const animationContextKey = {};
let animationCallbackDepth = 0;
let animationConditionDepth = 0;
function animationText(value) { return typeof value === 'string' && value.length > 0; }
function animationObject(value) {
    if (value === null || typeof value !== 'object' || Array.isArray(value)) return false;
    const prototype = Object.getPrototypeOf(value);
    return prototype === null || prototype === Object.prototype;
}
function animationOptions(value, fields) {
    return value === undefined || (animationObject(value) &&
        Object.keys(value).every(name => fields.includes(name)));
}
function animationSpeed(value) {
    return typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 16;
}
function animationSynchronous(value) {
    if (value !== null && (typeof value === 'object' || typeof value === 'function') &&
        typeof value.then === 'function') throw new TypeError('Animation callbacks must be synchronous');
    return value;
}
function animationFreeze(value, seen = new Set()) {
    if (value === null || (typeof value !== 'object' && typeof value !== 'function') ||
        seen.has(value)) return;
    if (handleRecords.has(value) || value instanceof Map || value instanceof Set ||
        value instanceof WeakMap || value instanceof WeakSet || ArrayBuffer.isView(value) ||
        value instanceof ArrayBuffer || value instanceof Date)
        throw new TypeError('Shared animation definitions cannot contain mutable instance data');
    seen.add(value);
    for (const name of Reflect.ownKeys(value)) {
        const descriptor = Object.getOwnPropertyDescriptor(value, name);
        if (Object.prototype.hasOwnProperty.call(descriptor, 'value'))
            animationFreeze(descriptor.value, seen);
    }
    Object.freeze(value);
}
function animationSnapshot(animator) {
    const h = handles.get(animator);
    if (h === undefined || h.kind !== native.AnimatorKind || h.instance === 0n) return null;
    const value = native.AnimatorSnapshot(h.scene, h.node);
    return value !== null && value.instance === h.instance ? value : null;
}
function animationMachineKey(animator) {
    const h = handles.get(animator);
    return h === undefined ? '' : `${h.scene}/${h.node}/${h.instance}`;
}
function animationRecord(animator) {
    const snapshot = animationSnapshot(animator);
    if (snapshot === null) return null;
    const record = animationMachines.get(animationMachineKey(animator));
    return record !== undefined && record.alive ? record : null;
}
function animationContext(context) {
    const view = animationContexts.get(context);
    if (view === undefined || !view.open || !view.record.alive ||
        animationSnapshot(view.record.animator) === null)
        throw new TypeError('Animation context is only valid during its synchronous callback');
    return view;
}
function animationRead(context, name, type) {
    const view = animationContext(context);
    const parameter = view.record.definition.parameters.get(name);
    return parameter !== undefined && parameter.type === type ? view.values[parameter.index] : null;
}
export class AnimationStateContext {
    constructor(token) {
        if (token !== animationContextKey) throw new TypeError('Animation context is borrowed');
    }
    get state() { return animationContext(this).state.name; }
    get deltaTime() { return animationContext(this).delta; }
    get stateTime() { return animationContext(this).stateTime; }
    get animationTime() { return animationContext(this).snapshot.time; }
    get animationDuration() { return animationContext(this).snapshot.duration; }
    get normalizedTime() { return animationContext(this).snapshot.normalizedTime; }
    get finished() { return animationContext(this).snapshot.completed; }
    GetFloat(name) { return animationRead(this, name, 'float'); }
    GetInt(name) { return animationRead(this, name, 'int'); }
    GetBool(name) { return animationRead(this, name, 'bool'); }
    IsTriggered(name) {
        const view = animationContext(this);
        const parameter = view.record.definition.parameters.get(name);
        if (parameter === undefined || parameter.type !== 'trigger' ||
            view.values[parameter.index] !== true) return false;
        if (view.readTriggers !== null)
            view.readTriggers.set(parameter.index, view.versions[parameter.index]);
        return true;
    }
    SetSpeed(value) {
        const view = animationContext(this);
        if ((view.phase !== 'enter' && view.phase !== 'update') || !animationSpeed(value)) return false;
        const h = handles.get(view.record.animator);
        return native.AnimatorSpeed(h.scene, h.node, value);
    }
}
export class AnimationState {
    constructor(name, animationAlias, options = {}) {
        if (!animationText(name) || !animationText(animationAlias) ||
            !animationOptions(options, ['speed', 'loop']) ||
            (options.speed !== undefined && !animationSpeed(options.speed)) ||
            (options.loop !== undefined && typeof options.loop !== 'boolean'))
            throw new TypeError('Invalid animation state');
        animationStates.set(this, Object.freeze({name, alias: animationAlias,
            speed: options.speed === undefined ? 1 : options.speed,
            loop: options.loop === undefined ? true : options.loop}));
    }
    get name() { return animationStates.get(this).name; }
    get animationAlias() { return animationStates.get(this).alias; }
    OnEnter(context) {}
    OnUpdate(context) {}
    OnExit(context) {}
}
export class AnimationCondition {
    constructor() {
        if (new.target === AnimationCondition) throw new TypeError('AnimationCondition is abstract');
    }
    Test(context) { throw new TypeError('Override AnimationCondition.Test'); }
}
export class AnimationStateMachine {
    constructor(initialState) {
        if (!animationText(initialState)) throw new TypeError('Initial animation state is required');
        animationDefinitions.set(this, {initialState, parameters: new Map(), states: [],
            indices: new Map(), transitions: [], sealed: false});
    }
    get initialState() { return animationDefinitions.get(this).initialState; }
    AddParameter(name, type, defaultValue) {
        const definition = animationDefinitions.get(this);
        if (definition === undefined || definition.sealed || !animationText(name) ||
            definition.parameters.has(name) || !['float', 'int', 'bool', 'trigger'].includes(type))
            return false;
        if (defaultValue === undefined) defaultValue = type === 'float' || type === 'int' ? 0 : false;
        if (!animationParameterValue(type, defaultValue) || (type === 'trigger' && defaultValue))
            return false;
        definition.parameters.set(name, Object.freeze({type, defaultValue,
            index: definition.parameters.size}));
        return true;
    }
    AddState(state) {
        const definition = animationDefinitions.get(this), value = animationStates.get(state);
        if (definition === undefined || definition.sealed || value === undefined ||
            definition.indices.has(value.name) || typeof state.OnEnter !== 'function' ||
            typeof state.OnUpdate !== 'function' || typeof state.OnExit !== 'function') return false;
        definition.indices.set(value.name, definition.states.length);
        definition.states.push(state);
        return true;
    }
    AddTransition(from, to, condition, options = {}) {
        const definition = animationDefinitions.get(this);
        if (definition === undefined || definition.sealed ||
            !(from === null || animationText(from)) || !animationText(to) ||
            !(typeof condition === 'function' || (condition instanceof AnimationCondition &&
              typeof condition.Test === 'function')) ||
            !animationOptions(options, ['seconds', 'priority', 'exitTime', 'interrupt', 'reenter']) ||
            (options.seconds !== undefined && (!Number.isFinite(options.seconds) || options.seconds < 0)) ||
            (options.priority !== undefined && !Number.isSafeInteger(options.priority)) ||
            (options.exitTime !== undefined && (!Number.isFinite(options.exitTime) || options.exitTime < 0)) ||
            (options.interrupt !== undefined && typeof options.interrupt !== 'boolean') ||
            (options.reenter !== undefined && typeof options.reenter !== 'boolean')) return false;
        definition.transitions.push(Object.freeze({from, to, condition,
            seconds: options.seconds === undefined ? .15 : options.seconds,
            priority: options.priority === undefined ? 0 : options.priority,
            exitTime: options.exitTime, interrupt: options.interrupt === true,
            reenter: options.reenter === true, order: definition.transitions.length}));
        return true;
    }
}
function animationParameterValue(type, value) {
    if (type === 'bool' || type === 'trigger') return typeof value === 'boolean';
    return typeof value === 'number' && Number.isFinite(value) &&
        (type !== 'int' || Number.isSafeInteger(value));
}
function animationAliases(source) {
    if (!animationObject(source)) return null;
    const cached = animationAliasSnapshots.get(source);
    if (cached !== undefined) return cached;
    const result = Object.create(null);
    for (const [name, value] of Object.entries(source)) {
        if (!animationText(name)) return null;
        const action = typeof value === 'string' ? {path: value} : value;
        if (!animationObject(action) || !animationText(action.path) ||
            !Object.keys(action).every(key => key === 'path' || key === 'retargetProfile') ||
            (action.retargetProfile !== undefined && !animationText(action.retargetProfile))) return null;
        result[name] = Object.freeze({path: action.path, profile: action.retargetProfile || ''});
    }
    return Object.freeze(result);
}
function animationValidateDefinition(definition, aliases) {
    if (!definition.indices.has(definition.initialState)) return false;
    for (const state of definition.states)
        if (!Object.prototype.hasOwnProperty.call(aliases, animationStates.get(state).alias)) return false;
    return definition.transitions.every(transition => definition.indices.has(transition.to) &&
        (transition.from === null || definition.indices.has(transition.from)));
}
)js"
R"js(function animationView(record, phase, snapshot, values, versions, state, stateTime, delta, reads) {
    const view = animationContexts.get(record.context);
    Object.assign(view, {open: true, phase, snapshot, values, versions, state, stateTime,
        delta, readTriggers: reads});
    return view;
}
function animationInvoke(record, phase, snapshot, values = record.values.slice(),
                         versions = record.versions.slice(), stateTime = record.stateTime) {
    const state = record.definition.states[record.stateIndex];
    const view = animationView(record, phase, snapshot, values, versions,
        animationStates.get(state), stateTime, phase === 'update' ? record.delta : 0, null);
    ++animationCallbackDepth;
    try {
        const method = phase === 'enter' ? 'OnEnter' : phase === 'exit' ? 'OnExit' : 'OnUpdate';
        animationSynchronous(state[method](record.context));
    } finally { view.open = false; --animationCallbackDepth; }
}
function animationPlay(record, stateIndex, seconds) {
    const state = animationStates.get(record.definition.states[stateIndex]);
    const action = record.aliases[state.alias], h = handles.get(record.animator);
    return native.AnimatorPlayAnimation(h.scene, h.node, action.path, action.profile,
        seconds, state.speed, state.loop, true);
}
function animationMachineBind(animator, source, aliasesSource) {
    if (animationCallbackDepth !== 0) return false;
    const before = animationSnapshot(animator), definition = animationDefinitions.get(source);
    if (before === null || definition === undefined) return false;
    const previous = animationRecord(animator);
    if (previous !== null && previous.source === source && previous.aliasSource === aliasesSource) return true;
    const aliases = animationAliases(aliasesSource);
    if (aliases === null || !animationValidateDefinition(definition, aliases)) return false;
    const record = {animator, source, definition, aliases, aliasSource: aliasesSource,
        values: [...definition.parameters.values()].map(value => value.defaultValue),
        versions: Array(definition.parameters.size).fill(0),
        stateIndex: definition.indices.get(definition.initialState), stateTime: 0,
        paused: false, resumePlaying: false, alive: true, delta: 0, playbackId: 0n,
        context: Object.freeze(new AnimationStateContext(animationContextKey))};
    animationContexts.set(record.context, {record, open: false});
    for (const state of definition.states) animationFreeze(state);
    for (const transition of definition.transitions) animationFreeze(transition.condition);
    animationFreeze(source);
    if (!animationPlay(record, record.stateIndex, 0)) return false;
    if (previous !== null) {
        animationInvoke(previous, 'exit', before);
        previous.alive = false;
    }
    const snapshot = animationSnapshot(animator);
    if (snapshot === null) { animationMachines.delete(animationMachineKey(animator)); return false; }
    definition.sealed = true;
    definition.transitions.sort((a, b) => b.priority - a.priority || a.order - b.order);
    animationAliasSnapshots.set(aliasesSource, aliases);
    record.playbackId = snapshot.playbackId;
    animationMachines.set(animationMachineKey(animator), record);
    animationInvoke(record, 'enter', snapshot);
    return true;
}
function animationMachineOwns(animator) { return animationRecord(animator) !== null; }
function animationMachineClear(animator) {
    if (animationCallbackDepth !== 0 || animationSnapshot(animator) === null) return false;
    const record = animationRecord(animator);
    if (record === null) return true;
    try { animationInvoke(record, 'exit', animationSnapshot(animator)); }
    finally { record.alive = false; animationMachines.delete(animationMachineKey(animator)); }
    return true;
}
function animationMachineSetParameters(animator, values) {
    const record = animationRecord(animator);
    if (animationConditionDepth !== 0 || record === null || !animationObject(values)) return false;
    const changes = [];
    for (const [name, value] of Object.entries(values)) {
        const parameter = record.definition.parameters.get(name);
        if (parameter === undefined || parameter.type === 'trigger' ||
            !animationParameterValue(parameter.type, value)) return false;
        changes.push([parameter.index, value]);
    }
    for (const [index, value] of changes) record.values[index] = value;
    return true;
}
function animationMachineTrigger(animator, name, reset = false) {
    const record = animationRecord(animator);
    if (animationConditionDepth !== 0 || record === null) return false;
    const parameter = record.definition.parameters.get(name);
    if (parameter === undefined || parameter.type !== 'trigger' ||
        record.versions[parameter.index] === Number.MAX_SAFE_INTEGER) return false;
    ++record.versions[parameter.index];
    record.values[parameter.index] = !reset;
    return true;
}
function animationMachinePause(animator, value) {
    const record = animationRecord(animator), snapshot = animationSnapshot(animator);
    if (animationCallbackDepth !== 0 || record === null || snapshot === null ||
        typeof value !== 'boolean') return false;
    if (record.paused === value) return true;
    const h = handles.get(animator);
    if (value) {
        record.resumePlaying = snapshot.playback === 'playing';
        if (record.resumePlaying && !native.AnimatorPause(h.scene, h.node)) return false;
    } else if (record.resumePlaying && !native.AnimatorPlay(h.scene, h.node)) return false;
    record.paused = value;
    return true;
}
function animationMachineState(animator) {
    const record = animationRecord(animator);
    return record === null ? null : animationStates.get(record.definition.states[record.stateIndex]).name;
}
function animationMachinePaused(animator) { return animationRecord(animator)?.paused ?? null; }
function animationMachineTransitioning(animator) {
    return animationRecord(animator) !== null && animationSnapshot(animator)?.transitioning === true;
}
function animationEvaluate(record, transition, snapshot, values, versions) {
    const reads = new Map();
    const state = animationStates.get(record.definition.states[record.stateIndex]);
    const view = animationView(record, 'condition', snapshot, values, versions,
        state, record.stateTime, record.delta, reads);
    ++animationCallbackDepth;
    ++animationConditionDepth;
    try {
        const condition = transition.condition;
        const accepted = animationSynchronous(typeof condition === 'function'
            ? condition(record.context) : condition.Test(record.context));
        if (typeof accepted !== 'boolean') throw new TypeError('Animation conditions must return boolean');
        return accepted ? reads : null;
    } finally { view.open = false; --animationConditionDepth; --animationCallbackDepth; }
}
export function __updateAnimationStateMachines() {
    const delta = Time.deltaTime;
    for (const [identity, record] of [...animationMachines]) {
        if (!record.alive || animationMachines.get(identity) !== record) continue;
        let snapshot = animationSnapshot(record.animator);
        if (snapshot === null) { record.alive = false; animationMachines.delete(identity); continue; }
        if (record.paused || !snapshot.active || snapshot.worldPaused) continue;
        if (snapshot.playbackId !== record.playbackId)
            throw new Error('Animator playback changed outside its bound state machine');
        record.delta = delta;
        record.stateTime += delta;
        const values = record.values.slice(), versions = record.versions.slice();
        const state = animationStates.get(record.definition.states[record.stateIndex]);
        for (const transition of record.definition.transitions) {
            if ((transition.from !== null && transition.from !== state.name) ||
                (!transition.reenter && transition.to === state.name) ||
                (!transition.interrupt && snapshot.transitioning) ||
                (transition.exitTime !== undefined && snapshot.normalizedTime < transition.exitTime)) continue;
            const reads = animationEvaluate(record, transition, snapshot, values, versions);
            if (reads === null) continue;
            if (animationSnapshot(record.animator) === null) break;
            const destination = record.definition.indices.get(transition.to);
            if (!animationPlay(record, destination, transition.seconds)) break;
            animationInvoke(record, 'exit', snapshot, values, versions);
            snapshot = animationSnapshot(record.animator);
            if (snapshot === null) break;
            for (const [index, version] of reads) {
                if (record.versions[index] === version) record.values[index] = false;
                values[index] = false;
            }
            record.stateIndex = destination;
            record.stateTime = 0;
            record.playbackId = snapshot.playbackId;
            animationInvoke(record, 'enter', snapshot, values, versions);
            break;
        }
        snapshot = animationSnapshot(record.animator);
        if (snapshot !== null) animationInvoke(record, 'update', snapshot, values, versions);
        else { record.alive = false; animationMachines.delete(identity); }
    }
}
function clearAnimationStateMachines() {
    let failed = false, failure;
    try {
        for (const record of animationMachines.values()) {
            try {
                const snapshot = animationSnapshot(record.animator);
                if (snapshot !== null) animationInvoke(record, 'exit', snapshot);
            } catch (error) { if (!failed) { failed = true; failure = error; } }
            finally { record.alive = false; }
        }
    } finally { animationMachines.clear(); }
    if (failed) throw failure;
}
bindingCleanupHooks.push(clearAnimationStateMachines);
Object.freeze(AnimationStateMachine.prototype);
Object.freeze(AnimationStateMachine);
Object.freeze(AnimationState.prototype);
Object.freeze(AnimationState);
Object.freeze(AnimationCondition.prototype);
Object.freeze(AnimationCondition);
Object.freeze(AnimationStateContext.prototype);
Object.freeze(AnimationStateContext);
)js";
} // namespace

std::string_view AnimationStateMachineBinding::name() const noexcept
{
    return "AnimationStateMachine";
}

std::string_view AnimationStateMachineBinding::source() const noexcept
{
    return javascript;
}

bool AnimationStateMachineBinding::register_functions(vm::ScriptVm&) const
{
    return true;
}
} // namespace engine::plugins::script::bindings
