# lcals 工作室标识影片

`assets/movies/lcals.mp4` 是本项目制作的 5 秒无衬底工作室标识：深色背景、`lcals` 逐字出现、细线与 `STUDIO` 字样，最后淡出。视频为 H.264，音频为 AAC；声音由正弦波、合成泛音与随机噪声混合生成，没有使用外部音效素材。文字使用项目自带 [Lato 字体](fonts.md)，版权和 SIL OFL 1.1 许可随字体保留；影片包含光栅化的文字画面，不包含字体文件。

以下为 macOS 上的制作过程，需要 Xcode 命令行工具和 Python 3。在仓库根目录执行；`/tmp` 下的文件是中间产物，可指定其他临时路径。不同系统的媒体编码器版本可能产生不完全相同的二进制文件。

```sh
swift projects/showcase/tools/movie/create_lcals_video.swift /tmp/lcals-silent.mp4 projects/showcase/assets/fonts/lato-bold.ttf projects/showcase/assets/fonts/lato-regular.ttf
python3 projects/showcase/tools/movie/create_lcals_audio.py /tmp/lcals-sound.wav
swift projects/showcase/tools/movie/mux_lcals.swift /tmp/lcals-silent.mp4 /tmp/lcals-sound.wav /tmp/lcals-with-sound.mp4
```

制作后应先审核 `/tmp/lcals-with-sound.mp4` 的画面与声音，再替换项目中的影片；保留现有相邻 meta 以维护 Movie 与 AudioClip 的 GUID。
