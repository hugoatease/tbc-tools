#ifndef EXPORTARGUMENTS_H
#define EXPORTARGUMENTS_H

#include <QString>

namespace ExportArguments {
bool shouldDisableDropoutCorrection(const QString &dropoutMode, int startFrameOneBased);
bool isDefaultActiveAreaFraming(const QString &resolutionMode, bool hasAnyVerticalLineAdjustment);
}

#endif // EXPORTARGUMENTS_H
