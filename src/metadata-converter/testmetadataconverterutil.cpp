/************************************************************************

    testmetadataconverterutil.cpp

    Sanity tests for metadata converter utility helpers.

************************************************************************/

#include <QCoreApplication>
#include <QDir>
#include <QtGlobal>

#include <cassert>

#include "metadataconverterutil.h"

namespace {
void testDirectionInference()
{
    using MetadataConverterUtil::MetadataConversionDirection;

    assert(MetadataConverterUtil::inferMetadataConversionDirection(QStringLiteral("captures/input.json"))
           == MetadataConversionDirection::JsonToSqlite);
    assert(MetadataConverterUtil::inferMetadataConversionDirection(QStringLiteral("captures/input.db"))
           == MetadataConversionDirection::SqliteToJson);
    assert(MetadataConverterUtil::inferMetadataConversionDirection(QStringLiteral("captures\\input.json"))
           == MetadataConversionDirection::JsonToSqlite);
    assert(MetadataConverterUtil::inferMetadataConversionDirection(QStringLiteral("captures\\input.db"))
           == MetadataConversionDirection::SqliteToJson);
    assert(MetadataConverterUtil::inferMetadataConversionDirection(QStringLiteral("captures/input.txt"))
           == MetadataConversionDirection::Unknown);
}

void assertNativeSeparatorsOnly(const QString &path)
{
    const QChar nativeSeparator = QDir::separator();
    const QChar alternateSeparator = (nativeSeparator == QLatin1Char('/')) ? QLatin1Char('\\') : QLatin1Char('/');
    assert(!path.contains(alternateSeparator));
}

void testOutputPathFormatting()
{
    using MetadataConverterUtil::MetadataConversionDirection;

    const QString outputFromForwardSlashes = MetadataConverterUtil::defaultMetadataOutputPath(
        QStringLiteral("captures/input.json"),
        MetadataConversionDirection::JsonToSqlite);
    assert(outputFromForwardSlashes.endsWith(QStringLiteral(".db")));
    assertNativeSeparatorsOnly(outputFromForwardSlashes);

    const QString outputFromBackslashes = MetadataConverterUtil::defaultMetadataOutputPath(
        QStringLiteral("captures\\input.db"),
        MetadataConversionDirection::SqliteToJson);
    assert(outputFromBackslashes.endsWith(QStringLiteral(".json")));
    assertNativeSeparatorsOnly(outputFromBackslashes);

    const QString unknownDirectionOutput = MetadataConverterUtil::defaultMetadataOutputPath(
        QStringLiteral("captures/input.unknown"),
        MetadataConversionDirection::Unknown);
    assert(unknownDirectionOutput.isEmpty());
}

void testPathNormalization()
{
    const QString normalizedMixed = MetadataConverterUtil::normalizePathForCurrentPlatform(
        QStringLiteral("captures\\nested/path\\input.json"));
    assertNativeSeparatorsOnly(normalizedMixed);
    assert(normalizedMixed.endsWith(QStringLiteral("input.json")));
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testDirectionInference();
    testOutputPathFormatting();
    testPathNormalization();

    return 0;
}
