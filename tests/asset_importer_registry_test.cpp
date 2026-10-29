#include "editor/asset_pipeline/asset_importer_registry.hpp"

#include <cstdio>
#include <memory>

namespace
{
using namespace engine;
using namespace engine::editor::asset_pipeline;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "%s\n", message);
    }
    return value;
}

class TestImporter : public AssetImporter
{
public:
    explicit TestImporter(schema::AssetKind kind) noexcept : kind_(kind)
    {
    }
    [[nodiscard]] schema::AssetKind kind() const noexcept override
    {
        return kind_;
    }
    [[nodiscard]] bool matches(const std::filesystem::path& source) const override
    {
        return source.extension() == ".custom";
    }
    [[nodiscard]] bool convert(const ImportAsset&, const ImportContext&,
                               std::string&) const override
    {
        return false;
    }

private:
    schema::AssetKind kind_;
};

class CustomImporter final : public TestImporter
{
public:
    CustomImporter() noexcept : TestImporter(schema::AssetKind::mesh)
    {
    }
};
class AlternateMeshImporter final : public TestImporter
{
public:
    AlternateMeshImporter() noexcept : TestImporter(schema::AssetKind::mesh)
    {
    }
    [[nodiscard]] std::string_view id() const noexcept override
    {
        return "alternate_mesh";
    }
    [[nodiscard]] bool matches(const std::filesystem::path& source) const override
    {
        return source.extension() == ".alternate";
    }
};
} // namespace

int main()
{
    AssetImporterRegistry registry;
    if (!expect(registry.select("mesh.custom") == nullptr, "empty registry has no importer") ||
        !expect(registry.initialize(), "default importers register") ||
        !expect(!registry.initialize(), "initialization cannot duplicate the registry"))
    {
        return 1;
    }
    const struct
    {
        const char* path;
        schema::AssetKind kind;
    } cases[] = {{"level.scene", schema::AssetKind::scene},
                 {"model.MESH", schema::AssetKind::mesh},
                 {"surface.material", schema::AssetKind::material},
                 {"program.shader", schema::AssetKind::shader},
                 {"albedo.JPEG", schema::AssetKind::texture},
                 {"sound.WAV", schema::AssetKind::audio},
                 {"logic.ts", schema::AssetKind::script},
                 {"types.d.ts", schema::AssetKind::source},
                 {"custom.blob", schema::AssetKind::source}};
    for (const auto& item : cases)
    {
        const auto* importer = registry.select(item.path);
        if (!expect(importer != nullptr && importer == registry.find(item.kind),
                    "source selects its registered type"))
        {
            return 1;
        }
    }
    if (!expect(registry.find(schema::AssetKind::script)->module_path("scripts/entry.ts") ==
                    "scripts/entry.js",
                "script owns module path mapping") ||
        !expect(registry.find(schema::AssetKind::source)->module_path("text.txt").empty(),
                "ordinary sources do not become script modules") ||
        !expect(registry.find(schema::AssetKind::texture)->default_meta().find("[texture]") !=
                    std::string_view::npos,
                "texture supplies its own import settings") ||
        !expect(registry.find(schema::AssetKind::mesh)->default_meta().empty(),
                "mesh does not inherit texture settings"))
    {
        return 1;
    }

    AssetImporterRegistry extension;
    std::unique_ptr<AssetImporter> fallback =
        std::make_unique<TestImporter>(schema::AssetKind::source);
    if (!expect(extension.add(fallback, true) && !fallback, "fallback ownership transfers") ||
        !expect(extension.add<CustomImporter>(), "typed registration extends the dispatcher") ||
        !expect(extension.select("object.custom") == extension.find(schema::AssetKind::mesh),
                "specific importer wins even when fallback registered first") ||
        !expect(extension.select("unknown.bin") == extension.find(schema::AssetKind::source),
                "fallback handles unmatched source"))
    {
        return 1;
    }
    std::unique_ptr<AssetImporter> duplicate = std::make_unique<CustomImporter>();
    std::unique_ptr<AssetImporter> second_fallback =
        std::make_unique<TestImporter>(schema::AssetKind::texture);
    std::unique_ptr<AssetImporter> empty;
    if (!expect(!extension.add(duplicate) && duplicate != nullptr,
                "duplicate type fails without consuming ownership") ||
        !expect(!extension.add(second_fallback, true) && second_fallback != nullptr,
                "second fallback fails without consuming ownership") ||
        !expect(!extension.add(empty), "null implementations are rejected") ||
        !expect(extension.add(second_fallback), "failed registration leaves registry usable") ||
        !expect(extension.select("object.custom") == nullptr,
                "ambiguous type matches fail instead of depending on order"))
    {
        return 1;
    }
    const auto* previous = extension.find(schema::AssetKind::mesh);
    std::unique_ptr<AssetImporter> unknown =
        std::make_unique<TestImporter>(schema::AssetKind::scene);
    std::unique_ptr<AssetImporter> invalid =
        std::make_unique<TestImporter>(static_cast<schema::AssetKind>(255));
    if (!expect(!extension.replace(empty), "replacement rejects null implementations") ||
        !expect(!extension.replace(unknown) && unknown,
                "replacement rejects unregistered kinds without consuming ownership") ||
        !expect(!extension.replace(invalid) && invalid,
                "replacement rejects invalid kinds without consuming ownership") ||
        !expect(extension.find(schema::AssetKind::mesh) == previous,
                "failed replacement preserves the existing implementation") ||
        !expect(extension.replace(duplicate) && !duplicate,
                "replacement consumes ownership after matching registered kind") ||
        !expect(extension.find(schema::AssetKind::mesh) != previous,
                "replacement publishes the new implementation"))
    {
        return 1;
    }
    fallback = std::make_unique<TestImporter>(schema::AssetKind::source);
    auto* next_fallback = fallback.get();
    if (!expect(extension.replace(fallback) && !fallback, "fallback can be replaced explicitly") ||
        !expect(extension.select("unknown.bin") == next_fallback,
                "replacement refreshes the fallback pointer"))
    {
        return 1;
    }
    extension.clear();
    if (!expect(extension.find(schema::AssetKind::mesh) == nullptr &&
                    extension.select("unknown.bin") == nullptr,
                "clear removes implementations and fallback") ||
        !expect(extension.initialize(), "cleared registry can restore defaults"))
    {
        return 1;
    }
    AssetImporterRegistry formats;
    if (!expect(formats.initialize() && formats.add<AlternateMeshImporter>(),
                "distinct source format ids may produce the same resource kind") ||
        !expect(formats.find(schema::AssetKind::mesh) == nullptr,
                "kind-only lookup rejects ambiguous format implementations") ||
        !expect(formats.select("object.mesh") == formats.find("mesh") &&
                    formats.select("object.alternate") == formats.find("alternate_mesh"),
                "source selection and id dispatch retain distinct implementations"))
    {
        return 1;
    }
    std::unique_ptr<AssetImporter> alternate = std::make_unique<AlternateMeshImporter>();
    if (!expect(!formats.add(alternate) && alternate,
                "duplicate importer id preserves ownership") ||
        !expect(formats.replace(alternate) && !alternate,
                "replacement uses importer id rather than output kind"))
    {
        return 1;
    }
    return 0;
}
