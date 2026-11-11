#include "plugins/runtime/camera/camera_state_machine.hpp"
namespace engine::plugins::camera
{
bool CameraStateMachine::select(const schema::CameraStateMachineDataT& data,
                                const CameraMachineRuntime& current,
                                const CameraEvaluationContext& context,
                                const CameraTypeRegistry& types, CameraTransitionRequest& out) const
{
    out = {};
    for (auto index : context.outgoing_transitions)
    {
        if (index >= data.transitions.size())
        {
            return false;
        }
        const auto* t = &data.transitions[index];
        if ((!t->reenter && t->to == current.state_id) ||
            ((current.blend_duration > current.blend_seconds) && !t->interrupt) ||
            current.state_seconds < t->min_state_seconds)
        {
            continue;
        }
        for (const auto& group : t->any_of)
        {
            bool matched = true;
            std::vector<std::string> triggers;
            for (const auto& ref : group.all_of)
            {
                const auto* ops = types.condition(ref.type);
                CameraConditionMatch result;
                if (!ops || !ops->evaluate(data, ref.id, context, result))
                {
                    return false;
                }
                if (!result.matched)
                {
                    matched = false;
                    break;
                }
                for (const auto& name : result.read_triggers)
                {
                    if (std::find(triggers.begin(), triggers.end(), name) == triggers.end())
                    {
                        triggers.push_back(name);
                    }
                }
            }
            if (matched)
            {
                out.selected = true;
                out.transition_id = t->id;
                out.from = current.state_id;
                out.to = t->to;
                out.consumed_triggers = std::move(triggers);
                return true;
            }
        }
    }
    return true;
}
void CameraStateMachine::commit(const CameraTransitionRequest& request,
                                CameraMachineRuntime& next) const
{
    if (request.selected)
    {
        next.source_state_id = request.from;
        next.source_scope = next.state_scope;
        next.state_scope = request.to + "@" + std::to_string(++next.entry_sequence);
        next.state_id = request.to;
        next.transition_id = request.transition_id;
        next.state_seconds = 0;
        next.blend_seconds = 0;
    }
}
} // namespace engine::plugins::camera
