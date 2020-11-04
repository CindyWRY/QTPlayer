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
    QList<QString> m_playList;    // �б�������ͬ��·��stroe��Ƶ�ļ�

    QString m_currentPlay;        // ��ǰ���ŵ���Ƶ�ļ�·��
    QString m_currentPlayType;

    QTimer *m_menuTimer;      // �˵����ؼ�ʱ��
    QTimer *m_progressTimer;  // ��鲥�Ž��ȼ�ʱ��

    bool m_menuIsVisible;     // �л���������ʾ/���ز˵�
    bool m_isKeepAspectRatio; // �л�����ͼ�������Ƿ񱣳ָ߿��

    QImage m_image;

    bool m_autoPlay;          // �л������Ƿ�������������ļ�
    bool m_loopPlay;          // �л��Կ����Ƿ��������ͬһ�ļ�
    bool m_closeNotExit;      // �л������ƣ�����˳���ť�����˳�����������

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
