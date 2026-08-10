#pragma once

#include "calibration.h"

#include <QString>
#include <QStringList>

#include <optional>

/**
 * @brief 可持久化的标定项目，包含图片列表和求解选项。
 */
struct CalibrationProject {
    CalibrationOptions options; ///< 项目保存的求解参数
    QStringList imagePaths;     ///< 标定输入图片路径
};

/**
 * @brief 标定项目 JSON 文件的读写入口。
 */
class CalibrationProjectIo {
public:
    /**
     * @brief 原子保存标定项目
     *
     * 图片路径会尽可能相对于项目文件所在目录写入
     *
     * @param filePath 目标 JSON 文件路径
     * @param project 待保存的项目数据
     * @param error 失败原因，可为 nullptr
     *
     * @return 文件成功提交时返回 true
     */
    static bool save(const QString& filePath,
                     const CalibrationProject& project,
                     QString* error = nullptr);

    /**
     * @brief 读取并校验标定项目
     *
     * 相对图片路径会以项目文件所在目录为基准解析为绝对路径
     *
     * @param filePath 项目 JSON 文件路径
     * @param error 失败原因，可为 nullptr
     *
     * @return 校验后的项目；读取或校验失败时返回 std::nullopt
     */
    static std::optional<CalibrationProject> load(
        const QString& filePath, QString* error = nullptr);
};
