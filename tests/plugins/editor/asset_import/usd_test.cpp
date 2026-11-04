#include "runtime/utils/path_utils.hpp"

#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace
{

[[nodiscard]] bool fail(const char* message)
{
    std::fprintf(stderr, "%s\n", message);
    return false;
}

[[nodiscard]] bool check_roundtrip(const std::filesystem::path& directory, const char* extension,
                                   std::string_view signature)
{
    const auto path = directory / (std::string("stage.") + extension);
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error)
    {
        return fail("Cannot remove previous USD stage");
    }

    {
        const auto stage = pxr::UsdStage::CreateNew(engine::PathUtils::to_utf8(path));
        if (!stage)
        {
            return fail("Cannot create USD stage");
        }
        const auto prim = pxr::UsdGeomXform::Define(stage, pxr::SdfPath("/Root")).GetPrim();
        if (!prim)
        {
            return fail("Cannot define USD prim");
        }
        const auto attribute =
            prim.CreateAttribute(pxr::TfToken("answer"), pxr::SdfValueTypeNames->Int);
        if (!attribute.Set(42))
        {
            return fail("Cannot set USD attribute");
        }
        if (!stage->GetRootLayer()->Save())
        {
            return fail("Cannot save USD stage");
        }
    }

    {
        std::ifstream input(path, std::ios::binary);
        std::string actual(signature.size(), '\0');
        if (!input.read(actual.data(), static_cast<std::streamsize>(actual.size())))
        {
            return fail("Cannot read USD file header");
        }
        if (actual != signature)
        {
            return fail("USD file format does not match its extension");
        }
    }

    {
        const auto stage = pxr::UsdStage::Open(engine::PathUtils::to_utf8(path));
        if (!stage)
        {
            return fail("Cannot reopen USD stage");
        }
        const auto prim = stage->GetPrimAtPath(pxr::SdfPath("/Root"));
        if (!prim)
        {
            return fail("Saved USD prim is missing");
        }
        int value = 0;
        if (!prim.GetAttribute(pxr::TfToken("answer")).Get(&value) || value != 42)
        {
            return fail("Saved USD attribute does not match");
        }
    }

    std::filesystem::remove(path, error);
    if (error)
    {
        return fail("Cannot remove USD test output");
    }
    std::printf("USD %s roundtrip passed\n", extension);
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::fputs("Expected an output directory\n", stderr);
        return 2;
    }
    const auto directory = engine::PathUtils::from_utf8(argv[1]);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        std::fprintf(stderr, "Cannot create USD output directory: %s\n", error.message().c_str());
        return 1;
    }
    if (!check_roundtrip(directory, "usda", "#usda") ||
        !check_roundtrip(directory, "usdc", "PXR-USDC"))
    {
        return 1;
    }
    // 正常退出也会验证 USD 工作结束后的 TBB 调度器清理。
    return 0;
}
