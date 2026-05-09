/************************************************************************

    teletextintegration.cpp

    ld-process-vbi - VBI and IEC NTSC specific processor for ld-decode
    Copyright (C) 2018-2026 Simon Inns
    Copyright (C) 2026 Harry Munday

    This file is part of ld-decode-tools.

    ld-process-vbi is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include "teletextintegration.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

namespace {
void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

void appendUniqueCandidate(QStringList &candidates, const QString &candidate)
{
    if (candidate.trimmed().isEmpty()) {
        return;
    }
    if (!candidates.contains(candidate)) {
        candidates.append(candidate);
    }
}

bool isTeletextVendorDirectory(const QString &directoryPath)
{
    if (directoryPath.trimmed().isEmpty()) {
        return false;
    }

    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return false;
    }

    const QString teletextModulePath = directory.filePath(QStringLiteral("teletext/__main__.py"));
    return QFileInfo::exists(teletextModulePath);
}

QString resolveTeletextVendorDirectory()
{
    QStringList candidates;

#ifdef TELETEXT_VENDOR_DIR
    appendUniqueCandidate(candidates, QString::fromUtf8(TELETEXT_VENDOR_DIR));
#endif

    const QString appDirectory = QCoreApplication::applicationDirPath();
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("vendor/vhs-teletext")));
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("../vendor/vhs-teletext")));
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("../../vendor/vhs-teletext")));

    for (const QString &candidate : candidates) {
        if (isTeletextVendorDirectory(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QString();
}
QString runPythonProbe(const QString &pythonExecutable,
                       const QString &script,
                       const QProcessEnvironment &environment,
                       bool *ok)
{
    if (ok) {
        *ok = false;
    }

    QProcess probeProcess;
    probeProcess.setProcessEnvironment(environment);
    probeProcess.start(pythonExecutable, QStringList() << QStringLiteral("-c") << script);

    if (!probeProcess.waitForStarted(5000)) {
        return QStringLiteral("python probe failed to start");
    }
    if (!probeProcess.waitForFinished(30000)) {
        probeProcess.kill();
        probeProcess.waitForFinished(1000);
        return QStringLiteral("python probe timed out");
    }

    const QString stdoutText = QString::fromLocal8Bit(probeProcess.readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromLocal8Bit(probeProcess.readAllStandardError()).trimmed();

    if (probeProcess.exitStatus() != QProcess::NormalExit || probeProcess.exitCode() != 0) {
        QString errorDetails = stderrText;
        if (errorDetails.isEmpty()) {
            errorDetails = stdoutText;
        }
        if (errorDetails.isEmpty()) {
            errorDetails = QStringLiteral("unknown error");
        }
        return errorDetails;
    }

    if (ok) {
        *ok = true;
    }
    return stdoutText;
}

QString resolvePythonExecutable()
{
    const QString overrideValue =
        qEnvironmentVariable(QStringLiteral("TELETEXT_PYTHON")).trimmed();
    if (!overrideValue.isEmpty()) {
        const QString overrideExecutable = QStandardPaths::findExecutable(overrideValue);
        if (!overrideExecutable.isEmpty()) {
            return overrideExecutable;
        }

        const QFileInfo overrideInfo(overrideValue);
        if (overrideInfo.exists() && overrideInfo.isFile() && overrideInfo.isExecutable()) {
            return overrideInfo.absoluteFilePath();
        }
    }
    const QStringList candidates = {
        QStringLiteral("python3"),
        QStringLiteral("python")
    };

    for (const QString &candidate : candidates) {
        const QString executablePath = QStandardPaths::findExecutable(candidate);
        if (!executablePath.isEmpty()) {
            return executablePath;
        }
    }

    return QString();
}

bool runPythonStep(const QString &pythonExecutable,
                   const QStringList &arguments,
                   const QProcessEnvironment &environment,
                   const QString &stepDescription,
                   QString *errorMessage)
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(pythonExecutable, arguments);

    if (!process.waitForStarted(5000)) {
        setError(errorMessage, QObject::tr("Could not start %1 step.").arg(stepDescription));
        return false;
    }

    if (!process.waitForFinished(-1)) {
        process.kill();
        process.waitForFinished(1000);
        setError(errorMessage, QObject::tr("%1 step timed out.").arg(stepDescription));
        return false;
    }

    const QString standardOutput = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString standardError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (!standardOutput.isEmpty()) {
        qInfo().noquote() << standardOutput;
    }
    if (!standardError.isEmpty()) {
        qWarning().noquote() << standardError;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError(errorMessage,
                 QObject::tr("%1 step failed with exit code %2.")
                     .arg(stepDescription)
                     .arg(process.exitCode()));
        return false;
    }

    return true;
}

bool copyFileReplacing(const QString &sourcePath, const QString &targetPath, QString *errorMessage)
{
    if (!QFileInfo::exists(sourcePath)) {
        return true;
    }

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        setError(errorMessage, QObject::tr("Could not replace existing file: %1").arg(targetPath));
        return false;
    }

    if (!QFile::copy(sourcePath, targetPath)) {
        setError(errorMessage, QObject::tr("Could not copy file from %1 to %2").arg(sourcePath, targetPath));
        return false;
    }

    return true;
}

void ensureCssSwitchScript(const QString &outputDirectory)
{
    const QString cssSwitchPath = QDir(outputDirectory).filePath(QStringLiteral("cssswitch.js"));
    if (QFileInfo::exists(cssSwitchPath)) {
        return;
    }

    QFile cssSwitchFile(cssSwitchPath);
    if (!cssSwitchFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&cssSwitchFile);
    stream << "function set_style_from_cookie() {\n";
    stream << "    // Placeholder helper for generated teletext HTML pages.\n";
    stream << "}\n";
    cssSwitchFile.close();
}
}

bool runTeletextHtmlExport(const TeletextIntegrationOptions &options, QString *errorMessage)
{
    setError(errorMessage, QString());

    const QString inputFilename = options.inputFilename.trimmed();
    const QString outputDirectoryPath = QDir::cleanPath(options.outputDirectory.trimmed());

    if (inputFilename.isEmpty()) {
        setError(errorMessage, QObject::tr("Teletext export input filename is empty."));
        return false;
    }
    if (!QFileInfo::exists(inputFilename)) {
        setError(errorMessage, QObject::tr("Teletext export input file does not exist: %1").arg(inputFilename));
        return false;
    }
    if (outputDirectoryPath.isEmpty()) {
        setError(errorMessage, QObject::tr("Teletext export output directory is empty."));
        return false;
    }

    QDir outputDirectory(outputDirectoryPath);
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        setError(errorMessage, QObject::tr("Could not create teletext output directory: %1").arg(outputDirectoryPath));
        return false;
    }

    const QString vendorDirectory = resolveTeletextVendorDirectory();
    if (vendorDirectory.isEmpty()) {
        setError(errorMessage, QObject::tr("Could not locate vendored vhs-teletext runtime directory."));
        return false;
    }

    const QString pythonExecutable = resolvePythonExecutable();
    if (pythonExecutable.isEmpty()) {
        setError(errorMessage, QObject::tr("Could not locate Python interpreter (python3/python)."));
        return false;
    }
    qInfo() << "Teletext export: using Python executable:" << pythonExecutable;
    if (!qEnvironmentVariableIsEmpty("TELETEXT_PYTHON")) {
        qInfo() << "Teletext export: TELETEXT_PYTHON override set to"
                << qEnvironmentVariable("TELETEXT_PYTHON");
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
    if (existingPythonPath.isEmpty()) {
        environment.insert(QStringLiteral("PYTHONPATH"), vendorDirectory);
    } else {
        environment.insert(QStringLiteral("PYTHONPATH"),
                           vendorDirectory + QDir::listSeparator() + existingPythonPath);
    }
    const QString forceCpuRaw = qEnvironmentVariable("TELETEXT_FORCE_CPU").trimmed().toLower();
    const bool forceCpu =
        (forceCpuRaw == QStringLiteral("1")
         || forceCpuRaw == QStringLiteral("true")
         || forceCpuRaw == QStringLiteral("yes"));
    environment.insert(QStringLiteral("USE_GPU"), forceCpu ? QStringLiteral("0") : QStringLiteral("1"));
    if (forceCpu) {
        qInfo() << "Teletext export: GPU acceleration disabled by TELETEXT_FORCE_CPU";
    }

    const QString dependencyProbeScript = QStringLiteral(R"PY(
import importlib,sys
required=("numpy","scipy","matplotlib","click","tqdm","zmq","watchdog","serial")
missing=[m for m in required if importlib.util.find_spec(m) is None]
if missing:
    print("missing:"+",".join(missing))
    sys.exit(2)
print("ok")
)PY");
    bool dependencyProbeOk = false;
    const QString dependencyProbeOutput =
        runPythonProbe(pythonExecutable, dependencyProbeScript, environment, &dependencyProbeOk);
    if (!dependencyProbeOk) {
        setError(errorMessage,
                 QObject::tr("Teletext Python dependency check failed: %1")
                     .arg(dependencyProbeOutput));
        return false;
    }
    if (dependencyProbeOutput.startsWith(QStringLiteral("missing:"))) {
        setError(errorMessage,
                 QObject::tr("Teletext Python dependencies missing: %1")
                     .arg(dependencyProbeOutput.mid(QStringLiteral("missing:").size())));
        return false;
    }

    const QString backendProbeScript = QStringLiteral(R"PY(
import importlib.util
print("pycuda=" + ("yes" if importlib.util.find_spec("pycuda") else "no"))
print("pyopencl=" + ("yes" if importlib.util.find_spec("pyopencl") else "no"))
)PY");
    bool backendProbeOk = false;
    const QString backendProbeOutput =
        runPythonProbe(pythonExecutable, backendProbeScript, environment, &backendProbeOk);
    if (backendProbeOk) {
        const QStringList backendLines = backendProbeOutput.split(
            QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        for (const QString &line : backendLines) {
            qInfo() << "Teletext export backend probe:" << line.trimmed();
        }
    } else {
        qWarning() << "Teletext export: GPU backend probe failed:" << backendProbeOutput;
    }

    const qint32 teletextThreads = qMax<qint32>(1, options.maxThreads);
    const qint32 minDuplicates = qMax<qint32>(1, options.minDuplicates);
    const QString tapeFormat = options.tapeFormat.trimmed().isEmpty()
        ? QStringLiteral("vhs")
        : options.tapeFormat.trimmed();

    const QString rawStreamPath = outputDirectory.filePath(QStringLiteral("teletext_raw.t42"));
    const QString squashedStreamPath = outputDirectory.filePath(QStringLiteral("teletext_squashed.t42"));

    if (forceCpu) {
        qInfo() << "Teletext export: starting deconvolution with CPU backend";
    } else {
        qInfo() << "Teletext export: starting deconvolution (CUDA/OpenCL auto-detect enabled)";
    }
    const QStringList deconvolveArguments = {
        QStringLiteral("-m"), QStringLiteral("teletext"),
        QStringLiteral("deconvolve"),
        QStringLiteral("-c"), QStringLiteral("tbc"),
        QStringLiteral("--tape-format"), tapeFormat,
        QStringLiteral("--threads"), QString::number(teletextThreads),
        QStringLiteral("--no-progress"),
        QStringLiteral("-o"), QStringLiteral("bytes"), rawStreamPath,
        inputFilename
    };
    if (!runPythonStep(pythonExecutable, deconvolveArguments, environment,
                       QObject::tr("Teletext deconvolution"), errorMessage)) {
        return false;
    }

    qInfo() << "Teletext export: squashing duplicate packets";
    const QStringList squashArguments = {
        QStringLiteral("-m"), QStringLiteral("teletext"),
        QStringLiteral("squash"),
        QStringLiteral("--min-duplicates"), QString::number(minDuplicates),
        QStringLiteral("--no-progress"),
        QStringLiteral("-o"), QStringLiteral("bytes"), squashedStreamPath,
        rawStreamPath
    };
    if (!runPythonStep(pythonExecutable, squashArguments, environment,
                       QObject::tr("Teletext squash"), errorMessage)) {
        return false;
    }

    qInfo() << "Teletext export: generating HTML pages";
    const QStringList htmlArguments = {
        QStringLiteral("-m"), QStringLiteral("teletext"),
        QStringLiteral("html"),
        QStringLiteral("--no-progress"),
        outputDirectoryPath,
        squashedStreamPath
    };
    if (!runPythonStep(pythonExecutable, htmlArguments, environment,
                       QObject::tr("Teletext HTML generation"), errorMessage)) {
        return false;
    }

    if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext.css")),
                           outputDirectory.filePath(QStringLiteral("teletext.css")),
                           errorMessage)) {
        return false;
    }
    if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext-noscanlines.css")),
                           outputDirectory.filePath(QStringLiteral("teletext-noscanlines.css")),
                           errorMessage)) {
        return false;
    }
    ensureCssSwitchScript(outputDirectoryPath);

    const QStringList generatedHtmlFiles = outputDirectory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    if (generatedHtmlFiles.isEmpty()) {
        qWarning() << "Teletext export finished but generated no HTML pages";
    } else {
        qInfo() << "Teletext export complete - generated" << generatedHtmlFiles.size() << "HTML pages in"
                << outputDirectoryPath;
    }

    return true;
}
