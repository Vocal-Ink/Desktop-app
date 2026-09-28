#include "core/HistoryModel.h"

#include <QLocale>

HistoryModel::HistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole: {
        QString prefix;
        switch (e.status) {
        case Status::Queued: prefix = QStringLiteral("⏳ "); break;   // hourglass
        case Status::Speaking: prefix = QStringLiteral("▶ "); break; // play
        case Status::Failed: prefix = QStringLiteral("⚠ "); break;   // warning
        case Status::Stopped: prefix = QStringLiteral("■ "); break;  // stop
        case Status::Done: break;
        }
        return prefix + e.text;
    }
    case Qt::ToolTipRole: {
        QString tip = QLocale().toString(e.time.time(), QLocale::ShortFormat);
        if (!e.voiceName.isEmpty())
            tip += QStringLiteral(" • ") + e.voiceName;
        if (!e.error.isEmpty())
            tip += QStringLiteral("\n") + e.error;
        return tip;
    }
    case Qt::AccessibleTextRole:
        return e.text;
    case TextRole:
        return e.text;
    case TimeRole:
        return e.time;
    case VoiceNameRole:
        return e.voiceName;
    case StatusRole:
        return int(e.status);
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {{TextRole, "text"}, {TimeRole, "time"}, {VoiceNameRole, "voiceName"}, {StatusRole, "status"}};
}

void HistoryModel::add(quint64 id, const QString &text, const QString &voiceName)
{
    beginInsertRows({}, 0, 0);
    m_entries.prepend(Entry{id, text, voiceName, QDateTime::currentDateTime(), Status::Queued, {}});
    endInsertRows();
    if (m_entries.size() > MaxEntries) {
        const int first = MaxEntries;
        const int last = int(m_entries.size()) - 1;
        beginRemoveRows({}, first, last);
        m_entries.erase(m_entries.begin() + first, m_entries.end());
        endRemoveRows();
    }
}

int HistoryModel::rowOf(quint64 id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id)
            return i;
    }
    return -1;
}

void HistoryModel::setStatus(quint64 id, Status status, const QString &error)
{
    const int row = rowOf(id);
    if (row < 0)
        return;
    m_entries[row].status = status;
    m_entries[row].error = error;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

const HistoryModel::Entry *HistoryModel::entry(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return nullptr;
    return &m_entries.at(row);
}

QString HistoryModel::lastText() const
{
    return m_entries.isEmpty() ? QString() : m_entries.first().text;
}

QStringList HistoryModel::texts() const
{
    QStringList list;
    list.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        if (list.isEmpty() || list.last() != e.text)
            list << e.text;
    }
    return list;
}

void HistoryModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}
