#include "runtime/render/render_extension.hpp"

#include "runtime/render/render_system.hpp"

namespace engine
{

RenderExtension::~RenderExtension() noexcept
{
    // 派生类负责在自身析构中释放资源；基类只能撤销登记，不再调用虚函数。
    if (renderer_ != nullptr)
    {
        renderer_->detach_extension(*this);
    }
}

} // namespace engine
