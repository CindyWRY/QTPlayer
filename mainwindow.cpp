#include <QFileDialog>
#include <QStandardPaths>
#include <QPainter>
#include <QCloseEvent>
#include <QEvent>
#include <QFileInfoList>
#include <QMenu>

#include <QDebug>

#include "mainwindow.h"
#include "ui_mainwindow.h"

extern "C"
{
#include "libavformat/avformat.h"
}

#define VOLUME_INT  (13)

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_decoder(new DecoderThread),
    m_menuTimer(new QTimer),
    m_progressTimer(new QTimer),
    m_menuIsVisible(true),
    m_isKeepAspectRatio(false),
    m_image(QImage(":/image/bac.jpg")),
    m_autoPlay(true),
    m_loopPlay(false),
    m_closeNotExit(false),
    m_playState(DecoderThread::STOP),
    m_seekInterval(15)
{
    ui->setupUi(this);

    qRegisterMetaType<DecoderThread::PlayState>("Decoder::PlayState");

    m_menuTimer->setInterval(8000);
    m_menuTimer->start(5000);

    m_progressTimer->setInterval(500);

    initUI();
    initTray();
    initSlot();
    initFFmpeg();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initUI()
{
    this->setWindowTitle("QtPlayer");
    this->setWindowIcon(QIcon(":/image/player.ico"));
    this->centralWidget()->setMouseTracking(true);
    this->setMouseTracking(true);


    ui->titleLable->setAlignment(Qt::AlignCenter);

    ui->labelTime->setStyleSheet("background: #5FFFFFFF;");
    ui->labelTime->setText(QString("00.00.00 / 00:00:00"));

    ui->btnNext->setIcon(QIcon(":/image/next.ico"));
    ui->btnNext->setIconSize(QSize(48, 48));
    ui->btnNext->setStyleSheet("background: transparent;border:none;");

    ui->btnPreview->setIcon(QIcon(":/image/forward.ico"));
    ui->btnPreview->setIconSize(QSize(48, 48));
    ui->btnPreview->setStyleSheet("background: transparent;border:none;");

    ui->btnStop->setIcon(QIcon(":/image/stop.ico"));
    ui->btnStop->setIconSize(QSize(48, 48));
    ui->btnStop->setStyleSheet("background: transparent;border:none;");

    ui->btnPause->setIcon(QIcon(":/image/play.ico"));
    ui->btnPause->setIconSize(QSize(48, 48));
    ui->btnPause->setStyleSheet("background: transparent;border:none;");

    setHide(ui->btnOpenLocal);
    setHide(ui->btnOpenUrl);
    setHide(ui->btnStop);
    setHide(ui->btnPause);
    setHide(ui->btnNext);
    setHide(ui->btnPreview);
    setHide(ui->lineEdit);
    setHide(ui->videoProgressSlider);
    setHide(ui->labelTime);
	//��QObject�������installEvenFilter������
	//��Ϊ�����װ������������ʹ���¼�������������ơ�
    ui->videoProgressSlider->installEventFilter(this);
}

void MainWindow::initFFmpeg()
{
//    av_log_set_level(AV_LOG_INFO);

    avfilter_register_all();

    /* ffmpeg init */
    av_register_all();

    /* ffmpeg network init for rtsp */
    if (avformat_network_init()) {
        qDebug() << "avformat network init failed";
    }

    /* init sdl audio */
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        qDebug() << "SDL init failed";
    }
}

void MainWindow::initSlot()
{
    connect(ui->btnOpenLocal,   SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnOpenUrl,     SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnStop,        SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnPause,       SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnNext,        SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->btnPreview,     SIGNAL(clicked(bool)), this, SLOT(buttonClickSlot()));
    connect(ui->lineEdit,       SIGNAL(cursorPositionChanged(int,int)),     this, SLOT(editText()));

    connect(m_menuTimer,      SIGNAL(timeout()), this, SLOT(timerSlot()));
    connect(m_progressTimer,  SIGNAL(timeout()), this, SLOT(timerSlot()));
//���û��϶����飬����sliderMoved �źš�
    connect(ui->videoProgressSlider,    SIGNAL(sliderMoved(int)), this, SLOT(seekProgress(int)));

    connect(this, SIGNAL(selectedVideoFile(QString,QString)),   m_decoder, SLOT(decoderFile(QString,QString)));
    connect(this, SIGNAL(stopVideo()),                          m_decoder, SLOT(stopVideo()));
    connect(this, SIGNAL(pauseVideo()),                         m_decoder, SLOT(pauseVideo()));

    connect(m_decoder, SIGNAL(playStateChanged(DecoderThread::PlayState)),  this, SLOT(playStateChanged(DecoderThread::PlayState)));
    connect(m_decoder, SIGNAL(gotVideoTime(qint64)),                  this, SLOT(videoTime(qint64)));
    connect(m_decoder, SIGNAL(gotVideo(QImage)),                      this, SLOT(showVideo(QImage)));
}

void MainWindow::initTray()
{
    QSystemTrayIcon *trayIcon = new QSystemTrayIcon(this);

    trayIcon->setToolTip(tr("QtPlayer"));
    trayIcon->setIcon(QIcon(":/image/player.ico"));
    trayIcon->show();

    QAction *minimizeAction = new QAction(tr("minimize(&I)"), this);
    connect(minimizeAction, SIGNAL(triggered()), this, SLOT(hide()));
    QAction *restoreAction = new QAction(tr("restore(&R)"), this);
    connect(restoreAction, SIGNAL(triggered()), this, SLOT(showNormal()));
    QAction *quitAction = new QAction(tr("quit(&Q)"), this);
    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));

    /* tray right click menu */
    QMenu *trayIconMenu = new QMenu(this);

    trayIconMenu->addAction(minimizeAction);
    trayIconMenu->addAction(restoreAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
    trayIcon->setContextMenu(trayIconMenu);

    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    int width = this->width();
    int height = this->height();


    painter.setBrush(Qt::black);
    painter.drawRect(0, 0, width, height);

    if (m_isKeepAspectRatio) {
        QImage img = m_image.scaled(QSize(width, height), Qt::KeepAspectRatio);

        /* calculate display position */
        int x = (this->width() - img.width()) / 2;
        int y = (this->height() - img.height()) / 2;

        painter.drawImage(QPoint(x, y), img);
    } else {
        QImage img = m_image.scaled(QSize(width, height));

        painter.drawImage(QPoint(0, 0), img);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_closeNotExit) {
        /* ignore original close event */
        event->ignore();

        /* hide window & not show in task bar */
        this->hide();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    /* judge whether is window change event */
    if (event->type() == QEvent::WindowStateChange) {
        if (this->windowState() == Qt::WindowMinimized) {
            /* hide window & not show in task bar */
            event->ignore();
            this->hide();
        } else if (this->windowState() == Qt::WindowMaximized) {

        }
    }
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
	//??????
	if (object == ui->videoProgressSlider) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                int duration = ui->videoProgressSlider->maximum() - ui->videoProgressSlider->minimum();
                int pos = ui->videoProgressSlider->minimum() + duration * (static_cast<double>(mouseEvent->x()) / ui->videoProgressSlider->width());
                if (pos != ui->videoProgressSlider->sliderPosition()) {
                    ui->videoProgressSlider->setValue(pos);
                    m_decoder->decoderSeekProgress(static_cast<qint64>(pos) * 1000000);
                }
            }
        }
    }
	//?????????????????????????????????????
    //??????????evenFilter????
    return QObject::eventFilter(object, event);
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    int progressVal;
    int volumnVal = m_decoder->getVolume();

    switch (event->key()) {
    case Qt::Key_Up:
        if (volumnVal + VOLUME_INT > SDL_MIX_MAXVOLUME) {
            m_decoder->setVolume(SDL_MIX_MAXVOLUME);
        } else {
            m_decoder->setVolume(volumnVal + VOLUME_INT);
        }
        break;

    case Qt::Key_Down:
        if (volumnVal - VOLUME_INT < 0) {
            m_decoder->setVolume(0);
        } else {
            m_decoder->setVolume(volumnVal - VOLUME_INT);
        }
        break;

    case Qt::Key_Left:
        if (ui->videoProgressSlider->value() > m_seekInterval) {
            progressVal = ui->videoProgressSlider->value() - m_seekInterval;
            m_decoder->decoderSeekProgress(static_cast<qint64>(progressVal) * 1000000);
        }
        break;

    case Qt::Key_Right:
        if (ui->videoProgressSlider->value() + m_seekInterval < ui->videoProgressSlider->maximum()) {
            progressVal = ui->videoProgressSlider->value() + m_seekInterval;
            m_decoder->decoderSeekProgress(static_cast<qint64>(progressVal) * 1000000);
        }
        break;

    case Qt::Key_Escape:
        showNormal();
        break;

    case Qt::Key_Space:
        emit pauseVideo();
        break;

    default:

        break;
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    Q_UNUSED(event);

    /* stop timer & restart it while having mouse moving */
    if (m_currentPlayType == "video") {
        m_menuTimer->stop();
        if (!m_menuIsVisible) {
            showControl(true);
            m_menuIsVisible = true;
            QApplication::setOverrideCursor(Qt::ArrowCursor);
        }
        m_menuTimer->start();
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->buttons() == Qt::RightButton) {
        showPlayMenu();
    } else if (event->buttons() == Qt::LeftButton) {
        emit pauseVideo();
    }
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->buttons() == Qt::LeftButton) {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    }
}

void MainWindow::showPlayMenu()
{
    QMenu *menu = new QMenu;

    QAction * fullSrcAction = new QAction("FullScreen", this);
    fullSrcAction->setCheckable(true);
    if (isFullScreen()) {
        fullSrcAction->setChecked(true);
    }

    QAction *keepRatioAction = new QAction("KeepRatio", this);
    keepRatioAction->setCheckable(true);
    if (m_isKeepAspectRatio) {
        keepRatioAction->setChecked(true);
    }

    QAction *autoPlayAction = new QAction("AutoPlay", this);
    autoPlayAction->setCheckable(true);
    if (m_autoPlay) {
        autoPlayAction->setChecked(true);
    }

    QAction *loopPlayAction = new QAction("LoopPlay", this);
    loopPlayAction->setCheckable(true);
    if (m_loopPlay) {
        loopPlayAction->setChecked(true);
    }

    QAction *captureAction = new QAction("Capture", this);

    connect(fullSrcAction,      SIGNAL(triggered(bool)), this, SLOT(setFullScreen()));
    connect(keepRatioAction,    SIGNAL(triggered(bool)), this, SLOT(setKeepRatio()));
    connect(autoPlayAction,     SIGNAL(triggered(bool)), this, SLOT(setAutoPlay()));
    connect(loopPlayAction,     SIGNAL(triggered(bool)), this, SLOT(setLoopPlay()));
    connect(captureAction,      SIGNAL(triggered(bool)), this, SLOT(saveCurrentFrame()));

    menu->addAction(fullSrcAction);
    menu->addAction(keepRatioAction);
    menu->addAction(autoPlayAction);
    menu->addAction(loopPlayAction);
    menu->addAction(captureAction);

    menu->exec(QCursor::pos());

    disconnect(fullSrcAction,   SIGNAL(triggered(bool)), this, SLOT(setFullScreen()));
    disconnect(keepRatioAction, SIGNAL(triggered(bool)), this, SLOT(setKeepRatio()));
    disconnect(autoPlayAction,  SIGNAL(triggered(bool)), this, SLOT(setAutoPlay()));
    disconnect(loopPlayAction,  SIGNAL(triggered(bool)), this, SLOT(setLoopPlay()));
    disconnect(captureAction,       SIGNAL(triggered(bool)), this, SLOT(saveCurrentFrame()));

    delete fullSrcAction;
    delete keepRatioAction;
    delete autoPlayAction;
    delete loopPlayAction;
    delete captureAction;
    delete menu;
}

void MainWindow::setHide(QWidget *widget)
{
    m_hideVector.push_back(widget);
}

void MainWindow::showControl(bool show)
{
    if (show) {
        for (QWidget *widget : m_hideVector) {
            widget->show();
        }
    } else {
        for (QWidget *widget : m_hideVector) {
            widget->hide();
        }
    }
}

inline QString MainWindow::getFilenameFromPath(QString path)
{
    return path.right(path.size() - path.lastIndexOf("/") - 1);
}

QString MainWindow::fileType(QString file)
{
    QString type;

    QString suffix = file.right(file.size() - file.lastIndexOf(".") - 1);
    if (suffix == "mp3" || suffix == "ape" || suffix == "flac" || suffix == "wav") {
        type = "music";
    } else {
        type = "video";
    }

    return type;
}

void MainWindow::addPathVideoToList(QString path)
{
    QDir dir(path);

    QRegExp rx(".*\\.(264|rmvb|flv|mp4|mov|avi|mkv|ts|wav|flac|ape|mp3)$");

    QFileInfoList list = dir.entryInfoList(QDir::Files);
    for(int i = 0; i < list.count(); i++) {
        QFileInfo fileInfo = list.at(i);

        if (rx.exactMatch(fileInfo.fileName())) {
            QString filename = getFilenameFromPath(fileInfo.fileName());
            /* avoid adding repeat file */
            if (!m_playList.contains(filename)) {
                m_playList.push_back(fileInfo.absoluteFilePath());
            }
        }
    }
}

void MainWindow::playVideo(QString file)
{
    emit stopVideo();

    m_currentPlay = file;
    m_currentPlayType = fileType(file);
    if (m_currentPlayType == "video") {
        m_menuTimer->start();
        ui->titleLable->setText("");
    } else {
        m_menuTimer->stop();
        if (!m_menuIsVisible) {
            showControl(true);
            m_menuIsVisible = true;
        }
        ui->titleLable->setStyleSheet("color:rgb(25, 125, 203);font-size:24px;background: transparent;");
        ui->titleLable->setText(QString("Music:").arg(getFilenameFromPath(file)));
    }

    emit selectedVideoFile(file, m_currentPlayType);
}

void MainWindow::playNext()
{
    int playIndex = 0;
    int videoNum = m_playList.size();

    if (videoNum <= 0) {
        return;
    }

    int currentIndex = m_playList.indexOf(m_currentPlay);

    if (currentIndex != videoNum - 1) {
        playIndex = currentIndex + 1;
    }

    QString nextVideo = m_playList.at(playIndex);

    /* check file whether exists */
    QFile file(nextVideo);
    if (!file.exists()) {
        m_playList.removeAt(playIndex);
        return;
    }

    playVideo(nextVideo);
}

void MainWindow::playPreview()
{
    int playIndex = 0;
    int videoNum = m_playList.size();
    int currentIndex = m_playList.indexOf(m_currentPlay);

    if (videoNum <= 0) {
        return;
    }

    /* if current file index greater than 0, means have priview video
     * play last index video, otherwise if this file is head,
     * play tail index video
     */
    if (currentIndex > 0) {
        playIndex = currentIndex - 1;
    } else {
        playIndex = videoNum - 1;
    }

    QString preVideo = m_playList.at(playIndex);

    /* check file whether exists */
    QFile file(preVideo);
    if (!file.exists()) {
        m_playList.removeAt(playIndex);
        return;
    }

    playVideo(preVideo);
}

/******************* slot ************************/

void MainWindow::buttonClickSlot()
{
    QString filePath;

    if (QObject::sender() == ui->btnOpenLocal) { // open local file
        filePath = QFileDialog::getOpenFileName(
                this, "?????????", "/",
                "(*.264 *.mp4 *.rmvb *.avi *.mov *.flv *.mkv *.ts *.mp3 *.flac *.ape *.wav)");
        if (!filePath.isNull() && !filePath.isEmpty()) {
            playVideo(filePath);

            QString path = filePath.left(filePath.lastIndexOf("/") + 1);
            addPathVideoToList(path);
        }
    } else if (QObject::sender() == ui->btnOpenUrl) {   // open network file
        filePath = ui->lineEdit->text();
        if (!filePath.isNull() && !filePath.isEmpty()) {
            QString type = "video";
            emit selectedVideoFile(filePath, type);
        }
    } else if (QObject::sender() == ui->btnStop) {
        emit stopVideo();
    } else if (QObject::sender() == ui->btnPause) {
        emit pauseVideo();
    } else if (QObject::sender() == ui->btnPreview) {
        playPreview();
    } else if (QObject::sender() == ui->btnNext) {
        playNext();
    }
}

void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::DoubleClick:
        this->showNormal();
        this->raise();
        this->activateWindow();
        break;

    case QSystemTrayIcon::Trigger:
    default:
        break;
    }
}

void MainWindow::setFullScreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::setKeepRatio()
{
    m_isKeepAspectRatio = !m_isKeepAspectRatio;
}

void MainWindow::setAutoPlay()
{
    m_autoPlay = !m_autoPlay;
    m_loopPlay = false;
}

void MainWindow::setLoopPlay()
{
    m_loopPlay = !m_loopPlay;
    m_autoPlay = false;
}

void MainWindow::saveCurrentFrame()
{
    QString filename = QFileDialog::getSaveFileName(this, "save file", "/", "(*.jpg)");
    m_image.save(filename);
}

void MainWindow::timerSlot()
{
    if (QObject::sender() == m_menuTimer) {
        if (m_menuIsVisible && m_playState == DecoderThread::PLAYING) {
            if (isFullScreen()) {
                QApplication::setOverrideCursor(Qt::BlankCursor);
            }
            showControl(false);
            m_menuIsVisible = false;
        }
    } else if (QObject::sender() == m_progressTimer) {
        qint64 currentTime = static_cast<qint64>(m_decoder->getCurrentTime());
        ui->videoProgressSlider->setValue(currentTime);

        int hourCurrent = currentTime / 60 / 60;
        int minCurrent  = (currentTime / 60) % 60;
        int secCurrent  = currentTime % 60;

        int hourTotal = m_timeTotal / 60 / 60;
        int minTotal  = (m_timeTotal / 60) % 60;
        int secTotal  = m_timeTotal % 60;

        ui->labelTime->setText(QString("%1.%2.%3 / %4:%5:%6")
                               .arg(hourCurrent, 2, 10, QLatin1Char('0'))
                               .arg(minCurrent, 2, 10, QLatin1Char('0'))
                               .arg(secCurrent, 2, 10, QLatin1Char('0'))
                               .arg(hourTotal, 2, 10, QLatin1Char('0'))
                               .arg(minTotal, 2, 10, QLatin1Char('0'))
                               .arg(secTotal, 2, 10, QLatin1Char('0')));
    }
}

void MainWindow::seekProgress(int value)
{
    m_decoder->decoderSeekProgress(static_cast<qint64>(value) * 1000000);
}

void MainWindow::editText()
{
    /* forbid control hide while inputting */
    m_menuTimer->stop();
    m_menuTimer->start();
}

void MainWindow::videoTime(qint64 time)
{
    m_timeTotal = time / 1000000;

    ui->videoProgressSlider->setRange(0, m_timeTotal);

    int hour = m_timeTotal / 60 / 60;
    int min  = (m_timeTotal / 60 ) % 60;
    int sec  = m_timeTotal % 60;

    ui->labelTime->setText(QString("00.00.00 / %1:%2:%3").arg(hour, 2, 10, QLatin1Char('0'))
                           .arg(min, 2, 10, QLatin1Char('0'))
                           .arg(sec, 2, 10, QLatin1Char('0')));
}

void MainWindow::showVideo(QImage image)
{
    this->m_image = image;
    update();
}

void MainWindow::playStateChanged(DecoderThread::PlayState state)
{
    switch (state) {
    case DecoderThread::PLAYING:
        ui->btnPause->setIcon(QIcon(":/image/pause.ico"));
        m_playState = DecoderThread::PLAYING;
        m_progressTimer->start();
        break;

    case DecoderThread::STOP:
        m_image = QImage(":/image/bac.jpg");
        ui->btnPause->setIcon(QIcon(":/image/play.ico"));
        m_playState = DecoderThread::STOP;
        m_progressTimer->stop();
        ui->labelTime->setText(QString("00.00.00 / 00:00:00"));
        ui->videoProgressSlider->setValue(0);
        m_timeTotal = 0;
        update();
        break;

    case DecoderThread::PAUSE:
        ui->btnPause->setIcon(QIcon(":/image/play.ico"));
        m_playState = DecoderThread::PAUSE;
        break;

    case DecoderThread::FINISH:
        if (m_autoPlay) {
            playNext();
        } else if (m_loopPlay) {
            emit selectedVideoFile(m_currentPlay, m_currentPlayType);
        }else {
            m_image = QImage(":/image/bac.jpg");
            m_playState = DecoderThread::STOP;
            m_progressTimer->stop();
            ui->labelTime->setText(QString("00.00.00 / 00:00:00"));
            ui->videoProgressSlider->setValue(0);
            m_timeTotal = 0;
        }
        break;

    default:

        break;
    }
}
