#pragma once

#include <QString>

#ifndef GTD_APP_VERSION
#define GTD_APP_VERSION "0.0.0"
#endif

namespace gtd {

inline const QString ApplicationVersion = QStringLiteral(GTD_APP_VERSION);

}
