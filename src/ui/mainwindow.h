#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "calibration.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <memory>
#include <optional>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QProgressDialog;
class QScrollArea;
class QSpinBox;

class PoseResultDialog;

namespace Ui
{
    class MainWindow;
}

/**
 * @brief 协调图片管理、标定配置、后台求解和结果展示的应用主窗口
 */
class MainWindow: public QMainWindow
{
    Q_OBJECT

public:

    /**
     * @brief 创建主窗口并连接全部界面动作
     *
     * @param parent Qt 对象树中的父窗口，允许为空
     */
    explicit MainWindow(QWidget *parent = nullptr);

    /**
     * @brief 结束后台工作并销毁主窗口
     *
     * @details 销毁前请求正在运行的任务取消并等待结束，保证任务捕获的窗口状态仍然有效
     */
    ~MainWindow() override;

    /**
     * @brief 将一组图片加入当前标定项目
     *
     * @details 载入过程会过滤重复路径和不可解码文件，并为每张图片生成缩略图与质量预检结果
     *
     * @param files 待加入的图片路径
     */
    void loadImages(const QStringList &files);

protected:

    /**
     * @brief 在窗口尺寸变化时重新适配预览图
     *
     * @param event Qt 窗口缩放事件
     */
    void resizeEvent(QResizeEvent *event) override;

private slots:

    void onAddImages();

    void onAddImagesFromFolder();

    void onOpenProject();

    void onSaveProject();

    void onImportParameters();

    void onCalibrate();

    void onCompareAlgorithms();

    void onShowPoses();

    void onExportParameters();

    void onExportUndistortedImages();

    void onClearAll();

    void onImageSelectionChanged();

    void onCameraModelChanged();

    void onBoardTypeChanged();

    void onCalibrationOptionsChanged();

    void onDisplayModeChanged();

    void onExcludeImagesAndRecalibrate(const QStringList &imagePaths,
        double baselineRms);

private:

    void buildMenuBar();

    void buildToolBar();

    void configureCentralWidget();

    void buildStatusBar();

    void updateUiState();

    void invalidateCalibrationResult();

    /**
     * @brief 保存重新标定前的比较基准
     */
    struct CalibrationComparison
    {
        double baselineRms = 0.0; ///< 排除图片前的整体误差
        int excludedImages = 0; ///< 本轮重新标定移除的图片数
    };

    void startCalibration(
        std::optional<CalibrationComparison> comparison = std::nullopt);

    void showImage(const QString &filePath, bool *found = nullptr);

    void fitImageToView();

    void presentResult(const CalibrationResult &result);

    void applyOptions(const CalibrationOptions &options);

    bool isBusy() const;

    CalibrationOptions currentOptions() const;

    QStringList collectFiles() const;

    std::unique_ptr<Ui::MainWindow> ui_;

    // === 由 Qt 对象树管理的图片视图 ===
    QListWidget *imageList_ = nullptr;
    QScrollArea *imageScroll_ = nullptr;
    QLabel *imageDisplay_ = nullptr;

    // === 由 Qt 对象树管理的参数编辑控件 ===
    QComboBox *cameraModelCombo_ = nullptr;
    QComboBox *boardTypeCombo_ = nullptr;
    QSpinBox *boardColsSpin_ = nullptr;
    QSpinBox *boardRowsSpin_ = nullptr;
    QDoubleSpinBox *squareSizeSpin_ = nullptr;
    QDoubleSpinBox *markerSizeSpin_ = nullptr;
    QComboBox *dictionaryCombo_ = nullptr;
    QComboBox *methodCombo_ = nullptr;
    QCheckBox *skewCheck_ = nullptr;
    QCheckBox *tangentialCheck_ = nullptr;
    QSpinBox *radialCoeffSpin_ = nullptr;
    QCheckBox *showUndistortedCheck_ = nullptr;

    // === 由 Qt 对象树管理的状态组件和动作 ===
    QLabel *statusImageCount_ = nullptr;
    QLabel *statusRmsError_ = nullptr;
    QAction *calibrateAction_ = nullptr;
    QAction *compareAction_ = nullptr;
    QAction *poseAction_ = nullptr;
    QAction *exportAction_ = nullptr;
    QAction *batchExportAction_ = nullptr;
    QAction *openProjectAction_ = nullptr;
    QAction *saveProjectAction_ = nullptr;
    QAction *importAction_ = nullptr;
    QAction *clearAction_ = nullptr;

    QString lastDir_; ///< 文件对话框最近使用的目录
    QPixmap currentPixmap_; ///< 未按窗口缩放的当前预览图

    // === 当前标定状态 ===
    Calibrator calibrator_;
    CalibrationResult lastResult_; ///< 可供预览、位姿查看和导出的最近结果
    bool lastResultImported_ = false; ///< 最近结果是否来自参数文件

    // === 单次标定任务生命周期 ===
    QProgressDialog *progressDialog_ = nullptr; ///< 当前标定任务的进度窗口
    QFutureWatcher<CalibrationResult> *calibWatcher_ = nullptr; ///< 标定任务完成观察器
    std::atomic<bool> calibCanceled_{false}; ///< 工作线程读取的取消请求
    bool calibActive_ = false; ///< 防止同时启动第二次标定

    /**
     * @brief 批量去畸变任务的累计结果
     */
    struct BatchUndistortSummary
    {
        int written = 0; ///< 成功输出的文件数
        int skipped = 0; ///< 未能输出的文件数
        QStringList errors; ///< 各失败文件的诊断信息
    };

    // === 算法对比与批量去畸变任务生命周期 ===
    QFutureWatcher<std::vector<CalibrationResult>> *comparisonWatcher_ = nullptr; ///< 算法对比观察器
    QFutureWatcher<BatchUndistortSummary> *batchWatcher_ = nullptr; ///< 批量输出观察器
    QProgressDialog *auxiliaryProgressDialog_ = nullptr; ///< 两类辅助任务共用的进度窗口
    std::atomic<bool> auxiliaryCanceled_{false}; ///< 辅助工作线程读取的取消请求
    bool auxiliaryActive_ = false; ///< 是否有辅助任务正在占用界面

    QTimer *debounceTimer_ = nullptr; ///< 合并连续预览刷新请求的计时器
    QPointer<PoseResultDialog> poseDialog_; ///< 自动跟踪已打开的位姿窗口
};
