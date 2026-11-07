#include "plugins/editor/scene_bake/lighting_bake_job.hpp"
#include "editor/resource_access.hpp"
#include "runtime/resource/types/material_resource.hpp"
#include "runtime/resource/types/mesh_resource.hpp"
#include "runtime/world/world_system.hpp"

#include "runtime/render/render_system.hpp"
#include "runtime/render/scene_capture.hpp"
#include "runtime/resource/codecs/light_probe_resource_codec.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/scene.hpp"
#include "runtime/scene/transform.hpp"
#include "runtime/task/task_system.hpp"

#include <bgfx/bgfx.h>
#include <bimg/encode.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <bx/readerwriter.h>
#include <xatlas.h>
#define LIGHTMAPPER_IMPLEMENTATION
#include <lightmapper.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <map>
#include <numbers>
#include <set>

namespace engine::plugins::scene_bake
{
namespace
{
using Matrix = std::array<float, 16>;
bool fixed_node(const schema::SceneDataT& scene, std::uint64_t id)
{
    std::set<std::uint64_t> visited;
    while (id != 0)
    {
        if (!visited.insert(id).second)
        {
            return false;
        }
        const auto node = std::find_if(scene.nodes.begin(), scene.nodes.end(), [id](const auto& n)
                                       { return n && n->info && n->info->properties->id == id; });
        if (node == scene.nodes.end())
        {
            return false;
        }
        const schema::TransformDataT* t = nullptr;
        for (const auto& c : (*node)->components)
        {
            if (!c)
            {
                continue;
            }
            if (c->payload.AsTransformData())
            {
                t = c->payload.AsTransformData();
            }
            if (!c->flags->enabled)
            {
                continue;
            }
            if (c->payload.AsAnimatorData() || c->payload.AsDestructionData() ||
                c->payload.AsTerrainData() || c->payload.AsCharacterMotorData())
            {
                return false;
            }
            if (const auto* body = c->payload.AsRigidBodyData();
                body && body->motion != schema::BodyMotion::static_body)
            {
                return false;
            }
        }
        if (t == nullptr)
        {
            return false;
        }
        id = (*node)->info->parent_id;
    }
    return true;
}

bool texture(std::span<const float> rgba, std::uint16_t size, schema::TextureDataT& output)
{
    if (rgba.size() != std::size_t{size} * size * 4)
    {
        return false;
    }
    std::vector<std::uint16_t> half(rgba.size());
    for (std::size_t i = 0; i < rgba.size(); ++i)
    {
        if (!std::isfinite(rgba[i]) || rgba[i] < 0 || rgba[i] > 65504)
        {
            return false;
        }
        half[i] = bx::halfFromFloat(rgba[i]);
    }
    bx::DefaultAllocator allocator;
    bx::MemoryBlock block(&allocator);
    bx::MemoryWriter writer(&block);
    bx::Error error;
    const auto written = bimg::imageWriteKtx(&writer, bimg::TextureFormat::RGBA16F, false, size,
                                             size, 0, 1, 1, false, half.data(), &error);
    if (!error.isOk() || written <= 0)
    {
        return false;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(block.more(0));
    output.path = "bake/lightmap.ktx";
    output.width = output.height = size;
    output.srgb = false;
    output.encoding = schema::TextureEncoding::ktx;
    output.format = schema::TextureFormat::rgba16f;
    output.data.assign(bytes, bytes + written);
    return true;
}

bool unwrap(schema::MeshDataT& mesh, std::uint16_t resolution, const std::atomic_bool& cancel)
{
    std::unique_ptr<xatlas::Atlas, decltype(&xatlas::Destroy)> atlas(xatlas::Create(),
                                                                     xatlas::Destroy);
    if (!atlas)
    {
        return false;
    }
    xatlas::SetProgressCallback(
        atlas.get(), [](xatlas::ProgressCategory, int, void* data)
        { return !static_cast<const std::atomic_bool*>(data)->load(); },
        const_cast<std::atomic_bool*>(&cancel));
    xatlas::MeshDecl decl;
    decl.vertexPositionData = mesh.vertices.data();
    decl.vertexNormalData =
        reinterpret_cast<const std::byte*>(mesh.vertices.data()) + 3 * sizeof(float);
    decl.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    decl.vertexPositionStride = decl.vertexNormalStride = sizeof(schema::MeshVertex);
    decl.indexData = mesh.indices.data();
    decl.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
    decl.indexFormat = xatlas::IndexFormat::UInt32;
    if (xatlas::AddMesh(atlas.get(), decl, 1) != xatlas::AddMeshError::Success)
    {
        return false;
    }
    xatlas::PackOptions pack;
    pack.resolution = resolution;
    pack.padding = 2;
    pack.bilinear = true;
    xatlas::Generate(atlas.get(), {}, pack);
    if (cancel || atlas->meshCount != 1 || atlas->atlasCount != 1 || !atlas->width ||
        !atlas->height)
    {
        return false;
    }
    const auto& result = atlas->meshes[0];
    const auto source = mesh;
    mesh.vertices.clear();
    mesh.texcoords.clear();
    mesh.tangents.clear();
    mesh.colors.clear();
    mesh.lightmap_texcoords.clear();
    for (std::uint32_t i = 0; i < result.vertexCount; ++i)
    {
        const auto& vertex = result.vertexArray[i];
        if (vertex.xref >= source.vertices.size() || vertex.atlasIndex != 0)
        {
            return false;
        }
        mesh.vertices.push_back(source.vertices[vertex.xref]);
        if (!source.texcoords.empty())
        {
            mesh.texcoords.push_back(source.texcoords[vertex.xref]);
        }
        if (!source.tangents.empty())
        {
            mesh.tangents.push_back(source.tangents[vertex.xref]);
        }
        if (!source.colors.empty())
        {
            mesh.colors.push_back(source.colors[vertex.xref]);
        }
        mesh.lightmap_texcoords.emplace_back(vertex.uv[0] / atlas->width,
                                             vertex.uv[1] / atlas->height);
    }
    mesh.indices.assign(result.indexArray, result.indexArray + result.indexCount);
    return true;
}

// Real orthonormal SH matching the runtime PBR basis: [1,y,z,x,xy,yz,3z²−1,xz,x²−y²].
std::array<double, 9> sh(double x, double y, double z)
{
    return {0.28209479177387814,
            0.4886025119029199 * y,
            0.4886025119029199 * z,
            0.4886025119029199 * x,
            1.0925484305920792 * x * y,
            1.0925484305920792 * y * z,
            0.31539156525252005 * (3 * z * z - 1),
            1.0925484305920792 * x * z,
            0.5462742152960396 * (x * x - y * y)};
}

double solid_angle(double x, double y, double step)
{
    const auto area = [](double u, double v)
    { return std::atan2(u * v, std::sqrt(u * u + v * v + 1)); };
    return area(x + step, y + step) - area(x - step, y + step) - area(x + step, y - step) +
           area(x - step, y - step);
}
constexpr std::array<bx::Vec3, 6> face_forward{
    {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
constexpr std::array<bx::Vec3, 6> face_up{
    {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}}};
} // namespace

struct LightingBakeJob::State
{
    struct Preparation
    {
        LightingBakeOutputs outputs;
        std::atomic_bool cancel = false;
        std::string error;
    };
    RenderSystem& renderer;
    ResourceSystem& resources;
    TaskSystem& tasks;
    LightingBakeSettings settings;
    std::map<std::uint64_t, std::uint64_t> runtime_ids;
    std::shared_ptr<Preparation> preparation;
    std::future<bool> future;
    LightingBakeOutputs output;
    std::vector<std::shared_ptr<schema::TextureDataT>> next;
    std::vector<float> pixels;
    std::vector<float> lm_uv;
    std::unique_ptr<lm_context, decltype(&lmDestroy)> lm{nullptr, lmDestroy};
    SceneCaptureRequest request;
    std::array<double, 27> probe_sum{};
    double probe_valid = 0;
    std::size_t mesh = 0, probe_volume = 0, probe_index = 0;
    unsigned bounce = 0, probe_face = 0;
    std::uint32_t done = 0, count = 0;
    bool waiting = false, cancelled = false, started = false, prepared = false;
    BakeProgress progress = BakeProgress::running;
};

LightingBakeJob::LightingBakeJob(RenderSystem& renderer, ResourceSystem& resources,
                                 TaskSystem& tasks)
    : state_(std::make_unique<State>(renderer, resources, tasks))
{
}
LightingBakeJob::~LightingBakeJob() noexcept
{
    shutdown();
}

bool LightingBakeJob::start(const schema::SceneDataT& scene, const Scene& active_scene,
                            const LightingBakeSettings& settings, std::string& error)
{
    auto& s = *state_;
    if (s.started || settings.lightmap_resolution < 16 || settings.lightmap_resolution > 512 ||
        (settings.lightmap_resolution & (settings.lightmap_resolution - 1)) ||
        settings.hemisphere_resolution < 16 || settings.hemisphere_resolution > 128 ||
        (settings.hemisphere_resolution & (settings.hemisphere_resolution - 1)) ||
        settings.bounce_count < 1 || settings.bounce_count > 8 ||
        settings.interpolation_passes > 4 || !std::isfinite(settings.interpolation_threshold) ||
        settings.interpolation_threshold < 0 || settings.interpolation_threshold > 1 ||
        settings.source_fingerprint.empty())
    {
        error = "Invalid lighting bake state, resolution, bounce count, or fingerprint";
        return false;
    }
    std::vector<Node> active_nodes;
    if (!active_scene.collect_nodes(active_nodes))
    {
        error = "Cannot collect scene entities";
        return false;
    }
    for (const auto& node : active_nodes)
    {
        s.runtime_ids.emplace(node.local_id(), node.id());
    }
    for (const auto& node : scene.nodes)
    {
        const auto found = node && node->info ? s.runtime_ids.find(node->info->properties->id)
                                              : s.runtime_ids.end();
        const auto active =
            found == s.runtime_ids.end() ? Node{} : active_scene.find_node(found->second);
        if (!active || active.parent().local_id() != node->info->parent_id)
        {
            error = "Saved scene identities no longer match the active preview";
            return false;
        }
    }
    const auto world_matrix = [&](std::uint64_t id, Matrix& transform)
    {
        const auto found = s.runtime_ids.find(id);
        const auto node =
            found == s.runtime_ids.end() ? Node{} : active_scene.find_node(found->second);
        const auto t = node ? node.get_component<Transform>() : Transform{};
        return fixed_node(scene, id) && t && t.world_matrix(transform.data());
    };
    s.settings = settings;
    s.preparation = std::make_shared<State::Preparation>();
    auto& out = s.preparation->outputs;
    std::set<std::uint64_t> lights(settings.light_node_ids.begin(), settings.light_node_ids.end());
    if (lights.size() != settings.light_node_ids.size())
    {
        error = "Duplicate fixed light identities";
        return false;
    }
    for (const auto& node : scene.nodes)
    {
        if (!node || !node->info)
        {
            continue;
        }
        const schema::MeshRendererDataT* mesh = nullptr;
        bool dynamic = false, light = false;
        for (const auto& c : node->components)
        {
            if (!c || !c->flags->enabled)
            {
                continue;
            }
            if (const auto* m = c->payload.AsMeshRendererData(); m && m->bake_static)
            {
                mesh = m;
            }
            light = light || c->payload.AsLightData() != nullptr;
            dynamic = dynamic || c->payload.AsAnimatorData() || c->payload.AsDestructionData() ||
                      c->payload.AsTerrainData() || c->payload.AsCharacterMotorData();
            if (const auto* body = c->payload.AsRigidBodyData())
            {
                dynamic = dynamic || body->motion != schema::BodyMotion::static_body;
            }
        }
        if (lights.contains(node->info->properties->id))
        {
            if (!light || dynamic || !fixed_node(scene, node->info->properties->id))
            {
                error = "Bake lights must identify enabled fixed lights";
                return false;
            }
            lights.erase(node->info->properties->id);
        }
        if (!mesh)
        {
            continue;
        }
        engine::ResourceReference resource;
        BakedMeshLighting item;
        item.node_id = node->info->properties->id;
        if (dynamic ||
            !editor::resource_access::load(active_scene.world().resource_loader(), mesh->mesh_guid,
                                           resource) ||
            !resource || !resource.data<MeshResource>() ||
            !resource.data<MeshResource>()->skin.empty() ||
            !world_matrix(item.node_id, item.transform))
        {
            error = "Static lighting requires fixed, imported, unskinned mesh geometry";
            return false;
        }
        std::vector<std::string> materials = mesh->additional_material_guids;
        materials.push_back(mesh->material_guid);
        for (const auto& guid : materials)
        {
            if (guid.empty())
            {
                continue;
            }
            engine::ResourceReference material;
            if (!editor::resource_access::load(active_scene.world().resource_loader(), guid,
                                               material) ||
                !material || !material.data<MaterialResource>() ||
                material.data<MaterialResource>()->shader_path != "builtin://shaders/pbr" ||
                material.data<MaterialResource>()->blend_mode != schema::BlendMode::opaque)
            {
                error = "Static bake contributors require opaque builtin PBR materials";
                return false;
            }
        }
        item.mesh = std::make_shared<schema::MeshDataT>(*resource.data<MeshResource>());
        out.meshes.push_back(std::move(item));
    }
    if (!lights.empty() || out.meshes.size() > 4096 || settings.probes.size() > 64 ||
        (out.meshes.empty() && settings.probes.empty()))
    {
        error = "No bake targets, too many targets, or missing fixed light nodes";
        return false;
    }
    std::set<std::uint64_t> targets;
    for (const auto& target : settings.probes)
    {
        const auto node =
            std::find_if(scene.nodes.begin(), scene.nodes.end(), [&](const auto& n)
                         { return n && n->info && n->info->properties->id == target.node_id; });
        if (node == scene.nodes.end() ||
            !std::any_of((*node)->components.begin(), (*node)->components.end(),
                         [](const auto& c) { return c && c->payload.AsLightProbeVolumeData(); }))
        {
            error = "Probe target requires an existing LightProbeVolume component";
            return false;
        }
        BakedProbeLighting item;
        item.node_id = target.node_id;
        Matrix transform;
        auto& p = item.data;
        p.path = "bake/probes.lightprobe";
        p.bounds_min = target.bounds_min;
        p.bounds_max = target.bounds_max;
        p.grid_x = target.grid[0];
        p.grid_y = target.grid[1];
        p.grid_z = target.grid[2];
        if (!targets.insert(target.node_id).second || !world_matrix(target.node_id, transform) ||
            p.grid_x < 2 || p.grid_y < 2 || p.grid_z < 2 || p.grid_x > 64 || p.grid_y > 64 ||
            p.grid_z > 64)
        {
            error = "Invalid probe target identity, transform, or grid";
            return false;
        }
        const auto n = std::size_t{p.grid_x} * p.grid_y * p.grid_z;
        if (n > 32768)
        {
            error = "Probe target exceeds the bake budget";
            return false;
        }
        p.baked_transform.assign(transform.begin(), transform.end());
        p.coefficients.resize(n * 9);
        p.valid.resize(n);
        p.source_fingerprint = settings.source_fingerprint;
        if (!LightProbeResourceCodec::validate(p))
        {
            error = "Invalid probe target bounds";
            return false;
        }
        out.probes.push_back(std::move(item));
    }
    s.count = static_cast<std::uint32_t>(out.meshes.size() * settings.bounce_count);
    for (const auto& p : out.probes)
    {
        s.count += static_cast<std::uint32_t>(p.data.valid.size());
    }
    const auto preparation = s.preparation;
    s.future = s.tasks.submit(
        "Bake.prepareUv2",
        [preparation, resolution = settings.lightmap_resolution]
        {
            std::vector<float> black(std::size_t{resolution} * resolution * 4, 0);
            for (std::size_t i = 3; i < black.size(); i += 4)
            {
                black[i] = 1;
            }
            for (auto& m : preparation->outputs.meshes)
            {
                if (preparation->cancel || !unwrap(*m.mesh, resolution, preparation->cancel))
                {
                    preparation->error = "UV2 atlas generation cancelled or failed";
                    return false;
                }
                m.lightmap = std::make_shared<schema::TextureDataT>();
                if (!texture(black, resolution, *m.lightmap))
                {
                    preparation->error = "Cannot encode initial lightmap";
                    return false;
                }
            }
            return true;
        },
        TaskPriority::low);
    if (!s.future.valid())
    {
        error = "Editor task domain rejected UV2 preparation";
        return false;
    }
    s.started = true;
    return true;
}

BakeProgress LightingBakeJob::tick(std::string& error)
{
    auto& s = *state_;
    if (!s.started)
    {
        error = "Lighting bake was not started";
        return BakeProgress::failed;
    }
    if (s.progress != BakeProgress::running)
    {
        return s.progress;
    }
    if (!s.prepared)
    {
        if (s.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            return BakeProgress::running;
        }
        const bool result = s.future.get();
        if (s.cancelled)
        {
            return s.progress = BakeProgress::cancelled;
        }
        if (!result)
        {
            error = s.preparation->error;
            return s.progress = BakeProgress::failed;
        }
        s.output = std::move(s.preparation->outputs);
        s.preparation.reset();
        s.prepared = true;
        for (const auto id : s.settings.light_node_ids)
        {
            s.request.light_node_ids.push_back(s.runtime_ids[id]);
        }
        for (const auto& m : s.output.meshes)
        {
            s.request.node_ids.push_back(s.runtime_ids[m.node_id]);
        }
    }
    if (s.cancelled)
    {
        if (s.waiting && s.renderer.diffuse_capture_status() == SceneCaptureStatus::pending)
        {
            return BakeProgress::running;
        }
        s.lm.reset();
        return s.progress = BakeProgress::cancelled;
    }
    SceneCaptureImage image;
    if (s.waiting)
    {
        const auto status = s.renderer.diffuse_capture_status();
        if (status == SceneCaptureStatus::pending)
        {
            return BakeProgress::running;
        }
        if (status != SceneCaptureStatus::complete || !s.renderer.take_diffuse_capture(image) ||
            image.width != s.request.width || image.height != s.request.height ||
            image.rgba.size() != std::size_t{image.width} * image.height * 4)
        {
            error = "Diffuse capture failed or its scene changed";
            return s.progress = BakeProgress::failed;
        }
        s.waiting = false;
        if (s.lm)
        {
            lmSubmitPixels(s.lm.get(), image.rgba.data(), image.width, image.height);
            lmEnd(s.lm.get());
        }
        else
        {
            const auto forward = face_forward[s.probe_face];
            const auto up = face_up[s.probe_face];
            const auto right = bx::cross(forward, up);
            const double step = 1.0 / image.width;
            for (std::uint16_t y = 0; y < image.height; ++y)
            {
                for (std::uint16_t x = 0; x < image.width; ++x)
                {
                    const double u = (2.0 * x + 1) / image.width - 1;
                    const double v = 1 - (2.0 * y + 1) / image.height;
                    const auto direction = bx::normalize(
                        bx::add(forward, bx::add(bx::mul(right, static_cast<float>(u)),
                                                 bx::mul(up, static_cast<float>(v)))));
                    const auto basis = sh(direction.x, direction.y, direction.z);
                    const double weight = solid_angle(u, v, step);
                    const float* pixel = image.rgba.data() + (std::size_t{y} * image.width + x) * 4;
                    s.probe_valid += weight * pixel[3];
                    for (std::size_t i = 0; i < 9; ++i)
                    {
                        for (std::size_t channel = 0; channel < 3; ++channel)
                        {
                            s.probe_sum[i * 3 + channel] += basis[i] * weight * pixel[channel];
                        }
                    }
                }
            }
            if (++s.probe_face == 6)
            {
                auto& probe = s.output.probes[s.probe_volume].data;
                const bool valid = s.probe_valid > 0.9 * 4 * std::numbers::pi;
                probe.valid[s.probe_index] = valid ? 1 : 0;
                if (valid)
                {
                    for (std::size_t i = 0; i < 9; ++i)
                    {
                        probe.coefficients[s.probe_index * 9 + i] = schema::Vec3(
                            s.probe_sum[i * 3], s.probe_sum[i * 3 + 1], s.probe_sum[i * 3 + 2]);
                    }
                }
                ++s.done;
                ++s.probe_index;
                s.probe_face = 0;
                s.probe_sum.fill(0);
                s.probe_valid = 0;
                if (s.probe_index == probe.valid.size())
                {
                    ++s.probe_volume;
                    s.probe_index = 0;
                }
            }
        }
    }
    if (s.bounce < s.settings.bounce_count && !s.output.meshes.empty())
    {
        if (!s.lm)
        {
            auto& mesh = s.output.meshes[s.mesh];
            s.pixels.assign(std::size_t{s.settings.lightmap_resolution} *
                                s.settings.lightmap_resolution * 4,
                            0);
            s.lm.reset(lmCreate(s.settings.hemisphere_resolution, 0.02F, 10000, 0, 0, 0,
                                s.settings.interpolation_passes, s.settings.interpolation_threshold,
                                0));
            if (!s.lm)
            {
                error = "Cannot create lightmapper context";
                return s.progress = BakeProgress::failed;
            }
            lmSetTargetLightmap(s.lm.get(), s.pixels.data(), s.settings.lightmap_resolution,
                                s.settings.lightmap_resolution, 4);
            s.lm_uv.clear();
            for (const auto& uv : mesh.mesh->lightmap_texcoords)
            {
                s.lm_uv.push_back(static_cast<float>(uv.x()));
                s.lm_uv.push_back(static_cast<float>(uv.y()));
            }
            lmSetGeometry(s.lm.get(), mesh.transform.data(), LM_FLOAT, mesh.mesh->vertices.data(),
                          sizeof(schema::MeshVertex), LM_FLOAT,
                          reinterpret_cast<const std::byte*>(mesh.mesh->vertices.data()) +
                              3 * sizeof(float),
                          sizeof(schema::MeshVertex), LM_FLOAT, s.lm_uv.data(), 2 * sizeof(float),
                          static_cast<int>(mesh.mesh->indices.size()), LM_UNSIGNED_INT,
                          mesh.mesh->indices.data());
        }
        int viewport[4]{};
        if (!lmBegin(s.lm.get(), viewport, s.request.view.data(), s.request.projection.data()))
        {
            s.lm.reset();
            std::vector<float> temporary(s.pixels.size());
            for (unsigned padding = 0; padding < 2; ++padding)
            {
                lmImageDilate(s.pixels.data(), temporary.data(), s.settings.lightmap_resolution,
                              s.settings.lightmap_resolution, 4);
                s.pixels.swap(temporary);
            }
            auto result = std::make_shared<schema::TextureDataT>();
            if (!texture(s.pixels, s.settings.lightmap_resolution, *result))
            {
                error = "Lightmap contains invalid radiance or cannot be encoded";
                return s.progress = BakeProgress::failed;
            }
            s.next.push_back(std::move(result));
            ++s.done;
            if (++s.mesh == s.output.meshes.size())
            {
                for (std::size_t i = 0; i < s.next.size(); ++i)
                {
                    s.output.meshes[i].lightmap = std::move(s.next[i]);
                }
                s.next.clear();
                s.mesh = 0;
                ++s.bounce;
            }
            return BakeProgress::running;
        }
        s.request.width = static_cast<std::uint16_t>(viewport[2]);
        s.request.height = static_cast<std::uint16_t>(viewport[3]);
        if (!bgfx::getCaps()->homogeneousDepth)
        {
            for (std::size_t col = 0; col < 4; ++col)
            {
                s.request.projection[col * 4 + 2] =
                    0.5F * (s.request.projection[col * 4 + 2] + s.request.projection[col * 4 + 3]);
            }
        }
    }
    else
    {
        if (s.probe_volume == s.output.probes.size())
        {
            return s.progress = BakeProgress::complete;
        }
        const auto& p = s.output.probes[s.probe_volume].data;
        const std::array<std::size_t, 3> cell{s.probe_index % p.grid_x,
                                              (s.probe_index / p.grid_x) % p.grid_y,
                                              s.probe_index / (p.grid_x * p.grid_y)};
        const double lo[]{p.bounds_min.x(), p.bounds_min.y(), p.bounds_min.z()};
        const double hi[]{p.bounds_max.x(), p.bounds_max.y(), p.bounds_max.z()};
        const std::uint32_t grid[]{p.grid_x, p.grid_y, p.grid_z};
        float local[3]{};
        for (std::size_t a = 0; a < 3; ++a)
        {
            local[a] = lo[a] + (hi[a] - lo[a]) * cell[a] / (grid[a] - 1);
        }
        const auto& m = p.baked_transform;
        const bx::Vec3 position{m[0] * local[0] + m[4] * local[1] + m[8] * local[2] + m[12],
                                m[1] * local[0] + m[5] * local[1] + m[9] * local[2] + m[13],
                                m[2] * local[0] + m[6] * local[1] + m[10] * local[2] + m[14]};
        bx::mtxLookAt(s.request.view.data(), position,
                      bx::add(position, face_forward[s.probe_face]), face_up[s.probe_face],
                      bx::Handedness::Right);
        bx::mtxProj(s.request.projection.data(), 90, 1, 0.02F, 10000,
                    bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
        s.request.width = s.request.height = s.settings.hemisphere_resolution;
    }
    s.request.indirect.clear();
    for (const auto& mesh : s.output.meshes)
    {
        s.request.indirect.push_back({s.runtime_ids[mesh.node_id], mesh.lightmap, mesh.mesh});
    }
    if (!s.renderer.request_diffuse_capture(s.request))
    {
        error = "Renderer rejected the diffuse capture request";
        return s.progress = BakeProgress::failed;
    }
    s.waiting = true;
    return BakeProgress::running;
}
void LightingBakeJob::cancel() noexcept
{
    auto& s = *state_;
    s.cancelled = true;
    if (s.preparation)
    {
        s.preparation->cancel = true;
    }
    if (s.waiting)
    {
        s.renderer.cancel_diffuse_capture();
    }
}
void LightingBakeJob::shutdown() noexcept
{
    cancel();
}
std::uint32_t LightingBakeJob::completed() const noexcept
{
    return state_->done;
}
std::uint32_t LightingBakeJob::total() const noexcept
{
    return state_->count;
}
const LightingBakeOutputs& LightingBakeJob::outputs() const noexcept
{
    return state_->output;
}
} // namespace engine::plugins::scene_bake
