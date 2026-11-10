#include "runtime/render/render_extension.hpp"

#include "runtime/render/render_system.hpp"

namespace engine
{

SceneWorkState RenderExtension::prepare_scope(SceneScopeId, const SceneWorkBudget&)
{
    return SceneWorkState::not_required;
}
bool RenderExtension::scope_status(SceneScopeId scope, ScenePreparationSnapshot& output) const
{
    output = {};
    output.scope = scope;
    output.state = SceneWorkState::not_required;
    return true;
}
void RenderExtension::release_scope(SceneScopeId) noexcept
{
}

RenderExtension::~RenderExtension() noexcept
{
    // 派生类负责在自身析构中释放资源；基类只能撤销登记，不再调用虚函数。
    if (renderer_ != nullptr)
    {
        renderer_->detach_extension(*this);
    }
}

} // namespace engine
