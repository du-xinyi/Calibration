#pragma once

#include "calibration.h"

#include <QString>
#include <QStringList>

#include <optional>

/**
 * @brief 可持久化的标定项目，包含图片列表和求解选项。
 */
struct CalibrationProject {
    CalibrationOptions options;
    QStringList imagePaths;
};

/**
 * @brief 标定项目 JSON 文件的读写入口。
 */
class CalibrationProjectIo {
public:
    /// 原子保存项目；相对图片路径会相对于项目文件所在目录写入。
    static bool save(const QString& filePath,
                     const CalibrationProject& project,
                     QString* error = nullptr);

    /// 读取并校验项目；相对图片路径会解析为绝对路径。
    static std::optional<CalibrationProject> load(
        const QString& filePath, QString* error = nullptr);
};
