# soft-raster（软光栅六关）

> 面向「从零实现 C++ 软光栅化」的教学级 Agent Skill：Bresenham 线段 → 重心坐标三角形与面光照 → uv 纹理 + z-buffer → 切线空间法线贴图 → Phong 高光 → 阴影贴图，纯标准 C++、零第三方库、零 OpenGL，最终渲染 `african_head.obj` 成 TGA。

本技能是一份**内含可编译实现的完整工作流**：`SKILL.md` 注入方法论与验收门禁，`src/` 提供 4 个共享头 + 6 个关卡程序（每关独立 .cpp、自带数字门禁、全绿返回 0），`assets/` 打包渲染所需全部资产。Agent 既可按关卡逐关对照检查，也可只读 SKILL.md 的 Common Mistakes 把自己的光栅器救活。

```text
soft-raster/
├── SKILL.md    # 技能本体：六关工作流 / 深度坐标约定 / 常见错误 / 判决性诊断
├── README.md   # 本文件：面向人的项目说明与验收数据
├── LICENSE     # MIT 许可证（版权署名 liuye0209）
├── assets/     # african_head.obj + diffuse / nm_tangent / spec 贴图
├── src/        # geometry.h  model.h  tga.h  raster.h  lesson0..5.cpp  tga2bmp.cpp  debug_*.cpp
├── fetch-assets.ps1   # 资产下载（gh-proxy 回退 + 尺寸校验 + 重试）
├── tga2png.ps1        # out/ 下 .bmp 批转 .png（供 agent 目视链路）
└── out/        # 编译与输出目录（构建时生成）
```

## 1. 为什么用"关卡+门禁"学光栅

从零写光栅器最致命的失败模式不是写不出代码，而是**六个 bug 叠在一张错误成图上**：明暗反转（剔除符号）+ 贴图倒置（uv 镜像）+ 浮肿（m[3][3] 清零）+ 碎面（逐面切线）+ 假高光（clamp 后的 n·l）+ 全脸阴影 acne（bias 瞎猜）同时发生，任何调试都无从下手。

本技能把这些坑拆进六个关卡，每关 carrying 全部前置知识、带显式验收门禁（打印数字 + 退出码）：

| 关 | 主题 | 一句话验收 |
|---|---|---|
| L0 | Bresenham 线段 | 头像线框成图 |
| L1 | 三角形 + 面法线光照 | 凸亮凹暗；嘴部黑块=无 zbuf 教学伪影 |
| L2 | 重心坐标 + z-buffer + 纹理 | coverage 56.08% / uv∈[0.001,0.999] |
| L3 | 切线空间法线贴图 | 逐片元 TBN 最差相关 4.4e-16；毛孔疤痕可见 |
| L4 | Phong 高光 | spec>0.5 占比 0.0193%（鼻尖/锁骨/唇） |
| L5 | 阴影贴图 | 影图 28.56%；向光面阴影占比 9.10%（bias=0.03） |

**任何一步未验收，不进入下一步**——这是本库所有 bug 都被当场抓住的原因。

## 2. 30 秒跑通
```
g++ -std=c++17 -O2 -Isrc src/lesson5_shadow.cpp -o out/lesson5.exe
out/lesson5.exe assets out
out/tga2bmp.exe out/lesson5_shadow.tga out/lesson5_shadow.bmp
powershell -NoProfile -ExecutionPolicy Bypass -File tga2png.ps1
```
（Windows + MinGW g++ 实测；每关独立编译，L5 会打印四道门禁并写 `lesson5_shadowmap.tga` 深度图 + `lesson5_shadow.tga` 成图。）

## 3. 关键设计约定（详见 SKILL.md）
- 深度：GL 右手相机、depth=1−z_ndc ∈[0,2]、**大=近**、zbuf keep-max
- 面剔除：屏幕 y 翻转 → 正面 `area2<0` → `if (area2>=0) return;`
- 贴图采样：`y=(1−v)*(h−1)`；法线贴图 R=+u / G=+v / B=+几何法线，`(c/255)*2−1`
- 切线：逐顶点累积平均（逐面切线必碎面），逐片元先归一化平滑法线再 Gram-Schmidt
- 阴影：光源正交深度趟 + 主趟 depth 比对；bias 用 delta 直方图三峰定值，不瞎调

## 4. 致谢与许可
- 课程脉络与资产来自 [ssloy/tinyrenderer](https://github.com/ssloy/tinyrenderer)（本库资产文件与诊断思路在其之上做了全新实现与验收体系）
- 本技能代码与文档以 MIT 协议发布（署名 liuye0209）
