# 素材来源

本文件记录图像、模型、地图、声音与字形的来源。自有运行代码的 GPL-2.0-or-later 不覆盖这些第三方内容；总许可边界见 [../NOTICE.md](../NOTICE.md)。

## Valve 游戏素材

Dota 2 英雄、技能、物品图像、模型、地图、动画、音乐及音效属于 Valve Corporation。本项目从开发者本机 Steam 安装只读提取资源并适配 H1，没有获得可在此重新授予的素材所有权或独立自由许可，无 Valve 官方背书。

早期界面 PNG 来自 Valve 官方 Dota 2 CDN：`https://cdn.akamai.steamstatic.com/apps/dota2/images/dota_react/`。后续原素材来自同一本机 Steam 安装的 `pak01_dir.vpk` 与 `maps/dota.vpk`，包括原地形、树木与建筑实例、英雄及装备、骨骼动画、小兵/野怪、图标、粒子贴图及音频。保留的来源清单按资源记录 VPK 路径、CRC、SHA-256 和生成输出身份。

离线导出使用 [Source 2 Viewer](https://s2v.app) / [ValveResourceFormat](https://github.com/ValveResourceFormat/ValveResourceFormat) 20.1。其 MIT 许可适用于工具代码，不会把导出的游戏素材变为 MIT 素材。粒子字段及部分操作符解释参考其公开实现；这同样不改变 Valve 原资源权属。

泉水离线处理参考原 `global_lit_simple` 编译着色器及材质，解压字典来自同次安装的 `materialsystem2.dll`。H1 使用离线图层与缩短的循环动画，没有执行原着色器或完整粒子虚拟机。

声音包包含原音乐与技能、攻击、界面等音效，历史清单共 5 段音乐与 114 条音效。它们来自原 `vsnd_c` / `vsndevts_c`，为 H1 转换格式、采样与分块，仍为 Valve 素材。

## 仓库内位置

| 位置 | 内容 / 来源 |
| --- | --- |
| `assets/runtime/DOTA2.H1R` | v0.30统一运行包，112,861,448字节；含下列四个逻辑流 |
| 包内SCENE | 预烘地图、单位、动作、特效、界面资源及装备字形；完整原始虚拟字节与偏移保持不变 |
| 包内SOUND | 原音乐与音效的连续RAW区段，原音频字节不变，不二次压缩 |
| 包内VISION | 原地图高度/树木记录派生的139,328字节数据，完整字节不变 |
| 包内TREE | 原树体/树桩与无树背景的颜色、透明度和深度层；稀疏v2表示无损还原原canonical瓦片 |
| `private/generated/dota_art.h`、`dota_overview.h`、`icon.png` 等 | 原图标、小地图与程序图标的转换输出 |
| `private/generated/dota_shard_art.h` | 原 Steam 冰晶分身和喷吐图标，24×24 RGB565；来源清单记录于原 `private/shards/ui/manifest.json` |
| `private/generated/dota_font.h` | 676字形的Microsoft字体位图子集，另见下文 |
| `docs/screenshots/` | 本项目 H1 真机截图及明确标注的 host 渲染预览；含上述第三方图像与字形 |

统一包通过Git LFS存储。准确长度、物理SHA-256与四个逻辑流SHA-256见 [runtime/manifest.json](runtime/manifest.json)。原五个二进制文件合计450,526,944字节，统一包减少74.95%；去重保留所有动作信息，不降低颜色精度或删减帧。MAP12曾与SCENE重复存储562个相同地图瓦片，本版不再单独保存这份副本。

原五包、旧 `SCENE.json` 及其哈希已在离线构建目录 `build/v030/legacy/` 留档；必要小型来源与转换校验见 `provenance/resource-v030/`。TREE稀疏转换逐一还原并比对364个有效瓦片；无模型、原始像素或音频内容重烘。文件转换及哈希记录用于追溯来源，不是再分发许可。

## v0.28 视野数据来源

包内VISION逻辑流从已提交的 `provenance/vision/height-grid.i16` 和 `trees.json` 生成，输入源追溯到原地图的 `maps/dota.gnv`、`maps/dota.trm` 与 `maps/dota/world_physics.vmdl_c`。原VPK树身份、条目CRC/SHA及坐标/取整规则见 `provenance/vision/source-receipt.json`，视野数值与适配范围见 `provenance/vision/core-source-audit.json`。这些来源记录不改变Valve原地图与树木数据的权属。

`tools/prepare_vision.py --check` 可用上述仓库内小型输入逐字节复核输出，不需要Steam、NumPy或读取开发机路径。它不是行走碰撞/寻路可通行位图，树木掩码未加移动单位体积。发布程序将该文件作为必需视野资源，不把缺失文件当成正常无声降级。

v0.28曾将视野数据作为独立文件交付；v0.30在统一包中保留完全相同的逻辑流，不再要求设备单独放置VISION文件。栅格遮挡和高度分层是本项目适配，不是Valve完整战争迷雾、坡面或粒子着色器引擎的移植。

原地图五个启用的 `trigger_no_wards` 实体与其内嵌 PHYS 凸包用于禁插眼区域：两处泉水、两处肉山区及另一处小区域。来源审计位于 `provenance/ward-placement/source-data/source-audit.json`，原始实体条件和空连接记录见 `provenance/ward-placement/activation-audit.json`。五个小型原编译模型、PHYS 文本和 CRC/SHA 身份随来源审计保存；派生的 `game/ward_placement_data.h` 使用七个原凸包的二维 Q16 投影并集，不以整个视野隔离区或回血半径替代。原 Source 高度区间保留在审计中，H1 放置接口不模拟任意三维高度的引擎触发。数据及其转换结果仍保留 Valve 原来源与权属。

## 字体

基础UI现为676字形（历史646字形保持，v0.29追加26字，v0.30再追加“历双史己”4字，原672条记录不变），装备UI为1,012字形，使用本机Microsoft YaHei / Consolas离线栅格化。源字体文件没有随库或安装包复制；生成的位图字形仍不能据此认定为自由授权。本项目未另获或确立这些位图子集的再分发授权。字体不是Noto、Unifont，也不适用OFL。原字体身份见 [../docs/provenance/fonts.json](../docs/provenance/fonts.json)，历史追加见 [../docs/provenance/fonts-v0.29.json](../docs/provenance/fonts-v0.29.json)，本轮来源见 [../docs/provenance/fonts-v0.30.json](../docs/provenance/fonts-v0.30.json)，许可说明见 [../licenses/FONT-NOTICE.md](../licenses/FONT-NOTICE.md)。

## 代码依赖

`sdk/` 保持上游Apache-2.0许可；精确MXU内核保持GPL-2.0-or-later。统一包的有界DEFLATE读取采用 [miniz 3.1.0](https://github.com/richgel999/miniz/tree/3.1.0) 低层tinfl，保持MIT许可，完整文本见 [../third_party/miniz/LICENSE](../third_party/miniz/LICENSE)，来源哈希与无标准库适配记录见 `third_party/miniz/UPSTREAM.json`。解码器的MIT许可不延伸到游戏素材。源码许可与游戏资源、字形权属分开处理，详情见根目录 [NOTICE.md](../NOTICE.md)。
