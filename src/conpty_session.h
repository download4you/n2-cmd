#pragma once
#include "app_types.h"
#include <QObject>
#include <QByteArray>
#include <memory>

class ConPtySession : public QObject {
    Q_OBJECT
public:
    explicit ConPtySession(QObject* parent = nullptr);
    ~ConPtySession() override;
    bool start(const ShellProfile& profile, const QString& cwd, int columns, int rows, QString* error = nullptr);
    bool write(const QByteArray& bytes);
    bool resize(int columns, int rows);
    bool isRunning() const;
    QString workingDirectory() const;
    void stop();
signals:
    void outputReady(const QByteArray& bytes);
    void exited(int code);
    void errorOccurred(const QString& message);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
