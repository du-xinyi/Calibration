#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QComboBox>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

#include <cstdlib>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    if (argc != 2) {
        qCritical() << "用法: CalibrationComparisonLifecycleTest <图片目录>";
        return EXIT_FAILURE;
    }

    QDir imageDirectory(QString::fromLocal8Bit(argv[1]));
    QStringList files;
    for (const QString& name : imageDirectory.entryList(
             {QStringLiteral("*.jpg")}, QDir::Files, QDir::Name)) {
        files << imageDirectory.absoluteFilePath(name);
    }
    if (files.size() < 3) {
        qCritical() << "算法对比测试至少需要 3 张图片";
        return EXIT_FAILURE;
    }

    MainWindow window;
    window.loadImages(files);
    auto* boardTypeCombo = window.findChild<QComboBox*>(
        QStringLiteral("boardTypeCombo"));
    if (boardTypeCombo == nullptr) {
        qCritical() << "找不到标定板类型控件";
        return EXIT_FAILURE;
    }
    boardTypeCombo->setCurrentIndex(1);
    window.show();
    auto* compareAction = window.findChild<QAction*>(
        QStringLiteral("compareAlgorithmsAction"));
    if (compareAction == nullptr || !compareAction->isEnabled()) {
        qCritical() << "算法对比入口未启用";
        return EXIT_FAILURE;
    }

    QTimer monitor;
    monitor.setInterval(10);
    QObject::connect(&monitor, &QTimer::timeout, &app, [&] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName()
                    != QStringLiteral("AlgorithmComparisonDialog")
                || !widget->isVisible()) {
                continue;
            }
            auto* table = widget->findChild<QTableWidget*>(
                QStringLiteral("algorithmComparisonTable"));
            if (table == nullptr || table->rowCount() != 4) {
                qCritical() << "算法对比结果表内容错误";
                app.exit(EXIT_FAILURE);
                return;
            }
            monitor.stop();
            widget->close();
            window.close();
            QTimer::singleShot(0, &app,
                               [&app] { app.exit(EXIT_SUCCESS); });
            return;
        }
    });
    monitor.start();
    QTimer::singleShot(0, compareAction, &QAction::trigger);
    QTimer::singleShot(40000, &app, [&app] {
        qCritical() << "等待算法对比结果超时";
        app.exit(EXIT_FAILURE);
    });
    return app.exec();
}
