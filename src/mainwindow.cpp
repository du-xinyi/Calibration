#include "mainwindow.h"
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
#include <QToolBar>
#include <QWidget>

#include <QtConcurrent>

namespace {

constexpr int kThumbnailSize = 96;
constexpr int kRoleFilePath = Qt::UserRole + 1;

QIcon themedIcon(QStyle* style, QStyle::StandardPixmap fallback,
                 const QStringList& names)
{
    for (const QString& name : names) {
        const QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull()) {
            return icon;
        }
    }
    return style->standardIcon(fallback);
}

/// 返回 Qt 可读取的全部图片扩展名通配符（如 "*.png *.jpg"），用于目录扫描。
QStringList supportedImageNameFilters()
{
    const auto formats = QImageReader::supportedImageFormats();
    QStringList filters;
    filters.reserve(formats.size());
    for (const QByteArray& fmt : formats) {
        filters << QStringLiteral("*.") + QString::fromLatin1(fmt);
    }
    return filters;
}

/// 返回文件对话框使用的过滤器字符串。
QString supportedImageDialogFilter()
{
    const auto formats = QImageReader::supportedImageFormats();
    QStringList exts;
    exts.reserve(formats.size());
    for (const QByteArray& fmt : formats) {
        exts << QStringLiteral("*.") + QString::fromLatin1(fmt);
    }
    return QStringLiteral("图片 (%1)").arg(exts.join(QLatin1Char(' ')));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      ui_(std::make_unique<Ui::MainWindow>())
{
    ui_->setupUi(this);

#ifdef PROJECT_SOURCE_DIR
    // 默认打开项目所在文件夹（由 CMake 注入源码路径）。
    lastDir_ = QString::fromUtf8(PROJECT_SOURCE_DIR);
#else
    const QString pictures =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    lastDir_ = pictures.isEmpty()
                   ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                   : pictures;
#endif

    buildMenuBar();
    buildToolBar();
    configureCentralWidget();
    buildStatusBar();
}

MainWindow::~MainWindow()
{
    // 标定在后台线程运行时若主窗口被销毁，worker 仍会读取 calibCanceled_ 等
    // 已释放成员，造成 use-after-free / SIGSEGV。先取消并等待其结束。
    if (calibWatcher_ != nullptr && calibWatcher_->isRunning()) {
        calibCanceled_.store(true);
        calibWatcher_->waitForFinished();
    }
}

void MainWindow::buildMenuBar()
{
    auto* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("添加图片(&A)..."), QKeySequence::Open, this,
                        &MainWindow::onAddImages);
    fileMenu->addAction(tr("从文件夹添加(&F)..."),
                        QKeySequence("Ctrl+Shift+O"), this,
                        &MainWindow::onAddImagesFromFolder);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出(&X)"), QKeySequence::Quit, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(tr("适应窗口"), QKeySequence("Ctrl+0"), this,
                        &MainWindow::fitImageToView);

    menuBar()->addMenu(tr("帮助(&H)"))->addAction(tr("关于(&A)"), this, [this] {
        QMessageBox::about(this, tr("关于"),
                           tr("相机标定\n基于 Qt 的相机标定工具，"
                              "布局参考 MATLAB Camera Calibrator。"));
    });
}

void MainWindow::buildToolBar()
{
    auto* bar = addToolBar(tr("主工具栏"));
    bar->setMovable(false);
    bar->setIconSize({24, 24});
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* addImagesAction = bar->addAction(
        themedIcon(style(), QStyle::SP_FileIcon,
                   {QStringLiteral("insert-image-symbolic"),
                    QStringLiteral("insert-image"),
                    QStringLiteral("image-x-generic")}),
        tr("图片"), this, &MainWindow::onAddImages);
    addImagesAction->setToolTip(tr("添加图片"));
    auto* addFolderAction = bar->addAction(
        themedIcon(style(), QStyle::SP_DirOpenIcon,
                   {QStringLiteral("folder-open-symbolic"),
                    QStringLiteral("folder-open")}),
        tr("文件夹"), this, &MainWindow::onAddImagesFromFolder);
    addFolderAction->setToolTip(tr("从文件夹添加图片"));
    calibrateAction_ = bar->addAction(
        themedIcon(style(), QStyle::SP_MediaPlay,
                   {QStringLiteral("camera-photo-symbolic"),
                    QStringLiteral("camera-photo")}),
        tr("标定"), this, &MainWindow::onCalibrate);
    calibrateAction_->setObjectName(QStringLiteral("calibrateAction"));
    calibrateAction_->setToolTip(tr("至少添加 3 张图片后开始标定"));
    bar->addSeparator();
    poseAction_ = bar->addAction(
        themedIcon(style(), QStyle::SP_BrowserReload,
                   {QStringLiteral("object-rotate-right-symbolic"),
                    QStringLiteral("object-rotate-right")}),
        tr("位姿"), this, &MainWindow::onShowPoses);
    poseAction_->setObjectName(QStringLiteral("poseResultAction"));
    poseAction_->setToolTip(tr("查看位姿估计结果"));
    exportAction_ = bar->addAction(
        themedIcon(style(), QStyle::SP_DialogSaveButton,
                   {QStringLiteral("document-save-as-symbolic"),
                    QStringLiteral("document-save-as")}),
        tr("导出"), this, &MainWindow::onExportParameters);
    exportAction_->setObjectName(QStringLiteral("exportAction"));
    exportAction_->setToolTip(tr("导出相机参数"));
    bar->addSeparator();
    clearAction_ = bar->addAction(
        themedIcon(style(), QStyle::SP_TrashIcon,
                   {QStringLiteral("edit-clear-symbolic"),
                    QStringLiteral("edit-clear")}),
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

    // 参数变化时刷新预览（重新检测角点）。
    connect(cameraModelCombo_,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onCameraModelChanged);
    connect(boardTypeCombo_,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onBoardTypeChanged);
    connect(boardColsSpin_,
            qOverload<int>(&QSpinBox::valueChanged), this,
            &MainWindow::onDisplayModeChanged);
    connect(boardRowsSpin_,
            qOverload<int>(&QSpinBox::valueChanged), this,
            &MainWindow::onDisplayModeChanged);
    connect(methodCombo_,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onDisplayModeChanged);
    connect(markerSizeSpin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onDisplayModeChanged);
    connect(dictionaryCombo_,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onDisplayModeChanged);
    connect(squareSizeSpin_,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double squareSize) {
                markerSizeSpin_->setMaximum(
                    qMax(0.001, squareSize - 0.001));
                onDisplayModeChanged();
            });
    onCameraModelChanged();
    onBoardTypeChanged();

    // 两侧面板保持稳定宽度，中间图像预览区占用剩余空间。
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
    const bool busy = calibActive_;

    if (statusImageCount_ != nullptr) {
        statusImageCount_->setText(tr("%1 张图片").arg(imageCount));
    }
    ui_->imageListTitle->setText(
        QStringLiteral("Images (%1)").arg(imageCount));
    ui_->imageListStack->setCurrentIndex(hasImages ? 1 : 0);

    if (currentPixmap_.isNull()) {
        imageDisplay_->clear();
        imageDisplay_->setText(
            hasImages ? tr("选择左侧图片查看预览")
                      : tr("添加图片或文件夹以开始标定"));
    }

    calibrateAction_->setEnabled(imageCount >= 3 && !busy);
    calibrateAction_->setToolTip(
        busy ? tr("标定正在进行")
             : imageCount >= 3 ? tr("开始相机标定")
                               : tr("至少添加 3 张图片后开始标定"));
    clearAction_->setEnabled(hasImages && !busy);
    exportAction_->setEnabled(hasCalibration && !busy);
    exportAction_->setToolTip(
        hasCalibration ? tr("导出相机参数")
                       : tr("完成一次成功标定后才能导出"));
    poseAction_->setEnabled(
        hasCalibration && !lastResult_.poses.empty() && !busy);
    poseAction_->setToolTip(
        hasCalibration ? tr("查看位姿估计结果")
                       : tr("完成一次成功标定后查看位姿"));
}

void MainWindow::onAddImages()
{
    const auto files = QFileDialog::getOpenFileNames(
        this, tr("选择图片"), lastDir_, supportedImageDialogFilter());
    if (files.isEmpty()) {
        return;
    }
    lastDir_ = QFileInfo(files.first()).absolutePath();
    loadImages(files);
}

void MainWindow::onAddImagesFromFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择图片文件夹"), lastDir_);
    if (dir.isEmpty()) {
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
    for (const QFileInfo& info : entries) {
        files << info.absoluteFilePath();
    }

    if (files.isEmpty()) {
        QMessageBox::information(this, tr("从文件夹添加"),
                                 tr("所选文件夹中未找到图片文件。"));
        return;
    }
    loadImages(files);
}

void MainWindow::onCalibrate()
{
    if (calibActive_) {
        return;  // 已有标定在运行，拒绝重入（否则旧 finished 会破坏新状态）
    }

    const QStringList files = collectFiles();
    if (files.size() < 3) {
        QMessageBox::warning(this, tr("标定"),
                             tr("至少需要 3 张图片。"));
        return;
    }

    const CalibrationOptions opts = currentOptions();

    // 模态进度对话框（带 取消）。标定在后台线程执行，避免阻塞 UI。
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

    // 进度回调运行在工作线程，通过 invokeMethod(QueuedConnection) 转回 GUI 线程。
    // processed 与 files 顺序一致（即列表行号），据此给列表项着色，便于诊断。
    auto progressCb = [this](int processed, int total, bool detected) {
        QMetaObject::invokeMethod(
            this,
            [this, processed, total, detected] {
                if (progressDialog_) {
                    progressDialog_->setMaximum(total);
                    progressDialog_->setLabelText(
                        processed >= total ? tr("正在求解标定...")
                                           : tr("正在检测角点..."));
                    // 模态 QProgressDialog::setValue() 可能处理嵌套事件；
                    // finished 回调会在其中关闭对话框并清空指针，因此必须最后调用。
                    progressDialog_->setValue(processed);
                }
                const int row = processed - 1;
                if (row >= 0 && row < imageList_->count()) {
                    if (auto* item = imageList_->item(row)) {
                        item->setForeground(detected ? Qt::green : Qt::red);
                    }
                }
            },
            Qt::QueuedConnection);
    };
    auto cancelCb = [this] { return calibCanceled_.load(); };

    calibWatcher_ = new QFutureWatcher<CalibrationResult>(this);
    connect(calibWatcher_, &QFutureWatcher<CalibrationResult>::finished, this,
            [this] {
                const CalibrationResult result = calibWatcher_->result();
                calibWatcher_->deleteLater();
                calibWatcher_ = nullptr;
                calibActive_ = false;
                if (progressDialog_) {
                    progressDialog_->close();
                    progressDialog_->deleteLater();
                    progressDialog_ = nullptr;
                }
                presentResult(result);
            });

    Calibrator* worker = &calibrator_;  // 方法无共享可变状态，可跨线程调用
    calibWatcher_->setFuture(QtConcurrent::run(
        [worker, files, opts, progressCb, cancelCb]() {
            return worker->calibrate(files, opts, progressCb, cancelCb);
        }));
}

void MainWindow::presentResult(const CalibrationResult& result)
{
    lastResult_ = result;
    updateUiState();
    if (result.success) {
        statusRmsError_->setText(
            tr("RMS: %1 px").arg(result.rmsError, 0, 'f', 3));
        QMessageBox::information(this, tr("标定结果"),
                                 result.report);
    } else {
        statusRmsError_->setText(tr("RMS: --"));
        QMessageBox::warning(this, tr("标定"), result.report);
    }
    onImageSelectionChanged();  // 刷新预览（角点标注 / 去畸变）
}

void MainWindow::onShowPoses()
{
    if (!lastResult_.success || lastResult_.poses.empty()) {
        QMessageBox::warning(this, tr("位姿结果"),
                             tr("请先完成一次成功的相机标定。"));
        return;
    }

    auto* dialog = new PoseResultDialog(lastResult_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::onExportParameters()
{
    if (!lastResult_.success) {
        QMessageBox::warning(this, tr("导出相机参数"),
                             tr("请先完成一次成功的相机标定。"));
        return;
    }
    if (lastResult_.sourceDirectory.isEmpty()) {
        QMessageBox::warning(this, tr("导出相机参数"),
                             tr("无法确定标定图片所在目录。"));
        return;
    }

    const QString outputPath =
        QDir(lastResult_.sourceDirectory)
            .filePath(QStringLiteral("camera_parameters.yaml"));
    QString error;
    if (!calibrator_.exportParameters(outputPath, lastResult_, &error)) {
        QMessageBox::warning(
            this, tr("导出相机参数"),
            tr("导出失败：%1").arg(error));
        return;
    }

    QMessageBox::information(
        this, tr("导出相机参数"),
        tr("相机参数已保存到：\n%1").arg(outputPath));
}

void MainWindow::onClearAll()
{
    imageList_->clear();
    currentPixmap_ = {};
    lastResult_ = {};
    imageDisplay_->clear();
    statusRmsError_->setText(tr("RMS: --"));
    updateUiState();
}

void MainWindow::onImageSelectionChanged()
{
    const auto items = imageList_->selectedItems();
    if (items.isEmpty()) {
        currentPixmap_ = {};
        updateUiState();
        return;
    }
    auto* item = items.first();
    bool found = false;
    showImage(item->data(kRoleFilePath).toString(), &found);
    item->setForeground(found ? Qt::green : Qt::red);
}

void MainWindow::onCameraModelChanged()
{
    const bool isFisheye =
        currentOptions().cameraModel == CameraModel::Fisheye;
    skewCheck_->setVisible(isFisheye);
    tangentialCheck_->setVisible(!isFisheye);
    radialCoeffSpin_->setRange(2, isFisheye ? 4 : 3);
    radialCoeffSpin_->setValue(isFisheye ? 4 : 3);
    onDisplayModeChanged();
}

void MainWindow::onBoardTypeChanged()
{
    const bool isCharuco =
        currentOptions().boardType == CalibrationBoardType::Charuco;
    ui_->markerSizeLabel->setVisible(isCharuco);
    markerSizeSpin_->setVisible(isCharuco);
    ui_->dictionaryLabel->setVisible(isCharuco);
    dictionaryCombo_->setVisible(isCharuco);
    ui_->methodLabel->setVisible(!isCharuco);
    methodCombo_->setVisible(!isCharuco);
    onDisplayModeChanged();
}

void MainWindow::onDisplayModeChanged()
{
    // 标定板参数或显示模式变化后，重新检测并绘制当前图片。
    onImageSelectionChanged();
}

void MainWindow::loadImages(const QStringList& files)
{
    // 收集已加载路径，避免重复添加同一文件（如多次添加同一文件夹）。
    QSet<QString> loaded;
    loaded.reserve(imageList_->count() + files.size());
    for (int i = 0; i < imageList_->count(); ++i) {
        if (const QListWidgetItem* item = imageList_->item(i)) {
            loaded.insert(item->data(kRoleFilePath).toString());
        }
    }

    for (const QString& file : files) {
        if (loaded.contains(file)) {
            continue;
        }
        loaded.insert(file);

        QImageReader reader(file);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            continue;
        }

        auto* item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(image)), QFileInfo(file).fileName(),
            imageList_);
        item->setData(kRoleFilePath, file);
        item->setToolTip(file);
        imageList_->addItem(item);
    }

    updateUiState();
}

void MainWindow::showImage(const QString& filePath, bool* found)
{
    bool detected = false;
    const QImage annotated = calibrator_.previewImage(
        filePath, currentOptions(), lastResult_,
        showUndistortedCheck_->isChecked(), &detected);
    currentPixmap_ = QPixmap::fromImage(annotated);
    fitImageToView();

    if (found) {
        *found = detected;
    }
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
    for (int i = 0; i < imageList_->count(); ++i) {
        if (const QListWidgetItem* item = imageList_->item(i)) {
            files << item->data(kRoleFilePath).toString();
        }
    }
    return files;
}

void MainWindow::fitImageToView()
{
    if (currentPixmap_.isNull()) {
        imageDisplay_->clear();
        const bool hasImages =
            imageList_ != nullptr && imageList_->count() > 0;
        imageDisplay_->setText(
            hasImages ? tr("选择左侧图片查看预览")
                      : tr("添加图片或文件夹以开始标定"));
        return;
    }
    const QSize area = imageScroll_->viewport()->size() - QSize(20, 20);
    imageDisplay_->setPixmap(
        currentPixmap_.scaled(area, Qt::KeepAspectRatio,
                              Qt::SmoothTransformation));
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    fitImageToView();
}
