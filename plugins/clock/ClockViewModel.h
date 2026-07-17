// ClockViewModel.h — Clock 数据模型（Q_PROPERTY timeText + QTimer 1s）
#pragma once
#include <QObject>
#include <QString>

class QTimer;

class ClockViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString timeText READ timeText NOTIFY timeTextChanged)

public:
    explicit ClockViewModel(QObject* parent = nullptr);
    ~ClockViewModel() override;

    QString timeText() const { return m_timeText; }
    void setFormat(const QString& f);
    void start();
    void stop();

signals:
    void timeTextChanged();

private:
    void refresh();

    QTimer* m_timer;
    QString m_timeText;
    QString m_format = QStringLiteral("HH:mm:ss");
};
