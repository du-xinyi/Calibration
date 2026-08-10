#include "algorithm_comparison_dialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QString modelName(CameraModel model)
{
    return model == CameraModel::Fisheye ? QStringLiteral("Fisheye")
                                         : QStringLiteral("Pinhole");
}

QString detectorName(const CalibrationOptions& options)
{
    if (options.boardType == CalibrationBoardType::Charuco) {
        return QStringLiteral("ChArUco");
    }
    return options.method == CalibrationMethod::SectorBased
               ? QStringLiteral("Sector-Based")
               : QStringLiteral("Classic");
}

QTableWidgetItem* numberItem(double value, int precision)
{
    auto* item = new QTableWidgetItem(
        std::isfinite(value) ? QString::number(value, 'f', precision)
                             : QStringLiteral("--"));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

}  // namespace

AlgorithmComparisonDialog::AlgorithmComparisonDialog(
    std::vector<CalibrationResult> results, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("AlgorithmComparisonDialog"));
    setWindowTitle(tr("算法结果对比"));
    resize(900, 360);
    std::stable_sort(results.begin(), results.end(),
                     [](const CalibrationResult& lhs,
                        const CalibrationResult& rhs) {
                         if (lhs.success != rhs.success) {
                             return lhs.success;
                         }
                         return lhs.rmsError < rhs.rmsError;
                     });

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(
        tr("RMS 较低只表示当前数据集的拟合误差较小；最终仍应结合去畸变效果、视场与参数稳定性选择模型。"),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* table = new QTableWidget(static_cast<int>(results.size()), 9, this);
    table->setObjectName(QStringLiteral("algorithmComparisonTable"));
    table->setHorizontalHeaderLabels(
        {tr("相机模型"), tr("检测器"), tr("状态"), tr("RMS (px)"),
         tr("有效图片"), QStringLiteral("fx"), QStringLiteral("fy"),
         QStringLiteral("cx"), QStringLiteral("cy")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (int row = 0; row < static_cast<int>(results.size()); ++row) {
        const CalibrationResult& result = results[static_cast<size_t>(row)];
        table->setItem(row, 0,
                       new QTableWidgetItem(modelName(result.options.cameraModel)));
        table->setItem(row, 1,
                       new QTableWidgetItem(detectorName(result.options)));
        table->setItem(row, 2, new QTableWidgetItem(
                                   result.success ? tr("成功") : tr("失败")));
        table->setItem(row, 3, numberItem(result.success ? result.rmsError
                                                         : NAN, 4));
        table->setItem(row, 4, new QTableWidgetItem(
                                   tr("%1 / %2")
                                       .arg(result.imagesUsed)
                                       .arg(result.imagesTotal)));
        for (int col = 5; col < 9; ++col) {
            table->setItem(row, col, numberItem(NAN, 2));
        }
        if (result.success && result.cameraMatrix.rows == 3
            && result.cameraMatrix.cols == 3) {
            table->setItem(row, 5,
                           numberItem(result.cameraMatrix.at<double>(0, 0), 2));
            table->setItem(row, 6,
                           numberItem(result.cameraMatrix.at<double>(1, 1), 2));
            table->setItem(row, 7,
                           numberItem(result.cameraMatrix.at<double>(0, 2), 2));
            table->setItem(row, 8,
                           numberItem(result.cameraMatrix.at<double>(1, 2), 2));
        }
    }
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}
