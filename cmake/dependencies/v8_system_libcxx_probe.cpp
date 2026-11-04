#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

// The preparation command force-includes the same version/mode guard as V8.
int main(int argc, char** argv)
{
    std::vector<int> values{1, 2, 3};
    values.reserve(16);
    std::array<int, 16> storage{};
    std::span<int> view(storage.data(), 3);
    std::string text("abc");
    text.reserve(16);
    const auto shared = std::make_shared<int>(9);
    if (argc == 1)
    {
        if (values[1] != 2 || view[1] != 0 || text[1] != 'b' || *shared != 9)
        {
            return 2;
        }
        std::printf("libc++ %d, EXTENSIVE hardening active\n", _LIBCPP_VERSION);
        return 0;
    }
    if (argc != 3)
    {
        return 2;
    }
    const auto index = static_cast<std::size_t>(std::strtoul(argv[2], nullptr, 10));
    switch (argv[1][0])
    {
    case 'v':
    {
        volatile int result = values[index];
        (void)result;
        break;
    }
    case 's':
    {
        volatile int result = view[index];
        (void)result;
        break;
    }
    case 't':
    {
        volatile char result = text[index];
        (void)result;
        break;
    }
    default:
        return 2;
    }
    return 0;
}
