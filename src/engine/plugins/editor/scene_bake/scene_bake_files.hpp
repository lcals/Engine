#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine
{
class ResourceSystem;
}
namespace engine::plugins::scene_bake
{
struct BakeOutput
{
    std::string path;
    std::string guid;
};

class SceneBakeFiles final
{
public:
    [[nodiscard]] bool initialize(const std::filesystem::path& root, std::string_view scene,
                                  std::string_view input, std::string_view job,
                                  ResourceSystem& resources, std::string& error);
    [[nodiscard]] bool unchanged(ResourceSystem& resources, std::string& error) const;
    [[nodiscard]] bool stage(const std::array<std::vector<std::uint8_t>, 3>& images,
                             std::string& error);
    [[nodiscard]] bool publish(const std::function<bool(std::string&)>& refresh,
                               std::string& error);
    [[nodiscard]] const std::array<BakeOutput, 3>& outputs() const noexcept;
    void cleanup() noexcept;

private:
    struct File
    {
        std::filesystem::path target;
        std::filesystem::path staged;
        std::filesystem::path backup;
        std::string fingerprint;
        bool existed = false;
        bool moved = false;
        bool installed = false;
    };
    [[nodiscard]] bool rollback(std::string& error);
    std::filesystem::path root_;
    std::filesystem::path staging_;
    std::filesystem::path scene_;
    std::filesystem::path input_;
    std::string input_fingerprint_;
    std::string input_meta_fingerprint_;
    std::string input_guid_;
    std::string input_content_hash_;
    std::array<BakeOutput, 3> outputs_;
    std::vector<File> files_;
};
} // namespace engine::plugins::scene_bake
