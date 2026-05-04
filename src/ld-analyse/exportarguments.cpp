#include "exportarguments.h"

namespace ExportArguments {

bool shouldDisableDropoutCorrection(const QString &dropoutMode, int startFrameOneBased)
{
    Q_UNUSED(startFrameOneBased);
    const QString normalizedMode = dropoutMode.trimmed().toLower();
    return normalizedMode == QStringLiteral("disabled");
}

}
