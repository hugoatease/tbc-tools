#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QWidget>
#include <QProcess>
#include <QHash>

class TbcSource;

namespace Ui {
class ExportDialog;
}

class ExportDialog : public QWidget
{
    Q_OBJECT

public:
    enum class RunStage {
        Idle,
        MainExport,
        ProxyExport
    };
    struct ExportProcessStat {
        QString process;
        QString tbcType;
        QString trackedName;
        QString current;
        QString total;
        QString errors;
        QString fps;
    };
    explicit ExportDialog(QWidget *parent = nullptr);
    ~ExportDialog();

    void setSource(TbcSource *source);
    void setInPoint(int frameNumber);
    void setOutPoint(int frameNumber);
    void refreshResolutionOptions();

private slots:
    void on_outputBrowseButton_clicked();
    void on_audio1BrowseButton_clicked();
    void on_audio2BrowseButton_clicked();
    void on_audio3BrowseButton_clicked();
    void on_audio4BrowseButton_clicked();
    void on_exportButton_clicked();
    void on_cancelButton_clicked();

    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void handleProcessStdout();
    void handleProcessStderr();
    void handleParallelProxyProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleParallelProxyProcessError(QProcess::ProcessError error);
    void handleParallelProxyProcessStdout();
    void handleParallelProxyProcessStderr();

private:
    void syncResolutionModeSelectionFromSource();
    void refreshOutputResolutionModeOptions();
    void updateOutputResolutionModeControlsEnabledState(bool enabled);
    void updateFromSource();
    void refreshProfiles();
    void updateProfileDependentControls();
    void setBusy(bool busy);
    void updateRangeControlsForSource(bool resetToFullRange);
    void updateRangeLengthLabel();
    double timecodeFrameRate() const;
    int timecodeFrameBaseRate() const;
    QString frameToTimecode(int frameNumber) const;
    int timecodeToFrame(const QString &timecodeText, bool *ok = nullptr) const;
    void syncRangeTimecodeEditors();
    QString resolveVideoExportPath() const;
    QString resolveFfmpegPath() const;
    QString resolveLdChromaDecoderPath() const;
    QString defaultOutputBaseName(const QString &inputFile) const;
    QString sanitizeOutputBaseName(const QString &path) const;
    bool findExistingOutputFiles(const QString &outputBase, QStringList *existingFiles) const;
    QString selectedMainCodecId() const;
    QString selectedMainContainerId() const;
    int selectedMainBitDepth() const;
    QString selectedExportProfileName() const;
    QString proxyExportProfileName(const QString &proxyCodecId) const;
    QString videoSystemArg(int system) const;
    QStringList collectAudioTracks() const;
    bool shouldGenerateProxyForSelection() const;
    QString selectedProxyCodecId() const;
    QString proxyOutputPath(const QString &outputBase, const QString &proxyCodecId) const;
    QString findProxySourceVideoPath(const QString &outputBase, QString *errorMessage) const;
    QStringList buildProxyArguments(QString *errorMessage,
                                    const QString &ffmpegPath,
                                    const QString &sourceVideoPath,
                                    const QString &proxyOutputPath,
                                    const QString &proxyCodecId,
                                    bool overwriteExisting) const;
    bool startProxyExport(QString *errorMessage);
    void clearRunState();
    bool prepareTrimmedAudioTracks(int zeroBasedStartFrame, int rangeLengthFrames,
                                   QStringList *audioTracks, QString *errorMessage);
    QStringList buildArguments(QString *errorMessage, const QString &inputTbcJsonOverride = QString(),
                               bool overwriteExisting = false,
                               const QString &configFileOverride = QString(),
                               const QStringList &audioTracks = QStringList(),
                               int startFrameOneBasedOverride = -1,
                               int lengthOverride = -1,
                               const QString &profileOverride = QString(),
                               const QString &outputBaseOverride = QString()) const;
    QString createTemporaryExportConfig(QString *errorMessage,
                                        const QString &profileName,
                                        const QString &audioProfileName);
    QString createTemporaryMetadataSnapshot(QString *errorMessage);
    void cleanupTemporaryMetadataSnapshot();
    void appendStatus(const QString &message);
    void appendLog(const QString &message);
    QString formatCommand(const QString &program, const QStringList &args) const;
    void resetProcessStats();
    void initializeProcessStats();
    void updateProcessStat(const ExportProcessStat &stat);
    void processOutputLine(const QString &line, QString *lastStatus);
    void consumeProcessOutputChunk(const QString &chunk, QString *pendingBuffer);
    void flushPendingProcessOutput();

    Ui::ExportDialog *ui;
    TbcSource *tbcSource = nullptr;
    QProcess *exportProcess = nullptr;
    QProcess *parallelProxyProcess = nullptr;
    QString currentInputFile;
    QString processStdout;
    QString processStderr;
    QString pendingStdoutBuffer;
    QString pendingStderrBuffer;
    QString parallelProxyStdout;
    QString parallelProxyStderr;
    QString pendingParallelProxyStdoutBuffer;
    QString pendingParallelProxyStderrBuffer;
    QString temporaryInputJsonPath;
    QString temporaryExportConfigPath;
    QStringList temporaryAudioTrackPaths;
    RunStage activeRunStage = RunStage::Idle;
    bool generateProxyForCurrentRun = false;
    bool overwriteExistingForCurrentRun = false;
    QString outputBaseForCurrentRun;
    QString proxyCodecForCurrentRun;
    QString proxyOutputPathForCurrentRun;
    bool outputAutoSet = true;
    bool exportAvailable = false;
    bool cancelRequested = false;
    bool parallelProxyRunning = false;
    bool parallelProxyFinished = false;
    bool parallelProxySucceeded = false;
    bool mainExportFinished = false;
    bool mainExportSucceeded = false;
    QHash<QString, int> processRowMap;
};

#endif // EXPORTDIALOG_H
