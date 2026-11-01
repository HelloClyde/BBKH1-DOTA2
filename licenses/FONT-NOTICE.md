# Microsoft 位图字形来源与授权缺项

当前项目没有使用OFL字体。基础UI的 `private/generated/dota_font.h` 现包含672个位图字形（原646个保持，v0.29追加26个）；`assets/runtime/SCENE.H1P`的 `catalog.fnt` 包含1,012个装备/说明字形。它们由本机Windows字体通过Pillow离线栅格化，H1运行时不加载TTF/TTC。本轮追加记录见 `docs/provenance/fonts-v0.29.json`。

| 原字体 | 版本 / face | 字体文件内版权记录 |
| --- | --- | --- |
| `msyh.ttc` | Microsoft YaHei，Version 6.31，collection index 0 | © 2024 Microsoft Corporation. All Rights Reserved. Portions © 2024 Beijing Founder Electronics Co. Ltd. All Rights Reserved. |
| `consola.ttf` | Consolas Regular，Version 7.00 | © 2018 Microsoft Corporation. All rights reserved. |

源文件中的 name 表将它们描述为 “Microsoft supplied font”，将使用、嵌入与打印等权限关联到原 Microsoft 产品/服务/内容的条款及字体嵌入限制，并未提供 OFL 或 GPL 授权。本项目没有另行取得或确立将这些派生位图子集再分发的授权。

**原始字体文件未随本仓库或安装包复制，不等于已经获得位图字形的再分发许可。** 本项目的 GPL-2.0-or-later 许可不向这些字形形状重新授予权利；私有仓库状态和来源标注也不替代第三方授权。这里保留现有生成文件及准确来源，未将其误标为开放字体，未声称许可问题已经解决。后续公开或另行分发时仍需由相应权利人授权，或完成经过验证的可再分发字体替换。

原生成参数为 12px 栅格化、14 行位图，ASCII 步进 7、中文步进 12，分别使用 y=1 / y=-2 偏移，灰度阈值 90。现有基础字形头文件 SHA-256 为：

```text
a75aa3c0e2f0c17adc3ffc50fc5f99e0306f9a22b679ce5226e54bcb72323ffc
```

2026-11-01 的来源审计读取了当前原字体 name 表和 SHA-256，并用上述算法逐一重新栅格化，与已提交 646 个基础字形比较，全部一致。装备字形来源清单中的两个源字体 SHA-256 也与当前原字体相符。原基础生成清单没有记录源字体文件 SHA-256，此次核对作为补充审计单独保存，不伪造原记录。

完整 name 表字段、哈希、原清单身份和比较结果见 [../docs/provenance/fonts.json](../docs/provenance/fonts.json)。该记录只保存文本与文件身份，不复制源字体二进制。许可证/条款 URL 为字体 name 表中的原值，不能将链接本身视为本项目获得授权的证明。
