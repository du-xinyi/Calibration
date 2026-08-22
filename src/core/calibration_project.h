#pragma once

#include "calibration.h"

#include <QString>
#include <QStringList>

#include <optional>

/**
 * @brief 可写入项目文件的标定输入快照
 */
struct CalibrationProject {
    CalibrationOptions options; ///< 保存时的完整标定选项
    QStringList imagePaths;     ///< 参与项目的图片路径
};

/**
 * @brief 负责标定项目 JSON 的版本校验、路径转换和原子保存
 */
class CalibrationProjectIo {
public:
    /**
     * @brief 将项目序列化到 JSON 文件
     *
     * @details 图片路径以项目文件目录为基准写成相对路径，目标文件仅在数据完整写入后提交
     *
     * @param filePath 目标项目文件
     * @param project 待序列化的项目快照
     * @param error 接收失败原因，可为 nullptr
     *
     * @return 成功校验并提交文件时返回 true
     */
    static bool save(const QString& filePath,
                     const CalibrationProject& project,
                     QString* error = nullptr);

    /**
     * @brief 从 JSON 文件恢复标定项目
     *
     * @details 仅接受受支持的格式版本和参数范围，并将相对图片路径解析为清理后的绝对路径
     *
     * @param filePath 待读取的项目文件
     * @param error 接收失败原因，可为 nullptr
     *
     * @return 有效项目；文件格式或参数无效时返回 std::nullopt
     */
    static std::optional<CalibrationProject> load(
        const QString& filePath, QString* error = nullptr);
};
