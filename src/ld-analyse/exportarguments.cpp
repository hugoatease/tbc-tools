#include "exportarguments.h"

namespace ExportArguments {

bool shouldDisableDropoutCorrection(const QString &dropoutMode, int startFrameOneBased)
{
    Q_UNUSED(startFrameOneBased);
    const QString normalizedMode = dropoutMode.trimmed().toLower();
    return normalizedMode == QStringLiteral("disabled");
}
bool isDefaultActiveAreaFraming(const QString &resolutionMode, bool hasAnyVerticalLineAdjustment)
{
    const QString normalizedMode = resolutionMode.trimmed().toLower();
    return normalizedMode == QStringLiteral("active_area") && !hasAnyVerticalLineAdjustment;
}

}
