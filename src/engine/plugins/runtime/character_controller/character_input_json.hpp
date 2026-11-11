#pragma once
#include "plugins/runtime/character_controller/character_input.hpp"
#include "runtime/schema/control_json_generated.hpp"
namespace engine::plugins::character_controller::detail
{
inline bool read_input(const nlohmann::json& j, CharacterInput& output, std::string& error)
{
    if (!j.is_object() || j.size() != 5 || !j.contains("move") || !j.contains("lookDelta") ||
        !j.contains("lookRate") || !j.contains("toggleView") || !j.contains("actions"))
    {
        return schema::control_json::reject(error, "input");
    }
    const auto tuple = [&](const char* name, Vec2& out)
    {
        const auto& value = j[name];
        double x = 0, y = 0;
        if (!value.is_array() || value.size() != 2 ||
            !schema::control_json::read(value[0], x, error, true, name) ||
            !schema::control_json::read(value[1], y, error, true, name))
        {
            return false;
        }
        out = {x, y};
        return true;
    };
    CharacterInput candidate;
    if (!tuple("move", candidate.move) || !tuple("lookDelta", candidate.look_delta) ||
        !tuple("lookRate", candidate.look_rate) || !j["toggleView"].is_boolean() ||
        !j["actions"].is_object())
    {
        return schema::control_json::reject(error, "input values");
    }
    candidate.toggle_view = j["toggleView"].get<bool>();
    for (auto i = j["actions"].begin(); i != j["actions"].end(); ++i)
    {
        const auto& v = i.value();
        if (!v.is_object() || v.size() != 2 || !v.contains("held") || !v.contains("pressed") ||
            !v["held"].is_boolean() || !v["pressed"].is_boolean())
        {
            return schema::control_json::reject(error, "input.actions." + i.key());
        }
        candidate.actions.push_back({i.key(), v["held"].get<bool>(), v["pressed"].get<bool>()});
    }
    output = std::move(candidate);
    return true;
}
inline nlohmann::json input_schema()
{
    using Json = nlohmann::json;
    const Json pair = {
        {"type", "array"}, {"minItems", 2}, {"maxItems", 2}, {"items", {{"type", "number"}}}};
    const Json action = {
        {"type", "object"},
        {"additionalProperties", false},
        {"required", Json::array({"held", "pressed"})},
        {"properties", {{"held", {{"type", "boolean"}}}, {"pressed", {{"type", "boolean"}}}}}};
    const Json properties = {{"move", pair},
                             {"lookDelta", pair},
                             {"lookRate", pair},
                             {"toggleView", {{"type", "boolean"}}},
                             {"actions", {{"type", "object"}, {"additionalProperties", action}}}};
    return {{"type", "object"},
            {"additionalProperties", false},
            {"required", Json::array({"move", "lookDelta", "lookRate", "toggleView", "actions"})},
            {"properties", properties}};
}
} // namespace engine::plugins::character_controller::detail
