#include "calibration.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <opencv2/core/persistence.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include <cmath>
#include <cstdlib>

namespace {

bool writeChessboard(const QString& path, const QSize& squareCount)
{
    constexpr int kSquarePixels = 60;
    constexpr int kMarginPixels = 40;
    cv::Mat image(
        squareCount.height() * kSquarePixels + 2 * kMarginPixels,
        squareCount.width() * kSquarePixels + 2 * kMarginPixels,
        CV_8UC1, cv::Scalar(255));
    for (int row = 0; row < squareCount.height(); ++row) {
        for (int col = 0; col < squareCount.width(); ++col) {
            if ((row + col) % 2 != 0) {
                continue;
            }
            const cv::Point topLeft(
                kMarginPixels + col * kSquarePixels,
                kMarginPixels + row * kSquarePixels);
            cv::rectangle(
                image, cv::Rect(topLeft, cv::Size(kSquarePixels, kSquarePixels)),
                cv::Scalar(0), cv::FILLED);
        }
    }
    return cv::imwrite(path.toStdString(), image);
}

bool writeCharucoBoard(const QString& path, const CalibrationOptions& opts)
{
    const auto dictionary = cv::aruco::getPredefinedDictionary(
        cv::aruco::DICT_5X5_100);
    const cv::aruco::CharucoBoard board(
        cv::Size(opts.boardSize.width(), opts.boardSize.height()),
        static_cast<float>(opts.squareSize),
        static_cast<float>(opts.markerSize), dictionary);
    cv::Mat image;
    board.generateImage(cv::Size(840, 540), image, 20, 1);
    return cv::imwrite(path.toStdString(), image);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    if (argc != 2) {
        qCritical() << "用法: CalibrationBoardTest <ChArUco 图片目录>";
        return EXIT_FAILURE;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qCritical() << "无法创建测试临时目录";
        return EXIT_FAILURE;
    }

    Calibrator calibrator;
    bool found = false;
    QString exportError;
    const QString invalidExportPath =
        tempDir.filePath(QStringLiteral("invalid.yaml"));
    if (calibrator.exportParameters(
            invalidExportPath, {}, &exportError)
        || exportError.isEmpty()
        || QFileInfo::exists(invalidExportPath)) {
        qCritical() << "无成功标定结果时不应生成参数文件";
        return EXIT_FAILURE;
    }

    CalibrationOptions chessboard;
    chessboard.boardType = CalibrationBoardType::Chessboard;
    chessboard.boardSize = {10, 7};
    const QString chessboardPath =
        tempDir.filePath(QStringLiteral("chessboard.png"));
    if (!writeChessboard(chessboardPath, chessboard.boardSize)
        || calibrator.previewImage(
               chessboardPath, chessboard, {}, false, &found).isNull()
        || !found) {
        qCritical() << "普通棋盘未按 10×7 方格解析为 9×6 内部角点";
        return EXIT_FAILURE;
    }

    CalibrationOptions charuco;
    const QString charucoPath =
        tempDir.filePath(QStringLiteral("charuco.png"));
    found = false;
    if (!writeCharucoBoard(charucoPath, charuco)
        || calibrator.previewImage(
               charucoPath, charuco, {}, false, &found).isNull()
        || !found) {
        qCritical() << "未能检测默认的 14×9 ChArUco 标定板";
        return EXIT_FAILURE;
    }

    QDir imageDir(QString::fromLocal8Bit(argv[1]));
    QStringList files;
    for (const QString& name : imageDir.entryList(
             {QStringLiteral("*.jpg")}, QDir::Files, QDir::Name)) {
        files.push_back(imageDir.absoluteFilePath(name));
    }
    const CalibrationResult result = calibrator.calibrate(files, charuco);
    if (!result.success || result.imagesUsed < 30
        || !std::isfinite(result.rmsError)
        || result.poses.size() != static_cast<size_t>(result.imagesUsed)) {
        qCritical().noquote()
            << "ChArUco 数据集标定失败：" << result.imagesUsed
            << result.rmsError << result.report;
        return EXIT_FAILURE;
    }
    for (const CalibrationPose& pose : result.poses) {
        if (pose.imagePath.isEmpty()) {
            qCritical() << "标定结果中的位姿缺少对应图片";
            return EXIT_FAILURE;
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(pose.rotationVector[axis])
                || !std::isfinite(pose.translationVector[axis])) {
                qCritical() << "标定结果包含无效位姿数值";
                return EXIT_FAILURE;
            }
        }
    }
    if (QDir::cleanPath(result.sourceDirectory)
        != QDir::cleanPath(imageDir.absolutePath())) {
        qCritical() << "标定结果没有记录原始图片目录";
        return EXIT_FAILURE;
    }

    exportError.clear();
    const QString exportPath =
        tempDir.filePath(QStringLiteral("camera_parameters.yaml"));
    if (!calibrator.exportParameters(exportPath, result, &exportError)) {
        qCritical().noquote() << "导出相机参数失败：" << exportError;
        return EXIT_FAILURE;
    }

    cv::FileStorage exported(
        exportPath.toStdString(), cv::FileStorage::READ);
    if (!exported.isOpened()) {
        qCritical() << "无法重新读取导出的 YAML 文件";
        return EXIT_FAILURE;
    }
    cv::Mat cameraMatrix;
    cv::Mat distortion;
    int imageWidth = 0;
    int imageHeight = 0;
    int boardColumns = 0;
    int boardRows = 0;
    cv::String cameraModel;
    cv::String boardType;
    const cv::FileNode exportedPoses = exported["poses"];
    exported["camera_matrix"] >> cameraMatrix;
    exported["distortion_coefficients"] >> distortion;
    exported["image_width"] >> imageWidth;
    exported["image_height"] >> imageHeight;
    exported["board_columns"] >> boardColumns;
    exported["board_rows"] >> boardRows;
    exported["camera_model"] >> cameraModel;
    exported["board_type"] >> boardType;
    if (imageWidth != result.imageSize.width()
        || imageHeight != result.imageSize.height()
        || boardColumns != charuco.boardSize.width()
        || boardRows != charuco.boardSize.height()
        || cameraModel != "pinhole"
        || boardType != "charuco"
        || !exportedPoses.isSeq()
        || exportedPoses.size() != result.poses.size()
        || cv::norm(cameraMatrix, result.cameraMatrix, cv::NORM_INF) > 1.0e-12
        || cv::norm(distortion, result.distCoeffs, cv::NORM_INF) > 1.0e-12) {
        qCritical() << "导出的 YAML 内容与标定结果不一致";
        return EXIT_FAILURE;
    }

    CalibrationOptions fisheye = charuco;
    fisheye.cameraModel = CameraModel::Fisheye;
    fisheye.radialCoeffs = 4;
    const CalibrationResult fisheyeResult =
        calibrator.calibrate(files, fisheye);
    if (!fisheyeResult.success
        || fisheyeResult.distCoeffs.total() != 4
        || fisheyeResult.poses.size()
               != static_cast<size_t>(fisheyeResult.imagesUsed)
        || !std::isfinite(fisheyeResult.rmsError)) {
        qCritical().noquote()
            << "鱼眼模型标定失败：" << fisheyeResult.rmsError
            << fisheyeResult.report;
        return EXIT_FAILURE;
    }
    found = false;
    if (calibrator.previewImage(
            files.first(), fisheye, fisheyeResult, true, &found).isNull()
        || !found) {
        qCritical() << "鱼眼模型去畸变预览失败";
        return EXIT_FAILURE;
    }

    exportError.clear();
    const QString fisheyeExportPath =
        tempDir.filePath(QStringLiteral("fisheye_parameters.yaml"));
    if (!calibrator.exportParameters(
            fisheyeExportPath, fisheyeResult, &exportError)) {
        qCritical().noquote() << "导出鱼眼参数失败：" << exportError;
        return EXIT_FAILURE;
    }
    cv::FileStorage fisheyeExported(
        fisheyeExportPath.toStdString(), cv::FileStorage::READ);
    if (!fisheyeExported.isOpened()) {
        qCritical() << "无法重新读取导出的鱼眼 YAML 文件";
        return EXIT_FAILURE;
    }
    cv::String fisheyeModel;
    cv::Mat fisheyeDistortion;
    fisheyeExported["camera_model"] >> fisheyeModel;
    fisheyeExported["distortion_coefficients"] >> fisheyeDistortion;
    if (fisheyeModel != "fisheye" || fisheyeDistortion.total() != 4) {
        qCritical() << "导出的鱼眼模型参数不正确";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
