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

该仓库为私有，需有效的GitHub访问权限。`sdk/`固定提交为 `067fe072477861dfc8949d7b1a55279fb92d2548`，不要为了构建而更新到上游最新提交。v0.30只需Git LFS中的 `assets/runtime/DOTA2.H1R`，112,861,448字节；原五包合计450,526,944字节，减少74.95%。物理文件和SCENE、SOUND、VISION、TREE四个逻辑流的长度与SHA-256见同目录 `manifest.json`。LFS指针不能代替素材文件，普通构建不需要取得旧五包。

## 工具链和依赖

本地验证环境为 Windows、Python 3.11.5、GNU MIPS GCC 15.2.0。Python 包固定在 `tools/requirements-build.txt`：Pillow 12.3.0、pyelftools 0.33。Pillow 用于 SDK 图标打包，pyelftools 用于 ELF 验证；H1 成品没有这些运行依赖。

```powershell
python -m pip install -r tools/requirements-build.txt
python tools/install_toolchain.py
```

安装器只提供 Windows 版本工具链，校验固定下载包的 SHA-256 后解压到 `.tools/toolchain/`；可以用 `--archive PATH` 指向已有同一 ZIP。它会保留已存在目录。使用已有工具链时跳过安装器，将以下命令的 `--toolchain` 指向含 `mipsel-none-elf-gcc` 与 `mipsel-none-elf-objcopy` 的目录。构建器也可读取 `H1_GNU_BIN`。其他系统可提供同版本原生工具链，但此说明不声称已经完成其他平台的复现验证。

## 开发版构建与校验

```powershell
python tools/build.py --toolchain .tools/toolchain/bin --output build/native
python tools/verify.py --build build/native
```

脚本拒绝覆盖已有输出目录；再次执行时用新的 `--output`，并让 verify 的 `--build` 指向对应目录。生成的 `DOTA2.bda`、ELF、map、构建身份记录及校验文件保存在 `build/native/`。当前未设置v0.30发布pin，以上命令即可生成并校验开发候选，不要求正式安装ZIP。

构建器使用当前版本约定的源文件顺序、编译选项、图标和BDA头日期，校验固定SDK、统一资源包及实际ELF负载、ABI和RAM。C8为37个编译单元，Game/Unit/View为179008/240/148字节，由实际ELF复核；私栈64KiB，应用须留在 `0x83ff0000` 之前。

未设置发布pin的CI构建同样执行build和verify，随后上传开发artifact中的 `build/native/DOTA2.bda`、`build/native/DOTA2.build.json`、`assets/runtime/DOTA2.H1R`。按目标提交选择成功构建，手工把BDA复制到 `A:\应用\程序\DOTA2.bda`，把完整H1R复制到 `B:\DOTA2\DOTA2.H1R`；程序身份见构建JSON，资源身份见 `assets/runtime/manifest.json`。这类artifact不含正式安装ZIP，也不表示创建了tag或Release。

## 已pin候选的正式封包

`tools/package.py` 仅在 `provenance/releases/v0.30.json` 已固定版本号及与已验证构建相同的完整BDA SHA时，才允许生成正式安装包。发布负责人完成独立验收并明确固定该候选后，才执行：

```powershell
python tools/package.py --build build/native --output dist
python tools/verify.py --package dist
```

`dist/` 包含 `DOTA2-H1-install.zip`、独立程序、构建身份和 `SHA256SUMS.txt`；已有输出目录仍保留。已pin时CI沿用这一封包和校验路径。发布身份文件独立绑定于 `PACKAGE.json`，不是编译输入，添加它不要求重编译已测试候选。上述封包命令不创建Git标签或GitHub Release；当前main开发状态无需执行。

`provenance/source-curation.json` 始终保留为历史v0.27来源，当前源码哈希由每次 `DOTA2.build.json` 独立记录。源码、素材、BDA格式或ELF验证失败均不能标成功。[原真机记录](https://github.com/HelloClyde/BBKH1-DOTA2/blob/main/docs/verification/v0.27-h1.json)仅对应旧版确切BDA，不能证明v0.30通过设备验收。脚本和CI本身不运行H1或模拟器。安装方法见[主 README](../README.md#安装到-h1)。

历史v0.28最终交付使用规范LF的源码。对159项输入的检查只转换了 `platform/baked_scene.c/.h` 的CRLF，JSON和二进制保持原样，映射见 `provenance/releases/v0.28-line-endings.json`。规范后完整重编的BDA和ELF均与当时真机版本逐字节相同，两组窄MIPS回归另绑定最终元数据。旧UI报告仍记录转换前字节哈希，通过换行映射和相同二进制继承其等价性，未将旧 `source_current` 当作当前身份。旧版设备结果见[v0.28记录](https://github.com/HelloClyde/BBKH1-DOTA2/blob/main/docs/verification/v0.28-h1.json)。

## v0.30 统一资源包

SCENE保留完整原始虚拟字节、原索引和偏移，全部44,278帧均保留；其中4,349份完全重复的帧像素负载共用存储。VISION完整字节保持一致。SOUND是10,226,944字节、4096对齐的连续RAW区段，支持独立音乐/音效文件游标，不做二次压缩。TREE使用稀疏RAW v2逻辑流，转换器还原并逐字节比对364个原canonical瓦片；颜色、深度、透明度和树归属不变。MAP12与SCENE重复的562个地图瓦片不再另存副本。

外层按最大16 KiB canonical块进行全局去重，选择RAW、raw DEFLATE或可逆delta16 DEFLATE；TREE按瓦片连续分块以减少碎读。目录使用有CRC的固定记录和分页缓存，不需要整份索引驻留。精确布局见仓库 `tools/resource_pack_format.md`，来源、源包哈希、完整逻辑流校验与可复现命令见 `provenance/resource-v030/`。

解码器采用miniz 3.1.0低层tinfl的无标准库适配，原文件、逐文件哈希及适配说明见 `third_party/miniz/UPSTREAM.json`。MIT许可全文见 [../third_party/miniz/LICENSE](../third_party/miniz/LICENSE)。普通构建不运行离线重包器；若需重新转换，须准备留档旧资源和稀疏树转换输入，并使用新的输出目录，不能把精简源码检出当作Steam素材重烘环境。

本版C8已完成正常输入加载、11个约8秒FPS窗口、暂停恢复、镜头、音频、正常吃树/砍树和退出/资源释放的限定H1验收；完整新增功能覆盖仍未完成，C8未重新等待自然180秒长回。SD恢复后的同驱动、同实际阵容对照：新开局泉水13.47→15.24 FPS，C8恢复后18.43 FPS，自然首波后中路/英雄镜头16.59 / 15.22 FPS；热窗口大体相同，没有全程15 FPS保证。设备原始数据和适用范围见[第三轮性能记录](PERFORMANCE-v0.30-round3.md)、[设备摘要](verification/v0.30-h1.json)。资源大小下降和离线逐字节等价本身不证明帧率提高，旧版FPS不作为本版结果。

## v0.30 目标输入与对局历史

传送卷轴使用独立格，UI库存索引9、主动API的legacy槽7，六格装备和三格背包保持原布局，中立装备仍使用独立legacy槽6；战场按G，商店G仍切升级去向。合法物品450毫秒双击可以自施法，卷轴选择本方泉水；目标合法性、资源和状态检查仍由游戏规则执行。超出范围的合法单位/点地目标使用有界施法订单，先寻路走近再释放，过程中不提前消耗蓝量或冷却。

历史模块在Game中仅附加204字节的本局伤害/治疗/LH/DN计数，历史环和工作区不放入Game或静态BSS。最近50局共用54,096字节固件heap，按版本化小端记录和CRC序列化；每个磁盘快照52,064字节，每次读写不超过1 KiB并调用音频服务。内存记录和持久化完成分别处理，保存失败仍可看结算并以S重试同一match_id，不重复入库。

存档优先B盘 `DOTA2/MATCH0.H1S` / `MATCH1.H1S`，备用为A盘根目录 `DOTA2.HS0` / `DOTA2.HS1`，独立于只读H1R。写入非当前有效槽并关闭、全量回读校验后才切换；保留有效旧快照。若只有损坏槽但仍有不存在的空槽，新存档可写空槽而不覆盖坏文件；四槽全部损坏时保留原文件并报错。最多四个快照文件、共208,256字节，记录容量仍为50局，不是200局。SDK没有提供fsync，此策略不等于对介质或文件系统整体断电损坏的绝对保证。

MVP为本地可解释公式，优先胜方，结合击杀、助攻、参战率、敌英雄实际伤害、建筑伤害和有效友方治疗，按稳定规则打破平局；不调用或声称复刻Valve的算法。详见[结算与历史说明](MATCH-HISTORY.md)。这些新接口的host检查、MIPS语法检查、最终BDA与H1验收分别记录，不混用。

## 素材与字形

普通构建直接使用已提交的生成头文件与 LFS 包，不读开发机字体目录，不访问 Steam，也不重新生成数百 MB 素材。`private/` 名称来自原工程，其中的必要生成文件与机制来源已按实际依赖收录；它不是需要整体忽略的临时目录。

当前基础UI为676字形；v0.30只在原672条不变记录后增加“历双史己”四字，来源见 `docs/provenance/fonts-v0.30.json`。新增交互、结算和历史复用现有英雄/装备图标，不增加素材图集。

离线烘焙与游戏资源提取的原始实验环境不是此构建入口的依赖。需要重做美术或字形时，先核对[素材来源](../assets/NOTICE.md)及[字体授权缺项](../licenses/FONT-NOTICE.md)。现有字体来自 Microsoft 字体，不能改称 OFL 或项目原创。

## 视野逻辑与v0.28历史验证

VISION是从同一原地图派生的小型高度/树木视野数据。v0.28曾作为独立 `VISION.H1V` 交付，v0.30将相同数据保留在统一包内。其逻辑流和物理统一包均按清单核验；缺失、损坏或仍是LFS指针的统一包不能用于正式封包。

VISION仍是必需逻辑资源；视野数据或工作内存不可用时不会悄悄退回无树木/高地遮挡的玩法。v0.28的独立文件错误页与MAP12回退机制属于旧版：v0.30已移除MAP12依赖，并由统一资源读取器处理容器和流错误。本版错误路径仍需最终H1验收。当前格网和分层高地属于本项目近似，不声称精确复现Valve坡面算法或完整战争迷雾引擎。

v0.28历史视野掩码共享缓存有128项，完整键包含源格位置、格半径、高度层、肉山区、飞行标记和动态树版本。命中后把2,084字节复制回该视野源自己的掩码，避免共享项被替换影响已有源；未命中仍运行相同遮挡算法。当时增加270,848字节堆内存，MIPS视野工作区1,062,736字节，加139,264字节VISION负载共1,202,000字节。其真实出兵后快照24 tick对比中，完整Game、当前/已探索视野和单位可见性逐字节一致，core指令槽减少5.47%；这是历史指令解释器工作量，不能直接推为H1 FPS。

v0.30第一轮C3候选增加静态视野union复用，相对本轮基线再增加16,468字节；MIPS工作区为1,079,224字节，加139,264字节VISION负载共1,218,488字节。相同快照预热2tick、测量2tick的Game/迷雾/单位可见性逐字节相同，指令槽减少16.72% / 12.83%，属于离线CPU工作量检查。它与本轮真机帧率分别记录于[性能记录](PERFORMANCE-v0.30.md)，不覆盖上述历史24tick证据。

第二轮C7复用8类最终障碍平面及双缓冲树木字节平面，只在树版本变化时更新变化的非墙格，缓存满后走原高度/区域判定以限制填充成本；MIPS工作区1,727,724字节，加139,264字节VISION负载共1,866,988字节。小地图每帧只借用一次current bitplane，树贴图按局部状态失效，迷雾使用双像素精确RGB565算术。第二轮C7的host/MIPS与短窗H1范围见[性能历史](PERFORMANCE-v0.30.md)；当时C5的97帧shader证据和资源reader检查为明确分层继承。第三轮C8另完成353帧像素/深度/选取范围对照、局部树变化缓存检查和完整BDA限定MIPS检查，详见[第三轮记录](PERFORMANCE-v0.30-round3.md)。

另有184,320字节的可选雾着色结果缓存；分配失败走原渲染路径。它仅复用相同完整镜头、洞区、建筑存活状态、团队、实际商店/莲花/水动画状态及1,440个新鲜视野采样结果下的颜色处理。每帧仍绘制地图、深度、地面补丁和静态设施，再重新采样视野；英雄、阴影和动态特效照常绘制。连续四次键匹配后才捕获，资源读取失败会失效。196帧主机渲染的 RGB、深度和查询结果比对相同；该检查及抽取函数的 MIPS 工作量测试不等于整机帧率验收。

守卫初始生命200来自原物品创建值；当前英雄/非英雄攻击分别固定扣100/25、技能不伤眼属于H1适配，尚未完整解出原引擎受击回调，不能把这部分声称为全边界原版等价。

## v0.29 交互树与固定地图迷雾的历史实现

迷雾控制点固定在全局投影地图的8像素格上，包含完整屏外插值边界。每帧仍即时查询视野，控制点深度来自同一全局地图像素；树被砍后叠加对应深度。最多1701个控制点，缓存键包含树状态与资源生命周期。地图tile缓存从16槽改为15槽，所有128×128镜头相位下视口与halo总需求不超过15槽，释放81,936字节固定存储。旧v0.28的1440点数断言不用于验证本版。

2113棵原实体树的cut位数组为268字节，附带2113个16位重生期限、计数和版本号，每局树状态共4500字节，完整Game复制保留各自树状态。重生间隔3600逻辑tick，即180秒；期限比较处理16位回绕。导航基础改为源GNV/PHYS去树数据，叠加当前树状态与实际建筑碰撞；不是简单清除整张碰撞图的阻塞位。

v0.29独立 `TREE.H1T` 含原树体与树桩离线层、无树地面颜色/深度、原树坐标和覆盖表，绑定原SCENE SHA。最多8层按深度保留，无截层；压缩tile在生成时逐一解码回验。未砍树画面不读树tile、不改原地图像素。旧版第一次点树或显示砍树缺口时延迟申请图层工作空间与15槽缓存，新增动态分配2,248,855字节；启动缺/坏TREE报错16。这些旧文件及内存数字不作为v0.30读取器的新验收结论；本版使用统一包内的稀疏TREE逻辑流。

普通构建不调用离线烘焙器。可选 `tools/bake_tree_background.py`、`tools/bake_tree_layers.py` 需要开发者自己的原Steam素材导出；输出仅允许在当前项目build中，历史源目录只读。精简来源与QA清单见 `provenance/tree-visual/`。原版资源权属仍见素材NOTICE。

最终树包把所有图层alpha均为0、永不参与显示或命中的位置的两块base数据清0，节省6,082,452字节。364个非空tile的全部1400层字节及1,073,295个有效base像素逐字节未变，格式和颜色精度均未改变；原完整包仍在开发构建目录保留，来源与等价记录分别归档。

可选主机测试依赖 `tools/requirements-test.txt`，使用实际C模块检查重叠、重生、CRC失败和分配失败清理；迷雾测试比较同一冻结世界从不同镜头所见的重叠像素，而非复制一份着色公式作为期望。主机图像、MIPS逻辑执行与物理H1验收分别标记，均不互相替代。
