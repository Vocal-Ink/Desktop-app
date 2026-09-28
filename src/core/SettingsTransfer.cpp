#include "core/SettingsTransfer.h"

#include <QCoreApplication>

// Placeholder implementation (replaced by the text work package).
namespace SettingsTransfer {

bool exportTo(const QString &, Settings *, QString *error)
{
    if (error)
        *error = QCoreApplication::translate("SettingsTransfer", "Not available in this build.");
    return false;
}

bool importFrom(const QString &, Settings *, QString *error)
{
    if (error)
        *error = QCoreApplication::translate("SettingsTransfer", "Not available in this build.");
    return false;
}

} // namespace SettingsTransfer
