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

该仓库为私有，需有效的 GitHub 访问权限。`sdk/` 固定提交为 `067fe072477861dfc8949d7b1a55279fb92d2548`，不要为了构建而更新到上游最新提交。三个 Git LFS 对象位于 `assets/runtime/`，总计 432,848,352 字节。其长度与 SHA-256 保存在同目录 `manifest.json`。只有几十或几百字节的 LFS 指针不能代替素材文件。

## 工具链和依赖

本地验证环境为 Windows、Python 3.11.5、GNU MIPS GCC 15.2.0。Python 包固定在 `tools/requirements-build.txt`：Pillow 12.3.0、pyelftools 0.33。Pillow 用于 SDK 图标打包，pyelftools 用于 ELF 验证；H1 成品没有这些运行依赖。

```powershell
python -m pip install -r tools/requirements-build.txt
python tools/install_toolchain.py
```

安装器只提供 Windows 版本工具链，校验固定下载包的 SHA-256 后解压到 `.tools/toolchain/`；可以用 `--archive PATH` 指向已有同一 ZIP。它会保留已存在目录。使用已有工具链时跳过安装器，将以下命令的 `--toolchain` 指向含 `mipsel-none-elf-gcc` 与 `mipsel-none-elf-objcopy` 的目录。构建器也可读取 `H1_GNU_BIN`。其他系统可提供同版本原生工具链，但此说明不声称已经完成其他平台的复现验证。

## 构建、校验、打包

```powershell
python tools/build.py --toolchain .tools/toolchain/bin --output build/native
python tools/verify.py --build build/native
python tools/package.py --build build/native --output dist
```

脚本拒绝覆盖已有输出目录；再次执行时用新的 `--output`，并让 verify/package 的 `--build` 指向对应目录。生成的 `DOTA2.bda`、ELF、map、构建身份记录及校验文件保存在 `build/native/`，安装 ZIP 和独立程序保存在 `dist/`。

构建器使用固定源文件顺序、编译选项、图标和 BDA 头日期，并校验固定 SDK、素材与历史 v0.27 BDA 身份：

```text
608b9fdf3c2751b0c14378663e638819a749e7603b45d231001f9b097f43184d  DOTA2.bda
```

BDA 格式/ELF/哈希校验失败时不会把结果标成成功。路径无关构建记录与包清单由工具生成。BDA 身份一致允许复用[原真机记录](verification/v0.27-h1.json)，并不表示脚本或 CI 另行运行了 H1 或模拟器。安装方法见[主 README](../README.md#安装到-h1)。

## 素材与字形

普通构建直接使用已提交的生成头文件与 LFS 包，不读开发机字体目录，不访问 Steam，也不重新生成数百 MB 素材。`private/` 名称来自原工程，其中的必要生成文件与机制来源已按实际依赖收录；它不是需要整体忽略的临时目录。

离线烘焙与游戏资源提取的原始实验环境不是此构建入口的依赖。需要重做美术或字形时，先核对[素材来源](../assets/NOTICE.md)及[字体授权缺项](../licenses/FONT-NOTICE.md)。现有字体来自 Microsoft 字体，不能改称 OFL 或项目原创。
