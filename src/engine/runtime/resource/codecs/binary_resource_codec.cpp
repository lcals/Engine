#include "runtime/resource/codecs/binary_resource_codec.hpp"

namespace engine
{

bool BinaryResourceCodec::decode(const ResourceCatalog&, ByteView payload, Resource& output) const
{
    if (output.info().path.starts_with("builtin/"))
    {
        return false;
    }
    store(output, payload);
    return true;
}

} // namespace engine
