#include "plugins/editor/scene_bake/ibl_bake_job.hpp"

#include <basisu/transcoder/basisu_transcoder.h>
#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bx/allocator.h>
#include <bx/readerwriter.h>

#include "cs_bake_irradiance.bin.h"
#include "cs_bake_lut.bin.h"
#include "cs_bake_panorama.bin.h"
#include "cs_bake_prefilter.bin.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <span>

namespace engine::plugins::scene_bake
{
namespace
{
constexpr bgfx::ViewId compute_view = 240;
constexpr bgfx::ViewId copy_view = 241;
constexpr std::uint64_t clamp_sampler =
    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;

std::uint8_t mip_count(std::uint16_t size)
{
    std::uint8_t count = 1;
    while (size > 1)
    {
        size >>= 1;
        ++count;
    }
    return count;
}

template <typename T> void destroy(T& handle)
{
    if (bgfx::isValid(handle))
    {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

bgfx::ProgramHandle program(std::span<const std::uint8_t> bytes)
{
    const auto shader = bgfx::createShader(bgfx::copy(bytes.data(), bytes.size()));
    return bgfx::isValid(shader) ? bgfx::createProgram(shader, true)
                                 : bgfx::ProgramHandle{bgfx::kInvalidHandle};
}

bool source_texture(const schema::TextureDataT& data, bgfx::TextureHandle& output,
                    std::string& error)
{
    const auto flags = (data.cube_map ? clamp_sampler : BGFX_SAMPLER_V_CLAMP) |
                       (data.srgb ? BGFX_TEXTURE_SRGB : 0);
    if (data.encoding == schema::TextureEncoding::ktx)
    {
        output = bgfx::createTexture(bgfx::copy(data.data.data(), data.data.size()), flags);
    }
    else
    {
        output = bgfx::createTexture2D(static_cast<std::uint16_t>(data.width),
                                       static_cast<std::uint16_t>(data.height), data.mip_count > 1,
                                       1, bgfx::TextureFormat::RGBA8, flags);
        if (!bgfx::isValid(output))
        {
            error = "Cannot allocate the IBL input texture";
            return false;
        }
        basist::basisu_transcoder decoder;
        if (data.encoding == schema::TextureEncoding::basis)
        {
            static std::once_flag initialized;
            std::call_once(initialized, [] { basist::basisu_transcoder_init(); });
            if (!decoder.start_transcoding(data.data.data(), data.data.size()))
            {
                error = "Cannot transcode the IBL input texture";
                return false;
            }
        }
        std::size_t offset = 0;
        for (std::uint8_t mip = 0; mip < data.mip_count; ++mip)
        {
            const auto width = static_cast<std::uint16_t>(std::max(1U, data.width >> mip));
            const auto height = static_cast<std::uint16_t>(std::max(1U, data.height >> mip));
            const std::size_t bytes = std::size_t(width) * height * 4;
            std::vector<std::uint8_t> pixels(bytes);
            if (data.encoding == schema::TextureEncoding::basis)
            {
                if (!decoder.transcode_image_level(data.data.data(), data.data.size(), 0, mip,
                                                   pixels.data(), width * height,
                                                   basist::transcoder_texture_format::cTFRGBA32))
                {
                    error = "Cannot decode an IBL input mip level";
                    return false;
                }
            }
            else if (data.encoding != schema::TextureEncoding::rgba8 ||
                     offset + bytes > data.data.size())
            {
                error = "Unsupported IBL input texture encoding";
                return false;
            }
            else
            {
                std::memcpy(pixels.data(), data.data.data() + offset, bytes);
                offset += bytes;
            }
            bgfx::updateTexture2D(output, 0, mip, 0, 0, width, height,
                                  bgfx::copy(pixels.data(), pixels.size()));
        }
    }
    if (!bgfx::isValid(output))
    {
        error = "Cannot upload the IBL input texture";
        return false;
    }
    return true;
}

bool ktx(std::span<const std::uint8_t> pixels, std::uint16_t size, bool cube, bool mips,
         bimg::TextureFormat::Enum format, std::vector<std::uint8_t>& output)
{
    bx::DefaultAllocator allocator;
    bx::MemoryBlock memory(&allocator);
    bx::MemoryWriter writer(&memory);
    bx::Error error;
    const auto bytes =
        bimg::imageWriteKtx(&writer, format, cube, size, size, 1, mips ? mip_count(size) : 1, 1,
                            false, pixels.data(), &error);
    if (!error.isOk() || bytes <= 0)
    {
        return false;
    }
    const auto* data = static_cast<const std::uint8_t*>(memory.more(0));
    output.assign(data, data + bytes);
    return true;
}
} // namespace

struct IblBakeJob::State
{
    enum class Pass
    {
        panorama,
        irradiance,
        prefilter,
        lut
    };
    struct Work
    {
        Pass pass = Pass::panorama;
        std::uint16_t size = 0;
        std::uint8_t face = 0;
        std::uint8_t mip = 0;
    };
    std::vector<Work> work;
    std::size_t index = 0;
    std::array<bgfx::ProgramHandle, 4> programs{
        {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle parameters = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle face_uniform = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle input = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cube = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle scratch = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle staging = BGFX_INVALID_HANDLE;
    std::array<std::vector<std::uint8_t>, 3> pixels;
    std::array<std::vector<std::uint8_t>, 3> encoded;
    std::vector<std::uint8_t> readback;
    std::uint32_t submitted_frame = 0;
    std::uint32_t readback_frame = 0;
    std::uint16_t resolution = 0;
    std::uint16_t samples = 0;
    std::uint16_t lut_size = 0;
    std::uint16_t cube_size = 0;
    std::uint16_t irradiance_size = 0;
    bool input_is_cube = false;
    bool submitted = false;
    bool reading = false;
    bool cancelled = false;
    bool finished = false;

    void release() noexcept
    {
        for (auto& value : programs)
        {
            destroy(value);
        }
        destroy(sampler);
        destroy(parameters);
        destroy(face_uniform);
        destroy(input);
        destroy(cube);
        destroy(scratch);
        destroy(staging);
    }
};

IblBakeJob::IblBakeJob() : state_(std::make_unique<State>())
{
}
IblBakeJob::~IblBakeJob() noexcept
{
    shutdown();
}

bool IblBakeJob::start(const schema::TextureDataT& source, std::uint16_t resolution,
                       std::uint16_t samples, std::uint16_t lut_size, std::string& error)
{
    auto& s = *state_;
    const auto* caps = bgfx::getCaps();
    const auto required = BGFX_CAPS_COMPUTE | BGFX_CAPS_TEXTURE_BLIT | BGFX_CAPS_TEXTURE_READ_BACK;
    if (caps == nullptr)
    {
        error = "IBL baking requires initialized GPU capabilities";
        return false;
    }
    char diagnostics[256]{};
    std::snprintf(diagnostics, sizeof(diagnostics),
                  " [backend=%s caps=0x%llx RGBA16F=0x%x RG16F=0x%x maxTextureSize=%u]",
                  bgfx::getRendererName(caps->rendererType),
                  static_cast<unsigned long long>(caps->supported),
                  static_cast<unsigned>(caps->formats[bgfx::TextureFormat::RGBA16F]),
                  static_cast<unsigned>(caps->formats[bgfx::TextureFormat::RG16F]),
                  static_cast<unsigned>(caps->limits.maxTextureSize));
    const auto reject = [&error, &diagnostics](const char* reason)
    {
        error = std::string(reason) + diagnostics;
        return false;
    };
    if (!s.work.empty())
    {
        return reject("IBL bake job has already started");
    }
    if (resolution < 16 || resolution > 512 || (resolution & (resolution - 1)) != 0 ||
        lut_size < 32 || lut_size > 512 || (lut_size & (lut_size - 1)) != 0 || samples < 16 ||
        samples > 1024)
    {
        return reject("Invalid IBL resolution, sample count or LUT size");
    }
    if ((caps->supported & required) != required)
    {
        return reject("IBL baking requires compute, texture blit and texture readback support");
    }
    const auto rgba_required = BGFX_CAPS_FORMAT_TEXTURE_2D | BGFX_CAPS_FORMAT_TEXTURE_CUBE |
                               BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE;
    if ((caps->formats[bgfx::TextureFormat::RGBA16F] & rgba_required) != rgba_required)
    {
        return reject("IBL baking requires RGBA16F 2D, cubemap and compute image-write support");
    }
    if ((caps->formats[bgfx::TextureFormat::RG16F] & BGFX_CAPS_FORMAT_TEXTURE_2D) == 0)
    {
        return reject("IBL BRDF LUT requires RG16F 2D texture sampling support");
    }
    if (source.width == 0 || source.height == 0 || source.mip_count == 0 || source.mip_count > 16 ||
        source.data.empty() || source.data.size() > std::numeric_limits<std::uint32_t>::max() ||
        source.width > std::numeric_limits<std::uint16_t>::max() ||
        source.height > std::numeric_limits<std::uint16_t>::max())
    {
        return reject("Invalid IBL source texture dimensions or data");
    }
    if (resolution > caps->limits.maxTextureSize || lut_size > caps->limits.maxTextureSize ||
        source.width > caps->limits.maxTextureSize || source.height > caps->limits.maxTextureSize)
    {
        return reject("IBL texture dimensions exceed the GPU limit");
    }
    s.resolution = resolution;
    s.samples = samples;
    s.lut_size = lut_size;
    s.irradiance_size = std::min<std::uint16_t>(32, resolution);
    s.input_is_cube = source.cube_map;
    s.cube_size = s.input_is_cube ? static_cast<std::uint16_t>(source.width)
                                  : std::min<std::uint16_t>(512, resolution * 2);
    s.programs = {program(cs_bake_panorama), program(cs_bake_irradiance),
                  program(cs_bake_prefilter), program(cs_bake_lut)};
    s.sampler = bgfx::createUniform("s_source", bgfx::UniformType::Sampler);
    s.parameters = bgfx::createUniform("u_build_ibl_param", bgfx::UniformType::Vec4);
    s.face_uniform = bgfx::createUniform("u_bake_face", bgfx::UniformType::Vec4);
    if (std::any_of(s.programs.begin(), s.programs.end(),
                    [](auto handle) { return !bgfx::isValid(handle); }) ||
        !bgfx::isValid(s.sampler) || !bgfx::isValid(s.parameters) ||
        !bgfx::isValid(s.face_uniform) || !source_texture(source, s.input, error))
    {
        if (error.empty())
        {
            error = "Cannot create IBL compute programs or uniforms";
        }
        s.release();
        return false;
    }
    if (!s.input_is_cube)
    {
        s.cube = bgfx::createTextureCube(s.cube_size, true, 1, bgfx::TextureFormat::RGBA16F,
                                         BGFX_TEXTURE_BLIT_DST | clamp_sampler);
        if (!bgfx::isValid(s.cube))
        {
            error = "Cannot allocate the panorama conversion cubemap";
            s.release();
            return false;
        }
    }
    const auto add = [&s](State::Pass pass, std::uint16_t size, bool mips)
    {
        const auto levels = mips ? mip_count(size) : 1;
        for (std::uint8_t mip = 0; mip < levels; ++mip)
        {
            const auto faces = pass == State::Pass::lut ? 1 : 6;
            for (std::uint8_t face = 0; face < faces; ++face)
            {
                s.work.push_back(
                    {pass, static_cast<std::uint16_t>(std::max(1, size >> mip)), face, mip});
            }
        }
    };
    if (!s.input_is_cube)
    {
        add(State::Pass::panorama, s.cube_size, true);
    }
    add(State::Pass::irradiance, s.irradiance_size, false);
    add(State::Pass::prefilter, resolution, true);
    add(State::Pass::lut, lut_size, false);
    bgfx::setViewName(compute_view, "Scene Bake Compute");
    bgfx::setViewName(copy_view, "Scene Bake Readback");
    bgfx::setViewMode(compute_view, bgfx::ViewMode::Sequential);
    return true;
}

BakeProgress IblBakeJob::tick(std::uint32_t frame, std::string& error)
{
    auto& s = *state_;
    if (s.reading)
    {
        if (static_cast<std::int32_t>(frame - s.readback_frame) < 0)
        {
            return BakeProgress::running;
        }
        s.reading = false;
        if (!s.cancelled)
        {
            const auto& task = s.work[s.index];
            const auto destination = static_cast<std::size_t>(task.pass) - 1;
            auto& pixels = s.pixels[destination];
            if (task.pass == State::Pass::lut)
            {
                // Metal supports RGBA16F image writes; retain only RG for the output LUT.
                const auto offset = pixels.size();
                pixels.resize(offset + s.readback.size() / 2);
                for (std::size_t source = 0; source < s.readback.size(); source += 8)
                {
                    std::memcpy(pixels.data() + offset + source / 2, s.readback.data() + source, 4);
                }
            }
            else
            {
                pixels.insert(pixels.end(), s.readback.begin(), s.readback.end());
            }
            ++s.index;
        }
        destroy(s.staging);
        s.readback.clear();
    }
    if (s.cancelled)
    {
        s.release();
        return BakeProgress::cancelled;
    }
    if (s.index == s.work.size())
    {
        if (!s.finished)
        {
            if (!ktx(s.pixels[0], s.irradiance_size, true, false, bimg::TextureFormat::RGBA16F,
                     s.encoded[0]) ||
                !ktx(s.pixels[1], s.resolution, true, true, bimg::TextureFormat::RGBA16F,
                     s.encoded[1]) ||
                !ktx(s.pixels[2], s.lut_size, false, false, bimg::TextureFormat::RG16F,
                     s.encoded[2]))
            {
                error = "Cannot encode the completed IBL readback as KTX";
                s.release();
                return BakeProgress::failed;
            }
            s.finished = true;
            s.release();
        }
        return BakeProgress::complete;
    }
    const auto& task = s.work[s.index];
    constexpr auto format = bgfx::TextureFormat::RGBA16F;
    if (s.submitted)
    {
        if (frame == s.submitted_frame)
        {
            return BakeProgress::running;
        }
        s.submitted = false;
        if (task.pass == State::Pass::panorama)
        {
            bgfx::blit(copy_view, s.cube, task.mip, 0, 0, task.face, s.scratch, 0, 0, 0, 0,
                       task.size, task.size, 1);
            destroy(s.scratch);
            ++s.index;
            return BakeProgress::running;
        }
        s.staging = bgfx::createTexture2D(task.size, task.size, false, 1, format,
                                          BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        if (!bgfx::isValid(s.staging))
        {
            error = "Cannot allocate the 2D IBL readback texture";
            s.release();
            return BakeProgress::failed;
        }
        s.readback.resize(std::size_t(task.size) * task.size * 8);
        bgfx::blit(copy_view, s.staging, 0, 0, s.scratch);
        s.readback_frame = bgfx::readTexture(s.staging, s.readback.data());
        s.reading = true;
        destroy(s.scratch);
        return BakeProgress::running;
    }
    s.scratch = bgfx::createTexture2D(task.size, task.size, false, 1, format,
                                      BGFX_TEXTURE_COMPUTE_WRITE | clamp_sampler);
    if (!bgfx::isValid(s.scratch))
    {
        error = "Cannot allocate an IBL compute output texture";
        s.release();
        return BakeProgress::failed;
    }
    const bool lut = task.pass == State::Pass::lut;
    const std::array<float, 4> parameters{
        static_cast<float>(s.samples), 0, static_cast<float>(s.cube_size),
        static_cast<float>(task.mip) / std::max(1, mip_count(s.resolution) - 1)};
    const std::array<float, 4> face{static_cast<float>(task.face), static_cast<float>(task.mip), 0,
                                    0};
    bgfx::setUniform(s.parameters, parameters.data());
    bgfx::setUniform(s.face_uniform, face.data());
    if (!lut)
    {
        const auto texture =
            task.pass == State::Pass::panorama || s.input_is_cube ? s.input : s.cube;
        bgfx::setTexture(0, s.sampler, texture);
    }
    bgfx::setImage(lut ? 0 : 1, s.scratch, 0, bgfx::Access::Write, format);
    bgfx::dispatch(compute_view, s.programs[static_cast<std::size_t>(task.pass)],
                   (task.size + 7) / 8, (task.size + 7) / 8, 1);
    s.submitted_frame = frame;
    s.submitted = true;
    return BakeProgress::running;
}

void IblBakeJob::cancel() noexcept
{
    state_->cancelled = true;
}
void IblBakeJob::shutdown() noexcept
{
    state_->cancelled = true;
    state_->release();
}
std::uint32_t IblBakeJob::completed() const noexcept
{
    return static_cast<std::uint32_t>(state_->index);
}
std::uint32_t IblBakeJob::total() const noexcept
{
    return static_cast<std::uint32_t>(state_->work.size());
}
const std::array<std::vector<std::uint8_t>, 3>& IblBakeJob::outputs() const noexcept
{
    return state_->encoded;
}
} // namespace engine::plugins::scene_bake
