#include "v8_smoke.hpp"

#include <cstdio>

namespace
{

void log_message(const char* message)
{
    std::puts(message);
}

} // namespace

int main(int argc, char** argv)
{
    return engine::tests::run_v8_smoke(argc > 0 ? argv[0] : nullptr, nullptr, log_message) ? 0 : 1;
}
