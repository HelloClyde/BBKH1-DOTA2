# v0.29 candidate3 验证

错误页 BACK 退出修复后重新执行。四份 JSON 从 `build/tests/fog-motion/final-candidate3-*/verification.json` 按原始字节复制，均为 `passed: true`。三个 host 报告的 `source_current` 均为 `true`；MIPS 报告绑定本候选 BDA、ELF 和编译输入哈希。candidate2 的原始 JSON 与 PNG 均未覆盖。

| 记录 | 结果 |
| --- | --- |
| [motion.json](motion.json) | 390 镜头帧、65 行走状态，40,795,664 个对应地图像素零差异；即时可见性与 15 块缓存覆盖通过。 |
| [trees.json](trees.json) | 树 966、邻树 896 的冠层命中、独立砍倒与重生通过；RGB/深度完整还原；65 帧砍树后运动另比较 5,881,932 像素零差异。 |
| [cache.json](cache.json) | 196 帧缓存开启、关闭、分配失败的完整画面、深度、视野查询和统计一致。 |
| [mips-pan.json](mips-pan.json) | 完整 BDA 在 MIPS32 模型中加载真实 TREE，完成 8 帧预热及 3 帧拖镜渲染。渲染指令数为 5,219,603 / 4,946,192 / 4,968,253。 |

这些运行未访问 USB，也未测量真机 FPS。MIPS 使用既有设备状态快照，新增的 4500 字节树状态以零初始化，报告中保留该说明。

- BDA SHA256：`fe492e687875ec31168778f266d5010e4479b89a0ae901acc061b8722d95bcca`
- ELF SHA256：`56c8d9f169c379223b2b0040a999840d34613ec51833586f45d35e249130efa7`
- TREE.H1T SHA256：`81cc4839bc016445a36cd5157e978189d11fea8c1ada2386e7c16d3a5f3b8f47`

| 文件 | SHA256 |
| --- | --- |
| motion.json | `a9efe94ce7b8d986363ecc11d331155f40fd3d0f98c256543171b285e584c544` |
| trees.json | `61732120497b31005766c763ba53d71f8ce2d3ad73b8cbbce73655869007281d` |
| cache.json | `ac34d25792c0bab8ae1b5edfd83fcddb6549de0ef72bd0486139a0148618d1ee` |
| mips-pan.json | `8f93ab45c52d9fe64cff2833f7fb1edcab9ac0ce5347a6eee13f471d43f1463c` |

candidate3 的[砍树与重生近景](../../../../docs/screenshots/host-v029-tree-cut-regrow.png)与已保留的 candidate2 PNG 逐字节相同，因此复用同一文件，未覆盖图片。PNG SHA256：`ae293e4bbf898f69db701bf13acfca3e5b1832d3e3a8f413344e7d8f9f5ab3d1`。图像来自 host 实际 C 渲染输出，不是真机截图。
