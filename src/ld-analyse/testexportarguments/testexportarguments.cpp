/************************************************************************

    testexportarguments.cpp

    Unit tests for export argument helpers
    Copyright (C) 2026 Simon Inns

    This file is part of ld-decode-tools.

    ld-decode-tools is free software: you can redistribute it and/or
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

#include <cassert>
#include <iostream>

#include "exportarguments.h"

using std::cerr;

void testDropoutDisablePolicy()
{
    cerr << "Testing ExportArguments::shouldDisableDropoutCorrection\n";

    // Non-disabled modes should remain enabled regardless of range start.
    assert(!ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("basic"), 1));
    assert(!ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("basic"), 250));
    assert(!ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("heavy"), 1));
    assert(!ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("heavy"), 250));

    // Disabled mode must always disable dropout correction.
    assert(ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("disabled"), 1));
    assert(ExportArguments::shouldDisableDropoutCorrection(QStringLiteral("disabled"), 250));
}

int main()
{
    testDropoutDisablePolicy();
    return 0;
}
