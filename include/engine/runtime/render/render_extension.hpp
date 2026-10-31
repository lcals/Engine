#pragma once

namespace engine
{

struct RenderContext;
class RenderSystem;

// 主线程借用登记；回调期间不能改变登记或销毁扩展。
class RenderExtension
{
public:
    virtual ~RenderExtension() noexcept;
    RenderExtension(const RenderExtension&) = delete;
    RenderExtension& operator=(const RenderExtension&) = delete;
    RenderExtension(RenderExtension&&) = delete;
    RenderExtension& operator=(RenderExtension&&) = delete;

    [[nodiscard]] virtual bool draw(const RenderContext& context) = 0;
    // 清理只能访问自身状态；场景节点此时可能已经销毁。
    virtual void release_scene() noexcept = 0;
    // 在 GPU 关闭前调用，须支持重复调用和未创建 GPU 对象的状态。
    virtual void release_gpu() noexcept = 0;

protected:
    RenderExtension() noexcept = default;

private:
    friend class RenderSystem;
    // 双向登记由核心维护；任一方先析构都会解除，插件无需保留渲染器借用。
    RenderSystem* renderer_ = nullptr;
};

} // namespace engine
