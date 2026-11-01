# v0.29 candidate4 新树包验证

candidate4 的 BDA、ELF 与 candidate3 逐字节相同，TREE.H1T 缩减为 17,539,264 字节。本轮只重跑真实树资源渲染测试及一次短 MIPS 拖镜检查；candidate3 的镜头、行走和缓存报告保留，不冒充本轮重跑结果。

两份 JSON 从 `build/tests/fog-motion/final-candidate4-*/verification.json` 按原始字节复制，均为 `passed: true`。所有较早候选的报告和 PNG 均未覆盖。

| 记录 | 结果 |
| --- | --- |
| [trees.json](trees.json) | `source_current: true`；零砍树保留原始 RGB/深度且无树块读盘；树 966 与邻树 896 独立砍倒、冠层命中、重生及缓存失效通过。65 帧砍树后运动比较 5,881,932 个对应像素，零差异。 |
| [mips-pan.json](mips-pan.json) | 实际完整 BDA 在 MIPS32 模型中加载新 TREE，完成 8 帧预热、3 帧拖镜渲染。渲染指令数为 5,219,603 / 4,946,192 / 4,968,253。 |

另逐字节核对了 candidate3 与 candidate4 的 65 帧原始树木画面，以及站立、单树砍倒、两树砍倒、重生和近景 PNG，全部相同。树模块动态内存为 2,248,855 字节。本轮没有访问 USB或测量真机 FPS；设备验收由发布总汇另行记录。

- BDA SHA256：`fe492e687875ec31168778f266d5010e4479b89a0ae901acc061b8722d95bcca`
- ELF SHA256：`56c8d9f169c379223b2b0040a999840d34613ec51833586f45d35e249130efa7`
- 新 TREE.H1T SHA256：`cb665855d9b4d8c20f2e0603492ed4425cb11a2c3763f51eadf30c5d9162aa80`

| 文件 | SHA256 |
| --- | --- |
| trees.json | `e6aca6d0e109b8d344053e5fcb5e1fd5792e00279037f8b4332cde953195b003` |
| mips-pan.json | `f966d5177984dfb4a70665563e0231ef4aa1d8f1511be45927dbc29f3fdf0031` |

[砍树与重生近景](../../../../docs/screenshots/host-v029-tree-cut-regrow.png)与本轮输出逐字节相同，继续引用已保留的原 PNG。图像来自 host 实际 C 渲染输出，不是真机截图。
