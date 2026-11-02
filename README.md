# DOTA2 · BBK H1 离线版

面向步步高 **H1 / Y100 V1.41** 的 Dota 2 离线适配，分辨率 **480×272**，支持实体键与触摸。原地图、模型和动画经过离线预渲染，提供十名英雄的电脑对局。

![H1 真机游戏画面](docs/screenshots/h1-v030-resources.png)

## 游戏内容

- 十英雄不重复阵容，快速模式经验与金币，技能、天赋和魔晶。
- 动态河水、泉水、阴影、战争迷雾，以及可砍伐、会重生的树木。
- 装备合成与推荐、自动信使、独立传送卷轴、插眼和视野。
- 十人结算、MVP 和最近 50 局战绩。

当前为开发版，支持离线电脑对局，部分原版机制仍有简化。[性能实测](docs/PERFORMANCE-v0.30-round3.md)

## 安装

从 [Releases](https://github.com/HelloClyde/BBKH1-DOTA2/releases) 下载 **DOTA2-H1-install.zip**，已包含程序和全部素材。

解压到 **H1 A 盘根目录**，安全弹出后打开 **DOTA2**。A 盘空间不足时，将包内 **DOTA2 文件夹放到 B 盘根目录**，`应用/程序` 保留在 A 盘。

素材约 **113 MB**；更新时保留 `MATCH0.H1S`、`MATCH1.H1S` 战绩文件。

## 操作

| 操作 | 按键 / 触摸 |
| --- | --- |
| 移动、攻击 | 方向键 / 点地或点目标 |
| 技能 | Q / W / E / R，额外主动技能 T |
| 装备 / 传送卷轴 | Z / X / C / V / B / N；卷轴 G |
| 暂停 / 商店 | BACK / M |
| 切换视野 / 回中 | 点击或拖动小地图 / SPACE 或点击英雄状态栏 |
| 查看历史对局 | 主菜单 R |

指向技能先按技能键，再点目标；Esc 取消。暂停菜单可打开帮助。[完整键位](docs/CONTROLS.md)

## 构建

需要 Windows、Python 3.11、Git 和 Git LFS。SDK 固定到 [`067fe07`](https://github.com/MrDefinition1999/bbk-h1-bda-sdk/commit/067fe072477861dfc8949d7b1a55279fb92d2548)，脚本安装 GNU MIPS GCC 15.2.0。

```powershell
git lfs install
git clone --recurse-submodules https://github.com/HelloClyde/BBKH1-DOTA2.git
cd BBKH1-DOTA2
git lfs pull
python -m pip install -r tools/requirements-build.txt
python tools/install_toolchain.py
python tools/build.py --output build/native
python tools/verify.py --build build/native
```

普通构建使用现成素材，无需 Steam 或重新烘焙。[构建说明](docs/BUILDING.md) · [对局存档](docs/MATCH-HISTORY.md)

## 作者与许可

作者 **HelloClyde**。自有代码采用 [GPL-2.0-or-later](LICENSE)；游戏素材、字体及第三方组件的权属见 [来源说明](NOTICE.md)。本项目为独立适配，无 Valve 官方背书。

感谢 Valve、[BBK H1 SDK](https://github.com/MrDefinition1999/bbk-h1-bda-sdk)、[Source 2 Viewer](https://github.com/ValveResourceFormat/ValveResourceFormat)，以及协助反馈和真机测试的用户。
