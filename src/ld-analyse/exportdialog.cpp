#include "exportdialog.h"
#include "ui_exportdialog.h"

#include "tbcsource.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QLineEdit>
#include <QComboBox>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QTimer>
#include <signal.h>

namespace {
void setTableItem(QTableWidget *table, int row, int column, const QString &text)
{
    if (!table) {
        return;
    }
    QTableWidgetItem *item = table->item(row, column);
    if (!item) {
        item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    }
    item->setText(text);
}
QString stripAnsiSequences(const QString &input)
{
    static const QRegularExpression ansiPattern(QStringLiteral("\\x1B\\[[0-9;?]*[A-Za-z]"));
    QString cleaned = input;
    cleaned.replace(ansiPattern, QString());
    return cleaned;
}
bool isIgnorableLogLine(const QString &line)
{
    return line.contains(QStringLiteral("No support for ANSI escape sequences"), Qt::CaseInsensitive);
}
QString quoteArgument(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    if (arg.contains(QRegularExpression(QStringLiteral("[\\s\"]")))) {
        QString escaped = arg;
        escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return arg;
}

QString shellQuoteArgument(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("''");
    }
    QString escaped = arg;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString formatShellCommand(const QString &program, const QStringList &args)
{
    QStringList parts;
    parts << shellQuoteArgument(program);
    for (const QString &arg : args) {
        parts << shellQuoteArgument(arg);
    }
    return parts.join(' ');
}

QWidget *dialogParentWidget(QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }

    QWidget *window = widget->window();
    return window ? window : widget;
}

QStringList dialogNameFilters(const QString &filters)
{
    return filters.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
}

void applyCommonFileDialogOptions(QFileDialog *dialog)
{
    if (!dialog) {
        return;
    }

    dialog->setOption(QFileDialog::DontResolveSymlinks, true);
#if defined(Q_OS_MACOS)
    dialog->setOption(QFileDialog::DontUseNativeDialog, true);
#endif
}

QString runOpenFileDialog(QWidget *parent,
                          const QString &title,
                          const QString &startPath,
                          const QString &filters)
{
    QFileDialog dialog(dialogParentWidget(parent), title, startPath);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilters(dialogNameFilters(filters));
    applyCommonFileDialogOptions(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return QString();
    }
    return dialog.selectedFiles().constFirst();
}

QString runDirectoryDialog(QWidget *parent,
                           const QString &title,
                           const QString &startPath)
{
    QFileDialog dialog(dialogParentWidget(parent), title, startPath);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    applyCommonFileDialogOptions(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return QString();
    }
    return dialog.selectedFiles().constFirst();
}

QStringList defaultExecutableSearchDirs(const QString &currentInputFile)
{
    QStringList candidateDirs;
    auto appendUniqueDir = [&candidateDirs](const QString &dirPath) {
        if (dirPath.isEmpty()) {
            return;
        }
        for (const QString &existing : candidateDirs) {
            if (existing.compare(dirPath, Qt::CaseInsensitive) == 0) {
                return;
            }
        }
        candidateDirs << dirPath;
    };

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        appendUniqueDir(appDir);

        QDir parentDir(appDir);
        for (int i = 0; i < 4; ++i) {
            if (!parentDir.cdUp()) {
                break;
            }
            appendUniqueDir(parentDir.absolutePath());
        }
    }

    const QString currentDir = QDir::currentPath();
    if (!currentDir.isEmpty()) {
        appendUniqueDir(currentDir);
        appendUniqueDir(QDir(currentDir).filePath(QStringLiteral("build/bin")));
    }

    if (!currentInputFile.isEmpty()) {
        const QString inputDir = QFileInfo(currentInputFile).absolutePath();
        appendUniqueDir(inputDir);
    }

    const QString homeDir = QDir::homePath();
    if (!homeDir.isEmpty()) {
        const QString localBin = QDir(homeDir).filePath(QStringLiteral(".local/bin"));
        const QString userBin = QDir(homeDir).filePath(QStringLiteral("bin"));
        appendUniqueDir(localBin);
        appendUniqueDir(userBin);
    }

#if defined(Q_OS_MACOS)
    const QStringList platformDirs = {
        QStringLiteral("/opt/homebrew/bin"),
        QStringLiteral("/opt/homebrew/sbin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/usr/local/sbin"),
        QStringLiteral("/usr/bin"),
        QStringLiteral("/bin")
    };
#elif defined(Q_OS_UNIX)
    const QStringList platformDirs = {
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/usr/local/sbin"),
        QStringLiteral("/usr/bin"),
        QStringLiteral("/bin"),
        QStringLiteral("/snap/bin")
    };
#else
    const QStringList platformDirs;
#endif

    for (const QString &dir : platformDirs) {
        appendUniqueDir(dir);
    }

    return candidateDirs;
}

void prependUniquePathEntries(QStringList *pathEntries, const QStringList &prependDirs)
{
    if (!pathEntries) {
        return;
    }

    for (auto it = prependDirs.crbegin(); it != prependDirs.crend(); ++it) {
        if (!it->isEmpty() && !pathEntries->contains(*it)) {
            pathEntries->prepend(*it);
        }
    }
}

QStringList parseProfiles(const QString &rawOutput, QString *defaultProfile)
{
    QStringList profiles;
    const QString cleaned = stripAnsiSequences(rawOutput);
    const QStringList lines = cleaned.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QStringLiteral("--"))) {
            continue;
        }
        QString name = trimmed.mid(2).trimmed();
        const int spaceIndex = name.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (spaceIndex > 0) {
            name = name.left(spaceIndex);
        }
        if (name.isEmpty()) {
            continue;
        }
        if (defaultProfile && trimmed.contains(QStringLiteral("default"), Qt::CaseInsensitive)) {
            *defaultProfile = name;
        }
        if (!profiles.contains(name)) {
            profiles << name;
        }
    }
    if (!profiles.isEmpty()) {
        return profiles;
    }

    const QRegularExpression profilePattern(QStringLiteral("--([A-Za-z0-9_\\-]+)"));
    QRegularExpressionMatchIterator matches = profilePattern.globalMatch(cleaned);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString name = match.captured(1);
        if (!name.isEmpty() && !profiles.contains(name)) {
            profiles << name;
        }
    }
    return profiles;
}


struct ActiveAreaFrameDefaults {
    int ffll = 0;
    int lfll = 0;
    int ffrl = 0;
    int lfrl = 0;
};

ActiveAreaFrameDefaults activeAreaDefaultsForSystem(int system)
{
    switch (system) {
    case PAL:
    case PAL_M:
        return {22, 308, 44, 620};
    case NTSC:
    default:
        return {20, 259, 40, 525};
    }
}


bool hasExplicitVerticalFraming(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    return videoParameters.firstActiveFieldLine > 0
           && videoParameters.lastActiveFieldLine > 0
           && videoParameters.firstActiveFrameLine > 0
           && videoParameters.lastActiveFrameLine > 0;
}

bool usesCustomVerticalFraming(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    if (!videoParameters.isValid || !hasExplicitVerticalFraming(videoParameters)) {
        return false;
    }

    const ActiveAreaFrameDefaults defaults = activeAreaDefaultsForSystem(videoParameters.system);
    return videoParameters.firstActiveFieldLine != defaults.ffll
           || videoParameters.lastActiveFieldLine != defaults.lfll
           || videoParameters.firstActiveFrameLine != defaults.ffrl
           || videoParameters.lastActiveFrameLine != defaults.lfrl;
}

bool hasAnyNonDefaultVerticalFraming(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    if (!videoParameters.isValid) {
        return false;
    }

    const ActiveAreaFrameDefaults defaults = activeAreaDefaultsForSystem(videoParameters.system);
    return (videoParameters.firstActiveFieldLine > 0
            && videoParameters.firstActiveFieldLine != defaults.ffll)
           || (videoParameters.lastActiveFieldLine > 0
               && videoParameters.lastActiveFieldLine != defaults.lfll)
           || (videoParameters.firstActiveFrameLine > 0
               && videoParameters.firstActiveFrameLine != defaults.ffrl)
           || (videoParameters.lastActiveFrameLine > 0
               && videoParameters.lastActiveFrameLine != defaults.lfrl);
}

double frameRateForSystem(int system)
{
    switch (system) {
    case PAL:
        return 25.0;
    case PAL_M:
    case NTSC:
    default:
        return 30000.0 / 1001.0;
    }
}

bool isFfv1ProfileName(const QString &profileName)
{
    return profileName.trimmed().compare(QStringLiteral("ffv1"), Qt::CaseInsensitive) == 0;
}

const QList<int> &supportedFfv1SlicesValues()
{
    static const QList<int> values = {4, 6, 8, 9, 12, 15, 16, 18, 20, 24, 25, 28, 30, 32};
    return values;
}

bool isSupportedFfv1SlicesValue(int slices)
{
    return supportedFfv1SlicesValues().contains(slices);
}

QString supportedFfv1SlicesValuesText()
{
    QStringList valuesText;
    const QList<int> values = supportedFfv1SlicesValues();
    valuesText.reserve(values.size());
    for (const int value : values) {
        valuesText << QString::number(value);
    }
    return valuesText.join(QStringLiteral(", "));
}

QStringList profileVideoProfileNames(const QJsonObject &profileObject)
{
    QStringList names;
    const QJsonValue videoProfileValue = profileObject.value(QStringLiteral("video_profile"));
    if (videoProfileValue.isString()) {
        const QString name = videoProfileValue.toString().trimmed();
        if (!name.isEmpty()) {
            names << name;
        }
    } else if (videoProfileValue.isArray()) {
        const QJsonArray values = videoProfileValue.toArray();
        for (const QJsonValue &value : values) {
            if (!value.isString()) {
                continue;
            }
            const QString name = value.toString().trimmed();
            if (!name.isEmpty() && !names.contains(name)) {
                names << name;
            }
        }
    }
    return names;
}

void setOrAppendNumericOption(QJsonObject *videoProfileObject, const QString &optionName, int optionValue)
{
    if (!videoProfileObject) {
        return;
    }

    QJsonArray opts = videoProfileObject->value(QStringLiteral("opts")).toArray();
    for (int i = 0; i + 1 < opts.size(); ++i) {
        if (!opts.at(i).isString() || opts.at(i).toString() != optionName) {
            continue;
        }
        opts.replace(i + 1, optionValue);
        videoProfileObject->insert(QStringLiteral("opts"), opts);
        return;
    }

    opts.append(optionName);
    opts.append(optionValue);
    videoProfileObject->insert(QStringLiteral("opts"), opts);
}


bool parseRealtimeProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }
    static const QRegularExpression progressPattern(
        QStringLiteral("^\\s*[/\\\\\\-|●○]?\\s*([A-Za-z0-9_.\\-]+)\\s*(?:\\(([^)]+)\\))?\\s*([A-Za-z0-9_%/\\-]+)\\s*:\\s*([0-9,]+)(?:/([0-9,]+))?.*?errors:\\s*([0-9,]+)(?:.*?fps:\\s*([0-9.,]+))?"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = progressPattern.match(line);
    if (!match.hasMatch()) {
        return false;
    }
    stat->process = match.captured(1).trimmed();
    stat->tbcType = match.captured(2).trimmed();
    stat->trackedName = match.captured(3).trimmed();
    stat->current = match.captured(4).trimmed();
    stat->total = match.captured(5).trimmed();
    stat->errors = match.captured(6).trimmed();
    stat->fps = match.captured(7).trimmed();
    if (stat->tbcType.isEmpty()) {
        stat->tbcType = QStringLiteral("—");
    }
    if (stat->trackedName.isEmpty()) {
        stat->trackedName = QStringLiteral("—");
    }
    if (stat->total.isEmpty()) {
        stat->total = QStringLiteral("—");
    }
    if (stat->errors.isEmpty()) {
        stat->errors = QStringLiteral("0");
    }
    if (stat->fps.isEmpty()) {
        stat->fps = QStringLiteral("—");
    }
    return !stat->process.isEmpty();
}
bool parseFfmpegProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }
    static const QRegularExpression ffmpegPattern(
        QStringLiteral("frame\\s*=\\s*([0-9,]+)\\s+fps\\s*=\\s*([0-9.,]+)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator matches = ffmpegPattern.globalMatch(line);
    if (!matches.hasNext()) {
        return false;
    }
    QRegularExpressionMatch match;
    while (matches.hasNext()) {
        match = matches.next();
    }

    stat->process = QStringLiteral("ffmpeg");
    stat->tbcType = QStringLiteral("—");
    stat->trackedName = QStringLiteral("frame");
    stat->current = match.captured(1).trimmed();
    stat->total = QStringLiteral("—");
    stat->errors = QStringLiteral("0");
    stat->fps = match.captured(2).trimmed();
    return !stat->current.isEmpty();
}

bool parseInfoProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }

    static const QRegularExpression framesProcessedPattern(
        QStringLiteral("^\\s*Info:\\s*([0-9,]+)\\s+frames\\s+processed\\s*-\\s*([0-9.,]+)\\s+FPS\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = framesProcessedPattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-chroma-decoder");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = match.captured(1).trimmed();
        stat->total = QStringLiteral("—");
        stat->errors = QStringLiteral("0");
        stat->fps = match.captured(2).trimmed();
        return !stat->current.isEmpty();
    }

    static const QRegularExpression writtenFramePattern(
        QStringLiteral("^\\s*Info:\\s*Processed\\s+and\\s+written\\s+frame\\s+([0-9,]+)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    match = writtenFramePattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-chroma-decoder");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = match.captured(1).trimmed();
        stat->total = QStringLiteral("—");
        stat->errors = QStringLiteral("0");
        stat->fps = QStringLiteral("—");
        return !stat->current.isEmpty();
    }

    static const QRegularExpression startLengthPattern(
        QStringLiteral("^\\s*Info:\\s*Processing\\s+from\\s+start\\s+frame\\s*#\\s*([0-9,]+)\\s+with\\s+a\\s+length\\s+of\\s+([0-9,]+)\\s+frames\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    match = startLengthPattern.match(line);
    if (match.hasMatch()) {
        const QString startFrameText = match.captured(1).trimmed();
        const QString totalText = match.captured(2).trimmed();
        QString startFrameDigits = startFrameText;
        bool startOk = false;
        const int startFrame = startFrameDigits.remove(QLatin1Char(',')).toInt(&startOk);
        stat->process = QStringLiteral("ld-chroma-decoder");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = startOk && startFrame > 0 ? QString::number(startFrame - 1) : QStringLiteral("0");
        stat->total = totalText.isEmpty() ? QStringLiteral("—") : totalText;
        stat->errors = QStringLiteral("0");
        stat->fps = QStringLiteral("—");
        return true;
    }

    static const QRegularExpression dropoutWorkloadPattern(
        QStringLiteral("^\\s*Info:\\s*Using\\s+[0-9,]+\\s+threads\\s+to\\s+process\\s+([0-9,]+)\\s+frames\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    match = dropoutWorkloadPattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-dropout-correct");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = QStringLiteral("0");
        stat->total = match.captured(1).trimmed();
        stat->errors = QStringLiteral("0");
        stat->fps = QStringLiteral("—");
        return true;
    }

    return false;
}

bool parseCompletionSummaryLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }

    static const QRegularExpression dropoutCompletePattern(
        QStringLiteral("^\\s*Info:\\s*Dropout correction complete\\s*-\\s*([0-9,]+)\\s+frames?\\s+in\\s+[0-9.]+\\s+seconds\\s*\\(\\s*([0-9.,]+)\\s+FPS\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = dropoutCompletePattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-dropout-correct");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = match.captured(1).trimmed();
        stat->total = stat->current;
        stat->errors = QStringLiteral("0");
        stat->fps = match.captured(2).trimmed();
        return !stat->current.isEmpty();
    }

    static const QRegularExpression processingCompletePattern(
        QStringLiteral("^\\s*Info:\\s*Processing complete\\s*-\\s*([0-9,]+)\\s+frames?\\s+in\\s+[0-9.]+\\s+seconds\\s*\\(\\s*([0-9.,]+)\\s+FPS\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    match = processingCompletePattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-chroma-decoder");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = match.captured(1).trimmed();
        stat->total = stat->current;
        stat->errors = QStringLiteral("0");
        stat->fps = match.captured(2).trimmed();
        return !stat->current.isEmpty();
    }

    return false;
}

bool parseProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    return parseRealtimeProgressLine(line, stat)
        || parseInfoProgressLine(line, stat)
        || parseFfmpegProgressLine(line, stat)
        || parseCompletionSummaryLine(line, stat);
}

bool executableSupportsOption(const QString &program, const QString &option)
{
    if (program.isEmpty() || option.isEmpty()) {
        return false;
    }

    static QHash<QString, bool> supportCache;
    const QString cacheKey = program + QStringLiteral("|") + option;
    if (supportCache.contains(cacheKey)) {
        return supportCache.value(cacheKey);
    }

    QProcess helpProcess;
    helpProcess.setProcessChannelMode(QProcess::MergedChannels);
    helpProcess.start(program, QStringList() << QStringLiteral("--help"));

    bool supportsOption = false;
    if (helpProcess.waitForStarted(3000) && helpProcess.waitForFinished(5000)) {
        const QString helpOutput = QString::fromLocal8Bit(helpProcess.readAllStandardOutput());
        supportsOption = helpOutput.contains(option);
    }

    supportCache.insert(cacheKey, supportsOption);
    return supportsOption;
}

}

ExportDialog::ExportDialog(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ExportDialog)
{
    ui->setupUi(this);

    ui->inputLineEdit->setReadOnly(true);
    ui->profileComboBox->setEditable(false);
    if (ui->resolutionModeComboBox) {
        ui->resolutionModeComboBox->clear();
        ui->resolutionModeComboBox->addItem(tr("Active Area"), QStringLiteral("active_area"));
        ui->resolutionModeComboBox->addItem(tr("Active + VBI"), QStringLiteral("active_vbi"));
        ui->resolutionModeComboBox->addItem(tr("Full-Frame 4fsc"), QStringLiteral("full_frame_4fsc"));
        ui->resolutionModeComboBox->addItem(tr("User Defined"), QStringLiteral("user_defined"));
        const int activeAreaIndex = ui->resolutionModeComboBox->findData(QStringLiteral("active_area"));
        ui->resolutionModeComboBox->setCurrentIndex(activeAreaIndex >= 0 ? activeAreaIndex : 0);
    }
    if (ui->audioProfileComboBox) {
        ui->audioProfileComboBox->clear();
        ui->audioProfileComboBox->addItem(tr("FLAC 16-bit"), QStringLiteral("flac_16"));
        ui->audioProfileComboBox->addItem(tr("FLAC 24-bit"), QStringLiteral("flac_24"));
        ui->audioProfileComboBox->addItem(tr("PCM 16-bit"), QStringLiteral("pcm_16"));
        ui->audioProfileComboBox->addItem(tr("PCM 24-bit"), QStringLiteral("pcm_24"));
        const int pcm24Index = ui->audioProfileComboBox->findData(QStringLiteral("pcm_24"));
        ui->audioProfileComboBox->setCurrentIndex(pcm24Index >= 0 ? pcm24Index : 0);
    }
    if (ui->ffv1SlicesSpinBox) {
        ui->ffv1SlicesSpinBox->setMinimum(1);
        ui->ffv1SlicesSpinBox->setMaximum(32);
        ui->ffv1SlicesSpinBox->setValue(4);
        const QString tooltipText =
            tr("Supported FFV1 slice counts: %1").arg(supportedFfv1SlicesValuesText());
        ui->ffv1SlicesSpinBox->setToolTip(tooltipText);
        if (ui->ffv1SlicesLabel) {
            ui->ffv1SlicesLabel->setToolTip(tooltipText);
        }
    }
    if (ui->inPointTimecodeLineEdit) {
        ui->inPointTimecodeLineEdit->setPlaceholderText(tr("HH:MM:SS:FF"));
        ui->inPointTimecodeLineEdit->setMaxLength(15);
        ui->inPointTimecodeLineEdit->setAlignment(Qt::AlignCenter);
        QFont inFont = ui->inPointTimecodeLineEdit->font();
        inFont.setPointSize(qMax(10, inFont.pointSize() + 1));
        ui->inPointTimecodeLineEdit->setFont(inFont);
    }
    if (ui->outPointTimecodeLineEdit) {
        ui->outPointTimecodeLineEdit->setPlaceholderText(tr("HH:MM:SS:FF"));
        ui->outPointTimecodeLineEdit->setMaxLength(15);
        ui->outPointTimecodeLineEdit->setAlignment(Qt::AlignCenter);
        QFont outFont = ui->outPointTimecodeLineEdit->font();
        outFont.setPointSize(qMax(10, outFont.pointSize() + 1));
        ui->outPointTimecodeLineEdit->setFont(outFont);
    }
    if (ui->rangeLengthValueLabel) {
        QFont durationFont = ui->rangeLengthValueLabel->font();
        durationFont.setPointSize(qMax(10, durationFont.pointSize() + 1));
        ui->rangeLengthValueLabel->setFont(durationFont);
    }
    if (ui->actionLayout) {
        if (ui->actionSpacer) {
            ui->actionSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        }
        if (ui->outPointLabel && ui->outPointTimecodeLineEdit) {
            ui->actionLayout->removeWidget(ui->outPointLabel);
            const int outEditIndex = ui->actionLayout->indexOf(ui->outPointTimecodeLineEdit);
            ui->actionLayout->insertWidget(qMax(0, outEditIndex), ui->outPointLabel);
        }
        if (ui->rangeLengthLabel && ui->rangeLengthValueLabel && ui->outPointTimecodeLineEdit) {
            ui->actionLayout->removeWidget(ui->rangeLengthLabel);
            ui->actionLayout->removeWidget(ui->rangeLengthValueLabel);
            const int outEditIndex = ui->actionLayout->indexOf(ui->outPointTimecodeLineEdit);
            int insertIndex = qMax(0, outEditIndex + 1);
            ui->actionLayout->insertWidget(insertIndex++, ui->rangeLengthLabel);
            ui->actionLayout->insertWidget(insertIndex, ui->rangeLengthValueLabel);
        }
        if (ui->cancelButton) {
            const int cancelIndex = ui->actionLayout->indexOf(ui->cancelButton);
            if (cancelIndex >= 0) {
                ui->actionLayout->insertStretch(cancelIndex, 1);
            }
        }
        ui->actionLayout->invalidate();
    }
    if (ui->inPointSpinBox && ui->outPointSpinBox) {
        ui->inPointSpinBox->setMinimum(1);
        ui->inPointSpinBox->setMaximum(1);
        ui->inPointSpinBox->setValue(1);
        ui->outPointSpinBox->setMinimum(1);
        ui->outPointSpinBox->setMaximum(1);
        ui->outPointSpinBox->setValue(1);

        connect(ui->inPointSpinBox, &QSpinBox::valueChanged, this, [this](int value) {
            if (!ui->outPointSpinBox) {
                return;
            }
            if (ui->outPointSpinBox->value() < value) {
                ui->outPointSpinBox->setValue(value);
            }
            updateRangeLengthLabel();
            syncRangeTimecodeEditors();
        });
        connect(ui->outPointSpinBox, &QSpinBox::valueChanged, this, [this](int value) {
            if (!ui->inPointSpinBox) {
                return;
            }
            if (value < ui->inPointSpinBox->value()) {
                ui->inPointSpinBox->setValue(value);
            }
            updateRangeLengthLabel();
            syncRangeTimecodeEditors();
        });
    }
    if (ui->inPointSpinBox && ui->outPointSpinBox
        && ui->inPointTimecodeLineEdit && ui->outPointTimecodeLineEdit) {
        ui->inPointSpinBox->setVisible(false);
        ui->outPointSpinBox->setVisible(false);
        connect(ui->inPointTimecodeLineEdit, &QLineEdit::editingFinished, this, [this]() {
            if (!ui || !ui->inPointTimecodeLineEdit || !ui->inPointSpinBox || !ui->outPointSpinBox) {
                return;
            }
            bool ok = false;
            int inFrame = timecodeToFrame(ui->inPointTimecodeLineEdit->text(), &ok);
            if (!ok) {
                syncRangeTimecodeEditors();
                appendStatus(tr("Invalid In timecode. Use HH:MM:SS:FF."));
                return;
            }
            inFrame = qBound(ui->inPointSpinBox->minimum(), inFrame, ui->inPointSpinBox->maximum());
            ui->inPointSpinBox->setValue(inFrame);
            if (ui->outPointSpinBox->value() < inFrame) {
                ui->outPointSpinBox->setValue(inFrame);
            }
            syncRangeTimecodeEditors();
        });
        connect(ui->outPointTimecodeLineEdit, &QLineEdit::editingFinished, this, [this]() {
            if (!ui || !ui->outPointTimecodeLineEdit || !ui->outPointSpinBox || !ui->inPointSpinBox) {
                return;
            }
            bool ok = false;
            int outFrame = timecodeToFrame(ui->outPointTimecodeLineEdit->text(), &ok);
            if (!ok) {
                syncRangeTimecodeEditors();
                appendStatus(tr("Invalid Out timecode. Use HH:MM:SS:FF."));
                return;
            }
            outFrame = qBound(ui->outPointSpinBox->minimum(), outFrame, ui->outPointSpinBox->maximum());
            ui->outPointSpinBox->setValue(outFrame);
            if (outFrame < ui->inPointSpinBox->value()) {
                ui->inPointSpinBox->setValue(outFrame);
            }
            syncRangeTimecodeEditors();
        });
    }
    updateRangeLengthLabel();
    if (ui->processStatsTable) {
        ui->processStatsTable->setColumnCount(7);
        ui->processStatsTable->setHorizontalHeaderLabels({
            tr("Process"),
            tr("TBC"),
            tr("Track"),
            tr("Current"),
            tr("Total"),
            tr("Errors"),
            tr("FPS")
        });
        ui->processStatsTable->verticalHeader()->setVisible(false);
        ui->processStatsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->processStatsTable->setSelectionMode(QAbstractItemView::NoSelection);
        ui->processStatsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->processStatsTable->setAlternatingRowColors(true);
        ui->processStatsTable->horizontalHeader()->setStretchLastSection(true);
        ui->processStatsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui->processStatsTable->setMinimumHeight(90);
    }
    if (ui->logTextEdit) {
        ui->logTextEdit->setMaximumBlockCount(3);
        const int lineHeight = QFontMetrics(ui->logTextEdit->font()).lineSpacing();
        ui->logTextEdit->setFixedHeight(lineHeight * 3 + 8);
    }
    if (ui->cancelButton) {
        ui->cancelButton->setEnabled(false);
    }
    connect(ui->outputLineEdit, &QLineEdit::textEdited, this, [this]() {
        outputAutoSet = false;
    });
    if (ui->profileComboBox) {
        connect(ui->profileComboBox, &QComboBox::currentTextChanged, this,
                [this](const QString &) { updateProfileDependentControls(); });
    }

    exportProcess = new QProcess(this);
    connect(exportProcess, &QProcess::finished, this, &ExportDialog::handleProcessFinished);
    connect(exportProcess, &QProcess::errorOccurred, this, &ExportDialog::handleProcessError);
    connect(exportProcess, &QProcess::readyReadStandardOutput, this, &ExportDialog::handleProcessStdout);
    connect(exportProcess, &QProcess::readyReadStandardError, this, &ExportDialog::handleProcessStderr);
    connect(exportProcess, &QProcess::started, this, [this]() {
        appendLog(tr("tbc-video-export started (PID %1).").arg(exportProcess->processId()));
    });
    updateProfileDependentControls();

    appendStatus(tr("Ready."));
    appendLog(tr("Ready."));
}

ExportDialog::~ExportDialog()
{
    cleanupTemporaryMetadataSnapshot();
    delete ui;
}

void ExportDialog::setSource(TbcSource *source)
{
    tbcSource = source;
    updateFromSource();
    refreshProfiles();
    updateProfileDependentControls();
}

void ExportDialog::setInPoint(int frameNumber)
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    const int clamped = qBound(ui->inPointSpinBox->minimum(), frameNumber, ui->inPointSpinBox->maximum());
    ui->inPointSpinBox->setValue(clamped);
    if (ui->outPointSpinBox->value() < clamped) {
        ui->outPointSpinBox->setValue(clamped);
    }
    updateRangeLengthLabel();
    syncRangeTimecodeEditors();
}

void ExportDialog::setOutPoint(int frameNumber)
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    const int clamped = qBound(ui->outPointSpinBox->minimum(), frameNumber, ui->outPointSpinBox->maximum());
    ui->outPointSpinBox->setValue(clamped);
    if (clamped < ui->inPointSpinBox->value()) {
        ui->inPointSpinBox->setValue(clamped);
    }
    updateRangeLengthLabel();
    syncRangeTimecodeEditors();
}

void ExportDialog::updateRangeControlsForSource(bool resetToFullRange)
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }

    const bool canUseRange = exportAvailable && tbcSource && tbcSource->getIsSourceLoaded() && !tbcSource->getIsMetadataOnly();
    if (!canUseRange) {
        const QSignalBlocker blockIn(ui->inPointSpinBox);
        const QSignalBlocker blockOut(ui->outPointSpinBox);
        ui->inPointSpinBox->setMinimum(1);
        ui->inPointSpinBox->setMaximum(1);
        ui->inPointSpinBox->setValue(1);
        ui->outPointSpinBox->setMinimum(1);
        ui->outPointSpinBox->setMaximum(1);
        ui->outPointSpinBox->setValue(1);
        updateRangeLengthLabel();
        syncRangeTimecodeEditors();
        return;
    }

    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    int inPoint = ui->inPointSpinBox->value();
    int outPoint = ui->outPointSpinBox->value();
    if (resetToFullRange) {
        inPoint = 1;
        outPoint = totalFrames;
    } else {
        inPoint = qBound(1, inPoint, totalFrames);
        outPoint = qBound(1, outPoint, totalFrames);
        if (outPoint < inPoint) {
            outPoint = inPoint;
        }
    }

    const QSignalBlocker blockIn(ui->inPointSpinBox);
    const QSignalBlocker blockOut(ui->outPointSpinBox);
    ui->inPointSpinBox->setMinimum(1);
    ui->inPointSpinBox->setMaximum(totalFrames);
    ui->inPointSpinBox->setValue(inPoint);
    ui->outPointSpinBox->setMinimum(1);
    ui->outPointSpinBox->setMaximum(totalFrames);
    ui->outPointSpinBox->setValue(outPoint);

    updateRangeLengthLabel();
    syncRangeTimecodeEditors();
}

void ExportDialog::updateRangeLengthLabel()
{
    if (!ui || !ui->rangeLengthValueLabel || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    if (!exportAvailable) {
        ui->rangeLengthValueLabel->setText(QStringLiteral("—"));
        return;
    }
    const int inPoint = ui->inPointSpinBox->value();
    const int outPoint = ui->outPointSpinBox->value();
    const qint64 frameCount = qMax<qint64>(0, static_cast<qint64>(outPoint) - static_cast<qint64>(inPoint) + 1);
    const int fps = qMax(1, timecodeFrameRate());
    const qint64 totalSeconds = frameCount / fps;
    const qint64 framePart = frameCount % fps;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    ui->rangeLengthValueLabel->setText(QStringLiteral("%1:%2:%3:%4")
                                           .arg(hours, 2, 10, QChar('0'))
                                           .arg(minutes, 2, 10, QChar('0'))
                                           .arg(seconds, 2, 10, QChar('0'))
                                           .arg(framePart, 2, 10, QChar('0')));
}

int ExportDialog::timecodeFrameRate() const
{
    int system = NTSC;
    if (tbcSource && tbcSource->getIsSourceLoaded()) {
        system = tbcSource->getVideoParameters().system;
    }
    switch (static_cast<VideoSystem>(system)) {
    case PAL:
        return 25;
    case PAL_M:
    case NTSC:
    default:
        return 30;
    }
}

QString ExportDialog::frameToTimecode(int frameNumber) const
{
    const int fps = qMax(1, timecodeFrameRate());
    const qint64 frameIndex = qMax<qint64>(0, static_cast<qint64>(frameNumber) - 1);
    const qint64 totalSeconds = frameIndex / fps;
    const qint64 framePart = frameIndex % fps;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(framePart, 2, 10, QChar('0'));
}

int ExportDialog::timecodeToFrame(const QString &timecodeText, bool *ok) const
{
    if (ok) {
        *ok = false;
    }
    const QString trimmed = timecodeText.trimmed();
    if (trimmed.isEmpty()) {
        return 1;
    }

    bool numericOk = false;
    const int numericFrame = trimmed.toInt(&numericOk);
    if (numericOk) {
        if (ok) {
            *ok = true;
        }
        return qMax(1, numericFrame);
    }

    static const QRegularExpression pattern(
        QStringLiteral("^\\s*(\\d+):(\\d{1,2}):(\\d{1,2}):(\\d{1,2})\\s*$"));
    const QRegularExpressionMatch match = pattern.match(trimmed);
    if (!match.hasMatch()) {
        return 1;
    }

    bool hoursOk = false;
    bool minutesOk = false;
    bool secondsOk = false;
    bool framesOk = false;
    const qint64 hours = match.captured(1).toLongLong(&hoursOk);
    const int minutes = match.captured(2).toInt(&minutesOk);
    const int seconds = match.captured(3).toInt(&secondsOk);
    const int frames = match.captured(4).toInt(&framesOk);
    const int fps = qMax(1, timecodeFrameRate());
    if (!hoursOk || !minutesOk || !secondsOk || !framesOk
        || minutes < 0 || minutes >= 60
        || seconds < 0 || seconds >= 60
        || frames < 0 || frames >= fps) {
        return 1;
    }

    const qint64 frameNumber = ((hours * 3600) + (minutes * 60) + seconds) * fps + frames + 1;
    if (ok) {
        *ok = true;
    }
    return static_cast<int>(qMax<qint64>(1, frameNumber));
}

void ExportDialog::syncRangeTimecodeEditors()
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    if (ui->inPointTimecodeLineEdit) {
        const QSignalBlocker blocker(ui->inPointTimecodeLineEdit);
        ui->inPointTimecodeLineEdit->setText(frameToTimecode(ui->inPointSpinBox->value()));
    }
    if (ui->outPointTimecodeLineEdit) {
        const QSignalBlocker blocker(ui->outPointTimecodeLineEdit);
        ui->outPointTimecodeLineEdit->setText(frameToTimecode(ui->outPointSpinBox->value()));
    }
}

void ExportDialog::updateFromSource()
{
    const bool hasSource = tbcSource && tbcSource->getIsSourceLoaded();
    const bool metadataOnly = hasSource && tbcSource->getIsMetadataOnly();
    exportAvailable = hasSource && !metadataOnly;

    if (!hasSource) {
        ui->inputLineEdit->clear();
        updateRangeControlsForSource(true);
        appendStatus(tr("No source loaded."));
        setBusy(exportProcess->state() != QProcess::NotRunning);
        return;
    }

    if (metadataOnly) {
        ui->inputLineEdit->clear();
        updateRangeControlsForSource(true);
        appendStatus(tr("Metadata-only mode (no TBC source)."));
        setBusy(exportProcess->state() != QProcess::NotRunning);
        return;
    }

    const QString inputFile = tbcSource->getCurrentSourceFilename();
    bool sourceChanged = false;
    if (!inputFile.isEmpty()) {
        if (currentInputFile != inputFile) {
            sourceChanged = true;
            currentInputFile = inputFile;
            ui->inputLineEdit->setText(inputFile);
            if (outputAutoSet || ui->outputLineEdit->text().trimmed().isEmpty()) {
                ui->outputLineEdit->setText(defaultOutputBaseName(inputFile));
                outputAutoSet = true;
            }
            if (ui->resolutionModeComboBox) {
                const LdDecodeMetaData::VideoParameters &videoParameters = tbcSource->getVideoParameters();
                QString modeData = QStringLiteral("active_area");
                if (tbcSource->getOutputConfiguration().fullFrameDecode) {
                    modeData = QStringLiteral("full_frame_4fsc");
                } else if (usesCustomVerticalFraming(videoParameters)) {
                    modeData = QStringLiteral("user_defined");
                }
                const int modeIndex = ui->resolutionModeComboBox->findData(modeData);
                if (modeIndex >= 0) {
                    ui->resolutionModeComboBox->setCurrentIndex(modeIndex);
                }
            }
        }
    }
    updateRangeControlsForSource(sourceChanged);

    setBusy(exportProcess->state() != QProcess::NotRunning);
    if (exportAvailable && exportProcess->state() == QProcess::NotRunning) {
        appendStatus(tr("Ready to export."));
    }
}

void ExportDialog::refreshProfiles()
{
    QString defaultProfile;
    QString errorMessage;
    QString profileOutput;

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        appendStatus(tr("tbc-video-export not found."));
        appendLog(tr("tbc-video-export not found."));
        updateProfileDependentControls();
        return;
    }

    QProcess listProcess;
    listProcess.setProcessChannelMode(QProcess::MergedChannels);
    listProcess.start(exportPath, QStringList() << QStringLiteral("--list-profiles"));
    if (!listProcess.waitForStarted(3000)) {
        appendStatus(tr("Failed to start profile listing."));
        appendLog(tr("Failed to start profile listing."));
        updateProfileDependentControls();
        return;
    }
    if (!listProcess.waitForFinished(10000)) {
        listProcess.kill();
        appendStatus(tr("Profile list timed out."));
        appendLog(tr("Profile list timed out."));
        updateProfileDependentControls();
        return;
    }
    profileOutput = QString::fromLocal8Bit(listProcess.readAllStandardOutput());

    if (listProcess.exitStatus() != QProcess::NormalExit || listProcess.exitCode() != 0) {
        errorMessage = profileOutput.trimmed();
        if (errorMessage.isEmpty()) {
            errorMessage = tr("Failed to list profiles.");
        }
        appendStatus(errorMessage);
        appendLog(errorMessage);
        updateProfileDependentControls();
        return;
    }
    const QString currentText = ui->profileComboBox->currentText().trimmed();
    const QStringList profiles = parseProfiles(profileOutput, &defaultProfile);

    if (profiles.isEmpty()) {
        appendStatus(tr("No profiles found."));
        appendLog(tr("No profiles found."));
        updateProfileDependentControls();
        return;
    }

    ui->profileComboBox->clear();
    ui->profileComboBox->addItems(profiles);

    if (!currentText.isEmpty() && profiles.contains(currentText)) {
        ui->profileComboBox->setCurrentText(currentText);
    } else if (!defaultProfile.isEmpty() && profiles.contains(defaultProfile)) {
        ui->profileComboBox->setCurrentText(defaultProfile);
    } else {
        ui->profileComboBox->setCurrentIndex(0);
    }
    updateProfileDependentControls();
}

void ExportDialog::updateProfileDependentControls()
{
    const bool ffv1Selected = ui && ui->profileComboBox
                              && isFfv1ProfileName(ui->profileComboBox->currentText());
    if (ui->ffv1SlicesLabel) {
        ui->ffv1SlicesLabel->setVisible(ffv1Selected);
    }
    if (ui->ffv1SlicesSpinBox) {
        ui->ffv1SlicesSpinBox->setVisible(ffv1Selected);
        const bool canEdit = ffv1Selected
                             && exportAvailable
                             && exportProcess
                             && exportProcess->state() == QProcess::NotRunning;
        ui->ffv1SlicesSpinBox->setEnabled(canEdit);
    }
}


void ExportDialog::on_outputBrowseButton_clicked()
{
    QString suggestedOutput = ui->outputLineEdit->text().trimmed();
    if (suggestedOutput.isEmpty() && !currentInputFile.isEmpty()) {
        suggestedOutput = defaultOutputBaseName(currentInputFile);
    }
    QFileInfo suggestedInfo(suggestedOutput);
    QString initialDirectory;
    if (!suggestedOutput.isEmpty()) {
        if (suggestedInfo.exists() && suggestedInfo.isDir()) {
            initialDirectory = suggestedInfo.absoluteFilePath();
        } else {
            initialDirectory = suggestedInfo.absolutePath();
        }
    }
    if (initialDirectory.isEmpty() && !currentInputFile.isEmpty()) {
        initialDirectory = QFileInfo(currentInputFile).absolutePath();
    }
    if (initialDirectory.isEmpty()) {
        initialDirectory = QDir::homePath();
    }
    const QString selectedDirectory = runDirectoryDialog(this,
                                                         tr("Select output directory"),
                                                         initialDirectory);
    if (selectedDirectory.isEmpty()) {
        return;
    }
    QString baseName = QFileInfo(suggestedOutput).completeBaseName();
    if (baseName.isEmpty() && !currentInputFile.isEmpty()) {
        baseName = QFileInfo(currentInputFile).completeBaseName();
    }
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("output");
    }

    ui->outputLineEdit->setText(QDir(selectedDirectory).filePath(baseName));
    outputAutoSet = false;
}

void ExportDialog::on_audio1BrowseButton_clicked()
{
    const QString selected = runOpenFileDialog(this,
                                               tr("Select audio track 1"),
                                               currentInputFile.isEmpty() ? QString() : QFileInfo(currentInputFile).absolutePath(),
                                               tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio1LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio2BrowseButton_clicked()
{
    const QString selected = runOpenFileDialog(this,
                                               tr("Select audio track 2"),
                                               currentInputFile.isEmpty() ? QString() : QFileInfo(currentInputFile).absolutePath(),
                                               tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio2LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio3BrowseButton_clicked()
{
    const QString selected = runOpenFileDialog(this,
                                               tr("Select audio track 3"),
                                               currentInputFile.isEmpty() ? QString() : QFileInfo(currentInputFile).absolutePath(),
                                               tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio3LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio4BrowseButton_clicked()
{
    const QString selected = runOpenFileDialog(this,
                                               tr("Select audio track 4"),
                                               currentInputFile.isEmpty() ? QString() : QFileInfo(currentInputFile).absolutePath(),
                                               tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio4LineEdit->setText(selected);
    }
}

void ExportDialog::on_exportButton_clicked()
{
    if (!exportAvailable) {
        if (!tbcSource || !tbcSource->getIsSourceLoaded()) {
            appendStatus(tr("Export unavailable: no source loaded."));
            appendLog(tr("Export unavailable: no source loaded."));
            QMessageBox::warning(this, tr("Error"), tr("No source file loaded."));
        } else if (tbcSource->getIsMetadataOnly()) {
            appendStatus(tr("Export unavailable: metadata-only mode."));
            appendLog(tr("Export unavailable: metadata-only mode."));
            QMessageBox::warning(this, tr("Error"), tr("Metadata-only mode does not include a TBC source file."));
        } else {
            appendStatus(tr("Export unavailable."));
            appendLog(tr("Export unavailable."));
            QMessageBox::warning(this, tr("Error"), tr("Export is not available."));
        }
        return;
    }
    if (exportProcess->state() != QProcess::NotRunning) {
        appendStatus(tr("Export already running."));
        appendLog(tr("Export already running."));
        return;
    }
    cancelRequested = false;
    appendStatus(tr("Starting export..."));
    appendLog(tr("Starting export..."));
    QString snapshotErrorMessage;
    const QString metadataSnapshotPath = createTemporaryMetadataSnapshot(&snapshotErrorMessage);
    if (metadataSnapshotPath.isEmpty()) {
        const QString errorToShow = snapshotErrorMessage.isEmpty()
                                        ? tr("Could not prepare metadata for export.")
                                        : snapshotErrorMessage;
        appendStatus(errorToShow);
        appendLog(errorToShow);
        QMessageBox::warning(this, tr("Error"), errorToShow);
        return;
    }
    bool overwriteExisting = false;
    const QString outputBase = sanitizeOutputBaseName(ui->outputLineEdit->text().trimmed());
    QStringList existingOutputs;
    if (findExistingOutputFiles(outputBase, &existingOutputs)) {
        const int previewCount = qMin(existingOutputs.size(), 6);
        QStringList previewLines;
        for (int i = 0; i < previewCount; ++i) {
            previewLines << existingOutputs.at(i);
        }
        if (existingOutputs.size() > previewCount) {
            previewLines << tr("... and %1 more").arg(existingOutputs.size() - previewCount);
        }

        const QString promptText = tr("Output file(s) already exist for this base name:\n%1\n\nOverwrite existing output file(s)?")
                                       .arg(previewLines.join(QLatin1Char('\n')));
        const QMessageBox::StandardButton overwriteChoice = QMessageBox::question(
            this, tr("Output already exists"), promptText, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (overwriteChoice != QMessageBox::Yes) {
            cleanupTemporaryMetadataSnapshot();
            appendStatus(tr("Export cancelled: output already exists."));
            appendLog(tr("Export cancelled by user: overwrite was not confirmed."));
            return;
        }
        overwriteExisting = true;
        appendLog(tr("Overwrite confirmed for existing output file(s)."));
    }
    QString configErrorMessage;
    const QString selectedProfile = ui->profileComboBox ? ui->profileComboBox->currentText().trimmed() : QString();
    const QString selectedAudioProfile =
        ui->audioProfileComboBox ? ui->audioProfileComboBox->currentData().toString() : QString();
    if (isFfv1ProfileName(selectedProfile) && ui->ffv1SlicesSpinBox) {
        const int ffv1Slices = ui->ffv1SlicesSpinBox->value();
        if (!isSupportedFfv1SlicesValue(ffv1Slices)) {
            const QString supportedValuesText = supportedFfv1SlicesValuesText();
            const QString errorToShow = tr("Unsupported FFV1 slices value %1. Supported values: %2.")
                                            .arg(ffv1Slices)
                                            .arg(supportedValuesText);
            cleanupTemporaryMetadataSnapshot();
            appendStatus(errorToShow);
            appendLog(errorToShow);
            QMessageBox::warning(this, tr("Error"), errorToShow);
            return;
        }
    }
    const QString configOverridePath = createTemporaryExportConfig(&configErrorMessage,
                                                                   selectedProfile,
                                                                   selectedAudioProfile);
    if (configOverridePath.isEmpty()) {
        cleanupTemporaryMetadataSnapshot();
        const QString errorToShow = configErrorMessage.isEmpty()
                                        ? tr("Could not prepare export profile configuration.")
                                        : configErrorMessage;
        appendStatus(errorToShow);
        appendLog(errorToShow);
        QMessageBox::warning(this, tr("Error"), errorToShow);
        return;
    }
    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    int inPoint = ui->inPointSpinBox ? ui->inPointSpinBox->value() : 1;
    int outPoint = ui->outPointSpinBox ? ui->outPointSpinBox->value() : totalFrames;
    inPoint = qBound(1, inPoint, totalFrames);
    outPoint = qBound(1, outPoint, totalFrames);
    if (outPoint < inPoint) {
        cleanupTemporaryMetadataSnapshot();
        const QString errorToShow = tr("Out point must be greater than or equal to In point.");
        appendStatus(errorToShow);
        appendLog(errorToShow);
        QMessageBox::warning(this, tr("Error"), errorToShow);
        return;
    }
    const int startFrameOneBased = inPoint;
    const int zeroBasedStartForAudioTrim = qMax(0, startFrameOneBased - 1);
    const int rangeLength = outPoint - inPoint + 1;

    QStringList exportAudioTracks = collectAudioTracks();
    if (!exportAudioTracks.isEmpty() && (zeroBasedStartForAudioTrim > 0 || rangeLength < totalFrames)) {
        QString trimAudioErrorMessage;
        if (!prepareTrimmedAudioTracks(zeroBasedStartForAudioTrim, rangeLength, &exportAudioTracks, &trimAudioErrorMessage)) {
            cleanupTemporaryMetadataSnapshot();
            const QString errorToShow = trimAudioErrorMessage.isEmpty()
                                            ? tr("Could not prepare audio track range.")
                                            : trimAudioErrorMessage;
            appendStatus(errorToShow);
            appendLog(errorToShow);
            QMessageBox::warning(this, tr("Error"), errorToShow);
            return;
        }
        appendLog(tr("Prepared %1 range-aligned audio track(s).").arg(exportAudioTracks.size()));
    }

    QString errorMessage;
    const QStringList arguments = buildArguments(&errorMessage,
                                                 metadataSnapshotPath,
                                                 overwriteExisting,
                                                 configOverridePath,
                                                 exportAudioTracks,
                                                 startFrameOneBased,
                                                 rangeLength);
    if (arguments.isEmpty()) {
        cleanupTemporaryMetadataSnapshot();
        if (!errorMessage.isEmpty()) {
            appendStatus(errorMessage);
            appendLog(errorMessage);
            QMessageBox::warning(this, tr("Error"), errorMessage);
        }
        return;
    }

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        cleanupTemporaryMetadataSnapshot();
        appendStatus(tr("tbc-video-export not found."));
        appendLog(tr("tbc-video-export not found."));
        QMessageBox::warning(this, tr("Error"), tr("tbc-video-export not found in PATH or alongside ld-analyse."));
        return;
    }
    const QString commandLine = formatCommand(exportPath, arguments);
    appendLog(tr("Command: %1").arg(commandLine));
    resetProcessStats();
    initializeProcessStats();

    processStdout.clear();
    processStderr.clear();
    pendingStdoutBuffer.clear();
    pendingStderrBuffer.clear();
    setBusy(true);
    exportProcess->setWorkingDirectory(QFileInfo(currentInputFile).absolutePath());
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QStringList prependDirs = defaultExecutableSearchDirs(currentInputFile);

        QStringList pathEntries = env.value(QStringLiteral("PATH"))
                                      .split(QDir::listSeparator(), Qt::SkipEmptyParts);
        prependUniquePathEntries(&pathEntries, prependDirs);
        if (!pathEntries.isEmpty()) {
            env.insert(QStringLiteral("PATH"), pathEntries.join(QDir::listSeparator()));
        }
        exportProcess->setProcessEnvironment(env);
    }
    QString programToRun = exportPath;
    QStringList argsToRun = arguments;
#if defined(Q_OS_UNIX)
    const QString scriptPath = QStandardPaths::findExecutable(QStringLiteral("script"));
    if (!scriptPath.isEmpty()) {
#if defined(Q_OS_MACOS)
        programToRun = scriptPath;
        argsToRun = {QStringLiteral("-q"), QStringLiteral("/dev/null"), exportPath};
        argsToRun.append(arguments);
        appendLog(tr("Using script (bsd) to enable ANSI progress output."));
#else
        const QString scriptCommand = formatShellCommand(exportPath, arguments);
        programToRun = scriptPath;
        argsToRun = {QStringLiteral("-q"), QStringLiteral("-e"), QStringLiteral("-c"),
                     scriptCommand, QStringLiteral("/dev/null")};
        appendLog(tr("Using script to enable ANSI progress output."));
#endif
    }
#endif
    exportProcess->start(programToRun, argsToRun);
    if (!exportProcess->waitForStarted(5000)) {
        cleanupTemporaryMetadataSnapshot();
        appendStatus(tr("Failed to start tbc-video-export."));
        appendLog(tr("Failed to start tbc-video-export."));
        QMessageBox::warning(this, tr("Error"), tr("Failed to start tbc-video-export."));
        setBusy(false);
        return;
    }

    appendStatus(tr("Export running..."));
    appendLog(tr("Export running..."));
}
void ExportDialog::on_cancelButton_clicked()
{
    if (!exportProcess || exportProcess->state() == QProcess::NotRunning) {
        appendStatus(tr("No export running."));
        appendLog(tr("No export running."));
        return;
    }
    if (cancelRequested) {
        appendStatus(tr("Cancel already requested..."));
        appendLog(tr("Cancel already requested; waiting for process to exit."));
        return;
    }
    cancelRequested = true;

    appendStatus(tr("Cancel requested..."));
    appendLog(tr("Cancel requested."));

#if defined(Q_OS_UNIX)
    const qint64 pid = exportProcess->processId();
    if (pid > 0) {
        if (::kill(static_cast<pid_t>(pid), SIGINT) == 0) {
            appendLog(tr("Sent SIGINT to tbc-video-export."));
        } else {
            appendLog(tr("Failed to send SIGINT to tbc-video-export."));
        }
    } else {
        appendLog(tr("Export process ID unavailable; unable to send SIGINT."));
    }
    QTimer::singleShot(3000, this, [this]() {
        if (exportProcess && exportProcess->state() != QProcess::NotRunning) {
            appendLog(tr("Process still running after SIGINT; sending kill()."));
            exportProcess->kill();
        }
    });
#else
    const qint64 pid = exportProcess->processId();
    bool taskkillStarted = false;
    if (pid > 0) {
        const QString taskkillPath = QStandardPaths::findExecutable(QStringLiteral("taskkill"));
        const QString taskkillProgram = taskkillPath.isEmpty() ? QStringLiteral("taskkill") : taskkillPath;
        const QStringList taskkillArgs = {
            QStringLiteral("/PID"),
            QString::number(pid),
            QStringLiteral("/T"),
            QStringLiteral("/F")
        };
        taskkillStarted = QProcess::startDetached(taskkillProgram, taskkillArgs);
        if (taskkillStarted) {
            appendLog(tr("Invoked taskkill /PID %1 /T /F for export process tree.").arg(pid));
        } else {
            appendLog(tr("Failed to launch taskkill for export process tree."));
        }
    } else {
        appendLog(tr("Export process ID unavailable; cannot run taskkill by PID."));
    }
    exportProcess->terminate();
    appendLog(taskkillStarted
                  ? tr("Sent terminate request to export process (taskkill already requested).")
                  : tr("Sent terminate request to export process (fallback path)."));
    QTimer::singleShot(1500, this, [this]() {
        if (exportProcess && exportProcess->state() != QProcess::NotRunning) {
            appendLog(tr("Process still running after terminate; sending kill()."));
            exportProcess->kill();
        }
    });
#endif
}


void ExportDialog::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    setBusy(false);
    flushPendingProcessOutput();
    cleanupTemporaryMetadataSnapshot();
    const bool wasCancelRequested = cancelRequested;
    cancelRequested = false;
    const QString combinedOutput = processStdout + QStringLiteral("\n") + processStderr;
    const bool reportedFailure =
        combinedOutput.contains(QRegularExpression(QStringLiteral("\\bExport failed\\b"),
                                                   QRegularExpression::CaseInsensitiveOption))
        || combinedOutput.contains(QStringLiteral("InvalidOptsError"))
        || combinedOutput.contains(QStringLiteral("FileIOError"))
        || combinedOutput.contains(QStringLiteral("ProcessError"));

    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0 && !reportedFailure;
    if (success) {
        appendStatus(tr("Export complete."));
        appendLog(tr("Export complete."));
        return;
    }
    if (wasCancelRequested) {
        appendStatus(tr("Export cancelled."));
        appendLog(tr("Export cancelled."));
        return;
    }

    const QString errorText = processStderr.isEmpty() ? processStdout : processStderr;
    appendStatus(tr("Export failed."));
    appendLog(tr("Export failed."));
    QMessageBox::warning(this, tr("Export failed"),
                         errorText.isEmpty()
                             ? tr("tbc-video-export reported failure.")
                             : errorText);
    if (!errorText.trimmed().isEmpty()) {
        appendLog(errorText.trimmed());
    }
}

void ExportDialog::handleProcessError(QProcess::ProcessError)
{
    setBusy(false);
    flushPendingProcessOutput();
    cleanupTemporaryMetadataSnapshot();
    const QString errorText = exportProcess ? exportProcess->errorString() : QString();
    if (cancelRequested) {
        appendLog(errorText.isEmpty()
                      ? tr("Process reported an error during cancellation.")
                      : tr("Process reported an error during cancellation: %1").arg(errorText));
        return;
    }
    appendStatus(errorText.isEmpty() ? tr("Export failed to start.") : errorText);
    appendLog(errorText.isEmpty() ? tr("Export failed to start.") : errorText);
    QMessageBox::warning(this, tr("Export failed"),
                         errorText.isEmpty() ? tr("tbc-video-export could not be started.") : errorText);
}
void ExportDialog::processOutputLine(const QString &line, QString *lastStatus)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || isIgnorableLogLine(trimmed)) {
        return;
    }

    ExportProcessStat stat;
    if (parseProgressLine(trimmed, &stat)) {
        updateProcessStat(stat);
        return;
    }

    if (lastStatus) {
        *lastStatus = trimmed;
    }
    appendLog(trimmed);
}

void ExportDialog::consumeProcessOutputChunk(const QString &chunk, QString *pendingBuffer)
{
    if (!pendingBuffer || chunk.isEmpty()) {
        return;
    }

    QString buffer = *pendingBuffer + stripAnsiSequences(chunk);
    ExportProcessStat ffmpegStat;
    if (parseFfmpegProgressLine(buffer, &ffmpegStat)) {
        updateProcessStat(ffmpegStat);
    }
    buffer.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const int lastNewline = buffer.lastIndexOf(QLatin1Char('\n'));
    if (lastNewline < 0) {
        if (buffer.size() > 32768) {
            buffer = buffer.right(32768);
        }
        *pendingBuffer = buffer;
        return;
    }

    const QString completeLines = buffer.left(lastNewline);
    *pendingBuffer = buffer.mid(lastNewline + 1);

    QString lastStatus;
    const QStringList lines = completeLines.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        processOutputLine(line, &lastStatus);
    }
    if (!lastStatus.isEmpty()) {
        appendStatus(lastStatus);
    }
}

void ExportDialog::flushPendingProcessOutput()
{
    consumeProcessOutputChunk(QStringLiteral("\n"), &pendingStdoutBuffer);
    consumeProcessOutputChunk(QStringLiteral("\n"), &pendingStderrBuffer);
    pendingStdoutBuffer.clear();
    pendingStderrBuffer.clear();
}

void ExportDialog::handleProcessStdout()
{
    const QString chunk = QString::fromLocal8Bit(exportProcess->readAllStandardOutput());
    processStdout += chunk;
    consumeProcessOutputChunk(chunk, &pendingStdoutBuffer);
}

void ExportDialog::handleProcessStderr()
{
    const QString chunk = QString::fromLocal8Bit(exportProcess->readAllStandardError());
    processStderr += chunk;
    consumeProcessOutputChunk(chunk, &pendingStderrBuffer);
}
void ExportDialog::resetProcessStats()
{
    processRowMap.clear();
    if (ui->processStatsTable) {
        ui->processStatsTable->setRowCount(0);
    }
}

void ExportDialog::initializeProcessStats()
{
    if (!tbcSource || !ui->processStatsTable) {
        return;
    }

    const int totalFramesValue = qMax(0, tbcSource->getNumberOfFrames());
    const QString totalFrames = totalFramesValue > 0 ? QString::number(totalFramesValue) : QStringLiteral("—");
    auto seedStat = [this, &totalFrames](const QString &process, const QString &tbcType) {
        ExportProcessStat stat;
        stat.process = process;
        stat.tbcType = tbcType;
        stat.trackedName = QStringLiteral("frame");
        stat.current = QStringLiteral("0");
        stat.total = totalFrames;
        stat.errors = QStringLiteral("0");
        stat.fps = QStringLiteral("—");
        updateProcessStat(stat);
    };

    const TbcSource::SourceMode mode = tbcSource->getSourceMode();
    if (mode == TbcSource::BOTH_SOURCES) {
        seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("LUMA"));
        seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("CHROMA"));
        seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("LUMA"));
        seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("CHROMA"));
    } else if (mode == TbcSource::LUMA_SOURCE) {
        seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("LUMA"));
        seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("LUMA"));
    } else if (mode == TbcSource::CHROMA_SOURCE) {
        seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("CHROMA"));
        seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("CHROMA"));
    } else {
        seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("COMBINED"));
        seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("COMBINED"));
    }
    seedStat(QStringLiteral("ffmpeg"), QStringLiteral("—"));
}

void ExportDialog::updateProcessStat(const ExportProcessStat &stat)
{
    if (!ui->processStatsTable) {
        return;
    }
    if (stat.tbcType == QStringLiteral("—")) {
        QVector<int> processRows;
        processRows.reserve(ui->processStatsTable->rowCount());
        for (int rowIndex = 0; rowIndex < ui->processStatsTable->rowCount(); ++rowIndex) {
            QTableWidgetItem *processItem = ui->processStatsTable->item(rowIndex, 0);
            if (processItem && processItem->text() == stat.process) {
                processRows.push_back(rowIndex);
            }
        }
        if (!processRows.isEmpty()) {
            for (const int rowIndex : processRows) {
                setTableItem(ui->processStatsTable, rowIndex, 0, stat.process);
                setTableItem(ui->processStatsTable, rowIndex, 2, stat.trackedName);
                setTableItem(ui->processStatsTable, rowIndex, 3, stat.current);
                setTableItem(ui->processStatsTable, rowIndex, 4, stat.total);
                setTableItem(ui->processStatsTable, rowIndex, 5, stat.errors);
                setTableItem(ui->processStatsTable, rowIndex, 6, stat.fps);
            }
            return;
        }
    }
    const QString key = stat.process + QStringLiteral("|") + stat.tbcType;
    int row = processRowMap.value(key, -1);
    if (row < 0) {
        row = ui->processStatsTable->rowCount();
        ui->processStatsTable->insertRow(row);
        processRowMap.insert(key, row);
    }

    setTableItem(ui->processStatsTable, row, 0, stat.process);
    setTableItem(ui->processStatsTable, row, 1, stat.tbcType);
    setTableItem(ui->processStatsTable, row, 2, stat.trackedName);
    setTableItem(ui->processStatsTable, row, 3, stat.current);
    setTableItem(ui->processStatsTable, row, 4, stat.total);
    setTableItem(ui->processStatsTable, row, 5, stat.errors);
    setTableItem(ui->processStatsTable, row, 6, stat.fps);
}

void ExportDialog::setBusy(bool busy)
{
    const bool enabled = exportAvailable && !busy;
    const bool browseEnabled = !busy;
    ui->exportButton->setEnabled(enabled);
    if (ui->cancelButton) {
        ui->cancelButton->setEnabled(busy);
    }
    ui->outputBrowseButton->setEnabled(browseEnabled);
    ui->audio1BrowseButton->setEnabled(enabled);
    ui->audio2BrowseButton->setEnabled(enabled);
    ui->audio3BrowseButton->setEnabled(enabled);
    ui->audio4BrowseButton->setEnabled(enabled);
    ui->profileComboBox->setEnabled(enabled);
    if (ui->resolutionModeComboBox) {
        ui->resolutionModeComboBox->setEnabled(enabled);
    }
    if (ui->audioProfileComboBox) {
        ui->audioProfileComboBox->setEnabled(enabled);
    }
    if (ui->logProcessOutputCheckBox) {
        ui->logProcessOutputCheckBox->setEnabled(enabled);
    }
    if (ui->disableDropoutCompensationCheckBox) {
        ui->disableDropoutCompensationCheckBox->setEnabled(enabled);
    }
    if (ui->disableMetadataEmbeddingCheckBox) {
        ui->disableMetadataEmbeddingCheckBox->setEnabled(enabled);
    }
    if (ui->ffv1SlicesLabel) {
        ui->ffv1SlicesLabel->setEnabled(enabled);
    }
    if (ui->ffv1SlicesSpinBox) {
        ui->ffv1SlicesSpinBox->setEnabled(enabled);
    }
    ui->outputLineEdit->setEnabled(browseEnabled);
    ui->audio1LineEdit->setEnabled(enabled);
    ui->audio2LineEdit->setEnabled(enabled);
    ui->audio3LineEdit->setEnabled(enabled);
    ui->audio4LineEdit->setEnabled(enabled);
    if (ui->inPointSpinBox) {
        ui->inPointSpinBox->setEnabled(enabled);
    }
    if (ui->outPointSpinBox) {
        ui->outPointSpinBox->setEnabled(enabled);
    }
    if (ui->inPointTimecodeLineEdit) {
        ui->inPointTimecodeLineEdit->setEnabled(enabled);
    }
    if (ui->outPointTimecodeLineEdit) {
        ui->outPointTimecodeLineEdit->setEnabled(enabled);
    }
    updateProfileDependentControls();
}

QString ExportDialog::resolveVideoExportPath() const
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("tbc-video-export"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }
    const QStringList candidateDirs = defaultExecutableSearchDirs(currentInputFile);

    QStringList candidateNames;
#if defined(Q_OS_WIN)
    candidateNames << QStringLiteral("tbc-video-export.exe") << QStringLiteral("tbc-video-export");
#else
    candidateNames << QStringLiteral("tbc-video-export");
#endif

    for (const QString &dir : candidateDirs) {
        for (const QString &name : candidateNames) {
            const QString candidate = QDir(dir).filePath(name);
            QFileInfo candidateInfo(candidate);
            if (candidateInfo.exists() && candidateInfo.isExecutable()) {
                return candidate;
            }
        }
    }

    return QString();
}

QString ExportDialog::resolveLdChromaDecoderPath() const
{
    const QStringList candidateDirs = defaultExecutableSearchDirs(currentInputFile);

    QStringList candidateNames;
#if defined(Q_OS_WIN)
    candidateNames << QStringLiteral("ld-chroma-decoder.exe") << QStringLiteral("ld-chroma-decoder");
#else
    candidateNames << QStringLiteral("ld-chroma-decoder");
#endif

    for (const QString &dir : candidateDirs) {
        for (const QString &name : candidateNames) {
            const QString candidate = QDir(dir).filePath(name);
            QFileInfo candidateInfo(candidate);
            if (candidateInfo.exists() && candidateInfo.isExecutable()) {
                return candidate;
            }
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("ld-chroma-decoder"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    return QString();
}

QString ExportDialog::resolveFfmpegPath() const
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }
    const QStringList candidateDirs = defaultExecutableSearchDirs(currentInputFile);

    QStringList candidateNames;
#if defined(Q_OS_WIN)
    candidateNames << QStringLiteral("ffmpeg.exe") << QStringLiteral("ffmpeg");
#else
    candidateNames << QStringLiteral("ffmpeg");
#endif

    for (const QString &dir : candidateDirs) {
        for (const QString &name : candidateNames) {
            const QString candidate = QDir(dir).filePath(name);
            QFileInfo candidateInfo(candidate);
            if (candidateInfo.exists() && candidateInfo.isExecutable()) {
                return candidate;
            }
        }
    }

    return QString();
}

QString ExportDialog::defaultOutputBaseName(const QString &inputFile) const
{
    QFileInfo info(inputFile);
    const QString baseName = info.completeBaseName();
    const QString dir = info.absolutePath();
    if (dir.isEmpty()) {
        return baseName;
    }
    return QDir(dir).filePath(baseName);
}

bool ExportDialog::findExistingOutputFiles(const QString &outputBase, QStringList *existingFiles) const
{
    if (existingFiles) {
        existingFiles->clear();
    }

    const QString sanitizedBase = sanitizeOutputBaseName(outputBase.trimmed());
    if (sanitizedBase.isEmpty()) {
        return false;
    }

    QFileInfo outputInfo(sanitizedBase);
    QStringList foundFiles;
    if (outputInfo.exists()) {
        foundFiles << outputInfo.absoluteFilePath();
    }

    const QDir outputDir = outputInfo.absoluteDir();
    const QString baseName = outputInfo.fileName();
    if (outputDir.exists() && !baseName.isEmpty()) {
        const QStringList matchedNames = outputDir.entryList(
            QStringList() << (baseName + QStringLiteral(".*")),
            QDir::Files | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QString &name : matchedNames) {
            foundFiles << outputDir.filePath(name);
        }
    }

    foundFiles.removeDuplicates();
    if (existingFiles) {
        *existingFiles = foundFiles;
    }
    return !foundFiles.isEmpty();
}

QString ExportDialog::sanitizeOutputBaseName(const QString &path) const
{
    QFileInfo info(path);
    const QString baseName = info.completeBaseName();
    const QString dir = info.absolutePath();
    if (dir.isEmpty()) {
        return baseName;
    }
    return QDir(dir).filePath(baseName);
}

QString ExportDialog::videoSystemArg(int system) const
{
    switch (static_cast<VideoSystem>(system)) {
    case PAL:
        return QStringLiteral("pal");
    case PAL_M:
        return QStringLiteral("pal-m");
    case NTSC:
    default:
        return QStringLiteral("ntsc");
    }
}

QStringList ExportDialog::collectAudioTracks() const
{
    QStringList tracks;
    const QStringList candidates = {
        ui->audio1LineEdit->text().trimmed(),
        ui->audio2LineEdit->text().trimmed(),
        ui->audio3LineEdit->text().trimmed(),
        ui->audio4LineEdit->text().trimmed()
    };
    for (const QString &track : candidates) {
        if (!track.isEmpty()) {
            tracks << track;
        }
    }
    return tracks;
}

bool ExportDialog::prepareTrimmedAudioTracks(int zeroBasedStartFrame,
                                             int rangeLengthFrames,
                                             QStringList *audioTracks,
                                             QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!audioTracks) {
        if (errorMessage) {
            *errorMessage = tr("Internal error: no audio track list supplied.");
        }
        return false;
    }
    if (audioTracks->isEmpty()) {
        return true;
    }
    if (!tbcSource) {
        if (errorMessage) {
            *errorMessage = tr("No source loaded.");
        }
        return false;
    }

    for (const QString &path : temporaryAudioTrackPaths) {
        QFile::remove(path);
    }
    temporaryAudioTrackPaths.clear();

    const QString ffmpegPath = resolveFfmpegPath();
    if (ffmpegPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("ffmpeg not found in PATH or alongside ld-analyse.");
        }
        return false;
    }

    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    const int clampedStart = qBound(0, zeroBasedStartFrame, qMax(0, totalFrames - 1));
    const int maxLength = qMax(0, totalFrames - clampedStart);
    const int clampedLength = qBound(1, rangeLengthFrames, qMax(1, maxLength));
    if (clampedLength <= 0) {
        if (errorMessage) {
            *errorMessage = tr("Invalid audio trim range.");
        }
        return false;
    }

    const LdDecodeMetaData::VideoParameters &videoParameters = tbcSource->getVideoParameters();
    const double fps = frameRateForSystem(videoParameters.system);
    if (fps <= 0.0) {
        if (errorMessage) {
            *errorMessage = tr("Invalid video frame rate for audio trimming.");
        }
        return false;
    }

    const QString startArg = QString::number(static_cast<double>(clampedStart) / fps, 'f', 6);
    const QString durationArg = QString::number(static_cast<double>(clampedLength) / fps, 'f', 6);

    QStringList trimmedTracks;
    trimmedTracks.reserve(audioTracks->size());
    for (int index = 0; index < audioTracks->size(); ++index) {
        const QString sourceTrack = audioTracks->at(index).trimmed();
        if (sourceTrack.isEmpty()) {
            continue;
        }

        const QFileInfo sourceInfo(sourceTrack);
        if (!sourceInfo.exists() || !sourceInfo.isFile()) {
            for (const QString &path : trimmedTracks) {
                QFile::remove(path);
            }
            trimmedTracks.clear();
            temporaryAudioTrackPaths.clear();
            if (errorMessage) {
                *errorMessage = tr("Audio track not found: %1").arg(sourceTrack);
            }
            return false;
        }

        const QString trimmedPath = QDir::temp().filePath(
            QStringLiteral("ld-analyse-audio-range-%1-%2.wav")
                .arg(index + 1)
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QStringList ffmpegArgs;
        ffmpegArgs << QStringLiteral("-hide_banner")
                   << QStringLiteral("-v") << QStringLiteral("error")
                   << QStringLiteral("-nostdin")
                   << QStringLiteral("-y")
                   << QStringLiteral("-i") << sourceTrack
                   << QStringLiteral("-ss") << startArg
                   << QStringLiteral("-t") << durationArg
                   << QStringLiteral("-map") << QStringLiteral("0:a:0")
                   << QStringLiteral("-vn")
                   << QStringLiteral("-sn")
                   << QStringLiteral("-dn")
                   << QStringLiteral("-c:a") << QStringLiteral("pcm_s24le")
                   << trimmedPath;

        QProcess ffmpegProcess;
        ffmpegProcess.setProcessChannelMode(QProcess::MergedChannels);
        ffmpegProcess.start(ffmpegPath, ffmpegArgs);
        if (!ffmpegProcess.waitForStarted(5000)) {
            for (const QString &path : trimmedTracks) {
                QFile::remove(path);
            }
            trimmedTracks.clear();
            temporaryAudioTrackPaths.clear();
            if (errorMessage) {
                *errorMessage = tr("Failed to start ffmpeg for audio track trimming.");
            }
            return false;
        }
        if (!ffmpegProcess.waitForFinished(120000)) {
            ffmpegProcess.kill();
            for (const QString &path : trimmedTracks) {
                QFile::remove(path);
            }
            trimmedTracks.clear();
            temporaryAudioTrackPaths.clear();
            if (errorMessage) {
                *errorMessage = tr("ffmpeg timed out while trimming audio tracks.");
            }
            return false;
        }

        const QString ffmpegOutput = QString::fromLocal8Bit(ffmpegProcess.readAllStandardOutput()).trimmed();
        const bool ffmpegOk = ffmpegProcess.exitStatus() == QProcess::NormalExit
                              && ffmpegProcess.exitCode() == 0
                              && QFileInfo::exists(trimmedPath);
        if (!ffmpegOk) {
            QFile::remove(trimmedPath);
            for (const QString &path : trimmedTracks) {
                QFile::remove(path);
            }
            trimmedTracks.clear();
            temporaryAudioTrackPaths.clear();
            if (errorMessage) {
                *errorMessage = ffmpegOutput.isEmpty()
                                    ? tr("ffmpeg failed while trimming audio tracks.")
                                    : ffmpegOutput;
            }
            return false;
        }

        trimmedTracks << trimmedPath;
    }

    temporaryAudioTrackPaths = trimmedTracks;
    *audioTracks = trimmedTracks;
    return true;
}

QStringList ExportDialog::buildArguments(QString *errorMessage, const QString &inputTbcJsonOverride,
                                         bool overwriteExisting,
                                         const QString &configFileOverride,
                                         const QStringList &audioTracks,
                                         int startFrameOneBasedOverride,
                                         int lengthOverride) const
{
    QStringList args;
    if (!tbcSource) {
        if (errorMessage) {
            *errorMessage = tr("No source loaded.");
        }
        return args;
    }

    const QString inputFile = tbcSource->getCurrentSourceFilename();
    if (inputFile.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("No input file available.");
        }
        return args;
    }

    const QString outputBase = sanitizeOutputBaseName(ui->outputLineEdit->text().trimmed());
    if (outputBase.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Please provide an output base name.");
        }
        return args;
    }
    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    int startFrameOneBased = startFrameOneBasedOverride;
    int rangeLength = lengthOverride;
    if (startFrameOneBased <= 0 || rangeLength <= 0) {
        int inPoint = ui->inPointSpinBox ? ui->inPointSpinBox->value() : 1;
        int outPoint = ui->outPointSpinBox ? ui->outPointSpinBox->value() : totalFrames;
        inPoint = qBound(1, inPoint, totalFrames);
        outPoint = qBound(1, outPoint, totalFrames);
        if (outPoint < inPoint) {
            if (errorMessage) {
                *errorMessage = tr("Out point must be greater than or equal to In point.");
            }
            return args;
        }
        startFrameOneBased = inPoint;
        rangeLength = outPoint - inPoint + 1;
    }
    startFrameOneBased = qBound(1, startFrameOneBased, totalFrames);
    rangeLength = qMin(rangeLength, totalFrames - startFrameOneBased + 1);
    if (rangeLength <= 0) {
        if (errorMessage) {
            *errorMessage = tr("Invalid export range.");
        }
        return args;
    }
    args << QStringLiteral("--start") << QString::number(startFrameOneBased);
    args << QStringLiteral("--length") << QString::number(rangeLength);

    const QString profile = ui->profileComboBox->currentText().trimmed();
    if (!configFileOverride.isEmpty()) {
        args << QStringLiteral("--config-file") << configFileOverride;
    }
    if (!profile.isEmpty()) {
        args << QStringLiteral("--profile") << profile;
    }
    if (ui->disableDropoutCompensationCheckBox && ui->disableDropoutCompensationCheckBox->isChecked()) {
        args << QStringLiteral("--no-dropout-correct");
    }
    if (ui->disableMetadataEmbeddingCheckBox && ui->disableMetadataEmbeddingCheckBox->isChecked()) {
        args << QStringLiteral("--no-attach-json");
    }

    const QStringList tracksToUse = audioTracks.isEmpty() ? collectAudioTracks() : audioTracks;
    for (const QString &track : tracksToUse) {
        args << QStringLiteral("--audio-track") << track;
    }
    if (ui->logProcessOutputCheckBox && ui->logProcessOutputCheckBox->isChecked()) {
        args << QStringLiteral("--log-process-output");
    }
#if defined(Q_OS_WIN)
    args << QStringLiteral("--show-process-output");
#endif
    if (overwriteExisting) {
        args << QStringLiteral("--overwrite");
    }

    const LdDecodeMetaData::VideoParameters &videoParameters = tbcSource->getVideoParameters();
    const bool isPalSystem = (videoParameters.system == PAL || videoParameters.system == PAL_M);
    const bool isNtscSystem = (videoParameters.system == NTSC);
    if (videoParameters.isValid) {
        args << QStringLiteral("--video-system") << videoSystemArg(videoParameters.system);
    }

    if (!videoParameters.chromaDecoder.isEmpty()) {
        const QString decoderName = videoParameters.chromaDecoder.trimmed().toLower();
        bool decoderAllowed = false;
        if (isPalSystem) {
            decoderAllowed = decoderName == QStringLiteral("pal2d")
                             || decoderName == QStringLiteral("transform2d")
                             || decoderName == QStringLiteral("transform3d")
                             || decoderName == QStringLiteral("mono");
        } else if (isNtscSystem) {
            decoderAllowed = decoderName == QStringLiteral("ntsc1d")
                             || decoderName == QStringLiteral("ntsc2d")
                             || decoderName == QStringLiteral("ntsc3d")
                             || decoderName == QStringLiteral("ntsc3dnoadapt")
                             || decoderName == QStringLiteral("mono");
        }
        if (decoderAllowed) {
            args << QStringLiteral("--chroma-decoder") << decoderName;
        }
    }
    if (videoParameters.chromaGain >= 0.0) {
        args << QStringLiteral("--chroma-gain") << QString::number(videoParameters.chromaGain, 'f', 6);
    }
    if (videoParameters.chromaPhase != -1.0) {
        args << QStringLiteral("--chroma-phase") << QString::number(videoParameters.chromaPhase, 'f', 3);
    }
    const bool isSplitSource = tbcSource && tbcSource->getSourceMode() != TbcSource::ONE_SOURCE;
    if (!isSplitSource && videoParameters.lumaNR >= 0.0) {
        args << QStringLiteral("--luma-nr") << QString::number(videoParameters.lumaNR, 'f', 3);
    }
    QString resolutionMode = ui->resolutionModeComboBox
                                 ? ui->resolutionModeComboBox->currentData().toString()
                                 : QStringLiteral("active_area");
    if ((resolutionMode == QStringLiteral("active_area")
         || resolutionMode == QStringLiteral("user_defined"))
        && tbcSource->getOutputConfiguration().fullFrameDecode) {
        resolutionMode = QStringLiteral("full_frame_4fsc");
    }

    auto appendFramingArgs = [&args](int ffll, int lfll, int ffrl, int lfrl) {
        if (ffll > 0) {
            args << QStringLiteral("--ffll") << QString::number(ffll);
        }
        if (lfll > 0) {
            args << QStringLiteral("--lfll") << QString::number(lfll);
        }
        if (ffrl > 0) {
            args << QStringLiteral("--ffrl") << QString::number(ffrl);
        }
        if (lfrl > 0) {
            args << QStringLiteral("--lfrl") << QString::number(lfrl);
        }
    };
    const bool forwardActiveLines = hasAnyNonDefaultVerticalFraming(videoParameters);
    if (resolutionMode == QStringLiteral("full_frame_4fsc")) {
        const QString decoderPath = resolveLdChromaDecoderPath();
        const bool supportsFullFrame = executableSupportsOption(decoderPath, QStringLiteral("--full-frame"));
        if (supportsFullFrame) {
            args << QStringLiteral("--full-frame");
        } else {
            if (errorMessage) {
                if (decoderPath.isEmpty()) {
                    *errorMessage = tr("Full-Frame 4fsc export requires ld-chroma-decoder with --full-frame support, but no decoder executable was found.");
                } else {
                    *errorMessage = tr("Full-Frame 4fsc export requires ld-chroma-decoder with --full-frame support. Detected decoder does not support it: %1").arg(decoderPath);
                }
            }
            return QStringList();
        }
    } else if (resolutionMode == QStringLiteral("active_vbi")) {
        args << QStringLiteral("--vbi");
    } else if (resolutionMode == QStringLiteral("user_defined") || forwardActiveLines) {
        appendFramingArgs(videoParameters.firstActiveFieldLine,
                          videoParameters.lastActiveFieldLine,
                          videoParameters.firstActiveFrameLine,
                          videoParameters.lastActiveFrameLine);
    }

    if (isNtscSystem && videoParameters.ntscPhaseCompensation >= 0) {
        args << (videoParameters.ntscPhaseCompensation ? QStringLiteral("--ntsc-phase-comp")
                                                       : QStringLiteral("--no-ntsc-phase-comp"));
    }
    if (isPalSystem && videoParameters.palTransformThreshold >= 0.0) {
        args << QStringLiteral("--transform-threshold") << QString::number(videoParameters.palTransformThreshold, 'f', 3);
    }

    if (!inputTbcJsonOverride.isEmpty()) {
        args << QStringLiteral("--input-tbc-json") << inputTbcJsonOverride;
    }

    args << inputFile << outputBase;
    return args;
}
QString ExportDialog::createTemporaryExportConfig(QString *errorMessage,
                                                  const QString &profileName,
                                                  const QString &audioProfileName)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    if (!temporaryExportConfigPath.isEmpty()) {
        QFile::remove(temporaryExportConfigPath);
        temporaryExportConfigPath.clear();
    }

    const QString selectedProfile = profileName.trimmed();
    const QString selectedAudioProfile = audioProfileName.trimmed();
    if (selectedProfile.isEmpty() || selectedAudioProfile.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Please select both video and audio profiles.");
        }
        return QString();
    }

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("tbc-video-export not found.");
        }
        return QString();
    }

    const QString dumpDirPath = QDir::temp().filePath(
        QStringLiteral("ld-analyse-export-config-dump-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(dumpDirPath)) {
        if (errorMessage) {
            *errorMessage = tr("Failed to create temporary directory for export configuration.");
        }
        return QString();
    }
    const auto cleanupDumpDir = [&dumpDirPath]() {
        QDir dumpDir(dumpDirPath);
        if (dumpDir.exists()) {
            dumpDir.removeRecursively();
        }
    };

    QProcess dumpProcess;
    dumpProcess.setProcessChannelMode(QProcess::MergedChannels);
    dumpProcess.setWorkingDirectory(dumpDirPath);
    dumpProcess.start(exportPath, QStringList() << QStringLiteral("--dump-default-config"));
    if (!dumpProcess.waitForStarted(3000)) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Failed to start export config generation.");
        }
        return QString();
    }
    if (!dumpProcess.waitForFinished(15000)) {
        dumpProcess.kill();
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Export config generation timed out.");
        }
        return QString();
    }
    const QString dumpOutput = QString::fromLocal8Bit(dumpProcess.readAllStandardOutput()).trimmed();
    if (dumpProcess.exitStatus() != QProcess::NormalExit || dumpProcess.exitCode() != 0) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = dumpOutput.isEmpty()
                                ? tr("Failed to generate default export configuration.")
                                : dumpOutput;
        }
        return QString();
    }

    const QString dumpedConfigPath = QDir(dumpDirPath).filePath(QStringLiteral("tbc-video-export.json"));
    QFile dumpedConfigFile(dumpedConfigPath);
    if (!dumpedConfigFile.open(QIODevice::ReadOnly)) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Failed to read generated export configuration.");
        }
        return QString();
    }
    const QByteArray configData = dumpedConfigFile.readAll();
    dumpedConfigFile.close();

    QJsonParseError parseError;
    const QJsonDocument configDoc = QJsonDocument::fromJson(configData, &parseError);
    if (configDoc.isNull() || !configDoc.isObject()) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Invalid export configuration JSON: %1").arg(parseError.errorString());
        }
        return QString();
    }

    QJsonObject root = configDoc.object();
    QJsonArray audioProfiles = root.value(QStringLiteral("audio_profiles")).toArray();
    auto upsertAudioProfile = [&audioProfiles](const QJsonObject &profileObject) {
        const QString targetName = profileObject.value(QStringLiteral("name")).toString();
        for (int i = 0; i < audioProfiles.size(); ++i) {
            const QJsonObject existing = audioProfiles.at(i).toObject();
            if (existing.value(QStringLiteral("name")).toString() == targetName) {
                audioProfiles.replace(i, profileObject);
                return;
            }
        }
        audioProfiles.append(profileObject);
    };

    {
        QJsonObject flac16Profile;
        flac16Profile.insert(QStringLiteral("name"), QStringLiteral("flac_16"));
        flac16Profile.insert(QStringLiteral("description"), QStringLiteral("FLAC 16-bits"));
        flac16Profile.insert(QStringLiteral("codec"), QStringLiteral("flac"));
        QJsonArray opts;
        opts.append(QStringLiteral("-compression_level"));
        opts.append(12);
        opts.append(QStringLiteral("-sample_fmt"));
        opts.append(QStringLiteral("s16"));
        flac16Profile.insert(QStringLiteral("opts"), opts);
        upsertAudioProfile(flac16Profile);
    }
    {
        QJsonObject flac24Profile;
        flac24Profile.insert(QStringLiteral("name"), QStringLiteral("flac_24"));
        flac24Profile.insert(QStringLiteral("description"), QStringLiteral("FLAC 24-bits"));
        flac24Profile.insert(QStringLiteral("codec"), QStringLiteral("flac"));
        QJsonArray opts;
        opts.append(QStringLiteral("-compression_level"));
        opts.append(12);
        opts.append(QStringLiteral("-sample_fmt"));
        opts.append(QStringLiteral("s32"));
        flac24Profile.insert(QStringLiteral("opts"), opts);
        upsertAudioProfile(flac24Profile);
    }
    root.insert(QStringLiteral("audio_profiles"), audioProfiles);

    bool audioProfileExists = false;
    for (int i = 0; i < audioProfiles.size(); ++i) {
        const QJsonObject audioProfileObject = audioProfiles.at(i).toObject();
        if (audioProfileObject.value(QStringLiteral("name")).toString() == selectedAudioProfile) {
            audioProfileExists = true;
            break;
        }
    }
    if (!audioProfileExists) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Unsupported audio profile selected: %1").arg(selectedAudioProfile);
        }
        return QString();
    }

    QJsonArray profiles = root.value(QStringLiteral("profiles")).toArray();
    bool profileFound = false;
    QJsonObject selectedProfileObject;
    for (int i = 0; i < profiles.size(); ++i) {
        const QJsonValue currentValue = profiles.at(i);
        if (!currentValue.isObject()) {
            continue;
        }
        QJsonObject profileObject = currentValue.toObject();
        if (profileObject.value(QStringLiteral("name")).toString() != selectedProfile) {
            continue;
        }
        profileObject.insert(QStringLiteral("audio_profile"), selectedAudioProfile);
        selectedProfileObject = profileObject;
        profiles.replace(i, profileObject);
        profileFound = true;
        break;
    }
    if (!profileFound) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Selected video profile is not available in the export configuration.");
        }
        return QString();
    }
    root.insert(QStringLiteral("profiles"), profiles);
    if (isFfv1ProfileName(selectedProfile) && ui->ffv1SlicesSpinBox) {
        const int ffv1Slices = qBound(1, ui->ffv1SlicesSpinBox->value(), 32);
        const QStringList selectedVideoProfiles = profileVideoProfileNames(selectedProfileObject);
        QJsonArray videoProfiles = root.value(QStringLiteral("video_profiles")).toArray();
        bool changedVideoProfiles = false;
        for (int i = 0; i < videoProfiles.size(); ++i) {
            if (!videoProfiles.at(i).isObject()) {
                continue;
            }
            QJsonObject videoProfileObject = videoProfiles.at(i).toObject();
            const QString videoProfileName = videoProfileObject.value(QStringLiteral("name")).toString();
            if (!selectedVideoProfiles.isEmpty() && !selectedVideoProfiles.contains(videoProfileName)) {
                continue;
            }
            const QString codec = videoProfileObject.value(QStringLiteral("codec")).toString();
            const bool isFfv1VideoProfile = codec.compare(QStringLiteral("ffv1"), Qt::CaseInsensitive) == 0
                                            || videoProfileName.contains(QStringLiteral("ffv1"), Qt::CaseInsensitive);
            if (!isFfv1VideoProfile) {
                continue;
            }
            setOrAppendNumericOption(&videoProfileObject, QStringLiteral("-slices"), ffv1Slices);
            videoProfiles.replace(i, videoProfileObject);
            changedVideoProfiles = true;
        }
        if (changedVideoProfiles) {
            root.insert(QStringLiteral("video_profiles"), videoProfiles);
        }
    }

    const QString configPath = QDir::temp().filePath(
        QStringLiteral("ld-analyse-export-config-%1.json")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile outputConfigFile(configPath);
    if (!outputConfigFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Failed to write temporary export configuration.");
        }
        return QString();
    }
    outputConfigFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    outputConfigFile.close();

    cleanupDumpDir();
    temporaryExportConfigPath = configPath;
    return temporaryExportConfigPath;
}

QString ExportDialog::createTemporaryMetadataSnapshot(QString *errorMessage)
{
    cleanupTemporaryMetadataSnapshot();

    if (!tbcSource) {
        if (errorMessage) {
            *errorMessage = tr("No source loaded.");
        }
        return QString();
    }

    const QString tempPath = QDir::temp().filePath(
        QStringLiteral("ld-analyse-export-%1.tbc.json")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    QString snapshotError;
    if (!tbcSource->writeMetadataSnapshot(tempPath, &snapshotError)) {
        if (errorMessage) {
            *errorMessage = snapshotError.isEmpty()
                                ? tr("Failed to create metadata snapshot.")
                                : snapshotError;
        }
        return QString();
    }

    temporaryInputJsonPath = tempPath;
    return temporaryInputJsonPath;
}

void ExportDialog::cleanupTemporaryMetadataSnapshot()
{
    if (!temporaryInputJsonPath.isEmpty()) {
        QFile::remove(temporaryInputJsonPath);
        temporaryInputJsonPath.clear();
    }
    if (!temporaryExportConfigPath.isEmpty()) {
        QFile::remove(temporaryExportConfigPath);
        temporaryExportConfigPath.clear();
    }
    for (const QString &path : temporaryAudioTrackPaths) {
        QFile::remove(path);
    }
    temporaryAudioTrackPaths.clear();
}

void ExportDialog::appendStatus(const QString &message)
{
    ui->statusLabel->setText(message);
}

void ExportDialog::appendLog(const QString &message)
{
    if (!ui || !ui->logTextEdit) {
        return;
    }
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString line = QStringLiteral("[%1] %2").arg(timestamp, trimmed);
    ui->logTextEdit->appendPlainText(line);
}

QString ExportDialog::formatCommand(const QString &program, const QStringList &args) const
{
    QStringList parts;
    parts << quoteArgument(program);
    for (const QString &arg : args) {
        parts << quoteArgument(arg);
    }
    return parts.join(' ');
}
