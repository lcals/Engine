#include "resource_system.hpp"

#include <basisu/transcoder/basisu_transcoder.h>
#include <flatbuffers/idl.h>

namespace engine
{

ResourceSystem::ResourceSystem() noexcept = default;

ResourceSystem::~ResourceSystem() noexcept
{
    shutdown();
}

bool ResourceSystem::on_initialize(const EngineConfig&)
{
    // 转码查找表属于 Basis Universal，不需要单独销毁，重复初始化安全。
    basist::basisu_transcoder_init();

    // 以私有最小 schema 验证解析入口；该探针不定义正式资源格式。
    flatbuffers::Parser parser;
    return parser.Parse("table DependencyProbe { value:uint; } root_type DependencyProbe;");
}

void ResourceSystem::on_shutdown() noexcept
{
}

} // namespace engine
