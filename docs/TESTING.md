# 可复跑的离线测试

以下是当前 Windows 验收工具的实际入口。普通构建和 CI 仍只安装 `tools/requirements-build.txt` 的 Pillow、pyelftools；本页测试依赖单独安装，不会让成品依赖 Python 或 Unicorn。

## 环境

本地环境为 Python 3.11.5、Unicorn 2.1.4，以及 MSYS2 UCRT64 的主机 GCC（本次读取为 Rev5 / 16.1.0）。先准备固定 H1 SDK 子模块和完整运行素材，再执行：

```powershell
python -m pip install -r tools/requirements-test.txt
python -c "import unicorn; print(unicorn.__version__)"
& C:/msys64/ucrt64/bin/gcc.exe --version
```

这些 Host 测试和 MIPS 初始夹具生成器**目前硬编码** `C:/msys64/ucrt64/bin/gcc.exe`，部分脚本会在自身进程内把该目录加入 PATH。它是生成 Windows 主机测试程序的 GCC，不是 `.tools/toolchain/bin/` 中编译 H1 应用的 `mipsel-none-elf-gcc`。这里只记录已验证的 Windows 使用方式，不声称这些验收工具已在 Linux 通用运行。

测试使用本仓库的 `assets/runtime/SCENE.H1P`；涉及地形/树木和加载器时另用 `VISION.H1V`。缺失文件或 Git LFS 指针不能替代真实素材。普通测试不读取 Steam、固件或 H1 USB。

## 实际 C 核心与 Host UI

在仓库根目录执行，每次用一个全新的输出目录：

```powershell
python tools/verify_match_time.py --output build/tests/local-match-time-host-1
python tools/verify_vision_v028.py --output build/tests/local-vision-host-1
python tests/verify_vision_ui.py --output build/tests/local-vision-ui-1
```

- 时序入口编译实际游戏核心及真实外部装备 provider，检查赛前、号角、出兵和昼夜等规则。
- 视野核心入口检查团队视野、树/高地/洞穴、眼、隐身及小鱼人等相关行为。
- UI 入口运行实际输入、渲染、provider 和核心代码，但 SDK 键盘、时钟及音频界面服务采用模型。它还检查真实视野文件的加载、长度/CRC错误、内存失败和资源释放，并输出明确标为 Host 预览的 PNG。

这些属于主机代码执行，不能写成最终 BDA 指令执行、H1 屏幕抓取、实际音频播放或帧率验证。结果和编译日志保存在各输出目录的 `verification.json`、`compile.txt` 等文件中。失败输出应保留，下一次选择新目录。

## 针对确切 BDA 的 MIPS 核心检查

先按 `docs/BUILDING.md` 生成一个候选并静态校验。下面以 `build/local-native-1` 为例，替换为实际构建目录；SHA 从该候选文件计算，不能照抄旧版或另一轮的值：

```powershell
python tools/build.py --toolchain .tools/toolchain/bin --output build/local-native-1
python tools/verify.py --build build/local-native-1
$dotaTestBuild = 'build/local-native-1'
$dotaTestBdaSha = (Get-FileHash "$dotaTestBuild/DOTA2.bda" -Algorithm SHA256).Hash.ToLowerInvariant()
$dotaTestElfSha = (Get-FileHash "$dotaTestBuild/DOTA2.elf" -Algorithm SHA256).Hash.ToLowerInvariant()

python tools/verify_match_time_mips.py --build $dotaTestBuild --output build/tests/local-match-time-mips-1 --expected-bda-sha256 $dotaTestBdaSha --expected-elf-sha256 $dotaTestElfSha
python tests/verify_vision_mips.py --build $dotaTestBuild --output build/tests/local-vision-mips-1 --expected-bda-sha256 $dotaTestBdaSha --expected-elf-sha256 $dotaTestElfSha
```

两项 MIPS runner 都先由 Host 核心生成初始夹具和布局，再通过 `tests/mips_core_model.py` / Unicorn 调用这份 BDA/ELF 的公开核心函数。当前各有六个窄用例。初始状态设置与实际函数执行分开记录，结果不靠测试器在执行后直接写入 Game 修补。

它们不会运行原生入口、MXU、LCD、固件调度或真实 PCM；文件与堆服务由模型提供。这是实际 MIPS 核心结果检查，不是整台 H1 的模拟或真机体验证明。`--check-interface` 仅展示接口准备状态，不执行 BDA，不能当作通过六用例。

## 结果身份与时效

检查报告的 `passed`、`source_current`、BDA/ELF/metadata SHA、`compiled_source_sha256` 或源码输入表，以及测试自身与夹具的哈希。源码或顶层 `tools/*.py` 改变后，旧候选可能已过期，应重新构建；不要把旧报告的成功用于当前修改。

历史 `provenance/source-curation.json` 和 `docs/verification/v0.27-h1.json` 保持原版身份。v0.28 候选869的曾有结果也只对应那份文件，不能自动覆盖后续性能优化候选。最终发布身份另在 `provenance/releases/` 记录；没有发布身份时构建/静态检查可以进行，正式封包仍会拒绝。

## 工作量分析和真机测试

`tests/profile_actual_vision.py` 使用确切 BDA 与只读取得的173544字节 Game 快照，统计 MIPS 指令槽与函数分布。它是性能定位工具，不是规则验收或物理 FPS；不包含缓存/总线、除法周期、固件音频视频与调度开销。复跑需要匹配的候选、快照和新目录：

```powershell
python tests/profile_actual_vision.py --build build/local-native-1 --snapshot PATH/TO/game.bin --output build/tests/local-vision-profile-1 --warm 8 --ticks 32
```

`tests/physical_vision_h1.py` 是另外的真实设备流程，要求显式提供已安装的 H1Debug 工作区，使用正常输入和只读 RAM 采样。它不属于普通离线回归或 CI，会实际操作 H1；设备归属、独占连接和启动安排应由真机测试负责人处理。本页没有执行它，也不把 Host/MIPS 成功等同真机通过。
