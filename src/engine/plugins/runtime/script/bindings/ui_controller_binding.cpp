#include "plugins/runtime/script/bindings/ui_controller_binding.hpp"

namespace engine::plugins::script::bindings
{
namespace
{
constexpr std::string_view javascript = R"js(
const uiControllerStates = new WeakMap();
const uiControllers = new Set();
function uiControllerValid(state) {
    return state !== undefined && state.document !== 0n &&
        uiViewDocument(state.view) === state.document;
}
function uiUnbind(controller) {
    const state = uiControllerStates.get(controller);
    if (state === undefined || state.unbinding) return;
    const wasBound = state.document !== 0n;
    state.document = 0n;
    uiControllers.delete(controller);
    for (const subscription of state.subscriptions) uiSubscriptions.delete(subscription);
    state.subscriptions.clear();
    if (!wasBound) return;
    state.unbinding = true;
    try { controller.OnUnbind(); }
    finally { state.unbinding = false; }
}
function uiPruneSubscriptions() {
    for (const controller of [...uiControllers]) {
        if (!uiControllerValid(uiControllerStates.get(controller))) uiUnbind(controller);
    }
    for (const [id, subscription] of uiSubscriptions) {
        if (!subscription.element.isValid) uiSubscriptions.delete(id);
    }
}
export class UiController {
    constructor(view, model) {
        uiControllerStates.set(this, {
            view, model, document: 0n, subscriptions: new Set(),
            binding: false, refreshing: false, unbinding: false
        });
    }
    get view() { return uiControllerStates.get(this)?.view; }
    get model() { return uiControllerStates.get(this)?.model; }
    get bound() { return uiControllerValid(uiControllerStates.get(this)); }
    Bind() {
        const state = uiControllerStates.get(this);
        if (state === undefined || state.binding || state.refreshing || state.unbinding) return false;
        if (uiControllerValid(state)) return true;
        uiUnbind(this);
        const document = uiViewDocument(state.view);
        if (document === 0n) return false;
        state.document = document;
        state.binding = true;
        uiControllers.add(this);
        let success = false;
        try {
            if (this.OnBind() !== true || !uiControllerValid(state)) return false;
            if (this.OnRefresh() !== true || !uiControllerValid(state)) return false;
            success = true;
            return true;
        } finally {
            state.binding = false;
            if (!success) uiUnbind(this);
        }
    }
    Refresh() {
        const state = uiControllerStates.get(this);
        if (state === undefined || state.binding || state.refreshing || state.unbinding) return false;
        if (!uiControllerValid(state)) { uiUnbind(this); return false; }
        state.refreshing = true;
        try { return this.OnRefresh() === true && uiControllerValid(state); }
        finally {
            state.refreshing = false;
            if (!uiControllerValid(state)) uiUnbind(this);
        }
    }
    Listen(element, type, callback) {
        const state = uiControllerStates.get(this);
        const target = handles.get(element), view = handles.get(state?.view);
        if (!uiControllerValid(state) || !(element instanceof UiElement) ||
            target === undefined || view === undefined || target.scene !== view.scene ||
            target.node !== view.node || target.document !== state.document) return false;
        const subscription = element.On(type, callback);
        if (subscription === 0) return false;
        state.subscriptions.add(subscription);
        return true;
    }
    Unbind() { uiUnbind(this); }
    OnBind() { return true; }
    OnRefresh() { return true; }
    OnUnbind() {}
}
// 只由 ScriptSystem 在当前 Run 的可调用作用域中使用；不进入公开类型声明。
function dispatchUiEvents() {
    uiPruneSubscriptions();
    for (let count = 0; count < 256; ++count) {
        const event = native.UiPollEvent();
        if (event === null) break;
        const target = new UiElement(key, event.scene, event.node, event.document, event.element);
        if (!target.isValid) continue;
        const subscriptions = [...uiSubscriptions];
        for (const entry of event.route) {
            if (!target.isValid) break;
            const ancestor = new UiElement(key, event.scene, event.node,
                entry.document, entry.element);
            if (!ancestor.isValid) continue;
            for (const [id, subscription] of subscriptions) {
                if (!target.isValid || !ancestor.isValid) break;
                if (uiSubscriptions.get(id) !== subscription || subscription.type !== event.type ||
                    !uiSameElement(subscription.element, ancestor)) continue;
                const value = Object.freeze({
                    type: event.type, target, currentTarget: subscription.element,
                    value: event.value
                });
                const result = subscription.callback(value);
                if (result instanceof Promise) {
                    throw new TypeError("UI event callbacks must be synchronous");
                }
                uiPruneSubscriptions();
            }
        }
        // 场景切换或文档替换后，本次派发不继续消费旧事件批次。
        if (!target.isValid) break;
    }
    uiPruneSubscriptions();
}
function clearUiControllers() {
    let failed = false, failure;
    try {
        for (const controller of [...uiControllers]) {
            try { uiUnbind(controller); }
            catch (error) { if (!failed) { failed = true; failure = error; } }
        }
    } finally {
        // 某个项目钩子抛错也不能留下其他控制器和普通 On 订阅。
        for (const controller of uiControllers) {
            const state = uiControllerStates.get(controller);
            state.document = 0n;
            state.subscriptions.clear();
        }
        uiControllers.clear();
        uiSubscriptions.clear();
    }
    if (failed) throw failure;
}
bindingEventHooks.push(dispatchUiEvents);
bindingCleanupHooks.push(clearUiControllers);
)js";
} // namespace

std::string_view UiControllerBinding::name() const noexcept
{
    return "UiController";
}

std::string_view UiControllerBinding::source() const noexcept
{
    return javascript;
}

bool UiControllerBinding::register_functions(vm::ScriptVm&) const
{
    return true;
}

} // namespace engine::plugins::script::bindings
