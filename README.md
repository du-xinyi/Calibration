# camera-calibrator

基于 Qt 6 和 OpenCV 5 的桌面相机标定工具，用于从普通棋盘格或 ChArUco 图片估计相机内参、畸变系数以及每张图片的标定板位姿。

## 主要功能

- 支持 `Pinhole` 和 `Fisheye` 相机模型。
- 支持普通棋盘格与 ChArUco 标定板。
- 普通棋盘格可选择 `Classic` 或 `Sector-Based` 角点检测。
- 后台执行标定、算法对比和批量去畸变，运行期间可取消。
- 对图片进行模糊、曝光、亮暗裁切及相似画面预检。
- 显示整体 RMS、单图 RMS、内参、畸变系数和三维位姿分布。
- 标记高误差图片，并支持排除后重新标定和比较 RMS 变化。
- 保存和打开 `.calibration.json` 标定项目。
- 导入、导出 OpenCV 兼容 YAML 参数。
- 批量导出去畸变图片，并拒绝分辨率不匹配的输入。

## 环境要求

- Linux
- CMake 3.16 或更高版本
- 支持 C++17 的编译器
- Qt 6：`Widgets`、`Concurrent`
- OpenCV 5：`calib`、`geometry`、`objdetect`、`imgproc`、`imgcodecs`

CMake 会尝试通过 `qmake6` 和常见安装目录查找 Qt 6，并优先使用系统中的 OpenCV 5 配置。

## 构建与运行

```bash
git clone https://github.com/du-xinyi/camera-calibrator.git
cd camera-calibrator
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
./build/Calibration
```

程序成功启动后会显示“相机标定”主窗口。当前 CMake 可执行目标名为 `Calibration`，与仓库名不同。

如果 CMake 无法自动找到依赖，请显式提供安装位置：

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64 \
  -DOpenCV_DIR=/path/to/opencv5/lib/cmake/opencv5 \
  -DBUILD_TESTING=ON
```

## 快速开始

1. 点击“图片”或“文件夹”，加载至少 3 张标定图片。
2. 选择相机模型和标定板类型。
3. 按实际标定板填写方格数、方格尺寸及 ChArUco 参数。
4. 点击“标定”，等待角点检测与参数求解完成。
5. 检查整体 RMS、单图 RMS、位姿分布和去畸变预览。
6. 在位姿窗口中排除高误差图片，并根据需要重新标定。
7. 点击“导出”保存 YAML 参数。

图片列表中的颜色表示角点检测结果：绿色表示检测成功，红色表示未检测到可用角点。黄色背景和 `⚠` 表示质量预检发现潜在问题；预检只作提示，不会自动排除图片。

## 标定配置

### 相机模型

`Pinhole` 使用 `cv::calibrateCamera()`，输出 5 个畸变系数：

```text
[k1, k2, p1, p2, k3]
```

`Fisheye` 使用 `cv::fisheye::calibrate()`，输出 4 个畸变系数：

```text
[k1, k2, k3, k4]
```

不要只根据最低 RMS 选择模型。针孔模型启用切向畸变后自由参数更多，可能在参与标定的数据上得到更低 RMS，但画面边缘的外推效果未必更好。选择模型时还应比较：

- 两种模型使用的有效图片数是否相同。
- 去畸变后边缘直线是否自然。
- 画面边缘是否出现异常拉伸或波浪。
- 更换部分图片后，内参与畸变系数是否稳定。

“算法对比”会对同一批图片运行候选组合。普通棋盘格比较 `Pinhole/Fisheye × Classic/Sector-Based`，ChArUco 比较两种相机模型。

### 标定板参数

界面中的“方格数”表示完整方格数量：

- 普通棋盘格内部角点数为 `(列数 - 1) × (行数 - 1)`。
- ChArUco 使用完整方格数创建标定板模型。
- 方格尺寸和标记尺寸使用毫米，导出的平移向量也以毫米为单位。
- ChArUco 的标记尺寸必须小于方格尺寸，`Dictionary` 必须与实物一致。

`Classic` 和 `Sector-Based` 只决定普通棋盘格的角点检测方式。二者检测到角点后，仍由当前选择的 `Pinhole` 或 `Fisheye` 模型完成参数求解；`Sector-Based` 本身不是一套相机标定模型。

### 畸变选项

- `Tangential Distortion`：仅用于 `Pinhole`，控制是否估计 `p1`、`p2`。
- `Skew`：仅用于 `Fisheye`；没有明确硬件依据时建议关闭。
- `Radial Coeffs`：控制参与估计的径向畸变系数数量。

修改模型、标定板参数、检测方法、畸变选项或标定图片后，旧标定结果会自动失效。

## 结果质量

程序使用以下经验阈值进行提示：

- 整体 RMS 超过 `1.0 px` 时显示质量警告。
- 单图 RMS 超过 `2.0 px` 时标记对应图片。

阈值用于筛查，不是所有镜头和应用的统一合格标准。建议让标定板覆盖画面中心、边缘和四角，并包含不同距离、倾角和旋转方向。排除图片时只会修改当前列表，不会删除磁盘文件。

## 项目与参数文件

### 标定项目

“文件 → 保存项目”会将图片路径和标定选项写入 `.calibration.json`。项目文件不保存已经求解的内参、畸变系数或位姿；重新打开后需要再次标定。

### YAML 参数

导出文件的 `format_version` 当前为 `2`。针孔模型的畸变系数为 `1×5`，鱼眼模型为 `1×4`：

```yaml
format_version: 2
camera_model: pinhole
image_width: 1920
image_height: 1080
rms_reprojection_error: 0.42
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

导入时，`camera_matrix` 兼容 3×3 数组和 OpenCV `!!opencv-matrix`；畸变系数兼容行向量和列向量。参数文件必须包含有效的 `image_width` 和 `image_height`。

相机参数只适用于标定分辨率。图片尺寸不一致时，预览不会应用去畸变，批量导出也会跳过对应图片。

## 运行测试

仓库不包含标定图片。运行数据相关测试时，通过 `CALIBRATION_TEST_IMAGE_DIR` 指向外部 ChArUco 图片目录：

```bash
cmake -S . -B build \
  -DBUILD_TESTING=ON \
  -DCALIBRATION_TEST_IMAGE_DIR=/path/to/charuco/images
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

测试图片应为默认配置对应的 `14×9` ChArUco 标定板，方格尺寸 `20 mm`、标记尺寸 `15 mm`、字典 `DICT_5X5_100`。未提供有效目录时，CMake 仍会构建测试程序，但不会注册依赖外部图片的数据测试。

测试覆盖标定板检测、针孔与鱼眼求解、YAML 导入导出、项目文件、质量预检、去畸变、算法对比生命周期和 GUI 状态管理。全部通过时 `ctest` 返回退出码 `0`。

## 常见问题

### 无法检测标定板

确认填写的是完整方格数。使用 ChArUco 时，还要确认 `Dictionary`、方格尺寸和标记尺寸与实物一致。标定板应完整出现在画面中，并避免模糊、反光和严重过曝。

### 切换模型后无法勾选去畸变

模型或标定参数变化后，旧结果不再匹配，程序会清除并禁用去畸变状态。重新完成标定或导入匹配的 YAML 参数后即可启用。

### 参数偶尔变化很大

检查高 RMS 图片、图片分辨率、标定板尺寸和拍摄姿态分布。不要只为降低 RMS 随意开启 `Skew` 或增加自由参数；先处理模糊、反光、重复姿态和边缘角点错误。

### CMake 找不到 Qt 6 或 OpenCV 5

确认安装了所需组件，并通过 `CMAKE_PREFIX_PATH` 或 `OpenCV_DIR` 指向对应的 CMake 包目录。如果缓存中保留了旧路径，请删除构建目录后重新配置。

## 项目结构

```text
src/
  calibration.*                    标定、预览、参数导入导出和去畸变
  calibration_project.*            标定项目 JSON 读写
  image_quality.*                  图片质量预检
  algorithm_comparison_dialog.*    算法结果对比
  mainwindow.*                     主界面与异步任务
  pose_result_dialog.*             单图误差、图片排除与三维位姿
tests/
  calibration_board_test.cpp                  核心功能测试
  calibration_completion_test.cpp             GUI 标定流程测试
  calibration_comparison_lifecycle_test.cpp   算法对比生命周期测试
```
