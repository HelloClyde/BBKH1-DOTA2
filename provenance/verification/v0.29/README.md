# v0.29 host / MIPS 原始验证记录

压缩新 TREE 资源后的 [candidate4 验证索引](candidate4/README.md) 单独绑定 17,539,264 字节新树包。该候选 BDA/ELF 与 candidate3 相同；仅重跑真实树资源与 MIPS 检查，没有重复镜头和缓存测试。

修复运行时错误页 BACK 退出后的 [candidate3 验证索引](candidate3/README.md) 已单独归档；本目录下四份 JSON 和下述候选哈希仍保留 candidate2 原始记录。

以下 JSON 从 `build/tests/fog-motion/final-candidate2-*/verification.json` 按原始字节复制，未重写字段或换行。四份报告均为 `passed: true`；前三份包含 `source_current: true`，MIPS 报告通过 BDA、ELF 和编译输入哈希绑定候选程序，不包含该字段。

| 原始记录 | 验证范围 |
| --- | --- |
| [motion.json](motion.json) | 390 镜头帧、65 行走状态，40,795,664 个对应地图像素零差异；即时敌方可见性；15 块缓存相位覆盖。 |
| [trees.json](trees.json) | 真实 TREE 资源；树 966 与邻树 896 的冠层命中、独立砍倒、重生、RGB/深度还原；65 帧运动再比较 5,881,932 像素零差异。 |
| [cache.json](cache.json) | 196 帧缓存开启、关闭、分配失败的完整画面、深度、视野查询和统计一致。 |
| [mips-pan.json](mips-pan.json) | 最终完整 BDA 在 MIPS32 模型中执行，真实 TREE 已加载，8 帧预热、3 帧拖镜渲染。旧设备状态快照的新增 4500 字节树状态以零初始化，报告中明确记录。 |

这些记录没有访问 USB，也没有测量真机 FPS。MIPS 指令数不代表设备总耗时；真实 H1 记录由发布总汇另行登记。

另存的原始检查包括 [tree-gameplay-mips.json](tree-gameplay-mips.json)（12组完整BDA树/物品/技能/重生用例）、[vision-mips.json](vision-mips.json)（7组完整BDA视野回归）、[pause-help-host.json](pause-help-host.json)（27组实际输入与帮助/资源错误检查）以及 [tree-visual-loader-host.json](tree-visual-loader-host.json)（实际C文件读取、两种无损codec、重叠、CRC与分配失败清理）。这些复制身份见 `additional-copy-receipts.json`。

候选 BDA SHA256：`7dd00090b59f79da03dddb996dcd38474a0a468fc289c139770c474c404f4ee9`。

TREE.H1T SHA256：`81cc4839bc016445a36cd5157e978189d11fea8c1ada2386e7c16d3a5f3b8f47`。

| 文件 | SHA256 |
| --- | --- |
| motion.json | `50573afbba8971447509434b663f670e38876c0f32e1872f25671f10fc405328` |
| trees.json | `e72bed6e96b5ecdbe58944b050b822f097ffd12cdeadaba6246581fa69073db3` |
| cache.json | `bc76e13196d8c8ae414d48ce377063bba5dd46da845fc6da256b7ee8df68fcf2` |
| mips-pan.json | `fd085aab0211c1b75f7f7c076f0c06a9fe8c96a10c0398efb414034c2117f3e3` |

[树木站立、砍倒、重生对比图](../../../docs/screenshots/host-v029-tree-cut-regrow.png) 来自 host 执行实际 C 渲染器的画面，按实际差异区域裁剪并使用最近邻放大，不是真机截图。PNG SHA256：`ae293e4bbf898f69db701bf13acfca3e5b1832d3e3a8f413344e7d8f9f5ab3d1`。
