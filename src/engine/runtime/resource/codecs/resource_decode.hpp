#pragma once

#include "runtime/resource/resource.hpp"

#include <flatbuffers/verifier.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace engine::resource_decode_detail
{
[[nodiscard]] inline bool verify_flatbuffer(ByteView payload,
                                            bool (*verify)(flatbuffers::Verifier&),
                                            std::string_view invalid_message, std::string& error)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload.data());
    flatbuffers::Verifier verifier(bytes, payload.size());
    if (!verify(verifier))
    {
        error = invalid_message;
        return false;
    }
    return true;
}

template <class ResourceT, class ValidateEncoded, class Unpack>
[[nodiscard]] bool decode_flatbuffer(const schema::AssetEntryT& entry, ByteView payload,
                                     std::size_t max_bytes, ValidateEncoded validate, Unpack unpack,
                                     std::unique_ptr<Resource>& output, std::string& error)
{
    if (output)
    {
        error = "Resource output must be empty";
        return false;
    }
    error.clear();
    if (entry.kind != ResourceT::asset_kind || payload.empty() || payload.size() > max_bytes)
    {
        error = "Invalid resource kind or payload size";
        return false;
    }
    if (!validate(payload, error))
    {
        return false;
    }
    auto data = unpack(reinterpret_cast<const std::uint8_t*>(payload.data()));
    output = std::make_unique<ResourceT>(entry, std::move(*data));
    return true;
}
} // namespace engine::resource_decode_detail
