#include "calibration.h"

#include <QDateTime>
#include <QFileInfo>
#include <QIODevice>
#include <QObject>
#include <QSaveFile>

#include <opencv2/calib.hpp>
#include <opencv2/geometry/3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>

namespace {

constexpr double kOverallRmsWarningThreshold = 1.0;
constexpr double kPerViewRmsWarningThreshold = 2.0;

/// 检测棋盘格角点，按方法选择不同的 OpenCV 实现。
bool detectCorners(const cv::Mat& gray, const cv::Size& pattern,
                   CalibrationMethod method,
                   std::vector<cv::Point2f>& corners)
{
    corners.clear();
    if (method == CalibrationMethod::Classic) {
        const bool found = cv::findChessboardCorners(
            gray, pattern, corners,
            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
        if (!found) {
            return false;
        }
        cv::cornerSubPix(
            gray, corners, cv::Size(5, 5), cv::Size(-1, -1),
            cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT,
                             30, 0.1));
        return true;
    }
    // 使用 OpenCV 的 Sector-Based 棋盘格角点检测器。
    return cv::findChessboardCornersSB(gray, pattern, corners,
                                       cv::CALIB_CB_NORMALIZE_IMAGE);
}

int dictionaryId(ArucoDictionary dictionary)
{
    switch (dictionary) {
    case ArucoDictionary::Dict4x4_50:
        return cv::aruco::DICT_4X4_50;
    case ArucoDictionary::Dict5x5_100:
        return cv::aruco::DICT_5X5_100;
    case ArucoDictionary::Dict5x5_250:
        return cv::aruco::DICT_5X5_250;
    case ArucoDictionary::Dict6x6_250:
        return cv::aruco::DICT_6X6_250;
    case ArucoDictionary::Original:
        return cv::aruco::DICT_ARUCO_ORIGINAL;
    }
    return cv::aruco::DICT_5X5_100;
}

int dictionaryCapacity(ArucoDictionary dictionary)
{
    switch (dictionary) {
    case ArucoDictionary::Dict4x4_50:
        return 50;
    case ArucoDictionary::Dict5x5_100:
        return 100;
    case ArucoDictionary::Dict5x5_250:
    case ArucoDictionary::Dict6x6_250:
        return 250;
    case ArucoDictionary::Original:
        return 1024;
    }
    return 0;
}

cv::String boardTypeName(CalibrationBoardType boardType)
{
    return boardType == CalibrationBoardType::Charuco
               ? "charuco"
               : "chess";
}

cv::String cameraModelName(CameraModel cameraModel)
{
    return cameraModel == CameraModel::Fisheye
               ? "fisheye"
               : "pinhole";
}

QString cameraModelDisplayName(CameraModel cameraModel)
{
    return cameraModel == CameraModel::Fisheye
               ? QStringLiteral("Fisheye")
               : QStringLiteral("Pinhole");
}

cv::String dictionaryName(ArucoDictionary dictionary)
{
    switch (dictionary) {
    case ArucoDictionary::Dict4x4_50:
        return "DICT_4X4_50";
    case ArucoDictionary::Dict5x5_100:
        return "DICT_5X5_100";
    case ArucoDictionary::Dict5x5_250:
        return "DICT_5X5_250";
    case ArucoDictionary::Dict6x6_250:
        return "DICT_6X6_250";
    case ArucoDictionary::Original:
        return "DICT_ARUCO_ORIGINAL";
    }
    return "UNKNOWN";
}

QString detectionDisplayName(const CalibrationOptions& opts)
{
    if (opts.boardType == CalibrationBoardType::Charuco) {
        return QStringLiteral("ChArUco / %1")
            .arg(QString::fromLatin1(dictionaryName(opts.dictionary).c_str()));
    }
    return opts.method == CalibrationMethod::Classic
               ? QStringLiteral("Chessboard / Classic")
               : QStringLiteral("Chessboard / Sector-Based");
}

cv::String detectionMethodName(CalibrationMethod method)
{
    return method == CalibrationMethod::Classic
               ? "findChessboardCorners"
               : "findChessboardCornersSB";
}

QString validateOptions(const CalibrationOptions& opts)
{
    if (opts.boardSize.width() < 2 || opts.boardSize.height() < 2) {
        return QObject::tr("标定板每个方向至少需要 2 个方格。");
    }
    if (!(opts.squareSize > 0.0)) {
        return QObject::tr("方格尺寸必须大于 0。");
    }
    if (opts.boardType == CalibrationBoardType::Charuco) {
        if (!(opts.markerSize > 0.0 && opts.markerSize < opts.squareSize)) {
            return QObject::tr("ChArUco 标记尺寸必须大于 0 且小于方格尺寸。");
        }
        const int requiredMarkers = (opts.boardSize.width()
                                     * opts.boardSize.height()
                                     + 1)
                                    / 2;
        if (requiredMarkers > dictionaryCapacity(opts.dictionary)) {
            return QObject::tr(
                       "当前方格数需要 %1 个标记，但所选字典最多只有 %2 个。")
                .arg(requiredMarkers)
                .arg(dictionaryCapacity(opts.dictionary));
        }
    }
    return {};
}

bool sameDetectionOptions(const CalibrationOptions& lhs,
                          const CalibrationOptions& rhs)
{
    return lhs.boardType == rhs.boardType
           && lhs.boardSize == rhs.boardSize
           && lhs.squareSize == rhs.squareSize
           && lhs.markerSize == rhs.markerSize
           && lhs.dictionary == rhs.dictionary
           && lhs.method == rhs.method;
}

std::vector<cv::Point3f> chessboardObjectPoints(
    const CalibrationOptions& opts)
{
    const cv::Size pattern(opts.boardSize.width() - 1,
                           opts.boardSize.height() - 1);
    std::vector<cv::Point3f> points;
    points.reserve(static_cast<size_t>(pattern.area()));
    for (int row = 0; row < pattern.height; ++row) {
        for (int col = 0; col < pattern.width; ++col) {
            points.emplace_back(static_cast<float>(col * opts.squareSize),
                                static_cast<float>(row * opts.squareSize),
                                0.0F);
        }
    }
    return points;
}

bool hasNonCollinearPoints(const std::vector<cv::Point3f>& points)
{
    if (points.size() < 3) {
        return false;
    }
    const cv::Point3f& first = points.front();
    auto second = std::find_if(
        points.cbegin() + 1, points.cend(), [&first](const cv::Point3f& point) {
            return cv::norm(point - first) > 1.0e-6;
        });
    if (second == points.cend()) {
        return false;
    }
    const cv::Point3f direction = *second - first;
    return std::any_of(
        second + 1, points.cend(), [&first, &direction](const cv::Point3f& point) {
            const cv::Point3f offset = point - first;
            return std::abs(direction.x * offset.y
                            - direction.y * offset.x)
                   > 1.0e-6;
        });
}

struct BoardDetection {
    std::vector<cv::Point2f> imagePoints;
    std::vector<cv::Point3f> objectPoints;
    std::vector<int> ids;
    bool usable = false;
};

BoardDetection detectChessboard(const cv::Mat& gray,
                                const CalibrationOptions& opts)
{
    BoardDetection detection;
    const cv::Size pattern(opts.boardSize.width() - 1,
                           opts.boardSize.height() - 1);
    detection.usable =
        detectCorners(gray, pattern, opts.method, detection.imagePoints);
    if (detection.usable) {
        detection.objectPoints = chessboardObjectPoints(opts);
    }
    return detection;
}

BoardDetection detectCharuco(const cv::Mat& gray,
                             const CalibrationOptions& opts)
{
    BoardDetection detection;
    const cv::Size gridSize(opts.boardSize.width(), opts.boardSize.height());
    const float squareSize = static_cast<float>(opts.squareSize);
    const float markerSize = static_cast<float>(opts.markerSize);

    const auto dictionary =
        cv::aruco::getPredefinedDictionary(dictionaryId(opts.dictionary));
    const cv::aruco::CharucoBoard board(
        gridSize, squareSize, markerSize, dictionary);
    const cv::aruco::CharucoDetector detector(board);
    detector.detectBoard(gray, detection.imagePoints, detection.ids);
    const std::vector<cv::Point3f> boardCorners =
        board.getChessboardCorners();

    std::vector<cv::Point2f> validImagePoints;
    std::vector<cv::Point3f> validObjectPoints;
    std::vector<int> validIds;
    validImagePoints.reserve(detection.ids.size());
    validObjectPoints.reserve(detection.ids.size());
    validIds.reserve(detection.ids.size());
    for (size_t index = 0; index < detection.ids.size(); ++index) {
        const int id = detection.ids[index];
        if (id < 0 || id >= static_cast<int>(boardCorners.size())) {
            continue;
        }
        validImagePoints.push_back(detection.imagePoints[index]);
        validObjectPoints.push_back(boardCorners[static_cast<size_t>(id)]);
        validIds.push_back(id);
    }
    detection.imagePoints = std::move(validImagePoints);
    detection.objectPoints = std::move(validObjectPoints);
    detection.ids = std::move(validIds);
    detection.usable = detection.imagePoints.size() >= 4
                       && hasNonCollinearPoints(detection.objectPoints);
    return detection;
}

BoardDetection detectBoard(const cv::Mat& image,
                           const CalibrationOptions& opts)
{
    cv::Mat gray;
    if (image.channels() == 1) {
        gray = image;
    } else {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    return opts.boardType == CalibrationBoardType::Charuco
               ? detectCharuco(gray, opts)
               : detectChessboard(gray, opts);
}

void drawDetection(cv::Mat& image, const CalibrationOptions& opts,
                   const BoardDetection& detection)
{
    if (detection.imagePoints.empty()) {
        return;
    }
    if (opts.boardType == CalibrationBoardType::Charuco) {
        cv::aruco::drawDetectedCornersCharuco(
            image, detection.imagePoints, detection.ids);
        return;
    }
    const cv::Size pattern(opts.boardSize.width() - 1,
                           opts.boardSize.height() - 1);
    cv::drawChessboardCorners(
        image, pattern, detection.imagePoints, detection.usable);
}

/// cv::Mat（BGR / 灰度）转 QImage，深拷贝以保证数据独立。
QImage matToQImage(const cv::Mat& in)
{
    if (in.empty()) {
        return {};
    }
    if (in.channels() == 1) {
        return QImage(in.data, in.cols, in.rows, static_cast<int>(in.step),
                      QImage::Format_Grayscale8)
            .copy();
    }
    return QImage(in.data, in.cols, in.rows, static_cast<int>(in.step),
                  QImage::Format_BGR888)
        .copy();
}

cv::Vec3d matToVec3d(const cv::Mat& input)
{
    if (input.total() != 3) {
        return {};
    }
    cv::Mat values;
    input.reshape(1, 1).convertTo(values, CV_64F);
    return {values.at<double>(0, 0),
            values.at<double>(0, 1),
            values.at<double>(0, 2)};
}

double viewReprojectionError(
    const std::vector<cv::Point3f>& objectPoints,
    const std::vector<cv::Point2f>& imagePoints,
    const cv::Mat& rotationVector, const cv::Mat& translationVector,
    const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs,
    CameraModel cameraModel)
{
    std::vector<cv::Point2f> projectedPoints;
    if (cameraModel == CameraModel::Fisheye) {
        cv::fisheye::projectPoints(
            objectPoints, projectedPoints,
            rotationVector, translationVector,
            cameraMatrix, distCoeffs, 0.0);
    } else {
        cv::projectPoints(objectPoints, rotationVector, translationVector,
                          cameraMatrix, distCoeffs, projectedPoints);
    }
    if (projectedPoints.size() != imagePoints.size()
        || imagePoints.empty()) {
        return std::numeric_limits<double>::infinity();
    }

    double squaredError = 0.0;
    for (size_t index = 0; index < imagePoints.size(); ++index) {
        const cv::Point2f delta = projectedPoints[index] - imagePoints[index];
        squaredError += static_cast<double>(delta.dot(delta));
    }
    return std::sqrt(squaredError / static_cast<double>(imagePoints.size()));
}

/// 组装人类可读的标定报告。
QString formatReport(const CalibrationResult& r, const cv::Size& imgSize)
{
    const double fx = r.cameraMatrix.at<double>(0, 0);
    const double fy = r.cameraMatrix.at<double>(1, 1);
    const double cx = r.cameraMatrix.at<double>(0, 2);
    const double cy = r.cameraMatrix.at<double>(1, 2);

    const int n = static_cast<int>(r.distCoeffs.total());
    QString dist;
    for (int i = 0; i < n; ++i) {
        if (i > 0) {
            dist += QStringLiteral(", ");
        }
        dist += QString::number(r.distCoeffs.at<double>(i), 'f', 4);
    }

    return QObject::tr(
               "标定成功。\n"
               "相机模型：%1\n"
               "检测方法：%2\n"
               "使用图片：%3 / %4\n"
               "图像尺寸：%5 × %6\n"
               "重投影 RMS 误差：%7 px\n\n"
               "相机内参：\n"
               "  fx = %8\n  fy = %9\n  cx = %10\n  cy = %11\n\n"
               "畸变系数：\n  [%12]")
        .arg(cameraModelDisplayName(r.options.cameraModel))
        .arg(detectionDisplayName(r.options))
        .arg(r.imagesUsed)
        .arg(r.imagesTotal)
        .arg(imgSize.width)
        .arg(imgSize.height)
        .arg(r.rmsError, 0, 'f', 3)
        .arg(fx, 0, 'f', 2)
        .arg(fy, 0, 'f', 2)
        .arg(cx, 0, 'f', 2)
        .arg(cy, 0, 'f', 2)
        .arg(dist);
}

}  // namespace

QImage Calibrator::previewImage(const QString& filePath,
                                const CalibrationOptions& opts,
                                const CalibrationResult& calib,
                                bool showUndistorted, bool* found)
{
    if (found) {
        *found = false;
    }
    // OpenCV 在退化输入（空图、坏内参等）时会抛 cv::Exception，
    // 此处捕获以避免异常穿越线程边界 / Qt 事件循环导致闪退。
    try {
        const QFileInfo fileInfo(filePath);
        const qint64 modifiedMs =
            fileInfo.lastModified().toMSecsSinceEpoch();
        const bool cacheHit =
            filePath == previewCacheFilePath_
            && fileInfo.size() == previewCacheFileSize_
            && modifiedMs == previewCacheModifiedMs_
            && sameDetectionOptions(opts, previewCacheOptions_)
            && !previewCacheAnnotated_.empty();

        if (!cacheHit) {
            cv::Mat image =
                cv::imread(filePath.toStdString(), cv::IMREAD_COLOR);
            if (image.empty()) {
                previewCacheAnnotated_.release();
                return {};
            }

            const BoardDetection detection =
                validateOptions(opts).isEmpty() ? detectBoard(image, opts)
                                                : BoardDetection{};
            drawDetection(image, opts, detection);
            previewCacheFilePath_ = filePath;
            previewCacheFileSize_ = fileInfo.size();
            previewCacheModifiedMs_ = modifiedMs;
            previewCacheOptions_ = opts;
            previewCacheAnnotated_ = std::move(image);
            previewCacheFound_ = detection.usable;
        }
        if (found) {
            *found = previewCacheFound_;
        }

        cv::Mat display = previewCacheAnnotated_;
        if (showUndistorted && calib.success) {
            cv::Mat tmp;
            if (calib.options.cameraModel == CameraModel::Fisheye) {
                cv::fisheye::undistortImage(
                    display, tmp, calib.cameraMatrix, calib.distCoeffs,
                    calib.cameraMatrix, display.size());
            } else {
                cv::undistort(
                    display, tmp, calib.cameraMatrix, calib.distCoeffs);
            }
            display = tmp;
        }

        return matToQImage(display);
    } catch (const cv::Exception&) {
        return {};
    } catch (const std::exception&) {
        return {};
    }
}

CalibrationResult Calibrator::calibrate(const QStringList& files,
                                        const CalibrationOptions& opts,
                                        ProgressCallback progress,
                                        CancelPredicate isCanceled)
{
    CalibrationResult result;
    result.imagesTotal = static_cast<int>(files.size());
    result.options = opts;
    if (!files.isEmpty()) {
        result.sourceDirectory =
            QFileInfo(files.first()).absolutePath();
    }
    const QString optionsError = validateOptions(opts);
    if (!optionsError.isEmpty()) {
        result.report = optionsError;
        return result;
    }

    try {
        std::vector<std::vector<cv::Point3f>> objectPoints;
        std::vector<std::vector<cv::Point2f>> imagePoints;
        QStringList usedFiles;
        cv::Size imageSize(0, 0);
        int sizeMismatchCount = 0;

        const int total = static_cast<int>(files.size());
        for (int i = 0; i < total; ++i) {
            if (isCanceled && isCanceled()) {
                result.report = QObject::tr("标定已取消。");
                return result;
            }

            const cv::Mat img =
                cv::imread(files[i].toStdString(), cv::IMREAD_COLOR);
            bool detected = false;
            if (!img.empty()) {
                if (imageSize.width == 0) {
                    imageSize = img.size();
                } else if (img.size() != imageSize) {
                    ++sizeMismatchCount;
                    if (progress) {
                        progress(i + 1, total, false);
                    }
                    continue;
                }

                BoardDetection detection = detectBoard(img, opts);
                if (detection.usable) {
                    objectPoints.push_back(
                        std::move(detection.objectPoints));
                    imagePoints.push_back(
                        std::move(detection.imagePoints));
                    usedFiles.push_back(files[i]);
                    detected = true;
                }
            }

            if (progress) {
                progress(i + 1, total, detected);
            }
        }

        result.imagesUsed = static_cast<int>(objectPoints.size());
        result.imageSize = {imageSize.width, imageSize.height};
        if (objectPoints.size() < 2) {
            QString report =
                QObject::tr("仅 %1 / %2 张图片检测到可用角点，至少需要 2 张。\n\n"
                            "常见原因：\n"
                            "  - 标定板类型、方格数或 ArUco Dictionary 不匹配。\n"
                            "  - 标定板部分超出画面或不平整。\n"
                            "  - 光照差 / 反光 / 对比度低。\n"
                            "  - 普通棋盘可尝试切换角点检测方法。")
                    .arg(result.imagesUsed)
                    .arg(result.imagesTotal);
            if (sizeMismatchCount > 0) {
                report += QObject::tr("\n\n注：%1 张图片因尺寸与首张图片不一致而被跳过。")
                              .arg(sizeMismatchCount);
            }
            result.report = report;
            return result;
        }

        cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
        cv::Mat distCoeffs;
        std::vector<cv::Mat> rvecs;
        std::vector<cv::Mat> tvecs;

        double rms = 0.0;
        if (opts.cameraModel == CameraModel::Fisheye) {
            distCoeffs = cv::Mat::zeros(4, 1, CV_64F);
            int flags = cv::CALIB_RECOMPUTE_EXTRINSIC
                        | cv::CALIB_CHECK_COND;
            if (!opts.skew) {
                flags |= cv::CALIB_FIX_SKEW;
            }
            if (opts.radialCoeffs < 4) {
                flags |= cv::CALIB_FIX_K4;
            }
            if (opts.radialCoeffs < 3) {
                flags |= cv::CALIB_FIX_K3;
            }
            rms = cv::fisheye::calibrate(
                objectPoints, imagePoints, imageSize, cameraMatrix,
                distCoeffs, rvecs, tvecs, flags);
        } else {
            distCoeffs = cv::Mat::zeros(8, 1, CV_64F);
            int flags = 0;
            if (!opts.tangential) {
                flags |= cv::CALIB_ZERO_TANGENT_DIST;
            }
            if (opts.radialCoeffs < 3) {
                flags |= cv::CALIB_FIX_K3;
            }
            rms = cv::calibrateCamera(
                objectPoints, imagePoints, imageSize, cameraMatrix,
                distCoeffs, rvecs, tvecs, flags);
        }

        const bool parametersValid =
            std::isfinite(rms)
            && cv::checkRange(cameraMatrix)
            && cv::checkRange(distCoeffs)
            && cameraMatrix.at<double>(0, 0) > 0.0
            && cameraMatrix.at<double>(1, 1) > 0.0;
        result.success = parametersValid;
        result.rmsError = rms;
        result.cameraMatrix = cameraMatrix.clone();
        result.distCoeffs = distCoeffs.clone();
        if (!parametersValid) {
            result.report = QObject::tr(
                "标定求解返回了无效的内参或畸变系数，请检查图片质量、标定板参数和拍摄姿态。");
            return result;
        }
        const size_t poseCount =
            std::min({rvecs.size(), tvecs.size(),
                      objectPoints.size(), imagePoints.size(),
                      static_cast<size_t>(usedFiles.size())});
        result.poses.reserve(poseCount);
        for (size_t index = 0; index < poseCount; ++index) {
            const double reprojectionError = viewReprojectionError(
                objectPoints[index], imagePoints[index],
                rvecs[index], tvecs[index], cameraMatrix, distCoeffs,
                opts.cameraModel);
            if (!std::isfinite(reprojectionError)) {
                result.success = false;
                result.poses.clear();
                result.report = QObject::tr(
                    "无法计算有效的单图重投影误差，标定结果已拒绝。");
                return result;
            }
            result.poses.push_back(
                {usedFiles[static_cast<qsizetype>(index)],
                 matToVec3d(rvecs[index]),
                 matToVec3d(tvecs[index]),
                 reprojectionError});
        }
        result.report = formatReport(result, imageSize);
        const auto worstPose = std::max_element(
            result.poses.cbegin(), result.poses.cend(),
            [](const CalibrationPose& lhs, const CalibrationPose& rhs) {
                return lhs.reprojectionError < rhs.reprojectionError;
            });
        if (worstPose != result.poses.cend()) {
            result.report += QObject::tr(
                "\n最大单图 RMS：%1 px（%2）")
                                 .arg(worstPose->reprojectionError, 0, 'f', 3)
                                 .arg(QFileInfo(worstPose->imagePath).fileName());
            result.qualityWarning =
                rms > kOverallRmsWarningThreshold
                || worstPose->reprojectionError
                       > kPerViewRmsWarningThreshold;
        }
        if (result.qualityWarning) {
            result.report += QObject::tr(
                "\n\n警告：整体 RMS 超过 1 px 或存在单图 RMS 超过 2 px，参数可能不稳定。建议检查高误差图片、标定板配置和拍摄姿态分布。");
        }
        if (sizeMismatchCount > 0) {
            result.report += QObject::tr("\n注：%1 张图片因尺寸与首张不一致而被跳过。")
                                 .arg(sizeMismatchCount);
        }
    } catch (const cv::Exception& e) {
        result.success = false;
        result.report = QObject::tr("OpenCV 错误：%1")
                            .arg(QString::fromLocal8Bit(e.what()));
    } catch (const std::exception& e) {
        result.success = false;
        result.report = QObject::tr("错误：%1")
                            .arg(QString::fromLocal8Bit(e.what()));
    }
    return result;
}

bool Calibrator::exportParameters(const QString& filePath,
                                  const CalibrationResult& result,
                                  QString* error) const
{
    if (error) {
        error->clear();
    }
    if (!result.success || result.cameraMatrix.empty()
        || result.distCoeffs.empty()) {
        if (error) {
            *error = QObject::tr("没有可导出的成功标定结果。");
        }
        return false;
    }
    if (result.cameraMatrix.rows != 3 || result.cameraMatrix.cols != 3
        || result.cameraMatrix.channels() != 1) {
        if (error) {
            *error = QObject::tr("相机内参矩阵必须是 3×3 单通道矩阵。");
        }
        return false;
    }
    const size_t expectedDistortionCount =
        result.options.cameraModel == CameraModel::Fisheye ? 4U : 5U;
    if (result.distCoeffs.channels() != 1
        || result.distCoeffs.total() != expectedDistortionCount) {
        if (error) {
            *error = QObject::tr(
                "畸变系数数量与相机模型不匹配：针孔模型需要 5 个，鱼眼模型需要 4 个。");
        }
        return false;
    }

    try {
        cv::FileStorage storage(
            ".yaml", cv::FileStorage::WRITE
                         | cv::FileStorage::MEMORY
                         | cv::FileStorage::FORMAT_YAML);
        if (!storage.isOpened()) {
            if (error) {
                *error = QObject::tr("无法创建 YAML 数据。");
            }
            return false;
        }

        const CalibrationOptions& opts = result.options;
        storage << "format_version" << 2;
        storage << "camera_model" << cameraModelName(opts.cameraModel);
        storage << "image_width" << result.imageSize.width();
        storage << "image_height" << result.imageSize.height();
        storage << "board_type" << boardTypeName(opts.boardType);
        storage << "board_columns" << opts.boardSize.width();
        storage << "board_rows" << opts.boardSize.height();
        storage << "square_size_mm" << opts.squareSize;
        if (opts.boardType == CalibrationBoardType::Charuco) {
            storage << "marker_size_mm" << opts.markerSize;
            storage << "aruco_dictionary"
                    << dictionaryName(opts.dictionary);
        } else {
            storage << "corner_detection_method"
                    << detectionMethodName(opts.method);
        }
        if (opts.cameraModel == CameraModel::Fisheye) {
            storage << "estimate_skew"
                    << static_cast<int>(opts.skew);
        } else {
            storage << "estimate_tangential_distortion"
                    << static_cast<int>(opts.tangential);
        }
        storage << "radial_coefficients" << opts.radialCoeffs;
        storage << "rms_reprojection_error" << result.rmsError;
        storage << "quality_warning"
                << static_cast<int>(result.qualityWarning);
        storage << "images_used" << result.imagesUsed;
        storage << "images_total" << result.imagesTotal;
        cv::Mat cameraMatrix;
        result.cameraMatrix.convertTo(cameraMatrix, CV_64F);
        storage.startWriteStruct("camera_matrix", cv::FileNode::SEQ);
        for (int row = 0; row < 3; ++row) {
            storage.startWriteStruct(
                "", cv::FileNode::SEQ | cv::FileNode::FLOW);
            for (int col = 0; col < 3; ++col) {
                storage << cameraMatrix.at<double>(row, col);
            }
            storage.endWriteStruct();
        }
        storage.endWriteStruct();
        cv::Mat distortionCoefficients;
        result.distCoeffs.reshape(1, 1).convertTo(
            distortionCoefficients, CV_64F);
        storage << "distortion_coefficients" << 0;
        storage << "poses" << "[";
        for (const CalibrationPose& pose : result.poses) {
            storage << "{"
                    << "image"
                    << QFileInfo(pose.imagePath).fileName().toStdString()
                    << "rotation_vector" << "["
                    << pose.rotationVector[0]
                    << pose.rotationVector[1]
                    << pose.rotationVector[2] << "]"
                    << "translation_vector" << "["
                    << pose.translationVector[0]
                    << pose.translationVector[1]
                    << pose.translationVector[2] << "]"
                    << "reprojection_error" << pose.reprojectionError
                    << "}";
        }
        storage << "]";

        cv::String yaml = storage.releaseAndGetString();
        std::ostringstream distortionBlock;
        distortionBlock.imbue(std::locale::classic());
        distortionBlock
            << std::setprecision(
                   std::numeric_limits<double>::max_digits10)
            << "distortion_coefficients: !!opencv-matrix\n"
            << "   rows: 1\n"
            << "   cols: " << distortionCoefficients.cols << '\n'
            << "   dt: d\n"
            << "   data: [ ";
        for (int col = 0; col < distortionCoefficients.cols; ++col) {
            if (col > 0) {
                distortionBlock << ", ";
            }
            distortionBlock << distortionCoefficients.at<double>(0, col);
        }
        distortionBlock << " ]";

        const cv::String distortionKey = "distortion_coefficients:";
        const size_t lineStart = yaml.find(distortionKey);
        const size_t lineEnd = yaml.find('\n', lineStart);
        if (lineStart == cv::String::npos || lineEnd == cv::String::npos) {
            if (error) {
                *error = QObject::tr("无法格式化畸变系数。");
            }
            return false;
        }
        yaml.replace(lineStart, lineEnd - lineStart, distortionBlock.str());
        QSaveFile output(filePath);
        if (!output.open(QIODevice::WriteOnly)) {
            if (error) {
                *error = output.errorString();
            }
            return false;
        }
        const qint64 bytes = static_cast<qint64>(yaml.size());
        if (output.write(yaml.data(), bytes) != bytes) {
            if (error) {
                *error = output.errorString();
            }
            output.cancelWriting();
            return false;
        }
        if (!output.commit()) {
            if (error) {
                *error = output.errorString();
            }
            return false;
        }
        return true;
    } catch (const cv::Exception& exception) {
        if (error) {
            *error = QObject::tr("OpenCV 错误：%1")
                         .arg(QString::fromLocal8Bit(exception.what()));
        }
    } catch (const std::exception& exception) {
        if (error) {
            *error = QObject::tr("错误：%1")
                         .arg(QString::fromLocal8Bit(exception.what()));
        }
    }
    return false;
}
