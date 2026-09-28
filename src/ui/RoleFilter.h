#pragma once

#include <QSortFilterProxyModel>

// Shows only the rows whose `role` equals `value` (empty value = all rows).
// QML: RoleFilter { sourceModel: App.phrases; role: "category"; value: "Stream" }
class RoleFilter : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString role READ role WRITE setRole NOTIFY changed)
    Q_PROPERTY(QString value READ value WRITE setValue NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit RoleFilter(QObject *parent = nullptr);

    QString role() const { return m_role; }
    void setRole(const QString &role);
    QString value() const { return m_value; }
    void setValue(const QString &value);
    int count() const { return rowCount(); }

    Q_INVOKABLE int sourceRow(int row) const;

signals:
    void changed();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int roleId() const;

    QString m_role;
    QString m_value;
};
