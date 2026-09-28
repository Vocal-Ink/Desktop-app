#include "stt/SttEngines.h"

// Placeholder: the real recognisers are added by the STT work package.
SttEngine *createSttEngine(const QString &id, const EngineContext &context, QObject *parent)
{
    Q_UNUSED(id)
    Q_UNUSED(context)
    Q_UNUSED(parent)
    return nullptr;
}

QStringList availableSttEngineIds()
{
    return {};
}
