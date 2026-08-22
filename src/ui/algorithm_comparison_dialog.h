#pragma once

#include "calibration.h"

#include <QDialog>

#include <vector>

/**
 * @brief 以表格汇总多个标定候选的求解状态、误差和核心内参
 */
class AlgorithmComparisonDialog: public QDialog
{
public:

    /**
     * @brief 构造标定算法对比窗口
     *
     * @param results 候选标定结果，窗口内部按成功状态和 RMS 重新排序
     * @param parent 所属窗口，允许为空
     */
    explicit AlgorithmComparisonDialog(
        std::vector<CalibrationResult> results,
        QWidget *parent = nullptr);
};
