#pragma once

#include <QString>
#include <QStringList>

class Backend;

// The command line: the same Backend the window drives, with nothing drawing
// it. `omafont list`, `info`, `install` and `remove`; `--json` for scripts.
namespace Cli {
bool isVerb(const QString &word);
int run(Backend &backend, const QStringList &words);
}
