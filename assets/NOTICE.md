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
| `assets/runtime/SCENE.H1P` | 预烘地图、单位、动作、特效、界面资源及装备字形 |
| `assets/runtime/SOUND.H1A` | 转换后的原音乐与音效 |
| `assets/runtime/MAP12.H1C` | 原地图颜色与深度数据的无损压缩缓存 |
| `assets/runtime/VISION.H1V` | v0.28由同次原地图高度和树木记录派生的139,328字节视野数据 |
| `private/generated/dota_art.h`、`dota_overview.h`、`icon.png` 等 | 原图标、小地图与程序图标的转换输出 |
| `private/generated/dota_shard_art.h` | 原 Steam 冰晶分身和喷吐图标，24×24 RGB565；来源清单记录于原 `private/shards/ui/manifest.json` |
| `private/generated/dota_font.h` | 646 字形的 Microsoft 字体位图子集，另见下文 |
| `docs/screenshots/` | 本项目 H1 真机截图及明确标注的 host 渲染预览；含上述第三方图像与字形 |

三大素材包通过 Git LFS 存储。准确长度、SHA-256 见 [runtime/manifest.json](runtime/manifest.json)；`SCENE.json` 另记录包内数据身份。文件转换及哈希记录用于追溯来源，不是再分发许可。

## v0.28 视野数据来源

`VISION.H1V` 从已提交的 `provenance/vision/height-grid.i16` 和 `trees.json` 生成，输入源追溯到原地图的 `maps/dota.gnv`、`maps/dota.trm` 与 `maps/dota/world_physics.vmdl_c`。原VPK树身份、条目CRC/SHA及坐标/取整规则见 `provenance/vision/source-receipt.json`，视野数值与适配范围见 `provenance/vision/core-source-audit.json`。这些来源记录不改变Valve原地图与树木数据的权属。

`tools/prepare_vision.py --check` 可用上述仓库内小型输入逐字节复核输出，不需要Steam、NumPy或读取开发机路径。它不是行走碰撞/寻路可通行位图，树木掩码未加移动单位体积。发布程序将该文件作为必需视野资源，不把缺失文件当成正常无声降级。

三个旧大包的内容与哈希保持不变；新增小文件独立存储，不修改数百MB原图形或声音包。栅格遮挡和高度分层是本项目适配，不是Valve完整战争迷雾、坡面或粒子着色器引擎的移植。

原地图五个启用的 `trigger_no_wards` 实体与其内嵌 PHYS 凸包用于禁插眼区域：两处泉水、两处肉山区及另一处小区域。来源审计位于 `provenance/ward-placement/source-data/source-audit.json`，原始实体条件和空连接记录见 `provenance/ward-placement/activation-audit.json`。五个小型原编译模型、PHYS 文本和 CRC/SHA 身份随来源审计保存；派生的 `game/ward_placement_data.h` 使用七个原凸包的二维 Q16 投影并集，不以整个视野隔离区或回血半径替代。原 Source 高度区间保留在审计中，H1 放置接口不模拟任意三维高度的引擎触发。数据及其转换结果仍保留 Valve 原来源与权属。

## 字体

基础 UI 的 646 字形和装备 UI 的 1,012 字形使用本机 Microsoft YaHei / Consolas 离线栅格化。源字体文件没有随库或安装包复制；生成的位图字形仍不能据此认定为自由授权。本项目未另获或确立这些位图子集的再分发授权。字体不是 Noto、Unifont，也不适用 OFL。实际字体名称、版权、版本、文件哈希和比较记录见 [../docs/provenance/fonts.json](../docs/provenance/fonts.json)，许可说明见 [../licenses/FONT-NOTICE.md](../licenses/FONT-NOTICE.md)。

## 代码依赖

`sdk/` 保持上游 Apache-2.0 许可；精确 MXU 内核保持 GPL-2.0-or-later。源码许可与游戏资源、字形权属分开处理，详情见根目录 [NOTICE.md](../NOTICE.md)。
