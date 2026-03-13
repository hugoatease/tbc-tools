#include "metadataconverterutil.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
namespace {
QString normalizeForPathParsing(const QString &path)
{
    QString normalized = path.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(normalized);
}

MetadataConverterUtil::MetadataConversionDirection metadataDirectionFromString(const QString &direction)
{
    const QString directionValue = direction.trimmed().toLower();
    if (directionValue == QLatin1String("json-to-sqlite")) {
        return MetadataConverterUtil::MetadataConversionDirection::JsonToSqlite;
    }
    if (directionValue == QLatin1String("sqlite-to-json")) {
        return MetadataConverterUtil::MetadataConversionDirection::SqliteToJson;
    }
    return MetadataConverterUtil::MetadataConversionDirection::Unknown;
}

QString outputPathWithSuffix(const QFileInfo &info, const QString &fallbackInput, const QString &suffix)
{
    const QString baseName = info.completeBaseName();
    if (baseName.isEmpty()) {
        return fallbackInput + suffix;
    }

    const QString path = info.path();
    if (path.isEmpty() || path == QLatin1String(".")) {
        return baseName + suffix;
    }
    return path + QLatin1Char('/') + baseName + suffix;
}
} // namespace

namespace MetadataConverterUtil {
QString normalizePathForCurrentPlatform(const QString &path)
{
    const QString normalized = normalizeForPathParsing(path);
    if (normalized.isEmpty()) {
        return QString();
    }
    return QDir::toNativeSeparators(normalized);
}

MetadataConversionDirection inferMetadataConversionDirection(const QString &inputFilename)
{
    const QString parsePath = normalizeForPathParsing(inputFilename);
    if (parsePath.isEmpty()) {
        return MetadataConversionDirection::Unknown;
    }

    const QString suffix = QFileInfo(parsePath).suffix().toLower();
    if (suffix == QLatin1String("json")) {
        return MetadataConversionDirection::JsonToSqlite;
    }
    if (suffix == QLatin1String("db")) {
        return MetadataConversionDirection::SqliteToJson;
    }
    return MetadataConversionDirection::Unknown;
}

QString metadataConversionDirectionArgument(MetadataConversionDirection direction)
{
    switch (direction) {
    case MetadataConversionDirection::JsonToSqlite:
        return QStringLiteral("json-to-sqlite");
    case MetadataConversionDirection::SqliteToJson:
        return QStringLiteral("sqlite-to-json");
    case MetadataConversionDirection::Unknown:
    default:
        return QString();
    }
}
QString resolveMetadataConverterPath()
{
    const QString appPath = QCoreApplication::applicationFilePath();
    if (!appPath.isEmpty() && QFileInfo::exists(appPath)) {
        return appPath;
    }

    const QStringList converterNames = {
        QStringLiteral("tbc-metadata-converter"),
        QStringLiteral("metadata-converter"),
        QStringLiteral("ld-json-converter")
    };

    for (const QString &name : converterNames) {
        const QString converterPath = QStandardPaths::findExecutable(name);
        if (!converterPath.isEmpty()) {
            return converterPath;
        }
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidateDirs = {
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("../bin"),
        QStringLiteral("../../bin")
    };

    for (const QString &dir : candidateDirs) {
        for (const QString &name : converterNames) {
            const QString localCandidate = QDir(appDir).filePath(dir + QLatin1Char('/') + name);
            if (QFileInfo::exists(localCandidate)) {
                return localCandidate;
            }
        }
    }

    return QString();
}

QString defaultMetadataOutputPath(const QString &inputFilename, bool jsonToSqlite)
{
    return defaultMetadataOutputPath(inputFilename,
                                     jsonToSqlite ? MetadataConversionDirection::JsonToSqlite
                                                  : MetadataConversionDirection::SqliteToJson);
}

QString defaultMetadataOutputPath(const QString &inputFilename, MetadataConversionDirection direction)
{
    const QString normalizedInput = normalizeForPathParsing(inputFilename);
    if (normalizedInput.isEmpty()) {
        return QString();
    }

    const QFileInfo info(normalizedInput);
    QString outputPath;
    switch (direction) {
    case MetadataConversionDirection::JsonToSqlite:
        outputPath = info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0
                         ? outputPathWithSuffix(info, normalizedInput, QStringLiteral(".db"))
                         : normalizedInput + QStringLiteral(".db");
        break;
    case MetadataConversionDirection::SqliteToJson:
        outputPath = info.suffix().compare(QStringLiteral("db"), Qt::CaseInsensitive) == 0
                         ? outputPathWithSuffix(info, normalizedInput, QStringLiteral(".json"))
                         : normalizedInput + QStringLiteral(".json");
        break;
    case MetadataConversionDirection::Unknown:
    default:
        return QString();
    }

    return QDir::toNativeSeparators(outputPath);
}

bool runMetadataConverter(const QString &direction,
                          const QString &inputFilename,
                          const QString &outputFilename,
                          QString *errorMessage)
{
    return runMetadataConverter(metadataDirectionFromString(direction), inputFilename, outputFilename, errorMessage);
}

bool runMetadataConverter(MetadataConversionDirection direction,
                          const QString &inputFilename,
                          const QString &outputFilename,
                          QString *errorMessage)
{
    const QString converterPath = resolveMetadataConverterPath();
    if (converterPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("tbc-metadata-converter not found in PATH or alongside the application.");
        }
        return false;
    }
    if (direction == MetadataConversionDirection::Unknown) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not determine metadata conversion direction from input file extension.");
        }
        return false;
    }

    const QString normalizedInput = normalizePathForCurrentPlatform(inputFilename);
    const QString normalizedOutput = normalizePathForCurrentPlatform(outputFilename);
    const QString directionArgument = metadataConversionDirectionArgument(direction);

    QStringList arguments;
    arguments << QStringLiteral("--direction") << directionArgument;
    if (direction == MetadataConversionDirection::JsonToSqlite) {
        arguments << QStringLiteral("--input-json") << normalizedInput
                  << QStringLiteral("--output-sqlite") << normalizedOutput;
    } else {
        arguments << QStringLiteral("--input-sqlite") << normalizedInput
                  << QStringLiteral("--output-json") << normalizedOutput;
    }

    QProcess process;
    process.start(converterPath, arguments);
    if (!process.waitForFinished(-1)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("tbc-metadata-converter did not finish.");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? stdOut : stdErr;
            if (errorMessage->isEmpty()) {
                *errorMessage = QObject::tr("tbc-metadata-converter failed with exit code %1.").arg(process.exitCode());
            }
        }
        return false;
    }

    return true;
}
} // namespace MetadataConverterUtil
