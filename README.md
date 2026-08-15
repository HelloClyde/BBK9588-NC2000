# NC2000 for BBK 9588

在 BBK / 步步高 9588 上运行文曲星 NC2000/NC2600 固件的原生 BDA 移植。

本项目基于 [wangyu-/NC2000](https://github.com/wangyu-/NC2000) 开发，是面向
BBK 9588 的第三方移植版，不是原项目的官方发行。桌面版模拟器、机型资料与完整历史请以
原仓库及其 Wiki 为准。

> 当前仍是实验版本。已在 SDK 模拟器和 BBK 9588 真机上验证官方 3.5 系列固件的启动、
> 按键、NAND 写回、蜂鸣器、字典查询与 DSP 单词发音；不同机器、固件和 NAND 内容仍可能
> 表现不同。

## 功能

- NC2000 原生 160×80 LCD，以 1× 比例居中显示
- 完整触摸软键盘，以及方向、确认、退出等实体键映射
- SPDS104A CELP/PCM 语音和蜂鸣器，输出为 22050 Hz 单声道 PCM
- 面向 MIPS32 的 65C02 基本块 JIT，包含 bank-aware 缓存和自修改代码失效处理
- 32 MiB NAND 分页读取与脏页写回，不把整个 dump 常驻内存
- `B:\NC2000\` 优先、`A:\NC2000\` 回退和系统文件选择器
- 右上角 `X` 系统确认退出，以及真机低频诊断日志

更完整的运行、性能和诊断说明见 [BBK 9588 移植文档](docs/bbk9588.md)。

## 构建

环境要求：Windows、PowerShell、Git 和 Python 3.10 或更高版本。首次构建会通过
[BBK 9588 BDA SDK](https://github.com/HelloClyde/bbk9588-bda-sdk) 下载并校验固定版本的
MIPS little-endian 交叉工具链。

```powershell
git submodule update --init sdk
.\tools\build_bbk9588.ps1
```

输出文件：

```text
build\nc2000_bbk9588\NC2000.bda
```

正式版本默认使用 `-O2` 和 LTO。仅调试 PCM 输出时才使用
`.\tools\build_bbk9588.ps1 -DspSelfTest`；不要发布带自测试音的构建。

## ROM / NAND

本仓库及其发布包不包含 NC2000/NC2600 固件、NAND、NOR、词典或官方软件。

原项目把整套固件习惯性称为“ROM”；对 NC2000/NC2600 而言，实际需要 NAND 和 NOR。
固件来源、可用内核与相关免责声明请直接查看：

- [原 NC2000 仓库的 Releases / Rom Files 说明](https://github.com/wangyu-/NC2000/releases)
- [原 NC2000 Wiki：切换不同机型和内核](https://github.com/wangyu-/NC2000/wiki/%E5%88%87%E6%8D%A2%E4%B8%8D%E5%90%8C%E6%9C%BA%E5%9E%8B%E5%92%8C%E5%86%85%E6%A0%B8)

请只使用你有权使用的真机 dump，并遵守上游页面中的限制。不要向本仓库提交 ROM、NAND、
NOR、词典数据或其他专有资源。

当前默认查找基本名为 `35` 的一组三个文件：

| 文件 | 要求 |
|---|---|
| `35.nand` | 至少 `65,536 × 528` 字节 |
| `35.nand0` | 启动区 dump，必须非空 |
| `35.nor` | 至少 512 KiB |

将三个文件放在同一目录，优先位置为：

```text
B:\NC2000\35.nand
B:\NC2000\35.nand0
B:\NC2000\35.nor
```

如果 B 盘没有完整文件组，程序会尝试 `A:\NC2000\35.*`；仍未找到时会打开 `.nand`
文件选择器，并按相同基本名匹配 `.nand0` 和 `.nor`。

模拟器会修改 NAND/NOR dump。首次使用前请自行备份。

## 安装与操作

1. 将构建得到的 `NC2000.bda` 安装到 9588 的应用程序目录。
2. 按上一节准备完整的 `.nand`、`.nand0` 和 `.nor` 文件组。
3. 启动 `NC2000`。
4. 软键盘 `ESC` 对应文曲星“跳出 / AC”，短按逐级返回；右上角 `X` 确认后保存并退出 BDA。
5. `SAY` 为发音键。方向键使用符号显示，`?` 为原机“求助”键。

诊断日志写入 `A:\NC2000\NC2000.LOG`，达到 512 KiB 后依次使用 `NC2001.LOG`、
`NC2002.LOG`。真机重启或死机后，请保留最后一个日志文件用于排查。

## 目录

| 路径 | 内容 |
|---|---|
| `platform/bbk9588/` | BDA 入口、前端、JIT、音频、运行时和精简 libc |
| `dsp/` | SPDS104A DSP 解码器及上游记录 |
| `tools/` | 交叉构建、BDA 打包和 SDK 模拟器辅助脚本 |
| `docs/` | 9588 使用、性能和诊断文档 |
| `sdk/` | BBK 9588 BDA SDK Git submodule |
| 其余核心源码 | 继承自原 NC2000 模拟器 |

## 当前限制

- 9588 前端当前面向 NC2000/NC2600 系列；NC1020、NC3000 和 PC1000 尚未接入。
- 即时存档、桌面命令行、红外/串口主机桥接和 LCD 残影滤镜尚未接入。
- 正常退出会保存 flash；突然断电或系统复位可能丢失尚未写回的数据。
- 模拟器通过不等同于所有 9588 固件和真机组合都已验证。

## 上游与鸣谢

- 核心模拟器：[wangyu-/NC2000](https://github.com/wangyu-/NC2000)
- DSP 解码器：[wangyu-/wqxdsp](https://github.com/wangyu-/wqxdsp)，原作者 Lee
- BBK 9588 SDK：[HelloClyde/bbk9588-bda-sdk](https://github.com/HelloClyde/bbk9588-bda-sdk)
- 9588 移植参考：[HelloClyde/gba-for9588](https://github.com/HelloClyde/gba-for9588)

更精确的版本和改动来源见 [NOTICE.md](NOTICE.md) 与 [dsp/UPSTREAM.md](dsp/UPSTREAM.md)。

## 许可证

本项目继承原 NC2000 项目的 [GNU GPL v3](LICENSE)。SDK submodule 和其他第三方组件
保留各自许可证。固件、NAND、NOR、词典及设备软件不属于本项目许可证覆盖范围，也不随
本项目分发。

> 发布前许可检查：`dsp/` 来源仓库在当前固定版本中没有附带明确的许可证文件。
> 在取得相关权利人的再分发许可，或以许可证兼容的实现替换该目录之前，不应公开发布
> 包含这些文件或其编译产物的版本。详见 [NOTICE.md](NOTICE.md)。
