#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>

// Messages the user has said this session, newest first.
class HistoryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { TextRole = Qt::UserRole + 1, TimeRole, VoiceNameRole, StatusRole, IdRole, ErrorRole, TimeTextRole };
    enum class Status { Queued, Speaking, Done, Failed, Stopped };
    Q_ENUM(Status)

    struct Entry
    {
        quint64 id = 0;
        QString text;
        QString voiceName;
        QDateTime time;
        Status status = Status::Queued;
        QString error;
    };

    explicit HistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void add(quint64 id, const QString &text, const QString &voiceName);
    void setStatus(quint64 id, Status status, const QString &error = {});
    const Entry *entry(int row) const;
    QString lastText() const;
    // Previous messages for Up/Down recall in the message box (newest first).
    QStringList texts() const;
    void clear();

    static constexpr int MaxEntries = 500;

private:
    int rowOf(quint64 id) const;
    QList<Entry> m_entries;
};
