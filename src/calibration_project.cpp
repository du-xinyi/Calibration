#include "calibration_project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

QString enumName(CameraModel value)
{
    return value == CameraModel::Fisheye ? QStringLiteral("fisheye")
                                          : QStringLiteral("pinhole");
}

QString enumName(CalibrationBoardType value)
{
    return value == CalibrationBoardType::Chessboard
               ? QStringLiteral("chessboard")
               : QStringLiteral("charuco");
}

QString enumName(CalibrationMethod value)
{
    return value == CalibrationMethod::SectorBased
               ? QStringLiteral("sector_based")
               : QStringLiteral("classic");
}

QString enumName(ArucoDictionary value)
{
    switch (value) {
    case ArucoDictionary::Dict4x4_50:
        return QStringLiteral("DICT_4X4_50");
    case ArucoDictionary::Dict5x5_100:
        return QStringLiteral("DICT_5X5_100");
    case ArucoDictionary::Dict5x5_250:
        return QStringLiteral("DICT_5X5_250");
    case ArucoDictionary::Dict6x6_250:
        return QStringLiteral("DICT_6X6_250");
    case ArucoDictionary::Original:
        return QStringLiteral("DICT_ARUCO_ORIGINAL");
    }
    return {};
}

template<typename Enum>
bool parseEnum(const QString&, Enum*) = delete;

template<>
bool parseEnum(const QString& text, CameraModel* value)
{
    if (text == QStringLiteral("pinhole")) {
        *value = CameraModel::Pinhole;
        return true;
    }
    if (text == QStringLiteral("fisheye")) {
        *value = CameraModel::Fisheye;
        return true;
    }
    return false;
}

template<>
bool parseEnum(const QString& text, CalibrationBoardType* value)
{
    if (text == QStringLiteral("charuco")) {
        *value = CalibrationBoardType::Charuco;
        return true;
    }
    if (text == QStringLiteral("chessboard")) {
        *value = CalibrationBoardType::Chessboard;
        return true;
    }
    return false;
}

template<>
bool parseEnum(const QString& text, CalibrationMethod* value)
{
    if (text == QStringLiteral("classic")) {
        *value = CalibrationMethod::Classic;
        return true;
    }
    if (text == QStringLiteral("sector_based")) {
        *value = CalibrationMethod::SectorBased;
        return true;
    }
    return false;
}

template<>
bool parseEnum(const QString& text, ArucoDictionary* value)
{
    const std::pair<const char*, ArucoDictionary> values[] = {
        {"DICT_4X4_50", ArucoDictionary::Dict4x4_50},
        {"DICT_5X5_100", ArucoDictionary::Dict5x5_100},
        {"DICT_5X5_250", ArucoDictionary::Dict5x5_250},
        {"DICT_6X6_250", ArucoDictionary::Dict6x6_250},
        {"DICT_ARUCO_ORIGINAL", ArucoDictionary::Original},
    };
    for (const auto& entry : values) {
        if (text == QString::fromLatin1(entry.first)) {
            *value = entry.second;
            return true;
        }
    }
    return false;
}

bool validOptions(const CalibrationOptions& options)
{
    // 持久化边界只接受界面与 Calibrator 均能安全处理的参数范围
    return options.boardSize.width() >= 2
           && options.boardSize.height() >= 2
           && options.squareSize > 0.0
           && (options.boardType != CalibrationBoardType::Charuco
               || (options.markerSize > 0.0
                   && options.markerSize < options.squareSize))
           && options.radialCoeffs >= 2
           && options.radialCoeffs
                  <= (options.cameraModel == CameraModel::Fisheye ? 4 : 3);
}

}  // namespace

bool CalibrationProjectIo::save(const QString& filePath,
                                const CalibrationProject& project,
                                QString* error)
{
    if (error) {
        error->clear();
    }
    if (!validOptions(project.options)) {
        if (error) {
            *error = QStringLiteral("标定项目包含无效参数。");
        }
        return false;
    }

    // format_version 与稳定的字符串枚举共同构成项目文件的兼容边界
    const CalibrationOptions& options = project.options;
    QJsonObject optionObject{
        {QStringLiteral("camera_model"), enumName(options.cameraModel)},
        {QStringLiteral("board_type"), enumName(options.boardType)},
        {QStringLiteral("board_columns"), options.boardSize.width()},
        {QStringLiteral("board_rows"), options.boardSize.height()},
        {QStringLiteral("square_size_mm"), options.squareSize},
        {QStringLiteral("marker_size_mm"), options.markerSize},
        {QStringLiteral("dictionary"), enumName(options.dictionary)},
        {QStringLiteral("method"), enumName(options.method)},
        {QStringLiteral("estimate_skew"), options.skew},
        {QStringLiteral("estimate_tangential"), options.tangential},
        {QStringLiteral("radial_coefficients"), options.radialCoeffs},
    };
    QJsonArray images;
    const QDir baseDirectory = QFileInfo(filePath).absoluteDir();
    for (const QString& imagePath : project.imagePaths) {
        // 使用相对路径便于将项目文件和图片目录整体移动到其他位置
        images.append(baseDirectory.relativeFilePath(imagePath));
    }
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("format_version"), 1},
        {QStringLiteral("options"), optionObject},
        {QStringLiteral("images"), images},
    });

    // QSaveFile 仅在完整写入后替换目标文件，避免留下半截 JSON
    QSaveFile output(filePath);
    const QByteArray data = document.toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(data) != data.size()
        || !output.commit()) {
        if (error) {
            *error = output.errorString();
        }
        return false;
    }
    return true;
}

std::optional<CalibrationProject> CalibrationProjectIo::load(
    const QString& filePath, QString* error)
{
    if (error) {
        error->clear();
    }
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = input.errorString();
        }
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("项目 JSON 无效：%1").arg(parseError.errorString());
        }
        return std::nullopt;
    }
    // 先验证顶层结构和版本，再读取各字段，避免把缺失值当成默认值接受
    const QJsonObject root = document.object();
    const QJsonObject object = root.value(QStringLiteral("options")).toObject();
    const QJsonArray images = root.value(QStringLiteral("images")).toArray();
    if (root.value(QStringLiteral("format_version")).toInt() != 1
        || object.isEmpty() || images.isEmpty()) {
        if (error) {
            *error = QStringLiteral("项目版本、参数或图片列表无效。");
        }
        return std::nullopt;
    }

    // 枚举必须严格匹配已知字符串，未知的新值不会被静默降级
    CalibrationProject project;
    CalibrationOptions& options = project.options;
    if (!parseEnum(object.value(QStringLiteral("camera_model")).toString(),
                   &options.cameraModel)
        || !parseEnum(object.value(QStringLiteral("board_type")).toString(),
                      &options.boardType)
        || !parseEnum(object.value(QStringLiteral("method")).toString(),
                      &options.method)
        || !parseEnum(object.value(QStringLiteral("dictionary")).toString(),
                      &options.dictionary)) {
        if (error) {
            *error = QStringLiteral("项目中包含未知的模型、标定板或检测选项。");
        }
        return std::nullopt;
    }
    options.boardSize = {
        object.value(QStringLiteral("board_columns")).toInt(),
        object.value(QStringLiteral("board_rows")).toInt()};
    options.squareSize = object.value(QStringLiteral("square_size_mm")).toDouble();
    options.markerSize = object.value(QStringLiteral("marker_size_mm")).toDouble();
    options.skew = object.value(QStringLiteral("estimate_skew")).toBool();
    options.tangential =
        object.value(QStringLiteral("estimate_tangential")).toBool();
    options.radialCoeffs =
        object.value(QStringLiteral("radial_coefficients")).toInt();
    if (!validOptions(options)) {
        if (error) {
            *error = QStringLiteral("项目中的标定参数超出有效范围。");
        }
        return std::nullopt;
    }

    // 对外统一返回清理后的路径，调用方无需再区分绝对和相对形式
    const QDir baseDirectory = QFileInfo(filePath).absoluteDir();
    for (const QJsonValue& value : images) {
        if (!value.isString() || value.toString().isEmpty()) {
            if (error) {
                *error = QStringLiteral("项目图片路径无效。");
            }
            return std::nullopt;
        }
        const QString path = value.toString();
        project.imagePaths.push_back(
            QDir::cleanPath(QDir::isAbsolutePath(path)
                                ? path
                                : baseDirectory.absoluteFilePath(path)));
    }
    return project;
}
