# 许可与第三方来源

本仓库自有应用源代码、构建工具与文档，以及适配的 MXU 内核，采用 **GPL-2.0-or-later**，见 [LICENSE](LICENSE) 与 [licenses/GPL-2.0.txt](licenses/GPL-2.0.txt)。该许可只覆盖本项目有权授予的部分，不向下列第三方素材、字形或商标重新授权。

| 内容 | 来源与许可边界 |
| --- | --- |
| H1 SDK | `sdk/` 固定提交 `067fe072477861dfc8949d7b1a55279fb92d2548`，上游 [MrDefinition1999/bbk-h1-bda-sdk](https://github.com/MrDefinition1999/bbk-h1-bda-sdk)；保持 Apache-2.0，见 SDK 自身 LICENSE / NOTICE |
| 精确 RGB565 MXU 内核 | `platform/dota_mxu_exact.h` 基于原工作区 `h1-arcade/src/mxu.S` 的 GPL-2.0-or-later 叶函数，增加与应用原转色一致的位复制；保留原来源和许可注释。指令编码参考 Ingenic XBurst MXU ISA 手册 |
| Dota 2 图像、地图、模型、动画、音乐、音效及转换结果 | Valve Corporation；本机 Steam 安装及早期官方 CDN。包括运行包、嵌入图标与展示截图中的游戏内容；不适用本项目 GPL，也不因转换而成为本项目原创素材 |
| 位图字形 | Microsoft YaHei 6.31（Microsoft / 北京北大方正电子）和 Consolas 7.00（Microsoft）生成的子集；原 TTF/TTC 未复制，**项目未另获或确立位图子集再分发授权**，不标为 OFL；详见 [licenses/FONT-NOTICE.md](licenses/FONT-NOTICE.md) |
| 历史导出工具 | Source 2 Viewer / ValveResourceFormat 20.1，MIT；工具许可不延伸到 Valve 游戏资产，普通 BDA 构建不运行该工具 |

Steam 来源包括 `pak01_dir.vpk`、`maps/dota.vpk` 及原编译材质/音频条目。运行资源 `SCENE.H1P`、`SOUND.H1A`、`MAP12.H1C` 通过 Git LFS 保存，其文件身份见 `assets/runtime/manifest.json`。素材包包含 Valve 派生素材，其中 `SCENE.H1P` 还包含 Microsoft 字体生成的装备字形。

v0.28 的 `VISION.H1V` 从原地图高度与树木记录派生；禁插眼区域另从同地图五个 `trigger_no_wards` 的原 PHYS 凸包派生为二维放置数据。这些小型派生数据和来源快照也保留 Valve 权属，项目 GPL 不对其重新授权。具体来源、投影与引擎近似边界见 [assets/NOTICE.md](assets/NOTICE.md)。

背景音乐与音效经解码、转换采样和分块；模型、贴图、动画与地图经离线预渲染。这些处理不改变原素材的权属，不表示本项目已获得 Valve 的独立再授权，也不表示完整 Source 2 引擎已移植。

Dota 2 及相关名称、标志属于各自权利人；本项目无 Valve、Microsoft、字体权利人或设备厂商的官方背书。仓库私有和记录来源均不替代第三方授权。详细来源索引见 [assets/NOTICE.md](assets/NOTICE.md)；随安装包携带本文件和许可证文本，不应把整个包描述为只有 GPL 素材。
