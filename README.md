# Calibration

Calibration 是一个基于 Qt 6 和 OpenCV 5 的桌面相机标定工具，用于从普通棋盘格或 ChArUco 图片计算相机内参、畸变系数及每张图片的标定板位姿。

## 功能

- 支持 `Pinhole` 和 `Fisheye` 相机模型。
- 支持普通棋盘格和 ChArUco 标定板。
- 普通棋盘格可选择 `Classic` 或 `Sector-Based` 角点检测。
- 一键比较 `Pinhole`、`Fisheye` 及可用角点检测器的标定结果。
- 加载时预检图片清晰度、曝光、亮暗裁切和相似画面。
- 后台执行标定，界面保持响应，并支持取消。
- 显示角点检测结果和去畸变预览。
- 输出整体 RMS、逐图片 RMS、相机内参、畸变系数和外参。
- 在三维视图中查看相机与标定板的位姿分布。
- 按逐图片 RMS 降序显示结果，标记高误差图片，并支持排除后重新标定。
- 将标定参数原子导出为 OpenCV 可读取的 YAML 文件。
- 支持导入 YAML 参数，并在分辨率不匹配时阻止去畸变。
- 支持批量导出去畸变图片。
- 支持将图片列表和标定选项保存为项目文件并重新打开。

## 环境要求

- CMake 3.16 或更高版本
- 支持 C++17 的编译器
- Qt 6，包含 `Widgets` 和 `Concurrent` 组件
- OpenCV 5，包含以下模块：
  - `calib`
  - `geometry`
  - `objdetect`
  - `imgproc`
  - `imgcodecs`

当前构建配置已在 Linux 环境验证。CMake 会优先通过 `qmake6` 和常见安装目录查找 Qt 6，并在常见系统目录查找 OpenCV 5。

## 构建与运行

在项目根目录执行：

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
./build/Calibration
```

构建成功后会生成 `build/Calibration`。如果 CMake 无法自动找到依赖，可以显式提供安装位置：

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64 \
  -DOpenCV_DIR=/path/to/opencv5/lib/cmake/opencv5
```

## 快速开始

1. 点击工具栏中的“图片”或“文件夹”，加载至少 3 张标定图片。
2. 选择相机模型和标定板类型。
3. 按实际标定板填写方格数、方格尺寸及 ChArUco 参数。
4. 点击“标定”，等待角点检测和参数求解完成。
5. 查看整体 RMS、最大单图 RMS 和位姿结果。
6. 如有高误差图片，在位姿窗口中勾选后点击“排除所选并重新标定”。
7. 检查“Show Undistorted”预览，确认画面校正效果。
8. 点击“导出”保存 YAML 参数文件。

如果尚不确定应选择哪种相机模型，可在加载图片后点击工具栏“对比”。普通棋盘格会比较 `Pinhole/Fisheye × Classic/Sector-Based` 四种组合，ChArUco 会比较两种相机模型。表格会按成功状态和 RMS 排列，并同时显示有效图片数与核心内参；不要只根据最低 RMS 作最终选择。

排除操作只会从当前界面的图片列表中移除对应项，不会删除磁盘上的原图。需要恢复时可以重新添加图片或文件夹。

## 图片质量预检

图片加入列表时会在缩略图上执行轻量预检。可能模糊、曝光不足、过曝、亮暗裁切较多或与已加载画面高度相似的图片会显示黄色背景和 `⚠`，鼠标悬停可查看提示及清晰度、平均亮度指标。

预检采用通用经验阈值，只用于快速筛查，不会自动排除图片。标定板纹理、分辨率和光照条件不同会影响指标；是否移除仍应结合角点检测结果和单图 RMS 判断。

## 保存和打开项目

通过“文件 → 保存项目”可保存当前图片列表和全部标定选项，项目扩展名建议使用 `.calibration.json`。图片路径会尽可能相对于项目文件保存，移动整个项目目录后仍可打开。

项目文件不保存求解出的内参、畸变系数和位姿。打开项目后需要重新标定；已经求解的参数应另行导出为 YAML。

## 导入参数与批量去畸变

“文件 → 导入相机参数”可读取本工具导出的 YAML。`camera_matrix` 同时兼容 3×3 数组和 OpenCV `!!opencv-matrix`，畸变系数兼容行向量或列向量，导入后统一在内存中转为行向量。

导入成功后，“Show Undistorted”和“文件 → 批量导出去畸变图片”会启用。批量导出在后台运行，输出文件名增加 `_undistorted`；来自不同目录但同名的图片会自动增加序号，避免互相覆盖。

相机内参只适用于标定时的图像分辨率。当前图片尺寸与 YAML 中的 `image_width`、`image_height` 不一致时：

- 预览会保留原图，不应用去畸变，并在状态栏提示。
- 批量导出会跳过该图片，并在结果中列出原因。

不要仅按宽高比例缩放后直接复用内参；如果相机输出经过缩放或裁剪，应按实际成像变换调整内参，或使用目标分辨率重新标定。

## 标定参数说明

### 相机模型

`Pinhole` 适用于普通镜头，使用 OpenCV `calibrateCamera()` 求解，畸变系数顺序为：

```text
[k1, k2, p1, p2, k3]
```

`Fisheye` 适用于大视场鱼眼镜头，使用 OpenCV `fisheye::calibrate()` 求解，畸变系数顺序为：

```text
[k1, k2, k3, k4]
```

不要仅根据标定 RMS 选择模型，还应检查去畸变后的直线、图像边缘和有效视场是否符合预期。

### 标定板尺寸

界面中的“方格数”表示横向和纵向的完整方格数量。

- 普通棋盘格内部角点数为 `(列数 - 1) × (行数 - 1)`。
- ChArUco 使用填写的完整网格尺寸创建标定板模型。
- “方格尺寸”和“标记尺寸”使用毫米，因此导出的平移向量单位也是毫米。
- ChArUco 的标记尺寸必须小于方格尺寸，并且 `Dictionary` 必须与实际标定板一致。

### Classic 与 Sector-Based

这两个选项只控制普通棋盘格的角点检测方法，不是两套相机标定模型：

- `Classic`：使用 `findChessboardCorners()`，并通过 `cornerSubPix()` 做亚像素细化。
- `Sector-Based`：使用 `findChessboardCornersSB()`，通常对透视变化和成像条件更鲁棒。

检测到角点后，程序仍根据所选相机模型调用 `calibrateCamera()` 或 `fisheye::calibrate()` 求解参数。`Sector-Based` 本身不是张正友标定算法。

### Skew 与畸变选项

- `Skew` 只用于 `Fisheye`。不勾选时固定斜率项，适合绝大多数现代相机；没有明确硬件依据时建议保持关闭。
- `Tangential Distortion` 只用于 `Pinhole`，控制是否估计切向畸变 `p1`、`p2`。
- `Radial Coeffs` 控制参与估计的径向畸变系数数量。

修改相机模型、标定板参数、检测方法、畸变选项或图片列表后，旧标定结果会自动失效，必须重新标定。

## 判断标定质量

程序提供两级提示阈值：

- 整体 RMS 超过 `1.0 px` 时显示质量警告。
- 任意单图 RMS 超过 `2.0 px` 时标红该图片并显示质量警告。

这些阈值用于提示，不代表所有相机和应用的统一合格标准。判断结果时还应检查：

- 标定板是否覆盖画面中心、边缘和四角。
- 图片是否包含不同距离、倾角和旋转方向。
- 是否存在模糊、反光、过曝或标定板明显弯曲的图片。
- 去畸变后直线是否自然，边缘是否出现异常拉伸。
- 排除高误差图片后，内参和畸变系数是否趋于稳定。

位姿结果窗口会按单图 RMS 从高到低排列。RMS 超过 `2.0 px` 的图片会预先勾选，但只有点击“排除所选并重新标定”后才会从当前列表移除。重新标定完成后，报告会显示排除前后的 RMS 对比。

## YAML 输出

导出文件的 `format_version` 当前为 `2`。主要字段示例：

```yaml
format_version: 2
camera_model: pinhole
image_width: 1920
image_height: 1080
rms_reprojection_error: 0.42
quality_warning: 0
camera_matrix:
   - [ 820.0, 0.0, 960.0 ]
   - [ 0.0, 821.0, 540.0 ]
   - [ 0.0, 0.0, 1.0 ]
distortion_coefficients: !!opencv-matrix
   rows: 1
   cols: 5
   dt: d
   data: [ -0.21, 0.05, 0.001, -0.001, -0.004 ]
```

针孔模型的 `distortion_coefficients` 为 `1×5`，鱼眼模型为 `1×4`。畸变矩阵元数据保留为 OpenCV YAML 格式，`data` 固定在一行输出。

`poses` 保存每张有效图片的以下信息：

- 图片文件名
- Rodrigues 旋转向量 `rotation_vector`
- 平移向量 `translation_vector`
- 单图重投影误差 `reprojection_error`

外参约定为：

```text
X_camera = R * X_board + t
```

## 运行测试

```bash
ctest --test-dir build --output-on-failure
```

当前测试覆盖：

- 普通棋盘格和 ChArUco 检测与标定
- `Pinhole` 和 `Fisheye` 参数求解
- 去畸变预览
- YAML 导出与 OpenCV 回读
- YAML 参数导入及矩阵一致性校验
- 标定项目的保存与打开
- 图片质量预检
- 单图去畸变导出及分辨率不匹配拒绝
- 标定期间的界面状态保护
- 参数变化后的旧结果失效
- 高 RMS 图片标记、排除和自动重新标定

测试成功时，`ctest` 返回退出码 `0`，并显示全部测试通过。

## 常见问题

### 无法检测标定板

确认方格数填写的是完整方格数量，而不是内部角点数量。ChArUco 还需要确认 `Dictionary`、方格尺寸和标记尺寸与实物一致。图片中的标定板应清晰、完整，并具有足够对比度。

### 切换算法后“Show Undistorted”不可用

切换模型或标定参数后，原来的内参和畸变系数不再匹配，程序会取消并禁用去畸变预览。重新完成一次成功标定后，该选项会恢复。

### 标定参数偶尔变化很大

优先检查高 RMS 图片、标定板尺寸、相机模型和拍摄姿态分布。不要为了降低 RMS 随意启用 `Skew` 或增加自由参数；先排除模糊、反光、边缘检测错误和姿态重复的图片。

### CMake 找不到 Qt 6 或 OpenCV 5

确认安装了所需组件，并通过 `CMAKE_PREFIX_PATH` 或 `OpenCV_DIR` 指向对应的 CMake 包目录。删除旧构建目录后重新配置，可以避免失效的缓存路径继续生效。

## 项目结构

```text
src/
  calibration.*                    标定、预览、YAML 导入导出和去畸变
  calibration_project.*            标定项目 JSON 读写
  image_quality.*                  图片质量预检
  algorithm_comparison_dialog.*    算法结果对比表格
  mainwindow.*           主界面与异步标定流程
  pose_result_dialog.*   位姿表格、异常图片排除和三维视图
tests/
  calibration_board_test.cpp       标定核心与导出测试
  calibration_completion_test.cpp  GUI 端到端回归测试
image/                   测试与示例图片
```
