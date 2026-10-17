---
name: soft-raster
description: Use when the user wants to build, read, teach, or extend a from-scratch C++ software rasterizer (no OpenGL / no third-party libs) — Bresenham lines, triangles with barycentric rasterization, z-buffer, texture mapping, normal mapping, Phong shading, shadow mapping, TGA output, tinyrenderer-style course. 触发词：软光栅、软渲染、tinyrenderer、Bresenham、重心坐标、z-buffer、法线贴图、切线空间、Phong、阴影贴图、渲染管线从零、图形学课程。
---

# soft-raster（软光栅六关）

## Overview
以 ssloy/tinyrenderer 的教学脉络为范本、用**纯标准 C++（无 OpenGL/无第三方库）**从零实现软光栅化工作流，逐关渲染 `african_head.obj` 输出 TGA。核心原则：**一关一文件 → 每关显式验收门禁（打印数字 + 退出码）→ 调参前先仪器化取证 → 上一关未绿不动下一关**。本技能目录已内含全部源码与资产，可直接编译验证，也可按本文工作流从零重写再对照。

## When to Use
- 从零实现/读懂/讲解软光栅（线框 → 光照 → 纹理 → 法线 → 高光 → 阴影完整管线）
- 调试自己光栅器的疑难杂症（明暗反转、鬼鼻子、碎面法线、阴影 acne、镜像贴图……本文 Common Mistakes 覆盖率极高）
- 讲授图形学入门课程（每关都有"伪影→取证→根因→修复→门禁"的完整故事）
不适用：追求性能的生产光栅（SIMD/多线程/GPU）、WebGL/OpenGL API 教学。

## 目录结构与构建
```text
soft-raster/
├── SKILL.md  README.md  LICENSE
├── assets/   african_head.obj  african_head_diffuse.tga  african_head_nm_tangent.tga  african_head_spec.tga
├── src/      geometry.h  model.h  tga.h  raster.h   lesson0..5.cpp   tga2bmp.cpp  debug_*.cpp
├── fetch-assets.ps1  /  tga2png.ps1
└── out/      （编译与输出目录，运行时生成）
```
编译运行（向 agent 提供的语言，MinGW g++）：
```
mkdir -p out && \
g++ -std=c++17 -O2 -Isrc src/lesson5_shadow.cpp -o out/lesson5.exe   # out/ 必须先建，否则 ld 报 cannot open output file
out/lesson5.exe assets out          # argv: [1]=资产目录 [2]=输出目录（L5 追加 [3]=bias，省略默认 0.03）
out/tga2bmp.exe out/lesson5_shadow.tga out/lesson5_shadow.bmp   # TGA 人眼查看的桥梁
powershell -NoProfile -ExecutionPolicy Bypass -File tga2png.ps1 # out/ 全部 .bmp 批转 .png
```
把关卡图转 PNG 再 `read` 查看（部分环境 BMP 直读不支持）；`$` 变量的 PowerShell 内联命令会被工具层吞掉，必须走 .ps1 文件。

## 六关与验收门禁（全绿才进下一关）
| 关 | 文件 | 内容 | 门禁（本库实测锚点） |
|---|---|---|---|
| L0 | lesson0_wireframe.cpp | Bresenham 线段画人头线框 | 出图线数、canvas 非空 |
| L1 | lesson1_flat.cpp | 扇形三角化 + 面法线光照（无 zbuf） | 明暗形状正确；**嘴部黑块=无 zbuf 教学伪影，勿修** |
| L2 | lesson2_texture_zbuffer.cpp | 重心坐标 uv + z-buffer + 背面剔除 | G1 depth∈[0.059,0.132]/filled 56.08%；G2 uv∈[0.001,0.999]；G3 coverage 56.08% |
| L3 | lesson3_normal_mapping.cpp | 切线空间法线贴图（毛孔/疤痕） | G2 逐片元 TBN worst |dot|≤1e-9（实测 4.44e-16）；G4 细节占比 13.04% |
| L4 | lesson4_phong.cpp | 高光（镜面反射 + spec 强度图） | G4 spec>0.5 像素占比 0.005%..15%（实测 0.0193%，指数 8 增益 1.4） |
| L5 | lesson5_shadow.cpp | 阴影贴图两趟渲染（最终成图） | G1 影图 filled>15%、depth 极差>0.05（实测 28.56%、[0.138,1.179]）；G3 向光片元阴影占比 5..45%（实测 9.10%，bias=0.03） |

共享约定（raster.h 是 L2-L5 的单一事实来源）：`IShader{ clip[3]; vertex(); fragment(bc,color,x,y) }` + `draw_triangle(shader, frame, zbuf)`（透视校正插值、面向光栅器的背面剔除、keep-max zbuf）。第 2 关起所有课程代码必须走它，**禁止每关复制光栅循环**。

## 深度/坐标约定（全库一致，先背再写）
- GL 式右手相机看 −z；NDC z∈[−1,+1]（−1 近 1 远）；深度 depth=1−z_ndc∈[0,2]，**越大越近**；zbuf keep-max、初始化 0
- `viewport()` 翻 z（m[2][2]）翻 y（m[1][1]=−h/2）；本库在 `draw_triangle` 内联映射：x=(ndc.x+1)*W/2、y=(1−ndc.y)*H/2
- TGA 内存：read 后 row0=图顶；采样 `y=(1−v)*(h−1)`；写出 desc=0（底部原点倒序）；`at()/sample()` 返回 vec3 顺序为 **R,G,B**（c.x=R）；`set()` 自动 clamp；灰度走 `scalar_sample(uv)`
- 法线只乘视图矩阵 V（lookat），**永不乘透视**

## Workflow（从零重写/复刻时）
1. **资产与工具链**：`fetch-assets.ps1`（被掐断直连时走 `https://gh-proxy.com/https://raw.githubusercontent.com/ssloy/tinyrenderer/master/...`）+ 尺寸校验 + 重试；编译 `tga2bmp` 打通"数字→人眼"链路
2. **L0/L1**：画线 → 画三角形 → 逐面光照；先用相机转台（debug_turntable）确认三视图几何正确
3. **L2 骨架**：抽取 `raster.h`（IShader+draw_triangle 单一实现）；uv chessboard 对照（debug_uvflat）验证插值；回归锚点 fragments/written 数字不复变
4. **L3/L4/L5**:逐关加输入（法线/高光图/影图），每关写齐上文门禁再谈出图
5. **调参纪律**：任何"看起来不对"，先加仪器取分布（通道均值、直方图、逐片元 delta），**一个原则支撑一个旋钮**，单次只动一个
6. **RED/GREEN 验证**：新开子代理加载本技能→按六关从头实现→对照门禁数字与成图
7. **打包**：README（用途/构建/各关成图）+ LICENSE（MIT）+ 验收数据表

## Common Mistakes（按出现频率排序，全部实证过）
1. **背面剔除符号**：屏幕 y 翻转后正面三角形 area2<0 → 剔除写 `if (area2 >= 0) return;`。写反=明暗反转+下巴鬼鼻子+黑月牙三连。判决法：2×2 表（kept/culled × 几何朝向），正面面应 ~99% 落进 kept
2. **变换矩阵 m[3][3] 清零**：lookat/perspective/viewport 组装时底右必须是 1，否则 clip.w=0 全军覆没
3. **uv 垂直镜像**：忘 `1−v` → 全图倒贴；嘴在额头
4. **深度约定自相矛盾**：一半代码"大=近"另一半"小=近"→ 阴影全错。写库前先把本文深度约定抄到 raster.h 顶部注释
5. **逐面切线 → 碎面**：每面独立切线法 Fame-Schmidt 会让脸上每块碎片走向不同；正确做法=**逐顶点累积平均切线**（每面切线加到 3 顶点后归一化），渲染时再对归一化平滑法线正交化
6. **插值法线非单位长**：透视校正插值出的 n_smooth |n|~0.7（褶皱区），直接喂 Gram-Schmidt 假设崩坏（worst |dot| 0.49）；先 `normalized(n_smooth)` 再建 TBN → 1e-16
7. **nm_tangent 通道约定搞错**：R=+u 切线、G=+v 副切线、B=+法线；`(c/255)*2−1` 解码；平坦极点 (0.5,0.5,1.0)。取证法：全图通道均值 −0.5 中心（0.498/0.498/0.983）
8. **高光反射用 clamp 过的 n·l**：`r=2(n·l)n−l` 必须用**原始** n·l，clamp 后背光面 r=−L 产生假高光
9. **1×1 dummy frame 压扁光栅器**：`draw_triangle` 的屏幕映射由 frame 的 W/H 驱动；影图趟即使不写颜色也要给它正尺寸 (W,H) 的 frame，否则整图缩进 1 像素、filled=0%
10. **阴影 bias 瞎调**：打印向光片元的 (shadow_z−depth_light) 直方图再定值——分布**三峰**：±0.006 量化 acne 尖峰（吸收）、大幅负值（平滑法线朝光但三角形背对太阳的掠射带，影图正当地没记它，视觉无害勿修）、大正值真阴影（保留）。本库尖峰与真影之间取 bias=0.03
11. **fragment 拿不到像素坐标**：签名就应该是 `(bc,color,x,y)`；屏幕空间效果和调试转储都靠它
12. **单文件一把梭无门禁**：六类 bug 挤一次爆栈调试；一关一验收是本库所有 bug 都能在"下一关之前"抓到的根本原因

## 判决性诊断（本库真实破案记录）
- **绕向 2×2 表**（debug_winding/winding2/shadowwinding）：kept×几何朝向四个计数定位剔除符号/模型绕向问题
- **物体空间法线图对照趟**：同一管线换 `african_head_nm.tga` 若完全平滑 → 参数化/切线是分离点（L3 碎面案的关键证据）
- **自投影测试**（debug_selfproj）:向光顶点光空间投影处 3×3 邻域查自身深度——影图对齐性一测定音（98.3% 命中=对齐，worst 0.00034）
- **delta 转储图**：delta=0 映射中灰，一眼分辨"对齐/错位/acne"三种像；配 ghost 转储（前 5 个异常片元的 sc/texel/3×3 影图值）
- **调参三件套**：max(spec_term)/max(rv)/max(s) 三数定位"贴图太暗 vs 镜面方向不可达 vs 指数过狠"（L4 案例：rv=1.000、s=0.641 → 该动增益不该动几何）

## 成图链（验收时逐张查）
L0 线框 → L1 面光照 → L2 纹理+zbuffer → L3 毛孔疤痕 → L4 鼻尖锁骨高光 → L5 下巴投影到胸/眉弓压眼窝的完整成图（黑底 3/4 视角，太阳左上 (−0.35,0.55,0.95)）。每张都过 `tga2bmp`+`tga2png.ps1` 后目视 + 数字门禁双验收。
