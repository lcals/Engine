#include "runtime/storage/storage_file.hpp"

#include "runtime/utils/path_utils.hpp"

#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

#include <cerrno>

namespace
{
em_proxying_queue* queue = nullptr;

// clang-format off
EM_JS(void, publish_on_browser_thread,
      (em_proxying_ctx* context, const char* temporary, const char* target, int* success,
       char* error, int capacity),
{
    const complete = (message) => {
        if (message) stringToUTF8(message, error, capacity);
        HEAP32[success >> 2] = message ? 0 : 1;
        _emscripten_proxy_finish(context);
    };
    if (!Module.webStorage) {
        complete('Persistent storage is unavailable');
        return;
    }
    Module.webStorage.publish(UTF8ToString(temporary), UTF8ToString(target))
        .then(() => complete(''), fault => complete(String(fault?.message || fault)));
});
// clang-format on
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE int engine_web_storage_open()
{
    if (queue != nullptr)
    {
        return 0;
    }
    queue = em_proxying_queue_create();
    return queue != nullptr;
}

extern "C" EMSCRIPTEN_KEEPALIVE void engine_web_storage_close()
{
    if (queue != nullptr)
    {
        em_proxying_queue_destroy(queue);
        queue = nullptr;
    }
}

namespace engine::storage_detail
{
std::FILE* open_exclusive_file(const std::filesystem::path& path, std::error_code& error)
{
    error.clear();
    auto* file = std::fopen(PathUtils::to_utf8(path).c_str(), "wbx");
    if (file == nullptr)
    {
        error = {errno, std::generic_category()};
    }
    return file;
}

bool publish_storage_file(const std::filesystem::path& temporary,
                          const std::filesystem::path& target, std::string& error)
{
    error.clear();
    if (queue == nullptr || emscripten_is_main_browser_thread())
    {
        error = "Persistent publishing requires a live application worker";
        return false;
    }
    const auto temporary_text = PathUtils::to_utf8(temporary);
    const auto target_text = PathUtils::to_utf8(target);
    struct Request
    {
        const char* temporary;
        const char* target;
        int success = 0;
        char error[1024]{};
    } request{temporary_text.c_str(), target_text.c_str()};
    const bool completed = emscripten_proxy_sync_with_ctx(
        queue, emscripten_main_runtime_thread_id(),
        [](em_proxying_ctx* context, void* userdata)
        {
            auto& value = *static_cast<Request*>(userdata);
            publish_on_browser_thread(context, value.temporary, value.target, &value.success,
                                      value.error, sizeof(value.error));
        },
        &request);
    if (!completed || request.success == 0)
    {
        error = request.error[0] ? request.error : "Persistent publishing did not complete";
        return false;
    }
    return true;
}
} // namespace engine::storage_detail
