# H1 精灵绘制与 IPU 评估

目标是 H1/Y100 V1.41 的 JZ4740。评估依据为 Ingenic《JZ4740 Mobile Application Processor Programming Manual》第 12 章，印刷页 207–225；本地核对文件 SHA-256 为 `854ff1cfeafdb3dc89fdc20414bf67a49f3f8af2ff1f198df82b7e2b755f057e`。公开[原厂手册镜像](https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/dingoo-sdk/JZ4740_pm.pdf)可供核查。

手册描述的 IPU 接收外部内存中的 YUV/YCbCr 4:2:0、4:2:2、4:4:4、4:1:1 数据，通过 DMA 进行缩放和色彩转换，再向外部内存输出 RGB565、RGB555 或 RGB888。最小输入是 33×33，最大输入是 2047×2047。定义的输入格式和寄存器没有提供本项目所需的逐像素透明混合、场景深度测试或精灵合成操作。

当前离线烘焙精灵提供 RGB565、透明度和深度，多数以原尺寸绘制。直接送入此 IPU 之前需要准备 YUV 平面；处理后仍须由 CPU 做透明混合和深度遮挡。因此，IPU 不能直接替换当前精灵绘制循环。此次按能力不匹配排除该方案，没有写入 IPU、时钟或中断寄存器，也没有把其他 Ingenic 芯片的 RGB 输入或混合功能归给 JZ4740。

如果以后加入大量整幅 YUV 图像缩放，可单独评估 IPU 路径，届时需要有经过验证的驱动、DMA 缓冲所有权、缓存一致性及固件并发使用规则。当前 SDK 的图形接口没有已验证的 IPU 调用。此次精灵优化采用解码帧缓存、透明行段裁剪和阴影高度复用，收益分别通过输出等价、指令计数与实际 H1 帧率验证。
