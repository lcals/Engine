// 仅供离线分析；用 Tracy 的 Worker 读取官方 .tracy，不另建 trace 格式。
#include "TracyFileRead.hpp"
#include "TracyWorker.hpp"

#include <cstdio>
#include <memory>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: tracy-frameexport capture.tracy\n");
        return 1;
    }
    std::unique_ptr<tracy::FileRead> file(tracy::FileRead::Open(argv[1]));
    if (!file)
    {
        std::fprintf(stderr, "Cannot open capture\n");
        return 2;
    }
    tracy::Worker worker(*file);
    const auto* frames = worker.GetFramesBase();
    if (frames == nullptr)
    {
        std::fprintf(stderr, "Capture has no base FrameMark timeline\n");
        return 3;
    }
    const auto count = worker.GetFrameCount(*frames);
    std::printf("index,start_ns,end_ns,duration_ns\n");
    // on-demand 的前两项属于连接前；其他模式的首项含启动阶段。
    // 最后一项没有下一次 FrameMark，不能作为完整帧统计。
    const std::size_t first = worker.IsOnDemand() ? 2 : 1;
    for (std::size_t index = first; index + 1 < count; ++index)
    {
        std::printf("%zu,%lld,%lld,%lld\n", index,
                    static_cast<long long>(worker.GetFrameBegin(*frames, index)),
                    static_cast<long long>(worker.GetFrameEnd(*frames, index)),
                    static_cast<long long>(worker.GetFrameTime(*frames, index)));
    }
    return 0;
}
