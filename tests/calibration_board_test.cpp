#include "calibration.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
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
        || !result.report.contains(QStringLiteral("ChArUco"))
        || (result.qualityWarning
            && !result.report.contains(QStringLiteral("警告")))
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
        if (!std::isfinite(pose.reprojectionError)
            || pose.reprojectionError < 0.0) {
            qCritical() << "标定结果包含无效的单图重投影误差";
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

    CalibrationResult invalidMatrixResult = result;
    invalidMatrixResult.cameraMatrix = cv::Mat::eye(2, 2, CV_64F);
    const QString invalidMatrixPath =
        tempDir.filePath(QStringLiteral("invalid_matrix.yaml"));
    exportError.clear();
    if (calibrator.exportParameters(
            invalidMatrixPath, invalidMatrixResult, &exportError)
        || exportError.isEmpty() || QFileInfo::exists(invalidMatrixPath)) {
        qCritical() << "非 3×3 相机矩阵不应被导出";
        return EXIT_FAILURE;
    }

    CalibrationResult invalidDistortionResult = result;
    invalidDistortionResult.distCoeffs = cv::Mat::zeros(4, 1, CV_64F);
    const QString invalidDistortionPath =
        tempDir.filePath(QStringLiteral("invalid_distortion.yaml"));
    exportError.clear();
    if (calibrator.exportParameters(
            invalidDistortionPath, invalidDistortionResult, &exportError)
        || exportError.isEmpty()
        || QFileInfo::exists(invalidDistortionPath)) {
        qCritical() << "针孔模型的非 5 元畸变系数不应被导出";
        return EXIT_FAILURE;
    }

    exportError.clear();
    const QString exportPath =
        tempDir.filePath(QStringLiteral("camera_parameters.yaml"));
    if (!calibrator.exportParameters(exportPath, result, &exportError)) {
        qCritical().noquote() << "导出相机参数失败：" << exportError;
        return EXIT_FAILURE;
    }

    QFile yamlFile(exportPath);
    if (!yamlFile.open(QIODevice::ReadOnly)) {
        qCritical() << "无法读取导出的 YAML 文本";
        return EXIT_FAILURE;
    }
    const QByteArray yamlText = yamlFile.readAll();
    const qsizetype distortionLineStart =
        yamlText.indexOf(QByteArrayLiteral("distortion_coefficients:"));
    if (distortionLineStart < 0) {
        qCritical() << "导出的 YAML 缺少 distortion_coefficients";
        return EXIT_FAILURE;
    }
    const qsizetype distortionLineEnd =
        yamlText.indexOf('\n', distortionLineStart);
    const QByteArray distortionLine = yamlText.mid(
        distortionLineStart, distortionLineEnd - distortionLineStart);
    if (distortionLineEnd < 0 || !distortionLine.contains('[')
        || !distortionLine.contains(']')) {
        qCritical() << "distortion_coefficients 没有在一行内导出";
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
    int formatVersion = 0;
    cv::String cameraModel;
    cv::String boardType;
    const cv::FileNode exportedCameraMatrix = exported["camera_matrix"];
    const cv::FileNode exportedDistortion =
        exported["distortion_coefficients"];
    const cv::FileNode exportedPoses = exported["poses"];
    double firstPoseError = 0.0;
    if (!exportedCameraMatrix.isSeq()
        || exportedCameraMatrix.size() != 3) {
        qCritical() << "导出的 camera_matrix 不是 3×3 数组";
        return EXIT_FAILURE;
    }
    cameraMatrix = cv::Mat::zeros(3, 3, CV_64F);
    for (int row = 0; row < 3; ++row) {
        const cv::FileNode exportedRow = exportedCameraMatrix[row];
        if (!exportedRow.isSeq() || exportedRow.size() != 3) {
            qCritical() << "导出的 camera_matrix 行列数不正确";
            return EXIT_FAILURE;
        }
        for (int col = 0; col < 3; ++col) {
            exportedRow[col] >> cameraMatrix.at<double>(row, col);
        }
    }
    if (!exportedDistortion.isSeq() || exportedDistortion.size() != 5) {
        qCritical() << "导出的 distortion_coefficients 不是 1×5 数组";
        return EXIT_FAILURE;
    }
    distortion = cv::Mat::zeros(1, 5, CV_64F);
    for (int index = 0; index < distortion.cols; ++index) {
        exportedDistortion[index] >> distortion.at<double>(0, index);
    }
    exported["format_version"] >> formatVersion;
    exported["image_width"] >> imageWidth;
    exported["image_height"] >> imageHeight;
    exported["board_columns"] >> boardColumns;
    exported["board_rows"] >> boardRows;
    exported["camera_model"] >> cameraModel;
    exported["board_type"] >> boardType;
    exportedPoses[0]["reprojection_error"] >> firstPoseError;
    if (formatVersion != 2
        || imageWidth != result.imageSize.width()
        || imageHeight != result.imageSize.height()
        || boardColumns != charuco.boardSize.width()
        || boardRows != charuco.boardSize.height()
        || cameraModel != "pinhole"
        || boardType != "charuco"
        || !exportedPoses.isSeq()
        || exportedPoses.size() != result.poses.size()
        || std::abs(firstPoseError - result.poses.front().reprojectionError)
               > 1.0e-12
        || cv::norm(cameraMatrix, result.cameraMatrix, cv::NORM_INF) > 1.0e-12
        || distortion.rows != 1 || distortion.cols != 5
        || cv::norm(distortion, result.distCoeffs.reshape(1, 1),
                    cv::NORM_INF) > 1.0e-12) {
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
    for (const CalibrationPose& pose : fisheyeResult.poses) {
        if (!std::isfinite(pose.reprojectionError)
            || pose.reprojectionError < 0.0) {
            qCritical() << "鱼眼标定结果包含无效的单图重投影误差";
            return EXIT_FAILURE;
        }
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
    const cv::FileNode fisheyeDistortionNode =
        fisheyeExported["distortion_coefficients"];
    if (!fisheyeDistortionNode.isSeq()
        || fisheyeDistortionNode.size() != 4) {
        qCritical() << "导出的鱼眼畸变系数不是 1×4 数组";
        return EXIT_FAILURE;
    }
    fisheyeDistortion = cv::Mat::zeros(1, 4, CV_64F);
    for (int index = 0; index < fisheyeDistortion.cols; ++index) {
        fisheyeDistortionNode[index]
            >> fisheyeDistortion.at<double>(0, index);
    }
    if (fisheyeModel != "fisheye"
        || fisheyeDistortion.rows != 1 || fisheyeDistortion.cols != 4) {
        qCritical() << "导出的鱼眼模型参数不正确";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
