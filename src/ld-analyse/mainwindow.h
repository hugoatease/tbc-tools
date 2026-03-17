/******************************************************************************
 * mainwindow.h
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QRect>
#include <QTimer>
#include <QVector>
#include <QActionGroup>

#include "oscilloscopedialog.h"
#include "vectorscopedialog.h"
#include "aboutdialog.h"
#include "vbidialog.h"
#include "dropoutanalysisdialog.h"
#include "visibledropoutanalysisdialog.h"
#include "blacksnranalysisdialog.h"
#include "whitesnranalysisdialog.h"
#include "busydialog.h"
#include "closedcaptionsdialog.h"
#include "videoparametersdialog.h"
#include "chromadecoderconfigdialog.h"
#include "exportdialog.h"
#include "metadataconversiondialog.h"
#include "metadatastatusdialog.h"
#include "configuration.h"
#include "tbcsource.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QString inputFilenameParam, bool metadataOnlyParam = false, QWidget *parent = nullptr);
	TbcSource& getTbcSource();
    ~MainWindow();

private slots:
    // Menu bar handlers
    void on_actionExit_triggered();
    void on_actionOpen_TBC_file_triggered();
    void on_actionReload_TBC_triggered();
    void on_actionMetadata_Conversion_triggered();
    void on_actionMetadata_Status_triggered();
    void on_actionSave_Metadata_triggered();
    void on_actionLine_scope_triggered();
    void on_actionVectorscope_triggered();
    void on_actionAbout_ld_analyse_triggered();
    void on_actionVBI_triggered();
    void on_actionDropout_analysis_triggered();
    void on_actionVisible_Dropout_analysis_triggered();
    void on_actionSNR_analysis_triggered();
    void on_actionWhite_SNR_analysis_triggered();
    void on_actionSave_frame_as_PNG_triggered();
    void on_actionSave_all_modes_as_PNGs_triggered();
    void on_actionCopy_current_display_to_clipboard_triggered();
    void on_actionZoom_In_triggered();
    void on_actionZoom_Out_triggered();
    void on_actionZoom_1x_triggered();
    void on_actionZoom_2x_triggered();
    void on_actionZoom_3x_triggered();
    void on_actionClosed_Captions_triggered();
    void on_actionVideo_parameters_triggered();
    void on_actionChroma_decoder_configuration_triggered();
    void on_actionToggleChromaDuringSeek_triggered();
    void on_actionExport_Decode_Metadata_triggered();
    void on_actionProcess_VBI_triggered();
    void on_actionProcess_VITS_triggered();
    void on_actionFix_JSON_SNR_triggered();

    // Media control frame handlers
    void on_previousPushButton_clicked();
    void on_nextPushButton_clicked();
    void on_previousPushButton_pressed();
    void on_previousPushButton_released();
    void on_nextPushButton_pressed();
    void on_nextPushButton_released();
    void on_endPushButton_clicked();
    void on_startPushButton_clicked();
    void on_playPushButton_toggled(bool checked);
    void on_posNumberSpinBox_editingFinished();
    void on_posTimecodeLineEdit_editingFinished();
    void on_posHorizontalSlider_valueChanged(int value);
    void on_posHorizontalSlider_sliderPressed();
    void on_posHorizontalSlider_sliderReleased();
    void on_posHorizontalSlider_customContextMenuRequested(const QPoint &pos);
    void on_videoPushButton_clicked();
    void on_aspectPushButton_clicked();
    void on_dropoutsPushButton_clicked();
    void on_sourcesPushButton_clicked();
    void on_viewPushButton_clicked();
    void on_fieldOrderPushButton_clicked();
    void on_zoomInPushButton_clicked();
    void on_zoomOutPushButton_clicked();
    void on_originalSizePushButton_clicked();
    void on_mouseModePushButton_clicked();
    //void on_autoResizeButton_clicked();
	void on_toggleAutoResize_toggled(bool checked);
	void on_actionResizeFrameWithWindow_toggled(bool checked);

    // Miscellaneous handlers
    void scopeCoordsChangedSignalHandler(qint32 xCoord, qint32 yCoord);
    void vectorscopeChangedSignalHandler();
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void videoParametersChangedSignalHandler(const LdDecodeMetaData::VideoParameters &videoParameters);
    void videoLevelsChangedSignalHandler(qint32 blackLevel, qint32 whiteLevel);
    void chromaDecoderConfigChangedSignalHandler();
    void exportBoundaryToggledSignalHandler(bool enabled);
    void exportBoundaryThicknessChangedSignalHandler(int thickness);

    // Tbc Source signal handlers
    void on_busy(QString infoMessage);
    void on_finishedLoading(bool success);
    void on_finishedSaving(bool success);
	
	// UI handler
	void resize_on_aspect();
protected:
    bool event(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct UiStateSnapshot {
        bool valid = false;
        qint32 frameNumber = 1;
        qint32 fieldNumber = 1;
        TbcSource::ViewMode viewMode = TbcSource::ViewMode::FRAME_VIEW;
        bool stretchField = true;
        bool reverseFieldOrder = false;
        bool highlightDropouts = false;
        bool chromaEnabled = false;
        TbcSource::ChromaDecodeMode chromaDecodeMode = TbcSource::ChromaDecodeMode::HYBRID_CHROMA_MODE;
        TbcSource::SourceMode sourceMode = TbcSource::SourceMode::ONE_SOURCE;
        bool aspectRatioEnabled = false;
        double imageScaleFactor = 1.0;
        int activeMainTabIndex = 0;
        bool showVbiDialog = false;
        bool showDropoutDialog = false;
        bool showVisibleDropoutDialog = false;
        bool showBlackSnrDialog = false;
        bool showWhiteSnrDialog = false;
        bool showVideoParametersDialog = false;
        bool showChromaConfigDialog = false;
    };
    Ui::MainWindow *ui;

    // Dialogues
    OscilloscopeDialog* oscilloscopeDialog;
    VectorscopeDialog* vectorscopeDialog;
    AboutDialog* aboutDialog;
    VbiDialog* vbiDialog;
    DropoutAnalysisDialog* dropoutAnalysisDialog;
    VisibleDropOutAnalysisDialog* visibleDropoutAnalysisDialog;
    BlackSnrAnalysisDialog* blackSnrAnalysisDialog;
    WhiteSnrAnalysisDialog* whiteSnrAnalysisDialog;
    BusyDialog* busyDialog;
    ClosedCaptionsDialog *closedCaptionDialog;
    VideoParametersDialog *videoParametersDialog;
    ChromaDecoderConfigDialog *chromaDecoderConfigDialog;
    MetadataConversionDialog *metadataConversionDialog;
    MetadataStatusDialog *metadataStatusDialog;
    ExportDialog *exportDialog;

    // Class globals
    Configuration configuration;
    QLabel sourceVideoStatus;
    QLabel fieldNumberStatus;
    QLabel vbiStatus;
    QLabel timeCodeStatus;
    TbcSource tbcSource;
    bool displayAspectRatio;
    bool showExportBoundary = false;
    qint32 exportBoundaryThickness = 4;
	bool autoResize = true;
	bool resizeFrameWithWindow = true;
    qint32 lastScopeLine;
    qint32 lastScopeDot;
    qint32 currentFieldNumber, currentFrameNumber;
    double scaleFactor;
    QPalette buttonPalette;
    QString lastFilename;
    bool metadataJsonLoaded = false;
    QString metadataJsonFilename;
    QString metadataTempSqliteFilename;
    QTimer* playbackTimer = nullptr;
    QTimer* resizeTimer = nullptr;
    double playbackTickCarryMs = 0.0;
    bool playbackRunning = false;
    
    // Chroma toggle during seek
    bool chromaSeekMode;
    bool originalChromaState;
    QTimer* seekTimer;

    // Update GUI methods
    void setGuiEnabled(bool enabled);
    void resetGui();
    void updateGuiLoaded();
    void updateGuiUnloaded();
    void updateVideoPushButton();
    void updateAspectPushButton();
    void updateSourcesPushButton();
    void updateMetadataStatusPanel();
    void updateBottomStatusReadout();
    void setViewValues();
    double timecodeFrameRate() const;
    int timecodeFrameBaseRate() const;
    QString frameToTimecode(qint32 frameNumber) const;
    QString framesToDurationTimecode(qint32 frameCount) const;
    qint32 timecodeToFrame(const QString &timecodeText, bool *ok = nullptr) const;
    void updatePositionEditorValue(qint32 currentNumber);
    bool playbackUseFastMode() const;
    QString playbackStartToolTip() const;
    double playbackFrameIntervalMs() const;
    void scheduleNextPlaybackTick();
    void setPlaybackRunning(bool running);
    void stepPlayback();
    void setCurrentFrame(qint32 frame);
    void setCurrentField(qint32 field);
    void sanitizeCurrentPosition();

	// Image display methods
    void showImage();
    void updateImage();
    qint32 getAspectAdjustment() const;
    QImage renderedCurrentImageForExport();
    bool isViewerTabActive() const;
    QString outputRootDirectoryForCurrentSource();
    QString outputBaseNameForCurrentSource();
    void updateImageViewer();
    QVector<QRect> getActiveVideoRects() const;
    void hideImage();
    void resizeFrameToWindow();
    void enterChromaSeekMode(QPushButton* button);
    void exitChromaSeekMode(QPushButton* button);

    // TBC source signal handlers
    void loadTbcFile(QString inputFileName, bool forceMetadataOnly = false, bool preserveStatusDuringReload = false);
    void cleanupTempMetadataFile();
    void updateOscilloscopeDialogue();
    void updateVectorscopeDialogue();
    void populateThemesMenu();
    void applyThemeStyle(const QString &styleName);
    void mouseScanLineSelect(qint32 oX, qint32 oY);
	void resizeEvent(QResizeEvent *event);
    bool runExternalToolWithProgress(const QString &program, const QStringList &arguments,
                                     const QString &toolDisplayName, QString *errorMessage);
    void queueAnalysisRefreshPreservingUserState(const QString &processedInputFile);
    UiStateSnapshot captureUiStateSnapshot() const;
    void applyUiStateSnapshot(const UiStateSnapshot &snapshot);
    QActionGroup *themesActionGroup = nullptr;
    QAction *saveAllModesPngAction = nullptr;
    QAction *copyCurrentDisplayAction = nullptr;
    UiStateSnapshot pendingUiStateSnapshot;
    bool restoreUiStateAfterReload = false;
};

#endif // MAINWINDOW_H
