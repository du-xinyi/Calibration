#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

#include <opencv2/core.hpp>

#include <functional>
#include <optional>
#include <vector>

inline constexpr double kOverallRmsWarningThresholdPx = 1.0; ///< 整体标定结果的 RMS 提示阈值
inline constexpr double kPerViewRmsWarningThresholdPx = 2.0; ///< 单张图片的 RMS 提示阈值

/**
 * @brief 标定板的图案类型
 */
enum class CalibrationBoardType {
    Chessboard, ///< 无编码标记的普通棋盘格
    Charuco,    ///< 由棋盘格角点与 ArUco 标记组成的 ChArUco 板
};

/**
 * @brief 标定求解采用的成像模型
 */
enum class CameraModel {
    Pinhole, ///< 带径向和可选切向畸变的针孔模型
    Fisheye, ///< 使用四个径向参数的 OpenCV 鱼眼模型
};

/**
 * @brief 普通棋盘格的角点检测策略
 */
enum class CalibrationMethod {
    Classic,     ///< 传统检测并进行亚像素角点细化
    SectorBased, ///< 基于扇区的鲁棒棋盘格检测
};

/**
 * @brief ChArUco 标记可选用的预定义字典
 */
enum class ArucoDictionary {
    Dict4x4_50,  ///< 50 个 4×4 标记
    Dict5x5_100, ///< 100 个 5×5 标记
    Dict5x5_250, ///< 250 个 5×5 标记
    Dict6x6_250, ///< 250 个 6×6 标记
    Original,    ///< OpenCV 早期版本提供的原始标记集合
};

/**
 * @brief 一次角点检测与标定求解所需的全部选项
 */
struct CalibrationOptions {
    CameraModel cameraModel = CameraModel::Pinhole; ///< 成像与畸变模型
    CalibrationBoardType boardType = CalibrationBoardType::Charuco; ///< 标定板图案
    QSize boardSize{14, 9}; ///< 标定板横向和纵向的方格数量
    double squareSize = 20.0; ///< 方格边长，单位为毫米
    double markerSize = 15.0; ///< ChArUco 标记边长，单位为毫米
    ArucoDictionary dictionary = ArucoDictionary::Dict5x5_100; ///< ChArUco 字典
    CalibrationMethod method = CalibrationMethod::Classic; ///< 普通棋盘格检测策略
    bool skew = false; ///< 鱼眼求解是否允许非零坐标轴斜率
    bool tangential = true; ///< 针孔求解是否估计切向畸变
    int radialCoeffs = 3; ///< 待估计的径向系数数量
};

/**
 * @brief 一张有效图片对应的标定板外参和投影误差
 *
 * @details 外参满足 X_camera = R * X_board + t，R 由 rotationVector 经 Rodrigues
 * 变换得到，translationVector 与方格边长使用相同物理单位
 */
struct CalibrationPose {
    QString imagePath; ///< 此位姿对应的输入图片
    cv::Vec3d rotationVector; ///< 标定板到相机坐标系的旋转向量
    cv::Vec3d translationVector; ///< 标定板到相机坐标系的平移向量
    double reprojectionError = 0.0; ///< 本视图的重投影 RMS，单位为像素
};

/**
 * @brief 标定求解或参数导入产生的结果对象
 */
struct CalibrationResult {
    bool success = false; ///< 是否包含通过完整性检查的相机参数
    bool qualityWarning = false; ///< 是否有整体或单图 RMS 超出提示阈值
    double rmsError = 0.0; ///< 全部有效观测的重投影 RMS
    cv::Mat cameraMatrix; ///< 3×3 双精度相机内参矩阵
    cv::Mat distCoeffs; ///< 与 cameraModel 匹配的畸变系数
    int imagesUsed = 0; ///< 实际进入求解的图片数量
    int imagesTotal = 0; ///< 调用方提交的图片总数
    QSize imageSize; ///< 内参对应的图像分辨率
    CalibrationOptions options; ///< 生成此结果时采用的选项
    std::vector<CalibrationPose> poses; ///< 各有效图片的外参与单图误差
    QString sourceDirectory; ///< 参数导出时建议使用的目录
    QString report; ///< 适合直接展示给用户的结果说明
};

/**
 * @brief 提供标定板检测、相机标定、参数读写和图片去畸变能力
 *
 * @details 批量求解支持进度通知和协作式取消；预览接口会在对象内部缓存最近一次检测结果
 */
class Calibrator {
public:
    /**
     * @brief 单图检测结束后的进度通知，参数依次为已处理数、总数和检测状态
     */
    using ProgressCallback = std::function<void(int, int, bool)>;

    /**
     * @brief 返回 true 时请求批量求解尽快停止
     */
    using CancelPredicate = std::function<bool()>;

    /**
     * @brief 读取图片、检测标定板并生成预览图
     *
     * @details 检测结果会被缓存；启用去畸变时，仅在结果有效且分辨率兼容的情况下应用内参
     *
     * @param filePath 输入图片路径
     * @param opts 当前检测选项
     * @param calib 可用于去畸变的标定结果
     * @param showUndistorted 是否请求显示去畸变结果
     * @param found 接收标定板是否可用于求解，可为 nullptr
     *
     * @return 绘制检测结果后的图片；读取或处理失败时返回空 QImage
     */
    QImage previewImage(const QString& filePath,
                        const CalibrationOptions& opts,
                        const CalibrationResult& calib, bool showUndistorted,
                        bool* found = nullptr);

    /**
     * @brief 从一组图片估计相机内参、畸变参数和逐图外参
     *
     * @details 仅使用成功检测且分辨率一致的图片。此函数不访问预览缓存，可在工作线程调用
     *
     * @param files 输入图片路径列表
     * @param opts 标定板和求解选项
     * @param progress 可选的逐图进度回调
     * @param isCanceled 可选的取消查询函数
     *
     * @return 包含求解状态、质量指标和用户报告的标定结果
     */
    CalibrationResult calibrate(const QStringList& files,
                                const CalibrationOptions& opts,
                                ProgressCallback progress = nullptr,
                                CancelPredicate isCanceled = nullptr);

    /**
     * @brief 以 OpenCV 可读取的 YAML 格式原子导出相机参数
     *
     * @param filePath 输出文件路径
     * @param result 待导出的成功标定结果
     * @param error 接收失败原因，可为 nullptr
     *
     * @return 参数有效且文件提交成功时返回 true
     */
    bool exportParameters(const QString& filePath,
                          const CalibrationResult& result,
                          QString* error = nullptr) const;

    /**
     * @brief 从 YAML 文件导入并验证相机参数
     *
     * @details camera_matrix 可使用嵌套序列或 OpenCV matrix 表示；核心矩阵、焦距、分辨率和系数数量均会校验
     *
     * @param filePath 输入文件路径
     * @param error 接收失败原因，可为 nullptr
     *
     * @return 有效参数；文件无法读取或数据不满足约束时返回 std::nullopt
     */
    std::optional<CalibrationResult> importParameters(
        const QString& filePath, QString* error = nullptr) const;

    /**
     * @brief 使用标定结果校正单张图片并写入新文件
     *
     * @details 输入分辨率必须与标定分辨率一致，避免在未缩放内参时产生错误结果
     *
     * @param inputPath 原始图片路径
     * @param outputPath 校正图片路径
     * @param result 去畸变所用的标定结果
     * @param error 接收失败原因，可为 nullptr
     *
     * @return 图片成功校正并写出时返回 true
     */
    bool undistortImageFile(const QString& inputPath,
                            const QString& outputPath,
                            const CalibrationResult& result,
                            QString* error = nullptr) const;

private:
    // === 预览检测缓存 ===
    QString previewCacheFilePath_; ///< 缓存对应的文件路径
    qint64 previewCacheFileSize_ = -1; ///< 缓存文件的字节数快照
    qint64 previewCacheModifiedMs_ = -1; ///< 缓存文件的修改时间快照
    CalibrationOptions previewCacheOptions_; ///< 缓存对应的检测选项
    cv::Mat previewCacheAnnotated_; ///< 尚未去畸变的角点叠加图
    bool previewCacheFound_ = false; ///< 缓存检测是否满足求解条件
};
