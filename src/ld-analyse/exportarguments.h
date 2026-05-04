#ifndef EXPORTARGUMENTS_H
#define EXPORTARGUMENTS_H

#include <QString>

namespace ExportArguments {
bool shouldDisableDropoutCorrection(const QString &dropoutMode, int startFrameOneBased);
}

#endif // EXPORTARGUMENTS_H
