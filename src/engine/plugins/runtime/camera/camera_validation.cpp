#include "plugins/runtime/camera/camera_math.hpp"
#include "plugins/runtime/camera/camera_runtime.hpp"
#include <set>
namespace engine::plugins::camera
{
bool valid_parameter_value(const schema::CameraParameterDataT& d,
                           const schema::CameraParameterValueT& v)
{
    if (d.name != v.name || d.type != v.type)
    {
        return false;
    }
    double n = 0;
    switch (v.type)
    {
    case schema::CameraParameterType::float_value:
        if (!std::isfinite(v.float_value) || v.int_value || v.bool_value)
        {
            return false;
        }
        n = v.float_value;
        break;
    case schema::CameraParameterType::int_value:
        if (v.float_value != 0 || v.bool_value)
        {
            return false;
        }
        n = v.int_value;
        break;
    case schema::CameraParameterType::bool_value:
    case schema::CameraParameterType::trigger:
        if (v.float_value != 0 || v.int_value)
        {
            return false;
        }
        n = v.bool_value ? 1 : 0;
        break;
    default:
        return false;
    }
    return !d.has_range || (n >= d.minimum && n <= d.maximum);
}
bool CameraController::valid_settings(const Data& d, const CameraTypeRegistry& types,
                                      std::string& error)
{
    using namespace detail;
    error.clear();
    if (!types.frozen())
    {
        return fail(error, "types: registry is not frozen");
    }
    const bool empty = d.parameters.empty() && d.targets.empty() && d.rigs.empty() &&
                       d.output_modules.empty() && !d.module_data;
    if (empty && (!d.machine || (d.machine->states.empty() && d.machine->initial_state.empty() &&
                                 d.machine->transitions.empty() && !d.machine->condition_data)))
    {
        return true;
    }
    if (!d.machine || d.machine->states.empty() || !d.module_data)
    {
        return fail(error, "machine: complete initial state, rigs and module_data required");
    }
    std::set<std::string> names;
    for (const auto& p : d.parameters)
    {
        if (p.name.empty() || !names.insert(p.name).second || !p.default_value ||
            !valid_parameter_value(p, *p.default_value))
        {
            return fail(error,
                        "parameters." + p.name + ": duplicate, missing default or incorrect value");
        }
        if (p.lifetime < schema::CameraParameterLifetime::MIN ||
            p.lifetime > schema::CameraParameterLifetime::MAX)
        {
            return fail(error, "parameters." + p.name + ": lifetime");
        }
        if (p.has_range &&
            (!std::isfinite(p.minimum) || !std::isfinite(p.maximum) || p.minimum > p.maximum ||
             (p.type != schema::CameraParameterType::float_value &&
              p.type != schema::CameraParameterType::int_value)))
        {
            return fail(error, "parameters." + p.name + ": range");
        }
        if ((p.type == schema::CameraParameterType::trigger &&
             (p.default_value->bool_value ||
              p.lifetime == schema::CameraParameterLifetime::frame)) ||
            (p.lifetime == schema::CameraParameterLifetime::frame &&
             (p.default_value->float_value != 0 || p.default_value->int_value != 0 ||
              p.default_value->bool_value)))
        {
            return fail(error, "parameters." + p.name + ": transient default");
        }
    }
    names.clear();
    for (const auto& t : d.targets)
    {
        if (t.name.empty() || !names.insert(t.name).second)
        {
            return fail(error, "targets." + t.name + ": duplicate or empty name");
        }
    }
    std::set<std::uint64_t> ids;
    const auto validate_collection = [&](const auto& values, std::string_view type)
    {
        const auto* ops = types.module(type);
        for (const auto& v : values)
        {
            if (!v.id || !ids.insert(v.id).second || !ops)
            {
                return fail(error, "module_data." + std::string(type) +
                                       ": duplicate id or unregistered type");
            }
            if (!ops->validate(d, v.id, error))
            {
                error =
                    "module_data." + std::string(type) + "[" + std::to_string(v.id) + "]." + error;
                return false;
            }
        }
        return true;
    };
    const auto& m = *d.module_data;
    if (!validate_collection(m.fixed, "fixed") || !validate_collection(m.follow, "follow") ||
        !validate_collection(m.orbit, "orbit") || !validate_collection(m.aim, "aim") ||
        !validate_collection(m.lens, "lens") || !validate_collection(m.noise, "noise") ||
        !validate_collection(m.collision, "collision") || !validate_collection(m.bounds, "bounds"))
    {
        return false;
    }
    std::map<std::string, schema::Projection> rigs;
    const auto validate_order = [&](const auto& refs, bool output)
    {
        int previous = -1;
        std::set<std::uint64_t> used;
        bool position = false, lens = false;
        for (const auto& r : refs)
        {
            const auto* ops = types.module(r.type);
            if (!ops || !used.insert(r.id).second || !ops->validate(d, r.id, error))
            {
                return fail(error, "module reference " + r.type + ": " + error);
            }
            if (!r.enabled)
            {
                continue;
            }
            const int stage = int(ops->stage);
            if (stage < previous || (output ? (stage < int(CameraModuleStage::modifier))
                                            : (stage > int(CameraModuleStage::aim))))
            {
                return fail(error, "modules: invalid stage order");
            }
            previous = stage;
            position |= ops->stage == CameraModuleStage::position;
            lens |= ops->stage == CameraModuleStage::lens;
        }
        return output || (position && lens) || fail(error, "rig: lens and position are required");
    };
    for (const auto& rig : d.rigs)
    {
        if (rig.id.empty() || rigs.contains(rig.id) || !validate_order(rig.modules, false))
        {
            return fail(error, "rigs." + rig.id + ": " + error);
        }
        schema::Projection projection = schema::Projection::perspective;
        for (const auto& ref : rig.modules)
        {
            if (ref.enabled && ref.type == "lens")
            {
                for (const auto& lens : m.lens)
                {
                    if (lens.id == ref.id)
                    {
                        projection = lens.projection;
                    }
                }
            }
        }
        rigs.emplace(rig.id, projection);
    }
    if (!validate_order(d.output_modules, true))
    {
        return false;
    }
    std::map<std::string, std::string> states;
    for (const auto& state : d.machine->states)
    {
        if (state.id.empty() || !states.emplace(state.id, state.rig).second ||
            !rigs.contains(state.rig))
        {
            return fail(error, "states." + state.id + ": duplicate or unknown rig");
        }
    }
    if (!states.contains(d.machine->initial_state))
    {
        return fail(error, "machine.initial_state: unknown state");
    }
    names.clear();
    for (const auto& t : d.machine->transitions)
    {
        if (t.id.empty() || !names.insert(t.id).second ||
            (!t.from.empty() && !states.contains(t.from)) || !states.contains(t.to) || !t.blend ||
            !bounded(t.min_state_seconds, 0, 1e9) || !bounded(t.blend->seconds, 0, 1e9) ||
            t.blend->curve < schema::CameraBlendCurve::MIN ||
            t.blend->curve > schema::CameraBlendCurve::MAX ||
            t.source_mode < schema::CameraBlendSource::MIN ||
            t.source_mode > schema::CameraBlendSource::MAX || t.any_of.empty())
        {
            return fail(error,
                        "transitions." + t.id + ": invalid identity, state, blend or conditions");
        }
        if (t.blend->seconds > 0)
        {
            for (const auto& [state, rig] : states)
            {
                if ((t.from.empty() || t.from == state) && rigs.at(rig) != rigs.at(states.at(t.to)))
                {
                    return fail(error,
                                "transitions." + t.id + ": projection changes require a cut");
                }
            }
        }
        for (const auto& g : t.any_of)
        {
            for (const auto& ref : g.all_of)
            {
                const auto* ops = types.condition(ref.type);
                if (!ref.id || !ops || !ops->validate(d, ref.id, error))
                {
                    return fail(error, "transitions." + t.id + ".conditions: " + error);
                }
            }
        }
    }
    if (d.machine->condition_data)
    {
        ids.clear();
        const auto check = [&](const auto& values, std::string_view type)
        {
            const auto* ops = types.condition(type);
            for (const auto& v : values)
            {
                if (!v.id || !ids.insert(v.id).second || !ops || !ops->validate(d, v.id, error))
                {
                    return false;
                }
            }
            return true;
        };
        const auto& c = *d.machine->condition_data;
        if (!check(c.parameter_tests, "parameter") || !check(c.trigger_tests, "trigger") ||
            !check(c.target_tests, "target") || !check(c.time_tests, "state_time"))
        {
            return fail(error, "condition_data: duplicate identity or " + error);
        }
    }
    return true;
}
} // namespace engine::plugins::camera
