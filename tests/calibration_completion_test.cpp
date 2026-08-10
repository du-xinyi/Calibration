#include "algorithm_comparison_dialog.h"
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedLayout>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include <cstdlib>
#include <limits>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    if (argc != 2) {
        qCritical() << "用法: CalibrationCompletionTest <图片目录>";
        return EXIT_FAILURE;
    }

    QDir imageDir(QString::fromLocal8Bit(argv[1]));
    QStringList files;
    const QStringList names = imageDir.entryList(
        {QStringLiteral("*.jpg")}, QDir::Files, QDir::Name);
    for (const QString& name : names) {
        files.push_back(imageDir.absoluteFilePath(name));
    }
    if (files.size() < 3) {
        qCritical() << "回归测试至少需要 3 张图片，实际为" << files.size();
        return EXIT_FAILURE;
    }

    MainWindow window;
    CalibrationResult comparisonPinhole;
    comparisonPinhole.success = true;
    comparisonPinhole.rmsError = 0.5;
    comparisonPinhole.imagesUsed = 8;
    comparisonPinhole.imagesTotal = 10;
    comparisonPinhole.cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    CalibrationResult comparisonFisheye = comparisonPinhole;
    comparisonFisheye.options.cameraModel = CameraModel::Fisheye;
    comparisonFisheye.rmsError = 0.8;
    AlgorithmComparisonDialog comparisonDialog(
        {comparisonFisheye, comparisonPinhole});
    auto* comparisonTable = comparisonDialog.findChild<QTableWidget*>(
        QStringLiteral("algorithmComparisonTable"));
    if (comparisonTable == nullptr || comparisonTable->rowCount() != 2
        || comparisonTable->columnCount() != 9
        || comparisonTable->item(0, 0) == nullptr
        || comparisonTable->item(0, 0)->text() != QStringLiteral("Pinhole")
        || comparisonTable->item(0, 3)->text() != QStringLiteral("0.5000")) {
        qCritical() << "算法对比表格内容或排序错误";
        return EXIT_FAILURE;
    }
    auto* rightPanel =
        window.findChild<QWidget*>(QStringLiteral("rightPanel"));
    auto* mainSplitter =
        window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    auto* imageListTitle =
        window.findChild<QLabel*>(QStringLiteral("imageListTitle"));
    auto* imageListEmptyLabel =
        window.findChild<QLabel*>(QStringLiteral("imageListEmptyLabel"));
    auto* imageDisplay =
        window.findChild<QLabel*>(QStringLiteral("imageDisplay"));
    auto* imageListStack =
        window.findChild<QStackedLayout*>(QStringLiteral("imageListStack"));
    auto* imageList =
        window.findChild<QListWidget*>(QStringLiteral("imageList"));
    auto* cameraModelCombo =
        window.findChild<QComboBox*>(QStringLiteral("cameraModelCombo"));
    auto* boardTypeCombo =
        window.findChild<QComboBox*>(QStringLiteral("boardTypeCombo"));
    auto* methodCombo =
        window.findChild<QComboBox*>(QStringLiteral("methodCombo"));
    auto* dictionaryCombo =
        window.findChild<QComboBox*>(QStringLiteral("dictionaryCombo"));
    auto* boardColsSpin =
        window.findChild<QSpinBox*>(QStringLiteral("boardColsSpin"));
    auto* boardRowsSpin =
        window.findChild<QSpinBox*>(QStringLiteral("boardRowsSpin"));
    auto* squareSizeSpin =
        window.findChild<QDoubleSpinBox*>(QStringLiteral("squareSizeSpin"));
    auto* markerSizeSpin =
        window.findChild<QDoubleSpinBox*>(QStringLiteral("markerSizeSpin"));
    auto* skewCheck =
        window.findChild<QCheckBox*>(QStringLiteral("skewCheck"));
    auto* tangentialCheck =
        window.findChild<QCheckBox*>(QStringLiteral("tangentialCheck"));
    auto* showUndistortedCheck =
        window.findChild<QCheckBox*>(
            QStringLiteral("showUndistortedCheck"));
    auto* radialCoeffSpin =
        window.findChild<QSpinBox*>(QStringLiteral("radialCoeffSpin"));
    auto* poseAction =
        window.findChild<QAction*>(QStringLiteral("poseResultAction"));
    auto* calibrateAction =
        window.findChild<QAction*>(QStringLiteral("calibrateAction"));
    auto* exportAction =
        window.findChild<QAction*>(QStringLiteral("exportAction"));
    auto* clearAction =
        window.findChild<QAction*>(QStringLiteral("clearAction"));
    auto* openProjectAction =
        window.findChild<QAction*>(QStringLiteral("openProjectAction"));
    auto* saveProjectAction =
        window.findChild<QAction*>(QStringLiteral("saveProjectAction"));
    auto* importAction =
        window.findChild<QAction*>(QStringLiteral("importParametersAction"));
    auto* compareAction =
        window.findChild<QAction*>(QStringLiteral("compareAlgorithmsAction"));
    auto* batchExportAction =
        window.findChild<QAction*>(QStringLiteral("batchExportAction"));
    if (rightPanel == nullptr || rightPanel->minimumWidth() != 220
        || rightPanel->maximumWidth() != 260
        || mainSplitter == nullptr || mainSplitter->handleWidth() != 4
        || imageListTitle == nullptr
        || imageListTitle->text() != QStringLiteral("Images (0)")
        || imageListEmptyLabel == nullptr
        || imageList == nullptr
        || imageListStack == nullptr || imageListStack->currentIndex() != 0
        || imageDisplay == nullptr
        || imageDisplay->text()
               != QStringLiteral("添加图片或文件夹以开始标定")
        || cameraModelCombo == nullptr || cameraModelCombo->count() != 2
        || cameraModelCombo->itemText(0) != QStringLiteral("Pinhole")
        || cameraModelCombo->itemText(1) != QStringLiteral("Fisheye")
        || cameraModelCombo->currentData().toInt()
               != static_cast<int>(CameraModel::Pinhole)
        || boardTypeCombo == nullptr || boardTypeCombo->count() != 2
        || boardTypeCombo->itemText(0) != QStringLiteral("ChArUco")
        || boardTypeCombo->itemText(1) != QStringLiteral("Chessboard")
        || methodCombo == nullptr || methodCombo->count() != 2
        || methodCombo->itemText(0) != QStringLiteral("Classic")
        || methodCombo->itemText(1)
               != QStringLiteral("Sector-Based")
        || methodCombo->currentData().toInt()
               != static_cast<int>(CalibrationMethod::Classic)
        || !methodCombo->isHidden()
        || dictionaryCombo == nullptr || dictionaryCombo->isHidden()
        || boardColsSpin == nullptr || boardColsSpin->value() != 14
        || boardRowsSpin == nullptr || boardRowsSpin->value() != 9
        || squareSizeSpin == nullptr || squareSizeSpin->value() != 20.0
        || squareSizeSpin->suffix() != QStringLiteral(" mm")
        || markerSizeSpin == nullptr
        || markerSizeSpin->suffix() != QStringLiteral(" mm")
        || skewCheck == nullptr || !skewCheck->isHidden()
        || skewCheck->text() != QStringLiteral("Skew")
        || tangentialCheck == nullptr || tangentialCheck->isHidden()
        || tangentialCheck->text()
               != QStringLiteral("Tangential Distortion")
        || showUndistortedCheck == nullptr
        || showUndistortedCheck->text()
               != QStringLiteral("Show Undistorted")
        || showUndistortedCheck->isEnabled()
        || radialCoeffSpin == nullptr || radialCoeffSpin->maximum() != 3
        || calibrateAction == nullptr || calibrateAction->isEnabled()
        || calibrateAction->icon().isNull()
        || exportAction == nullptr || exportAction->isEnabled()
        || exportAction->icon().isNull()
        || clearAction == nullptr || clearAction->isEnabled()
        || clearAction->icon().isNull()
        || poseAction == nullptr || poseAction->isEnabled()
        || poseAction->icon().isNull()
        || openProjectAction == nullptr || !openProjectAction->isEnabled()
        || saveProjectAction == nullptr || saveProjectAction->isEnabled()
        || importAction == nullptr || !importAction->isEnabled()
        || compareAction == nullptr || compareAction->isEnabled()
        || compareAction->icon().isNull()
        || batchExportAction == nullptr || batchExportAction->isEnabled()) {
        qCritical() << "相机模型、标定板或默认参数配置错误";
        return EXIT_FAILURE;
    }
    cameraModelCombo->setCurrentIndex(1);
    if (cameraModelCombo->currentData().toInt()
            != static_cast<int>(CameraModel::Fisheye)
        || skewCheck->isHidden() || !tangentialCheck->isHidden()
        || radialCoeffSpin->maximum() != 4
        || radialCoeffSpin->value() != 4) {
        qCritical() << "无法切换到鱼眼模型";
        return EXIT_FAILURE;
    }
    cameraModelCombo->setCurrentIndex(0);
    boardTypeCombo->setCurrentIndex(1);
    if (boardTypeCombo->currentData().toInt()
            != static_cast<int>(CalibrationBoardType::Chessboard)
        || !skewCheck->isHidden() || tangentialCheck->isHidden()
        || methodCombo->isHidden() || !markerSizeSpin->isHidden()
        || !dictionaryCombo->isHidden()) {
        qCritical() << "无法切换到普通棋盘格";
        return EXIT_FAILURE;
    }
    methodCombo->setCurrentIndex(1);
    if (methodCombo->currentData().toInt()
        != static_cast<int>(CalibrationMethod::SectorBased)) {
        qCritical() << "无法切换到 Sector-Based 角点检测";
        return EXIT_FAILURE;
    }
    methodCombo->setCurrentIndex(0);
    boardTypeCombo->setCurrentIndex(0);

    window.loadImages(files);
    if (!calibrateAction->isEnabled() || !clearAction->isEnabled()
        || exportAction->isEnabled() || !saveProjectAction->isEnabled()
        || !compareAction->isEnabled() || batchExportAction->isEnabled()
        || imageListStack->currentIndex() != 1
        || imageListTitle->text()
               != QStringLiteral("Images (%1)").arg(files.size())
        || imageDisplay->text()
               != QStringLiteral("选择左侧图片查看预览")) {
        qCritical() << "加载图片后界面状态错误";
        return EXIT_FAILURE;
    }
    window.show();

    bool resultDialogSeen = false;
    bool poseDialogRequested = false;
    bool exclusionRequested = false;
    bool comparisonResultSeen = false;
    QTimer dialogMonitor;
    dialogMonitor.setInterval(10);
    QObject::connect(&dialogMonitor, &QTimer::timeout, &app, [&] {
        bool progressDialogVisible = false;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* progress = qobject_cast<QProgressDialog*>(widget);
                progress != nullptr && progress->isVisible()) {
                progressDialogVisible = true;
            }
            if (auto* message = qobject_cast<QMessageBox*>(widget);
                message != nullptr && message->isVisible()) {
                if (exclusionRequested) {
                    if (!message->text().contains(
                            QStringLiteral("排除 1 张图片后重新标定"))
                        || !message->text().contains(
                            QStringLiteral("RMS"))) {
                        qCritical() << "排除重标定结果缺少 RMS 对比";
                        app.exit(EXIT_FAILURE);
                        return;
                    }
                    comparisonResultSeen = true;
                } else {
                    resultDialogSeen = true;
                }
                message->accept();
            }
            if (widget->objectName() == QStringLiteral("PoseResultDialog")
                && widget->isVisible() && !exclusionRequested) {
                auto* poseTable =
                    widget->findChild<QTableWidget*>(
                        QStringLiteral("poseTable"));
                auto* viewStyleCombo =
                    widget->findChild<QComboBox*>(
                        QStringLiteral("viewStyleCombo"));
                auto* excludeButton =
                    widget->findChild<QPushButton*>(
                        QStringLiteral("excludeAndRecalibrateButton"));
                if (poseTable == nullptr || poseTable->rowCount() < 1
                    || poseTable->columnCount() != 9
                    || viewStyleCombo == nullptr
                    || viewStyleCombo->count() != 2
                    || excludeButton == nullptr
                    || poseTable->horizontalHeaderItem(0) == nullptr
                    || poseTable->horizontalHeaderItem(0)->text()
                           != QStringLiteral("排除")
                    || poseTable->horizontalHeaderItem(1) == nullptr
                    || poseTable->horizontalHeaderItem(1)->text()
                           != QStringLiteral("Image")
                    || poseTable->horizontalHeaderItem(2) == nullptr
                    || poseTable->horizontalHeaderItem(2)->text()
                           != QStringLiteral("r_x")
                    || poseTable->horizontalHeaderItem(5) == nullptr
                    || poseTable->horizontalHeaderItem(5)->text()
                           != QStringLiteral("t_x")
                    || poseTable->horizontalHeaderItem(8) == nullptr
                    || poseTable->horizontalHeaderItem(8)->text()
                           != QStringLiteral("RMS (px)")) {
                    qCritical() << "位姿结果窗口内容错误";
                    app.exit(EXIT_FAILURE);
                    return;
                }
                double previousRms =
                    std::numeric_limits<double>::infinity();
                bool highErrorSeen = false;
                for (int row = 0; row < poseTable->rowCount(); ++row) {
                    const auto* rmsItem = poseTable->item(row, 8);
                    auto* excludeItem = poseTable->item(row, 0);
                    bool rmsOk = false;
                    const double rms = rmsItem == nullptr
                                           ? 0.0
                                           : rmsItem->text().toDouble(&rmsOk);
                    if (!rmsOk || rms > previousRms + 1.0e-5
                        || excludeItem == nullptr
                        || !excludeItem->flags().testFlag(
                            Qt::ItemIsUserCheckable)) {
                        qCritical() << "位姿结果没有按 RMS 降序或缺少排除项";
                        app.exit(EXIT_FAILURE);
                        return;
                    }
                    previousRms = rms;
                    if (rms > kPerViewRmsWarningThresholdPx) {
                        highErrorSeen = true;
                        if (rmsItem->background().color()
                            != QColor(255, 232, 232)) {
                            qCritical() << "高 RMS 图片没有标红";
                            app.exit(EXIT_FAILURE);
                            return;
                        }
                    }
                    excludeItem->setCheckState(Qt::Unchecked);
                }
                if (!highErrorSeen) {
                    qCritical() << "测试数据没有产生可验证的高 RMS 图片";
                    app.exit(EXIT_FAILURE);
                    return;
                }
                viewStyleCombo->setCurrentIndex(1);
                poseTable->item(0, 0)->setCheckState(Qt::Checked);
                if (!excludeButton->isEnabled()) {
                    qCritical() << "选择排除图片后重标定按钮未启用";
                    app.exit(EXIT_FAILURE);
                    return;
                }
                exclusionRequested = true;
                excludeButton->click();
            }
        }

        if (resultDialogSeen && !progressDialogVisible
            && !poseDialogRequested) {
            if (!poseAction->isEnabled()) {
                qCritical() << "标定完成后位姿结果入口未启用";
                app.exit(EXIT_FAILURE);
                return;
            }
            if (!exportAction->isEnabled()) {
                qCritical() << "标定完成后导出入口未启用";
                app.exit(EXIT_FAILURE);
                return;
            }
            if (!batchExportAction->isEnabled()) {
                qCritical() << "标定完成后批量去畸变入口未启用";
                app.exit(EXIT_FAILURE);
                return;
            }
            poseDialogRequested = true;
            QTimer::singleShot(0, poseAction, &QAction::trigger);
        }
        if (comparisonResultSeen && !progressDialogVisible) {
            if (imageList->count() != files.size() - 1
                || imageListTitle->text()
                       != QStringLiteral("Images (%1)")
                              .arg(files.size() - 1)
                || !rightPanel->isEnabled()
                || !cameraModelCombo->isEnabled()
                || !showUndistortedCheck->isEnabled()) {
                qCritical() << "排除图片并重新标定后的界面状态错误";
                app.exit(EXIT_FAILURE);
                return;
            }
            showUndistortedCheck->setChecked(true);
            cameraModelCombo->setCurrentIndex(1);
            if (exportAction->isEnabled() || poseAction->isEnabled()
                || showUndistortedCheck->isChecked()
                || showUndistortedCheck->isEnabled()) {
                qCritical() << "切换标定算法后旧结果状态没有清除";
                app.exit(EXIT_FAILURE);
                return;
            }
            dialogMonitor.stop();
            QTimer::singleShot(0, &app, [&app] { app.exit(EXIT_SUCCESS); });
        }
    });
    dialogMonitor.start();

    QTimer::singleShot(0, &window,
                       [&window, &app, rightPanel, cameraModelCombo] {
        if (!QMetaObject::invokeMethod(
                &window, "onCalibrate", Qt::DirectConnection)) {
            qCritical() << "无法调用标定槽函数";
            app.exit(EXIT_FAILURE);
            return;
        }
        if (rightPanel->isEnabled() || cameraModelCombo->isEnabled()) {
            qCritical() << "标定运行期间参数控件仍可修改";
            app.exit(EXIT_FAILURE);
        }
    });
    QTimer::singleShot(25000, &app, [&app] {
        qCritical() << "等待标定完成超时";
        app.exit(EXIT_FAILURE);
    });

    return app.exec();
}
