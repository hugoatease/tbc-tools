#ifndef METADATACONVERTERUTIL_H
#define METADATACONVERTERUTIL_H

#include <QString>

namespace MetadataConverterUtil {
enum class MetadataConversionDirection {
    Unknown,
    JsonToSqlite,
    SqliteToJson
};

QString normalizePathForCurrentPlatform(const QString &path);
MetadataConversionDirection inferMetadataConversionDirection(const QString &inputFilename);
QString metadataConversionDirectionArgument(MetadataConversionDirection direction);
QString resolveMetadataConverterPath();
QString defaultMetadataOutputPath(const QString &inputFilename, bool jsonToSqlite);
QString defaultMetadataOutputPath(const QString &inputFilename, MetadataConversionDirection direction);
QString resolveExportDecodeMetadataPath();
QString defaultExportDecodeMetadataOutputPath(const QString &inputFilename);
bool runMetadataConverter(const QString &direction,
                          const QString &inputFilename,
                          const QString &outputFilename,
                          QString *errorMessage = nullptr);
bool runMetadataConverter(MetadataConversionDirection direction,
                          const QString &inputFilename,
                          const QString &outputFilename,
                          QString *errorMessage = nullptr);
bool runExportDecodeMetadata(const QString &inputFilename,
                             const QString &outputFilename,
                             QString *errorMessage = nullptr);
}

#endif // METADATACONVERTERUTIL_H
