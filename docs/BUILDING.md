# 构建与验证

仓库面向 H1 / Y100 V1.41 的 MIPS little-endian 原生 BDA，固定画面 480×272。应用在 H1 上运行自己的 C 游戏逻辑与预烘图像合成，不执行 Android APK、Unity 或 Source 2。

## 获取完整输入

```powershell
git lfs install
git clone --recurse-submodules https://github.com/HelloClyde/BBKH1-DOTA2.git
cd BBKH1-DOTA2
git lfs pull
git submodule update --init --recursive
```

该仓库为私有，需有效的GitHub访问权限。`sdk/`固定提交为 `067fe072477861dfc8949d7b1a55279fb92d2548`，不要为了构建而更新到上游最新提交。SCENE、SOUND、MAP12、TREE由Git LFS保存，另有小型 `VISION.H1V`，均位于 `assets/runtime/`；五个二进制文件总计450,526,944字节。其长度与SHA-256保存在同目录 `manifest.json`。LFS指针不能代替素材文件。

## 工具链和依赖

本地验证环境为 Windows、Python 3.11.5、GNU MIPS GCC 15.2.0。Python 包固定在 `tools/requirements-build.txt`：Pillow 12.3.0、pyelftools 0.33。Pillow 用于 SDK 图标打包，pyelftools 用于 ELF 验证；H1 成品没有这些运行依赖。

```powershell
python -m pip install -r tools/requirements-build.txt
python tools/install_toolchain.py
```

安装器只提供 Windows 版本工具链，校验固定下载包的 SHA-256 后解压到 `.tools/toolchain/`；可以用 `--archive PATH` 指向已有同一 ZIP。它会保留已存在目录。使用已有工具链时跳过安装器，将以下命令的 `--toolchain` 指向含 `mipsel-none-elf-gcc` 与 `mipsel-none-elf-objcopy` 的目录。构建器也可读取 `H1_GNU_BIN`。其他系统可提供同版本原生工具链，但此说明不声称已经完成其他平台的复现验证。

## 构建、校验、打包

```powershell
python tools/build.py --toolchain .tools/toolchain/bin --output build/native
python tools/verify.py --build build/native
python tools/package.py --build build/native --output dist
```

脚本拒绝覆盖已有输出目录；再次执行时用新的 `--output`，并让 verify/package 的 `--build` 指向对应目录。生成的 `DOTA2.bda`、ELF、map、构建身份记录及校验文件保存在 `build/native/`，安装 ZIP 和独立程序保存在 `dist/`。

构建器使用当前版本约定的源文件顺序、编译选项、图标和BDA头日期，校验固定SDK、五个运行素材及实际ELF负载、ABI和RAM。v0.29为33个编译单元，Game/Unit/View为178044/240/112字节，私栈64KiB，应用须留在 `0x83ff0000` 之前。

未设置版本发布身份时，允许生成和静态校验开发候选；`tools/package.py` 会拒绝为其生成正式安装包。完成独立验收后，在 `provenance/releases/v0.29.json` 记录本版版本号与完整 BDA SHA，再对同一候选封包。发布身份文件独立绑定于 `PACKAGE.json`，不是编译输入，添加它不要求重编译已测试候选。

`provenance/source-curation.json` 始终保留为历史 v0.27 来源，当前源码哈希由每次 `DOTA2.build.json` 独立记录。源码、素材、BDA 格式或 ELF 验证失败均不能标成功。[原真机记录](verification/v0.27-h1.json)仅对应旧版确切 BDA，不能证明 v0.28 已通过设备验收。脚本和 CI 本身不运行 H1 或模拟器。安装方法见[主 README](../README.md#安装到-h1)。

v0.28 最终交付使用规范 LF 的源码。对159项输入的检查只转换了 `platform/baked_scene.c/.h` 的 CRLF，JSON和二进制保持原样，映射见 `provenance/releases/v0.28-line-endings.json`。规范后完整重编的 BDA 和 ELF 均与真机测试版本逐字节相同，两组窄 MIPS 回归另绑定最终元数据。旧 UI 主机报告仍记录转换前字节哈希，通过换行映射和相同二进制继承其等价性，未将其旧 `source_current` 当作当前原始字节身份。当前设备功能与帧率限制见[本版记录](verification/v0.28-h1.json)。

## 素材与字形

普通构建直接使用已提交的生成头文件与 LFS 包，不读开发机字体目录，不访问 Steam，也不重新生成数百 MB 素材。`private/` 名称来自原工程，其中的必要生成文件与机制来源已按实际依赖收录；它不是需要整体忽略的临时目录。

离线烘焙与游戏资源提取的原始实验环境不是此构建入口的依赖。需要重做美术或字形时，先核对[素材来源](../assets/NOTICE.md)及[字体授权缺项](../licenses/FONT-NOTICE.md)。现有字体来自 Microsoft 字体，不能改称 OFL 或项目原创。

## v0.28 必需视野素材与近似范围

`assets/runtime/VISION.H1V` 是从同一原地图派生的小型高度/树木视野数据，不替换 `SCENE.H1P`、`SOUND.H1A` 或 `MAP12.H1C`。其长度与 SHA 按运行清单核验；缺失、损坏或仍是 LFS 指针均不能用于正式封包。

发布程序把 VISION 作为必需资源：文件缺失、内容无效或视野工作内存分配失败时，在打开PCM音频之前进入明确的资源错误页（错误15），不进入对局；BACK可正常退出。它不会悄悄退回无树木/高地遮挡的玩法。只有MAP12地图压缩缓存保留原有回退。当前格网和分层高地属于本项目近似，不声称精确复现 Valve 坡面算法或完整战争迷雾引擎。

视野掩码共享缓存有128项，完整键包含源格位置、格半径、高度层、肉山区、飞行标记和动态树版本。命中后把2,084字节复制回该视野源自己的掩码，避免共享项被替换影响已有源；未命中仍运行相同遮挡算法。增加270,848字节堆内存，最终 MIPS 视野工作区为1,062,736字节，加139,264字节 VISION 负载共1,202,000字节。一次真实出兵后快照的24 tick对比中，完整 Game、当前/已探索视野和单位可见性逐字节一致，core 指令槽减少5.47%；这是指令解释器工作量，包含不了设备缓存、总线、渲染、音频或调度，不能直接推为 H1 FPS。

另有184,320字节的可选雾着色结果缓存；分配失败走原渲染路径。它仅复用相同完整镜头、洞区、建筑存活状态、团队、实际商店/莲花/水动画状态及1,440个新鲜视野采样结果下的颜色处理。每帧仍绘制地图、深度、地面补丁和静态设施，再重新采样视野；英雄、阴影和动态特效照常绘制。连续四次键匹配后才捕获，资源读取失败会失效。196帧主机渲染的 RGB、深度和查询结果比对相同；该检查及抽取函数的 MIPS 工作量测试不等于整机帧率验收。

守卫初始生命200来自原物品创建值；当前英雄/非英雄攻击分别固定扣100/25、技能不伤眼属于H1适配，尚未完整解出原引擎受击回调，不能把这部分声称为全边界原版等价。

## v0.29 交互树与固定地图迷雾

迷雾控制点固定在全局投影地图的8像素格上，包含完整屏外插值边界。每帧仍即时查询视野，控制点深度来自同一全局地图像素；树被砍后叠加对应深度。最多1701个控制点，缓存键包含树状态与资源生命周期。地图tile缓存从16槽改为15槽，所有128×128镜头相位下视口与halo总需求不超过15槽，释放81,936字节固定存储。旧v0.28的1440点数断言不用于验证本版。

2113棵原实体树的cut位数组为268字节，附带2113个16位重生期限、计数和版本号，每局树状态共4500字节，完整Game复制保留各自树状态。重生间隔3600逻辑tick，即180秒；期限比较处理16位回绕。导航基础改为源GNV/PHYS去树数据，叠加当前树状态与实际建筑碰撞；不是简单清除整张碰撞图的阻塞位。

`TREE.H1T`含原树体与原树桩离线层、无树地面颜色/深度、原树坐标和覆盖表，绑定原SCENE SHA。最多8层按深度保留，无截层；压缩tile在生成时逐一解码回验。未砍树画面不读树tile、不改原地图像素。第一次点树或显示砍树缺口时延迟申请图层工作空间与15槽缓存，完整新增动态分配2,248,855字节；后续缓存复用。启动缺/坏TREE报错16；运行读盘失败停止对局并显示资源错误，退出会释放全部树内存。

普通构建不调用离线烘焙器。可选 `tools/bake_tree_background.py`、`tools/bake_tree_layers.py` 需要开发者自己的原Steam素材导出；输出仅允许在当前项目build中，历史源目录只读。精简来源与QA清单见 `provenance/tree-visual/`。原版资源权属仍见素材NOTICE。

最终树包把所有图层alpha均为0、永不参与显示或命中的位置的两块base数据清0，节省6,082,452字节。364个非空tile的全部1400层字节及1,073,295个有效base像素逐字节未变，格式和颜色精度均未改变；原完整包仍在开发构建目录保留，来源与等价记录分别归档。

可选主机测试依赖 `tools/requirements-test.txt`，使用实际C模块检查重叠、重生、CRC失败和分配失败清理；迷雾测试比较同一冻结世界从不同镜头所见的重叠像素，而非复制一份着色公式作为期望。主机图像、MIPS逻辑执行与物理H1验收分别标记，均不互相替代。
