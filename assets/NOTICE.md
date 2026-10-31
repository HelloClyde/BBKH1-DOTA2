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
| `private/generated/dota_art.h`、`dota_overview.h`、`icon.png` 等 | 原图标、小地图与程序图标的转换输出 |
| `private/generated/dota_shard_art.h` | 原 Steam 冰晶分身和喷吐图标，24×24 RGB565；来源清单记录于原 `private/shards/ui/manifest.json` |
| `private/generated/dota_font.h` | 646 字形的 Microsoft 字体位图子集，另见下文 |
| `docs/screenshots/` | 本项目 H1 真机截图及明确标注的 host 渲染预览；含上述第三方图像与字形 |

三大素材包通过 Git LFS 存储。准确长度、SHA-256 见 [runtime/manifest.json](runtime/manifest.json)；`SCENE.json` 另记录包内数据身份。文件转换及哈希记录用于追溯来源，不是再分发许可。

## 字体

基础 UI 的 646 字形和装备 UI 的 1,012 字形使用本机 Microsoft YaHei / Consolas 离线栅格化。源字体文件没有随库或安装包复制；生成的位图字形仍不能据此认定为自由授权。本项目未另获或确立这些位图子集的再分发授权。字体不是 Noto、Unifont，也不适用 OFL。实际字体名称、版权、版本、文件哈希和比较记录见 [../docs/provenance/fonts.json](../docs/provenance/fonts.json)，许可说明见 [../licenses/FONT-NOTICE.md](../licenses/FONT-NOTICE.md)。

## 代码依赖

`sdk/` 保持上游 Apache-2.0 许可；精确 MXU 内核保持 GPL-2.0-or-later。源码许可与游戏资源、字形权属分开处理，详情见根目录 [NOTICE.md](../NOTICE.md)。
