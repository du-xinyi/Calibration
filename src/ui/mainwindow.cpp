#include "mainwindow.h"
#include "algorithm_comparison_dialog.h"
#include "calibration_project.h"
#include "image_quality.h"
#include "pose_result_dialog.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFutureWatcher>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPixmap>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include <QtConcurrent>

#include <algorithm>
#include <bitset>
#include <cmath>
#include <utility>

namespace
{
    constexpr int kThumbnailSize = 96;
    constexpr int kRoleFilePath = Qt::UserRole + 1;
    constexpr int kRoleImageHash = Qt::UserRole + 2;
    constexpr int kRoleQualityWarnings = Qt::UserRole + 3;

    QIcon themedIcon(QStyle *style, QStyle::StandardPixmap fallback,
        const QStringList &names)
    {
        // 优先遵循桌面主题，保证不同平台上仍有可用的内置后备图标
        for (const QString &name: names)
        {
            const QIcon icon = QIcon::fromTheme(name);
            if (!icon.isNull())
            {
                return icon;
            }
        }
        return style->standardIcon(fallback);
    }

    QStringList supportedImageNameFilters()
    {
        const auto formats = QImageReader::supportedImageFormats();
        QStringList filters;
        filters.reserve(formats.size());
        for (const QByteArray &fmt: formats)
        {
            filters << QStringLiteral("*.") + QString::fromLatin1(fmt);
        }
        return filters;
    }

    QString supportedImageDialogFilter()
    {
        const auto formats = QImageReader::supportedImageFormats();
        QStringList exts;
        exts.reserve(formats.size());
        for (const QByteArray &fmt: formats)
        {
            exts << QStringLiteral("*.") + QString::fromLatin1(fmt);
        }
        return QStringLiteral("图片 (%1)").arg(exts.join(QLatin1Char(' ')));
    }

    bool sameCalibrationOptions(const CalibrationOptions &lhs,
        const CalibrationOptions &rhs)
    {
        return lhs.cameraModel == rhs.cameraModel
                && lhs.boardType == rhs.boardType
                && lhs.boardSize == rhs.boardSize
                && lhs.squareSize == rhs.squareSize
                && lhs.markerSize == rhs.markerSize
                && lhs.dictionary == rhs.dictionary
                && lhs.method == rhs.method
                && lhs.skew == rhs.skew
                && lhs.tangential == rhs.tangential
                && lhs.radialCoeffs == rhs.radialCoeffs;
    }

    void setComboValue(QComboBox *combo, int value)
    {
        const int index = combo->findData(value);
        if (index >= 0)
        {
            combo->setCurrentIndex(index);
        }
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui_(std::make_unique<Ui::MainWindow>())
{
    ui_->setupUi(this);

#ifdef PROJECT_SOURCE_DIR

    lastDir_ = QString::fromUtf8(PROJECT_SOURCE_DIR);
#else
    const QString pictures =
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    lastDir_ = pictures.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : pictures;
#endif

    buildMenuBar();
    buildToolBar();
    configureCentralWidget();
    buildStatusBar();
}

MainWindow::~MainWindow()
{
    // 先移除完成回调再等待任务，避免析构过程中重新进入窗口逻辑
    if (calibWatcher_ != nullptr)
    {
        disconnect(calibWatcher_, &QFutureWatcher<CalibrationResult>::finished,
            this, nullptr);
        if (calibWatcher_->isRunning())
        {
            calibCanceled_.store(true);
            calibWatcher_->waitForFinished();
        }
    }

    // 两类辅助任务也捕获了窗口成员，必须在 QObject 子对象销毁前结束
    auxiliaryCanceled_.store(true);
    if (comparisonWatcher_ != nullptr && comparisonWatcher_->isRunning())
    {
        disconnect(comparisonWatcher_, nullptr, this, nullptr);
        comparisonWatcher_->waitForFinished();
    }
    if (batchWatcher_ != nullptr && batchWatcher_->isRunning())
    {
        disconnect(batchWatcher_, nullptr, this, nullptr);
        batchWatcher_->waitForFinished();
    }
    QCoreApplication::removePostedEvents(this);
}

void MainWindow::buildMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    openProjectAction_ = fileMenu->addAction(
        tr("打开项目(&O)..."), QKeySequence(QStringLiteral("Ctrl+Alt+O")),
        this, &MainWindow::onOpenProject);
    openProjectAction_->setObjectName(QStringLiteral("openProjectAction"));
    saveProjectAction_ = fileMenu->addAction(
        tr("保存项目(&S)..."), QKeySequence(QStringLiteral("Ctrl+Alt+S")),
        this, &MainWindow::onSaveProject);
    saveProjectAction_->setObjectName(QStringLiteral("saveProjectAction"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("添加图片(&A)..."), QKeySequence::Open, this,
        &MainWindow::onAddImages);
    fileMenu->addAction(tr("从文件夹添加(&F)..."),
        QKeySequence("Ctrl+Shift+O"), this,
        &MainWindow::onAddImagesFromFolder);
    fileMenu->addSeparator();
    importAction_ = fileMenu->addAction(
        tr("导入相机参数(&I)..."), this, &MainWindow::onImportParameters);
    importAction_->setObjectName(QStringLiteral("importParametersAction"));
    batchExportAction_ = fileMenu->addAction(
        tr("批量导出去畸变图片(&U)..."), this,
        &MainWindow::onExportUndistortedImages);
    batchExportAction_->setObjectName(QStringLiteral("batchExportAction"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出(&X)"), QKeySequence::Quit, this, &QWidget::close);

    auto *calibrationMenu = menuBar()->addMenu(tr("标定(&C)"));
    compareAction_ = calibrationMenu->addAction(
        tr("比较标定算法..."), this, &MainWindow::onCompareAlgorithms);
    compareAction_->setObjectName(QStringLiteral("compareAlgorithmsAction"));

    auto *viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(tr("适应窗口"), QKeySequence("Ctrl+0"), this,
        &MainWindow::fitImageToView);

    menuBar()->addMenu(tr("帮助(&H)"))->addAction(tr("关于(&A)"), this, [this]
    {
        QMessageBox::about(this, tr("关于"),
            tr("相机标定\n基于 Qt 的相机标定工具，"
                "布局参考 MATLAB Camera Calibrator。"));
    });
}

void MainWindow::buildToolBar()
{
    auto *bar = addToolBar(tr("主工具栏"));
    bar->setMovable(false);
    bar->setIconSize({24, 24});
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto *addImagesAction = bar->addAction(
        themedIcon(style(), QStyle::SP_FileIcon,
            {
                QStringLiteral("insert-image-symbolic"),
                QStringLiteral("insert-image"),
                QStringLiteral("image-x-generic")
            }),
        tr("图片"), this, &MainWindow::onAddImages);
    addImagesAction->setToolTip(tr("添加图片"));
    auto *addFolderAction = bar->addAction(
        themedIcon(style(), QStyle::SP_DirOpenIcon,
            {
                QStringLiteral("folder-open-symbolic"),
                QStringLiteral("folder-open")
            }),
        tr("文件夹"), this, &MainWindow::onAddImagesFromFolder);
    addFolderAction->setToolTip(tr("从文件夹添加图片"));
    calibrateAction_ = bar->addAction(
        themedIcon(style(), QStyle::SP_MediaPlay,
            {
                QStringLiteral("camera-photo-symbolic"),
                QStringLiteral("camera-photo")
            }),
        tr("标定"), this, &MainWindow::onCalibrate);
    calibrateAction_->setObjectName(QStringLiteral("calibrateAction"));
    calibrateAction_->setToolTip(tr("至少添加 3 张图片后开始标定"));
    bar->addAction(compareAction_);
    compareAction_->setIcon(themedIcon(
        style(), QStyle::SP_FileDialogDetailedView,
        {
            QStringLiteral("view-list-details-symbolic"),
            QStringLiteral("view-list-details")
        }));
    compareAction_->setText(tr("算法对比"));
    compareAction_->setToolTip(tr("比较 Pinhole/Fisheye 与可用角点检测器"));
    bar->addSeparator();
    poseAction_ = bar->addAction(
        themedIcon(style(), QStyle::SP_BrowserReload,
            {
                QStringLiteral("object-rotate-right-symbolic"),
                QStringLiteral("object-rotate-right")
            }),
        tr("位姿"), this, &MainWindow::onShowPoses);
    poseAction_->setObjectName(QStringLiteral("poseResultAction"));
    poseAction_->setToolTip(tr("查看位姿估计结果"));
    exportAction_ = bar->addAction(
        themedIcon(style(), QStyle::SP_DialogSaveButton,
            {
                QStringLiteral("document-save-as-symbolic"),
                QStringLiteral("document-save-as")
            }),
        tr("导出"), this, &MainWindow::onExportParameters);
    exportAction_->setObjectName(QStringLiteral("exportAction"));
    exportAction_->setToolTip(tr("导出相机参数"));
    bar->addSeparator();
    clearAction_ = bar->addAction(
        themedIcon(style(), QStyle::SP_TrashIcon,
            {
                QStringLiteral("edit-clear-symbolic"),
                QStringLiteral("edit-clear")
            }),
        tr("清空"), this, &MainWindow::onClearAll);
    clearAction_->setObjectName(QStringLiteral("clearAction"));
    clearAction_->setToolTip(tr("清空已添加图片"));
}

void MainWindow::configureCentralWidget()
{
    imageList_ = ui_->imageList;
    imageScroll_ = ui_->imageScroll;
    imageDisplay_ = ui_->imageDisplay;
    cameraModelCombo_ = ui_->cameraModelCombo;
    boardTypeCombo_ = ui_->boardTypeCombo;
    boardColsSpin_ = ui_->boardColsSpin;
    boardRowsSpin_ = ui_->boardRowsSpin;
    squareSizeSpin_ = ui_->squareSizeSpin;
    markerSizeSpin_ = ui_->markerSizeSpin;
    dictionaryCombo_ = ui_->dictionaryCombo;
    methodCombo_ = ui_->methodCombo;
    skewCheck_ = ui_->skewCheck;
    tangentialCheck_ = ui_->tangentialCheck;
    radialCoeffSpin_ = ui_->radialCoeffSpin;
    showUndistortedCheck_ = ui_->showUndistortedCheck;

    imageList_->setIconSize({kThumbnailSize, kThumbnailSize});
    connect(imageList_, &QListWidget::itemSelectionChanged, this,
        &MainWindow::onImageSelectionChanged);

    imageDisplay_->setBackgroundRole(QPalette::Dark);
    imageDisplay_->setAutoFillBackground(true);
    imageScroll_->setBackgroundRole(QPalette::Dark);

    cameraModelCombo_->setItemData(
        0, static_cast<int>(CameraModel::Pinhole));
    cameraModelCombo_->setItemData(
        1, static_cast<int>(CameraModel::Fisheye));
    boardTypeCombo_->setItemData(
        0, static_cast<int>(CalibrationBoardType::Charuco));
    boardTypeCombo_->setItemData(
        1, static_cast<int>(CalibrationBoardType::Chessboard));
    dictionaryCombo_->setItemData(
        0, static_cast<int>(ArucoDictionary::Dict5x5_100));
    dictionaryCombo_->setItemData(
        1, static_cast<int>(ArucoDictionary::Dict5x5_250));
    dictionaryCombo_->setItemData(
        2, static_cast<int>(ArucoDictionary::Dict4x4_50));
    dictionaryCombo_->setItemData(
        3, static_cast<int>(ArucoDictionary::Dict6x6_250));
    dictionaryCombo_->setItemData(
        4, static_cast<int>(ArucoDictionary::Original));
    methodCombo_->setItemData(
        0, static_cast<int>(CalibrationMethod::Classic));
    methodCombo_->setItemData(
        1, static_cast<int>(CalibrationMethod::SectorBased));

    markerSizeSpin_->setToolTip(tr("仅 ChArUco 标定板使用"));
    dictionaryCombo_->setToolTip(tr("仅 ChArUco 标定板使用"));
    methodCombo_->setToolTip(tr("仅 Chessboard 标定板使用：Classic / Sector-Based"));
    skewCheck_->setToolTip(tr("仅 Fisheye 模型使用"));
    tangentialCheck_->setToolTip(tr("仅 Pinhole 模型使用"));

    connect(showUndistortedCheck_, &QCheckBox::toggled, this,
        &MainWindow::onDisplayModeChanged);

    // 连续调整参数时合并预览请求，避免反复执行角点检测
    debounceTimer_ = new QTimer(this);
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(150);
    connect(debounceTimer_, &QTimer::timeout, this,
        &MainWindow::onImageSelectionChanged);

    connect(cameraModelCombo_,
        qOverload<int>(&QComboBox::currentIndexChanged), this,
        &MainWindow::onCameraModelChanged);
    connect(boardTypeCombo_,
        qOverload<int>(&QComboBox::currentIndexChanged), this,
        &MainWindow::onBoardTypeChanged);
    connect(boardColsSpin_,
        qOverload<int>(&QSpinBox::valueChanged), this,
        &MainWindow::onCalibrationOptionsChanged);
    connect(boardRowsSpin_,
        qOverload<int>(&QSpinBox::valueChanged), this,
        &MainWindow::onCalibrationOptionsChanged);
    connect(methodCombo_,
        qOverload<int>(&QComboBox::currentIndexChanged), this,
        &MainWindow::onCalibrationOptionsChanged);
    connect(markerSizeSpin_,
        qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        &MainWindow::onCalibrationOptionsChanged);
    connect(dictionaryCombo_,
        qOverload<int>(&QComboBox::currentIndexChanged), this,
        &MainWindow::onCalibrationOptionsChanged);
    connect(skewCheck_, &QCheckBox::toggled, this,
        &MainWindow::onCalibrationOptionsChanged);
    connect(tangentialCheck_, &QCheckBox::toggled, this,
        &MainWindow::onCalibrationOptionsChanged);
    connect(radialCoeffSpin_,
        qOverload<int>(&QSpinBox::valueChanged), this,
        &MainWindow::onCalibrationOptionsChanged);
    connect(squareSizeSpin_,
        qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this](double squareSize)
        {
            markerSizeSpin_->setMaximum(
                qMax(0.001, squareSize - 0.001));
            onCalibrationOptionsChanged();
        });
    onCameraModelChanged();
    onBoardTypeChanged();

    ui_->mainSplitter->setStretchFactor(0, 0);
    ui_->mainSplitter->setStretchFactor(1, 1);
    ui_->mainSplitter->setStretchFactor(2, 0);
    ui_->mainSplitter->setSizes({220, 840, 220});
}

void MainWindow::buildStatusBar()
{
    statusImageCount_ = new QLabel(tr("0 张图片"), this);
    statusRmsError_ = new QLabel(tr("RMS: --"), this);
    statusBar()->addWidget(statusImageCount_);
    statusBar()->addPermanentWidget(statusRmsError_);
    updateUiState();
}

void MainWindow::updateUiState()
{
    const int imageCount = imageList_ == nullptr ? 0 : imageList_->count();
    const bool hasImages = imageCount > 0;
    const bool hasCalibration = lastResult_.success;
    const bool busy = isBusy();

    if (statusImageCount_ != nullptr)
    {
        statusImageCount_->setText(tr("%1 张图片").arg(imageCount));
    }
    ui_->imageListTitle->setText(
        QStringLiteral("Images (%1)").arg(imageCount));
    ui_->imageListStack->setCurrentIndex(hasImages ? 1 : 0);

    if (currentPixmap_.isNull())
    {
        imageDisplay_->clear();
        imageDisplay_->setText(
            hasImages ? tr("选择左侧图片查看预览") : tr("添加图片或文件夹以开始标定"));
    }

    calibrateAction_->setEnabled(imageCount >= 3 && !busy);
    compareAction_->setEnabled(imageCount >= 3 && !busy);
    calibrateAction_->setToolTip(
        busy ? tr("标定正在进行") : imageCount >= 3 ? tr("开始相机标定") : tr("至少添加 3 张图片后开始标定"));
    clearAction_->setEnabled(hasImages && !busy);
    saveProjectAction_->setEnabled(hasImages && !busy);
    openProjectAction_->setEnabled(!busy);
    importAction_->setEnabled(!busy);
    exportAction_->setEnabled(hasCalibration && !busy);
    batchExportAction_->setEnabled(hasCalibration && hasImages && !busy);
    exportAction_->setToolTip(
        hasCalibration ? tr("导出相机参数") : tr("完成一次成功标定后才能导出"));
    poseAction_->setEnabled(
        hasCalibration && !lastResult_.poses.empty() && !busy);
    poseAction_->setToolTip(
        hasCalibration ? tr("查看位姿估计结果") : tr("完成一次成功标定后查看位姿"));
    showUndistortedCheck_->setEnabled(hasCalibration && !busy);
    ui_->rightPanel->setEnabled(!busy);
    imageList_->setEnabled(!busy);
}

bool MainWindow::isBusy() const
{
    return calibActive_ || auxiliaryActive_;
}

void MainWindow::invalidateCalibrationResult()
{
    lastResult_ = {};
    lastResultImported_ = false;
    showUndistortedCheck_->setChecked(false);
    if (statusRmsError_ != nullptr)
    {
        statusRmsError_->setText(tr("RMS: --"));
    }
    if (poseDialog_)
    {
        poseDialog_->close();
    }
    updateUiState();
}

void MainWindow::onAddImages()
{
    if (isBusy())
    {
        return;
    }
    const auto files = QFileDialog::getOpenFileNames(
        this, tr("选择图片"), lastDir_, supportedImageDialogFilter());
    if (files.isEmpty())
    {
        return;
    }
    lastDir_ = QFileInfo(files.first()).absolutePath();
    loadImages(files);
}

void MainWindow::onAddImagesFromFolder()
{
    if (isBusy())
    {
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择图片文件夹"), lastDir_);
    if (dir.isEmpty())
    {
        return;
    }
    lastDir_ = dir;

    QDir directory(dir);
    directory.setNameFilters(supportedImageNameFilters());
    directory.setFilter(QDir::Files | QDir::Readable);
    directory.setSorting(QDir::Name);
    const QFileInfoList entries = directory.entryInfoList();

    QStringList files;
    files.reserve(entries.size());
    for (const QFileInfo &info: entries)
    {
        files << info.absoluteFilePath();
    }

    if (files.isEmpty())
    {
        QMessageBox::information(this, tr("从文件夹添加"),
            tr("所选文件夹中未找到图片文件。"));
        return;
    }
    loadImages(files);
}

void MainWindow::onOpenProject()
{
    if (isBusy())
    {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开标定项目"), lastDir_,
        tr("标定项目 (*.calibration.json *.json)"));
    if (path.isEmpty())
    {
        return;
    }
    QString error;
    const auto project = CalibrationProjectIo::load(path, &error);
    if (!project.has_value())
    {
        QMessageBox::warning(this, tr("打开标定项目"),
            tr("打开失败：%1").arg(error));
        return;
    }
    imageList_->clear();
    currentPixmap_ = {};
    invalidateCalibrationResult();
    applyOptions(project->options);
    loadImages(project->imagePaths);
    lastDir_ = QFileInfo(path).absolutePath();
    statusBar()->showMessage(tr("已打开项目：%1").arg(path), 5000);
}

void MainWindow::onSaveProject()
{
    if (isBusy() || imageList_->count() == 0)
    {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("保存标定项目"),
        QDir(lastDir_).filePath(QStringLiteral("calibration.calibration.json")),
        tr("标定项目 (*.calibration.json *.json)"));
    if (path.isEmpty())
    {
        return;
    }
    QString error;
    if (!CalibrationProjectIo::save(
        path, CalibrationProject{currentOptions(), collectFiles()}, &error))
    {
        QMessageBox::warning(this, tr("保存标定项目"),
            tr("保存失败：%1").arg(error));
        return;
    }
    lastDir_ = QFileInfo(path).absolutePath();
    QMessageBox::information(this, tr("保存标定项目"),
        tr("项目已保存到：\n%1").arg(path));
}

void MainWindow::onImportParameters()
{
    if (isBusy())
    {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("导入相机参数"), lastDir_, tr("YAML 文件 (*.yaml *.yml)"));
    if (path.isEmpty())
    {
        return;
    }
    QString error;
    const auto result = calibrator_.importParameters(path, &error);
    if (!result.has_value())
    {
        QMessageBox::warning(this, tr("导入相机参数"),
            tr("导入失败：%1").arg(error));
        return;
    }
    applyOptions(result->options);
    lastResult_ = *result;
    lastResultImported_ = true;
    lastDir_ = QFileInfo(path).absolutePath();
    updateUiState();
    statusRmsError_->setText(
        tr("RMS: %1 px（导入）").arg(result->rmsError, 0, 'f', 3));

    QStringList mismatches;
    for (const QString &file: collectFiles())
    {
        QImageReader reader(file);
        if (reader.size().isValid() && reader.size() != result->imageSize)
        {
            mismatches << QFileInfo(file).fileName();
        }
    }
    if (!mismatches.isEmpty())
    {
        QMessageBox::warning(
            this, tr("分辨率不一致"),
            tr("已导入参数，但 %1 张图片与标定分辨率 %2×%3 不一致。"
                "这些图片不会应用去畸变。")
            .arg(mismatches.size())
            .arg(result->imageSize.width())
            .arg(result->imageSize.height()));
    }
    else
    {
        QMessageBox::information(this, tr("导入相机参数"), result->report);
    }
    onImageSelectionChanged();
}

void MainWindow::onCalibrate()
{
    startCalibration();
}

void MainWindow::onCompareAlgorithms()
{
    if (isBusy())
    {
        return;
    }
    const QStringList files = collectFiles();
    if (files.size() < 3)
    {
        QMessageBox::warning(this, tr("算法对比"), tr("至少需要 3 张图片。"));
        return;
    }

    // 固定标定板配置，只展开相机模型和适用于当前板型的检测器组合
    const CalibrationOptions base = currentOptions();
    std::vector<CalibrationOptions> candidates;
    for (CameraModel model: {CameraModel::Pinhole, CameraModel::Fisheye})
    {
        CalibrationOptions options = base;
        options.cameraModel = model;
        options.radialCoeffs = model == CameraModel::Fisheye ? 4 : 3;
        if (base.boardType == CalibrationBoardType::Chessboard)
        {
            options.method = CalibrationMethod::Classic;
            candidates.push_back(options);
            options.method = CalibrationMethod::SectorBased;
            candidates.push_back(options);
        }
        else
        {
            candidates.push_back(options);
        }
    }

    auxiliaryProgressDialog_ = new QProgressDialog(
        tr("正在比较标定算法..."), tr("取消"), 0,
        static_cast<int>(candidates.size()), this);
    auxiliaryProgressDialog_->setWindowModality(Qt::WindowModal);
    auxiliaryProgressDialog_->setMinimumDuration(0);
    auxiliaryProgressDialog_->setValue(0);
    auxiliaryCanceled_.store(false);
    auxiliaryActive_ = true;
    updateUiState();
    connect(auxiliaryProgressDialog_, &QProgressDialog::canceled, this,
        [this] { auxiliaryCanceled_.store(true); });

    // watcher 留在 GUI 线程，完成回调集中恢复界面并释放进度窗口
    comparisonWatcher_ =
            new QFutureWatcher<std::vector<CalibrationResult>>(this);
    connect(comparisonWatcher_,
        &QFutureWatcher<std::vector<CalibrationResult>>::finished,
        this, [this]
        {
            const auto results = comparisonWatcher_->result();
            comparisonWatcher_->deleteLater();
            comparisonWatcher_ = nullptr;
            auxiliaryActive_ = false;
            if (auxiliaryProgressDialog_)
            {
                auxiliaryProgressDialog_->close();
                auxiliaryProgressDialog_->deleteLater();
                auxiliaryProgressDialog_ = nullptr;
            }
            updateUiState();
            if (results.empty())
            {
                statusBar()->showMessage(tr("算法对比已取消"), 5000);
                return;
            }
            auto *dialog = new AlgorithmComparisonDialog(results, this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        });
    Calibrator *worker = &calibrator_;

    // 候选项串行求解，取消请求可在每个候选边界立即停止后续工作
    comparisonWatcher_->setFuture(QtConcurrent::run(
        [this, worker, files, candidates]
        {
            std::vector<CalibrationResult> results;
            results.reserve(candidates.size());
            for (size_t index = 0; index < candidates.size(); ++index)
            {
                if (auxiliaryCanceled_.load())
                {
                    break;
                }
                CalibrationResult result = worker->calibrate(
                    files, candidates[index], {},
                    [this] { return auxiliaryCanceled_.load(); });
                if (auxiliaryCanceled_.load())
                {
                    break;
                }
                results.push_back(std::move(result));
                QMetaObject::invokeMethod(
                    this, [this, index, total = candidates.size()]
                    {
                        if (auxiliaryProgressDialog_)
                        {
                            auxiliaryProgressDialog_->setLabelText(
                                tr("已完成 %1 / %2 个候选算法")
                                .arg(index + 1)
                                .arg(total));

                            // 到达最大值可能触发嵌套事件并完成任务，因此此调用必须位于最后
                            auxiliaryProgressDialog_->setValue(
                                static_cast<int>(index + 1));
                        }
                    }, Qt::QueuedConnection);
            }
            return results;
        }));
}

void MainWindow::startCalibration(
    std::optional<CalibrationComparison> comparison)
{
    if (isBusy())
    {
        // 单一 busy 状态保护 watcher 和进度窗口指针不被新任务覆盖
        return;
    }

    const QStringList files = collectFiles();
    if (files.size() < 3)
    {
        QMessageBox::warning(this, tr("标定"),
            tr("至少需要 3 张图片。"));
        return;
    }

    const CalibrationOptions opts = currentOptions();
    invalidateCalibrationResult();

    // 角点检测和非线性求解放入工作线程，进度窗口只在 GUI 线程更新
    progressDialog_ = new QProgressDialog(tr("正在检测角点..."),
        tr("取消"), 0, files.size(), this);
    progressDialog_->setWindowModality(Qt::WindowModal);
    progressDialog_->setMinimumDuration(0);
    progressDialog_->setAutoClose(false);
    progressDialog_->setAutoReset(false);
    progressDialog_->setValue(0);

    calibCanceled_.store(false);
    calibActive_ = true;
    updateUiState();
    connect(progressDialog_, &QProgressDialog::canceled, this,
        [this] { calibCanceled_.store(true); });

    // Calibrator 在工作线程触发此回调，所有 QWidget 操作必须排队回到 GUI 线程
    auto progressCb = [this](int processed, int total, bool detected)
    {
        QMetaObject::invokeMethod(
            this,
            [this, processed, total, detected]
            {
                if (progressDialog_)
                {
                    progressDialog_->setMaximum(total);
                    progressDialog_->setLabelText(
                        processed >= total ? tr("正在求解标定...") : tr("正在检测角点..."));

                    // setValue 可能让 finished 回调删除进度窗口，之后不得再解引用该指针
                    progressDialog_->setValue(processed);
                }
                const int row = processed - 1;
                if (row >= 0 && row < imageList_->count())
                {
                    if (auto *item = imageList_->item(row))
                    {
                        item->setForeground(detected ? Qt::green : Qt::red);
                    }
                }
            },
            Qt::QueuedConnection);
    };
    auto cancelCb = [this] { return calibCanceled_.load(); };

    calibWatcher_ = new QFutureWatcher<CalibrationResult>(this);
    connect(calibWatcher_, &QFutureWatcher<CalibrationResult>::finished, this,
        [this, files, opts, comparison]
        {
            CalibrationResult result = calibWatcher_->result();
            calibWatcher_->deleteLater();
            calibWatcher_ = nullptr;
            calibActive_ = false;
            if (progressDialog_)
            {
                progressDialog_->close();
                progressDialog_->deleteLater();
                progressDialog_ = nullptr;
            }

            if (!sameCalibrationOptions(opts, currentOptions())
                || files != collectFiles())
            {
                // 输入快照已过期时拒绝发布结果，避免导出与当前项目不匹配的参数
                result = {};
                result.report = tr(
                    "标定期间参数或图片列表发生了变化，结果已丢弃。请重新标定。");
            }
            if (comparison.has_value())
            {
                if (result.success)
                {
                    const double rmsChange =
                            result.rmsError - comparison->baselineRms;
                    result.report += tr(
                                "\n\n排除 %1 张图片后重新标定：RMS %2 → %3 px（%4 %5 px）。")
                            .arg(comparison->excludedImages)
                            .arg(comparison->baselineRms,
                                0, 'f', 3)
                            .arg(result.rmsError, 0, 'f', 3)
                            .arg(rmsChange <= 0.0 ? tr("下降") : tr("上升"))
                            .arg(std::abs(rmsChange),
                                0, 'f', 3);
                }
                else
                {
                    result.report += tr(
                                "\n\n已排除 %1 张图片，但重新标定未成功，无法比较 RMS。")
                            .arg(comparison->excludedImages);
                }
            }
            presentResult(result);
        });

    // calibrate 不读写预览缓存，可与 GUI 线程中的预览操作分离执行
    Calibrator *worker = &calibrator_;
    calibWatcher_->setFuture(QtConcurrent::run(
        [worker, files, opts, progressCb, cancelCb]()
        {
            return worker->calibrate(files, opts, progressCb, cancelCb);
        }));
}

void MainWindow::presentResult(const CalibrationResult &result)
{
    lastResult_ = result;
    lastResultImported_ = false;
    updateUiState();
    if (result.success)
    {
        statusRmsError_->setText(
            tr("RMS: %1 px").arg(result.rmsError, 0, 'f', 3));
        if (result.qualityWarning)
        {
            QMessageBox::warning(this, tr("标定结果质量警告"),
                result.report);
        }
        else
        {
            QMessageBox::information(this, tr("标定结果"),
                result.report);
        }
    }
    else
    {
        statusRmsError_->setText(tr("RMS: --"));
        QMessageBox::warning(this, tr("标定"), result.report);
    }
    onImageSelectionChanged();
}

void MainWindow::onShowPoses()
{
    if (!lastResult_.success || lastResult_.poses.empty())
    {
        QMessageBox::warning(this, tr("位姿结果"),
            tr("请先完成一次成功的相机标定。"));
        return;
    }

    if (poseDialog_)
    {
        poseDialog_->raise();
        poseDialog_->activateWindow();
        return;
    }

    auto *dialog = new PoseResultDialog(lastResult_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &PoseResultDialog::excludeImagesRequested,
        this, &MainWindow::onExcludeImagesAndRecalibrate);
    connect(dialog, &QObject::destroyed,
        this, [this] { poseDialog_ = nullptr; });
    poseDialog_ = dialog;
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::onExcludeImagesAndRecalibrate(
    const QStringList &imagePaths, double baselineRms)
{
    if (isBusy() || !lastResult_.success || imagePaths.isEmpty())
    {
        return;
    }

    const QSet<QString> excludedPaths(imagePaths.begin(), imagePaths.end());
    int matchedCount = 0;
    for (int row = 0; row < imageList_->count(); ++row)
    {
        const QListWidgetItem *item = imageList_->item(row);
        if (item != nullptr
            && excludedPaths.contains(
                item->data(kRoleFilePath).toString()))
        {
            ++matchedCount;
        }
    }
    if (matchedCount == 0)
    {
        return;
    }
    if (imageList_->count() - matchedCount < 3)
    {
        QMessageBox::warning(
            this, tr("排除图片"),
            tr("排除后少于 3 张图片，无法重新标定。"));
        return;
    }

    // 从尾部移除项目，避免删除动作改变尚未访问的行号
    for (int row = imageList_->count() - 1; row >= 0; --row)
    {
        const QListWidgetItem *item = imageList_->item(row);
        if (item != nullptr
            && excludedPaths.contains(
                item->data(kRoleFilePath).toString()))
        {
            std::unique_ptr<QListWidgetItem> removedItem(
                imageList_->takeItem(row));
        }
    }
    currentPixmap_ = {};
    imageDisplay_->clear();
    statusBar()->showMessage(
        tr("已排除 %1 张图片，正在重新标定").arg(matchedCount),
        5000);
    startCalibration(CalibrationComparison{baselineRms, matchedCount});
}

void MainWindow::onExportParameters()
{
    if (!lastResult_.success)
    {
        QMessageBox::warning(this, tr("导出相机参数"),
            tr("请先完成一次成功的相机标定。"));
        return;
    }

    const QString defaultName =
            QDir(lastResult_.sourceDirectory.isEmpty() ?
                QStandardPaths::writableLocation(
                    QStandardPaths::HomeLocation) :
                lastResult_.sourceDirectory)
            .filePath(QStringLiteral("camera_parameters.yaml"));
    const QString outputPath = QFileDialog::getSaveFileName(
        this, tr("导出相机参数"), defaultName,
        tr("YAML 文件 (*.yaml *.yml)"));
    if (outputPath.isEmpty())
    {
        return;
    }

    QString error;
    if (!calibrator_.exportParameters(outputPath, lastResult_, &error))
    {
        QMessageBox::warning(this, tr("导出相机参数"),
            tr("导出失败：%1").arg(error));
        return;
    }

    QMessageBox::information(
        this, tr("导出相机参数"),
        tr("相机参数已保存到：\n%1").arg(outputPath));
}

void MainWindow::onExportUndistortedImages()
{
    if (isBusy() || !lastResult_.success)
    {
        return;
    }
    const QString outputDirectory = QFileDialog::getExistingDirectory(
        this, tr("选择去畸变图片输出文件夹"), lastDir_);
    if (outputDirectory.isEmpty())
    {
        return;
    }
    const QStringList files = collectFiles();
    if (files.isEmpty())
    {
        return;
    }

    auxiliaryProgressDialog_ = new QProgressDialog(
        tr("正在批量导出去畸变图片..."), tr("取消"), 0, files.size(), this);
    auxiliaryProgressDialog_->setWindowModality(Qt::WindowModal);
    auxiliaryProgressDialog_->setMinimumDuration(0);
    auxiliaryProgressDialog_->setValue(0);
    auxiliaryCanceled_.store(false);
    auxiliaryActive_ = true;
    updateUiState();
    connect(auxiliaryProgressDialog_, &QProgressDialog::canceled, this,
        [this] { auxiliaryCanceled_.store(true); });

    // 后台批处理使用启动时的参数副本，不受后续窗口状态影响
    const CalibrationResult calibration = lastResult_;

    batchWatcher_ = new QFutureWatcher<BatchUndistortSummary>(this);
    connect(batchWatcher_, &QFutureWatcher<BatchUndistortSummary>::finished,
        this, [this, outputDirectory]
        {
            const BatchUndistortSummary summary = batchWatcher_->result();
            batchWatcher_->deleteLater();
            batchWatcher_ = nullptr;
            auxiliaryActive_ = false;
            if (auxiliaryProgressDialog_)
            {
                auxiliaryProgressDialog_->close();
                auxiliaryProgressDialog_->deleteLater();
                auxiliaryProgressDialog_ = nullptr;
            }
            updateUiState();
            QString message = auxiliaryCanceled_.load() ? tr("批量导出已取消。\n") : QString();
            message += tr("已导出 %1 张，跳过 %2 张。\n输出目录：%3")
                    .arg(summary.written)
                    .arg(summary.skipped)
                    .arg(outputDirectory);
            if (!summary.errors.isEmpty())
            {
                message += tr("\n\n前几项错误：\n%1")
                        .arg(summary.errors.mid(0, 5).join('\n'));
            }
            if (summary.skipped > 0)
            {
                QMessageBox::warning(this, tr("批量导出去畸变图片"), message);
            }
            else
            {
                QMessageBox::information(this, tr("批量导出去畸变图片"), message);
            }
        });
    Calibrator *worker = &calibrator_;
    batchWatcher_->setFuture(QtConcurrent::run(
        [this, worker, files, outputDirectory, calibration]
        {
            BatchUndistortSummary summary;
            QSet<QString> outputNames;
            for (int index = 0; index < files.size(); ++index)
            {
                if (auxiliaryCanceled_.load())
                {
                    break;
                }
                const QFileInfo inputInfo(files[index]);
                QString base = inputInfo.completeBaseName();
                QString suffix = inputInfo.suffix().toLower();
                if (suffix.isEmpty())
                {
                    suffix = QStringLiteral("png");
                }

                // 不同输入目录可能出现同名文件，输出名需在本批任务内保持唯一
                QString outputName = base + QStringLiteral("_undistorted.") + suffix;
                int duplicateIndex = 2;
                while (outputNames.contains(outputName))
                {
                    outputName = base + QStringLiteral("_undistorted_%1.")
                            .arg(duplicateIndex++)
                            + suffix;
                }
                outputNames.insert(outputName);
                QString error;
                if (worker->undistortImageFile(
                    files[index], QDir(outputDirectory).filePath(outputName),
                    calibration, &error))
                {
                    ++summary.written;
                }
                else
                {
                    ++summary.skipped;
                    summary.errors << tr("%1：%2")
                            .arg(inputInfo.fileName(), error);
                }
                QMetaObject::invokeMethod(
                    this, [this, value = index + 1, total = files.size()]
                    {
                        if (auxiliaryProgressDialog_)
                        {
                            auxiliaryProgressDialog_->setLabelText(
                                tr("正在批量导出... %1 / %2")
                                .arg(value)
                                .arg(total));

                            // 完成值会驱动模态事件处理，保持为本分支最后一次窗口访问
                            auxiliaryProgressDialog_->setValue(value);
                        }
                    }, Qt::QueuedConnection);
            }
            return summary;
        }));
}

void MainWindow::onClearAll()
{
    if (isBusy())
    {
        return;
    }
    imageList_->clear();
    currentPixmap_ = {};
    imageDisplay_->clear();
    invalidateCalibrationResult();
}

void MainWindow::onImageSelectionChanged()
{
    const auto items = imageList_->selectedItems();
    if (items.isEmpty())
    {
        currentPixmap_ = {};
        updateUiState();
        return;
    }
    auto *item = items.first();
    bool found = false;
    showImage(item->data(kRoleFilePath).toString(), &found);
    item->setForeground(found ? Qt::green : Qt::red);
}

void MainWindow::onCameraModelChanged()
{
    const bool isFisheye =
            cameraModelCombo_->currentData().toInt()
            == static_cast<int>(CameraModel::Fisheye);
    skewCheck_->setVisible(isFisheye);
    tangentialCheck_->setVisible(!isFisheye);
    radialCoeffSpin_->setRange(2, isFisheye ? 4 : 3);
    radialCoeffSpin_->setValue(isFisheye ? 4 : 3);
    onCalibrationOptionsChanged();
}

void MainWindow::onBoardTypeChanged()
{
    const bool isCharuco =
            boardTypeCombo_->currentData().toInt()
            == static_cast<int>(CalibrationBoardType::Charuco);
    ui_->markerSizeLabel->setVisible(isCharuco);
    markerSizeSpin_->setVisible(isCharuco);
    ui_->dictionaryLabel->setVisible(isCharuco);
    dictionaryCombo_->setVisible(isCharuco);
    ui_->methodLabel->setVisible(!isCharuco);
    methodCombo_->setVisible(!isCharuco);
    onCalibrationOptionsChanged();
}

void MainWindow::onCalibrationOptionsChanged()
{
    invalidateCalibrationResult();
    onDisplayModeChanged();
}

void MainWindow::onDisplayModeChanged()
{
    debounceTimer_->start();
}

void MainWindow::loadImages(const QStringList &files)
{
    // 路径级去重在解码前完成，避免重复读取同一文件
    QSet<QString> loaded;
    loaded.reserve(imageList_->count() + files.size());
    for (int i = 0; i < imageList_->count(); ++i)
    {
        if (const QListWidgetItem *item = imageList_->item(i))
        {
            loaded.insert(item->data(kRoleFilePath).toString());
        }
    }

    int added = 0;
    int duplicates = 0;
    int failures = 0;
    int qualityWarnings = 0;

    // 感知哈希用于发现路径不同但画面近似的输入，仅生成提示而不自动排除
    QSet<quint64> loadedHashes;
    for (int i = 0; i < imageList_->count(); ++i)
    {
        if (const QListWidgetItem *item = imageList_->item(i))
        {
            loadedHashes.insert(item->data(kRoleImageHash).toULongLong());
        }
    }
    for (const QString &file: files)
    {
        if (loaded.contains(file))
        {
            ++duplicates;
            continue;
        }
        loaded.insert(file);

        QImageReader reader(file);
        reader.setAutoTransform(true);
        const QSize decodedSize = reader.size().scaled(
            kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio);
        if (decodedSize.isValid())
        {
            reader.setScaledSize(decodedSize);
        }
        const QImage decoded = reader.read();
        if (decoded.isNull())
        {
            ++failures;
            continue;
        }

        const QImage thumb = decoded.scaled(
            kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        auto *item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(thumb)), QFileInfo(file).fileName());
        item->setData(kRoleFilePath, file);
        const ImageQualityResult quality = analyzeImageQuality(file);
        QStringList warnings = quality.warnings;

        // 64 位哈希的汉明距离不超过 3 时视为高度相似
        const bool similarImage = std::any_of(
            loadedHashes.cbegin(), loadedHashes.cend(),
            [&quality](quint64 existingHash)
            {
                return std::bitset < 64 > (
                            existingHash ^ quality.similarityHash)
                        .count()
                        <= 3;
            });
        if (similarImage)
        {
            warnings << tr("与已加载图片画面高度相似");
        }
        loadedHashes.insert(quality.similarityHash);
        item->setData(kRoleImageHash,
            QVariant::fromValue<qulonglong>(quality.similarityHash));
        item->setData(kRoleQualityWarnings, warnings);
        QString toolTip = file;
        if (!warnings.isEmpty())
        {
            ++qualityWarnings;
            item->setBackground(QColor(255, 244, 204));
            item->setText(item->text() + QStringLiteral(" ⚠"));
            toolTip += tr("\n质量预检：%1\n清晰度：%2，平均亮度：%3")
                    .arg(warnings.join(QStringLiteral("；")))
                    .arg(quality.sharpness, 0, 'f', 1)
                    .arg(quality.meanBrightness, 0, 'f', 1);
        }
        item->setToolTip(toolTip);
        imageList_->addItem(item);
        ++added;
    }

    if (failures > 0)
    {
        statusBar()->showMessage(
            tr("%1 张图片加载失败").arg(failures), 5000);
    }

    if (added > 0)
    {
        if (lastResultImported_)
        {
            updateUiState();
        }
        else
        {
            invalidateCalibrationResult();
        }
        if (qualityWarnings > 0)
        {
            statusBar()->showMessage(
                tr("已添加 %1 张图片，其中 %2 张有质量提示；鼠标悬停查看详情。")
                .arg(added)
                .arg(qualityWarnings),
                8000);
        }
        else if (duplicates > 0)
        {
            statusBar()->showMessage(
                tr("已忽略 %1 个重复路径").arg(duplicates), 5000);
        }
    }
    else
    {
        updateUiState();
    }
}

void MainWindow::showImage(const QString &filePath, bool *found)
{
    bool detected = false;
    bool applyUndistortion = showUndistortedCheck_->isChecked();
    if (applyUndistortion && lastResult_.success
        && lastResult_.imageSize.isValid())
    {
        QImageReader reader(filePath);
        if (reader.size().isValid() && reader.size() != lastResult_.imageSize)
        {
            applyUndistortion = false;
            statusBar()->showMessage(
                tr("当前图片为 %1×%2，标定参数适用于 %3×%4；未应用去畸变。")
                .arg(reader.size().width())
                .arg(reader.size().height())
                .arg(lastResult_.imageSize.width())
                .arg(lastResult_.imageSize.height()),
                8000);
        }
    }
    const QImage annotated = calibrator_.previewImage(
        filePath, currentOptions(), lastResult_,
        applyUndistortion, &detected);
    currentPixmap_ = QPixmap::fromImage(annotated);
    fitImageToView();

    if (found)
    {
        *found = detected;
    }
}

void MainWindow::applyOptions(const CalibrationOptions &options)
{
    setComboValue(cameraModelCombo_, static_cast<int>(options.cameraModel));
    setComboValue(boardTypeCombo_, static_cast<int>(options.boardType));
    boardColsSpin_->setValue(options.boardSize.width());
    boardRowsSpin_->setValue(options.boardSize.height());
    squareSizeSpin_->setValue(options.squareSize);
    markerSizeSpin_->setMaximum(qMax(0.001, options.squareSize - 0.001));
    markerSizeSpin_->setValue(options.markerSize);
    setComboValue(dictionaryCombo_, static_cast<int>(options.dictionary));
    setComboValue(methodCombo_, static_cast<int>(options.method));
    skewCheck_->setChecked(options.skew);
    tangentialCheck_->setChecked(options.tangential);
    radialCoeffSpin_->setValue(options.radialCoeffs);
}

CalibrationOptions MainWindow::currentOptions() const
{
    CalibrationOptions opts;
    opts.cameraModel = static_cast<CameraModel>(
        cameraModelCombo_->currentData().toInt());
    opts.boardType = static_cast<CalibrationBoardType>(
        boardTypeCombo_->currentData().toInt());
    opts.boardSize = {boardColsSpin_->value(), boardRowsSpin_->value()};
    opts.squareSize = squareSizeSpin_->value();
    opts.markerSize = markerSizeSpin_->value();
    opts.dictionary = static_cast<ArucoDictionary>(
        dictionaryCombo_->currentData().toInt());
    opts.method = static_cast<CalibrationMethod>(
        methodCombo_->currentData().toInt());
    opts.skew = skewCheck_->isChecked();
    opts.tangential = tangentialCheck_->isChecked();
    opts.radialCoeffs = radialCoeffSpin_->value();
    return opts;
}

QStringList MainWindow::collectFiles() const
{
    QStringList files;
    files.reserve(imageList_->count());
    for (int i = 0; i < imageList_->count(); ++i)
    {
        if (const QListWidgetItem *item = imageList_->item(i))
        {
            files << item->data(kRoleFilePath).toString();
        }
    }
    return files;
}

void MainWindow::fitImageToView()
{
    if (currentPixmap_.isNull())
    {
        imageDisplay_->clear();
        const bool hasImages =
                imageList_ != nullptr && imageList_->count() > 0;
        imageDisplay_->setText(
            hasImages ? tr("选择左侧图片查看预览") : tr("添加图片或文件夹以开始标定"));
        return;
    }
    const QSize area =
            (imageScroll_->viewport()->size() - QSize(20, 20)).expandedTo({1, 1});
    imageDisplay_->setPixmap(
        currentPixmap_.scaled(area, Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    fitImageToView();
}
