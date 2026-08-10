#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

#include <opencv2/core.hpp>

#include <functional>
#include <vector>

inline constexpr double kOverallRmsWarningThresholdPx = 1.0; ///< 整体 RMS 告警阈值
inline constexpr double kPerViewRmsWarningThresholdPx = 2.0; ///< 单图 RMS 告警阈值

/**
 * @brief 标定板类型。
 */
enum class CalibrationBoardType {
    Chessboard,  ///< 普通黑白棋盘格
    Charuco,     ///< 棋盘格与 ArUco 标记组合板
};

/**
 * @brief 相机投影与畸变模型。
 */
enum class CameraModel {
    Pinhole,  ///< 针孔模型：径向畸变 k1-k3，可选切向畸变 p1、p2
    Fisheye,  ///< 鱼眼模型：Kannala-Brandt k1-k4
};

/**
 * @brief 普通棋盘格角点检测方法。
 *
 * 两种方法在 OpenCV 中均用于检测棋盘格角点，检测结果随后交给
 * 当前选择的针孔或鱼眼模型标定接口。
 */
enum class CalibrationMethod {
    Classic,     ///< cv::findChessboardCorners + cornerSubPix 亚像素细化
    SectorBased, ///< cv::findChessboardCornersSB（Sector-Based，更鲁棒）
};

/**
 * @brief ChArUco 使用的预定义 ArUco 字典。
 */
enum class ArucoDictionary {
    Dict4x4_50,
    Dict5x5_100,
    Dict5x5_250,
    Dict6x6_250,
    Original,
};

/**
 * @brief 标定输入参数。
 */
struct CalibrationOptions {
    CameraModel cameraModel = CameraModel::Pinhole;
    CalibrationBoardType boardType = CalibrationBoardType::Charuco;
    QSize boardSize{14, 9};         ///< 标定板方格数（列 × 行）
    double squareSize = 20.0;       ///< 单个方格物理边长（毫米）
    double markerSize = 15.0;       ///< ChArUco 标记边长（毫米）
    ArucoDictionary dictionary = ArucoDictionary::Dict5x5_100;
    CalibrationMethod method = CalibrationMethod::Classic;
    bool skew = false;              ///< 鱼眼模型是否估计斜率项
    bool tangential = true;         ///< 针孔模型是否估计切向畸变
    int radialCoeffs = 3;           ///< 针孔支持 2-3 个，鱼眼支持 2-4 个
};

/**
 * @brief 单张有效标定图片对应的标定板到相机外参。
 *
 * 坐标变换约定为 X_camera = R * X_board + t，其中 R 由 rotationVector
 * 通过 Rodrigues 公式得到，平移单位与方格尺寸一致（当前为毫米）。
 */
struct CalibrationPose {
    QString imagePath;
    cv::Vec3d rotationVector;
    cv::Vec3d translationVector;
    double reprojectionError = 0.0; ///< 本张图片的重投影 RMS 误差（像素）
};

/**
 * @brief 标定结果。
 */
struct CalibrationResult {
    bool success = false;
    bool qualityWarning = false;     ///< 整体或单图重投影误差超过建议阈值
    double rmsError = 0.0;          ///< 重投影 RMS 误差（像素）
    cv::Mat cameraMatrix;           ///< 3×3 相机内参
    cv::Mat distCoeffs;             ///< 畸变系数向量
    int imagesUsed = 0;             ///< 成功检测角点的图像数
    int imagesTotal = 0;            ///< 输入图像总数
    QSize imageSize;                ///< 标定图像尺寸
    CalibrationOptions options;     ///< 本次标定使用的参数
    std::vector<CalibrationPose> poses; ///< 每张有效图片的外参
    QString sourceDirectory;        ///< 本次标定图片所在目录
    QString report;                 ///< 人类可读的标定报告
};

/**
 * @brief 封装 OpenCV 针孔/鱼眼及普通棋盘格/ChArUco 标定流程。
 *
 * 提供单图角点预览与批量标定两类能力。Qt 侧仅依赖 QImage 与结果结构体，
 * 不直接接触 cv::Mat 之外的 OpenCV 类型。
 */
class Calibrator {
public:
    /// 每处理完一张图时回调：(已处理数, 总数, 本张是否检测到角点)。
    using ProgressCallback = std::function<void(int, int, bool)>;
    /// 询问是否应取消（返回 true 则尽快终止）。
    using CancelPredicate = std::function<bool()>;

    /**
     * @brief 生成单图预览：检测角点并叠加绘制，可选去畸变。
     *
     * @param filePath 图片路径
     * @param opts 相机模型、标定板类型、方格数和检测参数
     * @param calib 已有标定结果（用于去畸变预览，可为失败结果）
     * @param showUndistorted 是否显示去畸变后的图像
     * @param found 输出本次检测结果是否足以用于标定，可为 nullptr
     * @return 带角点标注的预览图；读图失败返回空 QImage
     */
    QImage previewImage(const QString& filePath,
                        const CalibrationOptions& opts,
                        const CalibrationResult& calib, bool showUndistorted,
                        bool* found = nullptr);

    /**
     * @brief 对一批图像执行标定。
     *
     * 仅对成功检测到角点的图像参与求解；成功图像数少于 2 时返回失败。
     * 可在 GUI 线程之外调用，通过 progress 回调上报进度、isCanceled 响应取消。
     *
     * @param files 图片路径列表
     * @param opts 标定参数
     * @param progress 每张图处理完的进度回调，可为空
     * @param isCanceled 取消谓词，可为空
     * @return 标定结果，含 RMS、内参与畸变系数
     */
    CalibrationResult calibrate(const QStringList& files,
                                const CalibrationOptions& opts,
                                ProgressCallback progress = nullptr,
                                CancelPredicate isCanceled = nullptr);

    /**
     * @brief 将成功的标定结果原子写入 OpenCV YAML 文件。
     *
     * @param filePath 输出文件完整路径
     * @param result 成功的标定结果
     * @param error 失败原因，可为 nullptr
     * @return 写入并提交成功时返回 true
     */
    bool exportParameters(const QString& filePath,
                          const CalibrationResult& result,
                          QString* error = nullptr) const;

private:
    QString previewCacheFilePath_;
    qint64 previewCacheFileSize_ = -1;
    qint64 previewCacheModifiedMs_ = -1;
    CalibrationOptions previewCacheOptions_;
    cv::Mat previewCacheAnnotated_;
    bool previewCacheFound_ = false;
};
