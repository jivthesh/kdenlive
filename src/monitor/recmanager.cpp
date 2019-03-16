/***************************************************************************
 *   Copyright (C) 2015 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "recmanager.h"
#include "capture/managecapturesdialog.h"
#include "capture/mltdevicecapture.h"
#include "core.h"
#include "dialogs/profilesdialog.h"
#include "kdenlivesettings.h"
#include "monitor.h"

#include "klocalizedstring.h"
#include <KMessageBox>

#include <QComboBox>
#include <QDesktopWidget>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QStandardPaths>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

RecManager::RecManager(Monitor *parent)
    : QObject(parent)
    , m_monitor(parent)
    , m_recToolbar(new QToolBar(parent))
    , m_checkAudio(false)
    , m_checkVideo(false)
{
    m_playAction = m_recToolbar->addAction(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Preview"));
    m_playAction->setCheckable(true);
    connect(m_playAction, &QAction::toggled, this, &RecManager::slotPreview);

    m_recAction = m_recToolbar->addAction(QIcon::fromTheme(QStringLiteral("media-record")), i18n("Record"));
    m_recAction->setCheckable(true);
    connect(m_recAction, &QAction::toggled, this, &RecManager::slotRecord);

    m_showLogAction = new QAction(i18n("Show log"), this);
    connect(m_showLogAction, &QAction::triggered, this, &RecManager::slotShowLog);

    m_recVideo = new QCheckBox(i18n("Video"));
    m_recAudio = new QCheckBox(i18n("Audio"));
    m_recToolbar->addWidget(m_recVideo);
    m_recToolbar->addWidget(m_recAudio);
    m_recAudio->setChecked(KdenliveSettings::v4l_captureaudio());
    m_recVideo->setChecked(KdenliveSettings::v4l_capturevideo());

    // Check number of monitors for FFmpeg screen capture
    int screens = QApplication::desktop()->screenCount();
    if (screens > 1) {
        m_screenCombo = new QComboBox(parent);
        for (int ix = 0; ix < screens; ix++) {
            m_screenCombo->addItem(i18n("Monitor %1", ix));
        }
        m_recToolbar->addWidget(m_screenCombo);
        // Update screen grab monitor choice in case we changed from fullscreen
        m_screenCombo->setEnabled(KdenliveSettings::grab_capture_type() == 0);
    }
    QWidget *spacer = new QWidget(parent);
    spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    m_recToolbar->addWidget(spacer);

    m_audio_device = new QComboBox(parent);
    QStringList audioDevices = pCore->getAudioCaptureDevices();
    for (int ix = 0; ix < audioDevices.count(); ix++) {
        m_audio_device->addItem(audioDevices.at(ix), ix);
    }
    connect(m_audio_device, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &RecManager::slotAudioDeviceChanged);
    int selectedCapture = m_audio_device->findData(KdenliveSettings::defaultaudiocapture());
    if (selectedCapture > -1) {
        m_audio_device->setCurrentIndex(selectedCapture);
    }
    m_recToolbar->addWidget(m_audio_device);

    m_audioCaptureSlider = new QSlider(Qt::Vertical);
    m_audioCaptureSlider->setRange(0, 100);
    m_audioCaptureSlider->setValue(KdenliveSettings::audiocapturevolume());
    connect(m_audioCaptureSlider, &QSlider::valueChanged, this, &RecManager::slotSetVolume);
    auto *widgetslider = new QWidgetAction(parent);
    widgetslider->setText(i18n("Audio Capture Volume"));
    widgetslider->setDefaultWidget(m_audioCaptureSlider);
    auto *menu = new QMenu(parent);
    menu->addAction(widgetslider);
    m_audioCaptureButton = new QToolButton(parent);
    m_audioCaptureButton->setMenu(menu);
    m_audioCaptureButton->setToolTip(i18n("Audio Capture Volume"));
    m_audioCaptureButton->setPopupMode(QToolButton::InstantPopup);
    QIcon icon;
    if (KdenliveSettings::audiocapturevolume() == 0) {
        icon = QIcon::fromTheme(QStringLiteral("audio-volume-muted"));
    } else {
        icon = QIcon::fromTheme(QStringLiteral("audio-volume-medium"));
    }
    m_audioCaptureButton->setIcon(icon);
    m_recToolbar->addWidget(m_audioCaptureButton);
    m_recToolbar->addSeparator();

    m_device_selector = new QComboBox(parent);
    // TODO: re-implement firewire / decklink capture
    // m_device_selector->addItems(QStringList() << i18n("Firewire") << i18n("Webcam") << i18n("Screen Grab") << i18n("Blackmagic Decklink"));
    m_device_selector->addItem(i18n("Webcam"), Video4Linux);
    m_device_selector->addItem(i18n("Screen Grab"), ScreenGrab);
    selectedCapture = m_device_selector->findData(KdenliveSettings::defaultcapture());

    if (selectedCapture > -1) {
        m_device_selector->setCurrentIndex(selectedCapture);
    }
    connect(m_device_selector, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &RecManager::slotVideoDeviceChanged);
    m_recToolbar->addWidget(m_device_selector);

    QAction *configureRec = m_recToolbar->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Configure Recording"));
    connect(configureRec, &QAction::triggered, this, &RecManager::showRecConfig);
    m_recToolbar->addSeparator();
    m_switchRec = m_recToolbar->addAction(QIcon::fromTheme(QStringLiteral("list-add")), i18n("Show Record Control"));
    m_switchRec->setCheckable(true);
    connect(m_switchRec, &QAction::toggled, m_monitor, &Monitor::slotSwitchRec);
    m_recToolbar->setVisible(false);
    slotVideoDeviceChanged();
}

RecManager::~RecManager() = default;

void RecManager::showRecConfig()
{
    m_monitor->showConfigDialog(4, m_device_selector->currentData().toInt());
}

QToolBar *RecManager::toolbar() const
{
    return m_recToolbar;
}

QAction *RecManager::switchAction() const
{
    return m_switchRec;
}

void RecManager::stopCapture()
{
    if (m_captureProcess) {
        slotRecord(false);
    } else if (pCore->getMediaCaptureState() == 1 && (m_checkAudio || m_checkVideo)) {
        // QMediaRecorder::RecordingState value is 1
        pCore->stopMediaCapture(m_checkAudio, m_checkVideo);
        m_monitor->slotOpenClip(nullptr);
        emit addClipToProject(m_captureFile);
    }
}

void RecManager::stop()
{
    if (m_captureProcess) {
        // Don't stop screen rec when hiding rec toolbar
    } else {
        stopCapture();
        m_switchRec->setChecked(false);
    }
    toolbar()->setVisible(false);
}

void RecManager::slotRecord(bool record)
{
    if (m_device_selector->currentData().toInt() == Video4Linux) {
        if (record) {
            QDir captureFolder;
            if (KdenliveSettings::capturetoprojectfolder()) {
                captureFolder = QDir(m_monitor->projectFolder());
            } else {
                captureFolder = QDir(KdenliveSettings::capturefolder());
            }
            QString extension;
            if (!m_recVideo->isChecked()) {
                extension = QStringLiteral("wav");
            } else {
                extension = QStringLiteral("mpeg");
            }

            QString path = captureFolder.absoluteFilePath("capture0000." + extension);
            int i = 1;
            while (QFile::exists(path)) {
                QString num = QString::number(i).rightJustified(4, '0', false);
                path = captureFolder.absoluteFilePath("capture" + num + QLatin1Char('.') + extension);
                ++i;
            }
            QString audioDevice = m_audio_device->currentText();
            m_captureFile = QUrl::fromLocalFile(path);
            m_checkAudio = m_recAudio->isChecked();
            m_checkVideo = m_recVideo->isChecked();
            pCore->startMediaCapture(m_checkAudio, m_checkVideo, m_captureFile, audioDevice);
        } else {
            stopCapture();
        }
        return;
    }
    if (!record) {
        if (!m_captureProcess) {
            return;
        }
        m_captureProcess->write("q");
        if (!m_captureProcess->waitForFinished()) {
            m_captureProcess->terminate();
            QTimer::singleShot(1500, m_captureProcess, &QProcess::kill);
        }
        return;
    }
    if (m_captureProcess) {
        return;
    }
    m_recError.clear();

    QString extension = KdenliveSettings::grab_extension();
    QDir captureFolder;
    if (KdenliveSettings::capturetoprojectfolder()) {
        captureFolder = QDir(m_monitor->projectFolder());
    } else {
        captureFolder = QDir(KdenliveSettings::capturefolder());
    }

    QFileInfo checkCaptureFolder(captureFolder.absolutePath());
    if (!checkCaptureFolder.isWritable()) {
        emit warningMessage(i18n("The directory %1, could not be created.\nPlease "
                                 "make sure you have the required permissions.",
                                 captureFolder.absolutePath()));
        m_recAction->blockSignals(true);
        m_recAction->setChecked(false);
        m_recAction->blockSignals(false);
        return;
    }

    m_captureProcess = new QProcess;
    connect(m_captureProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &RecManager::slotProcessStatus);
    connect(m_captureProcess, &QProcess::readyReadStandardError, this, &RecManager::slotReadProcessInfo);

    QString path = captureFolder.absoluteFilePath("capture0000." + extension);
    int i = 1;
    while (QFile::exists(path)) {
        QString num = QString::number(i).rightJustified(4, '0', false);
        path = captureFolder.absoluteFilePath("capture" + num + QLatin1Char('.') + extension);
        ++i;
    }
    m_captureFile = QUrl::fromLocalFile(path);
    QString captureSize;
    int screen = -1;
    if (m_screenCombo) {
        // Multi monitor setup, capture monitor selected by user
        screen = m_screenCombo->currentIndex();
    }
    QRect screenSize = QApplication::desktop()->screenGeometry(screen);
    QStringList captureArgs;
#ifdef Q_OS_WIN
    captureArgs << QStringLiteral("-f") << QStringLiteral("gdigrab");
    // fps
    captureArgs << QStringLiteral("-framerate") << QString::number(KdenliveSettings::grab_fps());
    captureSize = QStringLiteral("desktop");
#else
    captureArgs << QStringLiteral("-f") << QStringLiteral("x11grab");
    if (KdenliveSettings::grab_follow_mouse()) {
        captureArgs << QStringLiteral("-follow_mouse") << QStringLiteral("centered");
    }
    if (!KdenliveSettings::grab_hide_frame()) {
        captureArgs << QStringLiteral("-show_region") << QStringLiteral("1");
    }
    captureSize = QStringLiteral(":0.0");
    if (KdenliveSettings::grab_capture_type() == 0) {
        // Full screen capture
        captureArgs << QStringLiteral("-s") << QString::number(screenSize.width()) + QLatin1Char('x') + QString::number(screenSize.height());
        captureSize.append(QLatin1Char('+') + QString::number(screenSize.left()) + QLatin1Char('.') + QString::number(screenSize.top()));
    } else {
        // Region capture
        captureArgs << QStringLiteral("-s")
                    << QString::number(KdenliveSettings::grab_width()) + QLatin1Char('x') + QString::number(KdenliveSettings::grab_height());
        captureSize.append(QLatin1Char('+') + QString::number(KdenliveSettings::grab_offsetx()) + QLatin1Char(',') +
                           QString::number(KdenliveSettings::grab_offsety()));
    }
    if (KdenliveSettings::grab_hide_mouse()) {
        captureSize.append(QStringLiteral("+nomouse"));
    }
    // fps
    captureArgs << QStringLiteral("-r") << QString::number(KdenliveSettings::grab_fps());
#endif
    captureArgs << QStringLiteral("-i") << captureSize;
    if (!KdenliveSettings::grab_parameters().simplified().isEmpty()) {
        captureArgs << KdenliveSettings::grab_parameters().simplified().split(QLatin1Char(' '));
    }
    captureArgs << path;
    m_captureProcess->start(KdenliveSettings::ffmpegpath(), captureArgs);
    if (!m_captureProcess->waitForStarted()) {
        // Problem launching capture app
        emit warningMessage(i18n("Failed to start the capture application:\n%1", KdenliveSettings::ffmpegpath()));
        // delete m_captureProcess;
    }
}

void RecManager::slotProcessStatus(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_recAction->setEnabled(true);
    m_recAction->setChecked(false);
    m_device_selector->setEnabled(true);
    if (exitStatus == QProcess::CrashExit) {
        emit warningMessage(i18n("Capture crashed, please check your parameters"), -1, QList<QAction *>() << m_showLogAction);
    } else {
        if (exitCode != 0 && exitCode != 255) {
            emit warningMessage(i18n("Capture crashed, please check your parameters"), -1, QList<QAction *>() << m_showLogAction);
        } else {
            // Capture successful, add clip to project
            emit addClipToProject(m_captureFile);
        }
    }
    if (m_captureProcess) {
        delete m_captureProcess;
        m_captureProcess = nullptr;
    }
}

void RecManager::slotReadProcessInfo()
{
    QString data = m_captureProcess->readAllStandardError().simplified();
    m_recError.append(data + QLatin1Char('\n'));
}

void RecManager::slotAudioDeviceChanged(int)
{
    int currentItem = m_audio_device->currentData().toInt();
    KdenliveSettings::setDefaultaudiocapture(currentItem);
}

void RecManager::slotSetVolume(int volume)
{
    KdenliveSettings::setAudiocapturevolume(volume);
    pCore->setAudioCaptureVolume(volume);
    QIcon icon;

    if (volume == 0) {
        icon = QIcon::fromTheme(QStringLiteral("audio-volume-muted"));
    } else {
        icon = QIcon::fromTheme(QStringLiteral("audio-volume-medium"));
    }
    m_audioCaptureButton->setIcon(icon);
}
void RecManager::slotVideoDeviceChanged(int)
{
    int currentItem = m_device_selector->currentData().toInt();
    KdenliveSettings::setDefaultcapture(currentItem);
    switch (currentItem) {
    case Video4Linux:
        m_playAction->setEnabled(true);
        break;
    case BlackMagic:
        m_playAction->setEnabled(false);
        break;
    default:
        m_playAction->setEnabled(false);
        break;
    }
    /*
    m_previewSettings->setEnabled(ix == Video4Linux || ix == BlackMagic);
    control_frame->setVisible(ix == Video4Linux);
    monitor_box->setVisible(ix == ScreenBag && monitor_box->count() > 0);
    m_playAction->setVisible(ix != ScreenBag);
    m_fwdAction->setVisible(ix == Firewire);
    m_discAction->setVisible(ix == Firewire);
    m_rewAction->setVisible(ix == Firewire);
    m_recAction->setEnabled(ix != Firewire);
    m_logger.setVisible(ix == BlackMagic);
    if (m_captureDevice) {
        // MLT capture still running, abort
        m_monitorManager->clearScopeSource();
        m_captureDevice->stop();
        delete m_captureDevice;
        m_captureDevice = nullptr;
    }

    // The m_videoBox container has to be shown once before the MLT consumer is build, or preview will fail
    switch (ix) {
    case ScreenBag:
    }
    */
}

Mlt::Producer *RecManager::createV4lProducer()
{
    QString profilePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/profiles/video4linux");
    Mlt::Profile *vidProfile = new Mlt::Profile(profilePath.toUtf8().constData());
    Mlt::Producer *prod = nullptr;
    if (m_recVideo->isChecked()) {
        prod = new Mlt::Producer(*vidProfile, QStringLiteral("video4linux2:%1").arg(KdenliveSettings::video4vdevice()).toUtf8().constData());
        if ((prod == nullptr) || !prod->is_valid()) {
            return nullptr;
        }
        prod->set("width", vidProfile->width());
        prod->set("height", vidProfile->height());
        prod->set("framerate", vidProfile->fps());
        /*p->set("standard", ui->v4lStandardCombo->currentText().toLatin1().constData());
        p->set("channel", ui->v4lChannelSpinBox->value());
        p->set("audio_ix", ui->v4lAudioComboBox->currentIndex());*/
        prod->set("force_seekable", 0);
    }
    if (m_recAudio->isChecked() && (prod != nullptr) && prod->is_valid()) {
        // Add audio track
        Mlt::Producer *audio = new Mlt::Producer(
            *vidProfile,
            QStringLiteral("alsa:%1?channels=%2").arg(KdenliveSettings::v4l_alsadevicename()).arg(KdenliveSettings::alsachannels()).toUtf8().constData());
        audio->set("mlt_service", "avformat-novalidate");
        audio->set("audio_index", 0);
        audio->set("video_index", -1);
        auto *tractor = new Mlt::Tractor(*vidProfile);
        tractor->set_track(*prod, 0);
        delete prod;
        tractor->set_track(*audio, 1);
        delete audio;
        prod = new Mlt::Producer(tractor->get_producer());
        delete tractor;
    }
    return prod;
}

void RecManager::slotPreview(bool preview)
{
    if (m_device_selector->currentData().toInt() == Video4Linux) {
        if (preview) {
            std::shared_ptr<Mlt::Producer> prod(createV4lProducer());
            if (prod && prod->is_valid()) {
                m_monitor->updateClipProducer(prod);
            } else {
                emit warningMessage(i18n("Capture crashed, please check your parameters"));
            }
        } else {
            m_monitor->slotOpenClip(nullptr);
        }
    }

    /*
       buildMltDevice(path);

       bool isXml;
       producer = getV4lXmlPlaylist(profile, &isXml);

       //producer =
    QString("avformat-novalidate:video4linux2:%1?width:%2&height:%3&frame_rate:%4").arg(KdenliveSettings::video4vdevice()).arg(profile.width).arg(profile.height).arg((double)
    profile.frame_rate_num / profile.frame_rate_den);
       if (!m_captureDevice->slotStartPreview(producer, isXml)) {
           // v4l capture failed to start
           video_frame->setText(i18n("Failed to start Video4Linux,\ncheck your parameters..."));

       } else {
           m_playAction->setEnabled(false);
           m_stopAction->setEnabled(true);
           m_isPlaying = true;
       }
    }*/
}

void RecManager::slotShowLog()
{
    KMessageBox::information(QApplication::activeWindow(), m_recError);
}
