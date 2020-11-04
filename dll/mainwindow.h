#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QImage>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVector>
#include <QList>

#include "decoder.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    void paintEvent(QPaintEvent *event);
    void closeEvent(QCloseEvent *event);
    void changeEvent(QEvent *event);
    void keyReleaseEvent(QKeyEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void mouseDoubleClickEvent(QMouseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);

    void initUI();
    void initFFmpeg();
    void initSlot();
    void initTray();

    QString fileType(QString file);
    void addPathVideoToList(QString path);
    void playVideo(QString file);
    void playNext();
    void playPreview();
    void showPlayMenu();

    void setHide(QWidget *widget);
    void showControl(bool show);

    inline QString getFilenameFromPath(QString path);

    Ui::MainWindow *ui;

    Decoder *m_decoder;
    QList<QString> m_playList;    // 列表以在相同的路径stroe视频文件

    QString m_currentPlay;        // 当前播放的视频文件路径
    QString m_currentPlayType;

    QTimer *m_menuTimer;      // 菜单隐藏计时器
    QTimer *m_progressTimer;  // 检查播放进度计时器

    bool m_menuIsVisible;     // 切换到控制显示/隐藏菜单
    bool m_isKeepAspectRatio; // 切换控制图像缩放是否保持高宽比

    QImage m_image;

    bool m_autoPlay;          // 切换控制是否继续播放其他文件
    bool m_loopPlay;          // 切换以控制是否继续播放同一文件
    bool m_closeNotExit;      // 切换到控制，点击退出按钮不是退出，而是隐藏

    Decoder::PlayState m_playState;

    QVector<QWidget *> m_hideVector;

    qint64 m_timeTotal;

    int m_seekInterval;

private slots:
    void buttonClickSlot();
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void timerSlot();
    void editText();
    void seekProgress(int value);
    void videoTime(qint64 time);
    void playStateChanged(Decoder::PlayState state);

    /* right click menu slot */
    void setFullScreen();
    void setKeepRatio();
    void setAutoPlay();
    void setLoopPlay();
    void saveCurrentFrame();

    void showVideo(QImage);

signals:
    void selectedVideoFile(QString file, QString type);
    void stopVideo();
    void pauseVideo();

};

#endif // MAINWINDOW_H
