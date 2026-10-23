#include "conpty_session.h"
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTimer>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace {
QString windowsError(DWORD code) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    const QString result = message ? QString::fromWCharArray(message).trimmed() : QString::number(code);
    if (message) LocalFree(message);
    return result;
}

QString quoteArgument(const QString& value) {
    QString result = QStringLiteral("\"");
    int slashes = 0;
    for (QChar ch : value) {
        if (ch == QLatin1Char('\\')) { ++slashes; continue; }
        if (ch == QLatin1Char('"')) {
            result += QString(slashes * 2 + 1, QLatin1Char('\\'));
        } else {
            result += QString(slashes, QLatin1Char('\\'));
        }
        slashes = 0;
        result += ch;
    }
    result += QString(slashes * 2, QLatin1Char('\\'));
    result += QLatin1Char('"');
    return result;
}

void closeHandle(HANDLE& handle) {
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handle = nullptr;
}
}

struct ConPtySession::Impl {
    HANDLE input = nullptr;
    HANDLE output = nullptr;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HPCON console = nullptr;
    std::thread reader;
    std::mutex inputMutex;
    std::atomic_bool running{false};
    std::atomic_uint generation{0};
    QString cwd;
    QTimer* poll = nullptr;
};

ConPtySession::ConPtySession(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {
    impl_->poll = new QTimer(this);
    impl_->poll->setInterval(80);
    connect(impl_->poll, &QTimer::timeout, this, [this] {
        if (!impl_->process || WaitForSingleObject(impl_->process, 0) != WAIT_OBJECT_0) return;
        DWORD code = 0;
        GetExitCodeProcess(impl_->process, &code);
        impl_->poll->stop();
        impl_->running = false;
        emit exited(static_cast<int>(code));
    });
}

ConPtySession::~ConPtySession() { stop(); }

bool ConPtySession::start(const ShellProfile& profile, const QString& requestedCwd, int columns, int rows, QString* error) {
    stop();
    if (profile.executable.isEmpty()) {
        if (error) *error = tr("The shell executable is empty.");
        return false;
    }
    impl_->cwd = requestedCwd.isEmpty() ? QDir::homePath() : requestedCwd;
    if (!QFileInfo(impl_->cwd).isDir()) {
        if (error) *error = tr("The working directory does not exist: %1").arg(impl_->cwd);
        return false;
    }
    HANDLE consoleInput = nullptr, consoleOutput = nullptr;
    auto fail = [&](QString reason) {
        closeHandle(consoleInput);
        closeHandle(consoleOutput);
        stop();
        if (error) *error = reason;
        return false;
    };
    if (!CreatePipe(&consoleInput, &impl_->input, nullptr, 0) ||
        !CreatePipe(&impl_->output, &consoleOutput, nullptr, 0)) {
        return fail(tr("Could not create terminal pipes: %1").arg(windowsError(GetLastError())));
    }
    const COORD size{static_cast<SHORT>(std::clamp(columns, 2, 1000)), static_cast<SHORT>(std::clamp(rows, 1, 1000))};
    const HRESULT created = CreatePseudoConsole(size, consoleInput, consoleOutput, 0, &impl_->console);
    closeHandle(consoleInput);
    closeHandle(consoleOutput);
    if (FAILED(created)) return fail(tr("Could not create a Windows pseudoconsole (0x%1).").arg(static_cast<quint32>(created), 8, 16, QLatin1Char('0')));

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<unsigned char> attributes(attributeSize);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &attributeSize)) {
        return fail(tr("Could not initialize terminal process attributes: %1").arg(windowsError(GetLastError())));
    }
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, impl_->console, sizeof(HPCON), nullptr, nullptr)) {
        const DWORD code = GetLastError();
        DeleteProcThreadAttributeList(list);
        return fail(tr("Could not attach the terminal process: %1").arg(windowsError(code)));
    }

    const QString executable = QDir::toNativeSeparators(profile.executable);
    QString command = quoteArgument(executable);
    const bool isCmd = QFileInfo(executable).fileName().compare(QStringLiteral("cmd.exe"), Qt::CaseInsensitive) == 0;
    QStringList arguments = profile.arguments;
    if (isCmd && std::none_of(arguments.cbegin(), arguments.cend(), [](const QString& argument) {
        return argument.compare("/c", Qt::CaseInsensitive) == 0 || argument.compare("/k", Qt::CaseInsensitive) == 0;
    })) arguments.append({"/k", "chcp 65001>nul"});
    for (int index = 0; index < arguments.size(); ++index) {
        const QString& argument = arguments[index];
        if (isCmd && (argument.compare("/c", Qt::CaseInsensitive) == 0 || argument.compare("/k", Qt::CaseInsensitive) == 0)) {
            // CMD command tails use shell quoting, not CRT backslash-escaped quotes.
            command += " /s " + argument + " \"" + arguments.mid(index + 1).join(' ') + '"';
            break;
        }
        const bool quoted = argument.isEmpty() || std::any_of(argument.cbegin(), argument.cend(), [](QChar ch) { return ch.isSpace() || ch == '"'; });
        command += QLatin1Char(' ') + (quoted ? quoteArgument(argument) : argument);
    }
    std::wstring commandLine = command.toStdWString();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("en_US.UTF-8"));
    environment.insert(QStringLiteral("N2_CMD_SHELL"), profile.key);
    environment.insert(QStringLiteral("n2-cmd"), QStringLiteral("1"));
    QStringList keys = environment.keys();
    keys.sort(Qt::CaseInsensitive);
    std::wstring environmentBlock;
    for (const QString& key : keys) {
        environmentBlock += (key + QLatin1Char('=') + environment.value(key)).toStdWString();
        environmentBlock.push_back(L'\0');
    }
    environmentBlock.push_back(L'\0');
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    // Redirected parent stdio must not override the pseudoconsole's streams.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdError = INVALID_HANDLE_VALUE;
    startup.lpAttributeList = list;
    PROCESS_INFORMATION information{};
    const std::wstring cwd = QDir::toNativeSeparators(impl_->cwd).toStdWString();
    const bool launched = CreateProcessW(reinterpret_cast<LPCWSTR>(executable.utf16()), commandLine.data(), nullptr, nullptr, FALSE,
                                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                                        environmentBlock.data(), cwd.c_str(), &startup.StartupInfo, &information);
    const DWORD launchError = GetLastError();
    DeleteProcThreadAttributeList(list);
    if (!launched) return fail(tr("Could not start %1: %2").arg(profile.label, windowsError(launchError)));

    impl_->process = information.hProcess;
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    if (impl_->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(impl_->job, impl_->process)) closeHandle(impl_->job);
    }
    impl_->running = true;
    const unsigned generation = impl_->generation;
    impl_->reader = std::thread([this, generation] {
        char buffer[16384];
        DWORD count = 0;
        while (ReadFile(impl_->output, buffer, sizeof(buffer), &count, nullptr) && count) {
            const QByteArray bytes(buffer, static_cast<qsizetype>(count));
            QMetaObject::invokeMethod(this, [this, generation, bytes] {
                if (impl_->generation == generation) emit outputReady(bytes);
            }, Qt::QueuedConnection);
        }
    });
    ResumeThread(information.hThread);
    CloseHandle(information.hThread);
    impl_->poll->start();
    return true;
}

bool ConPtySession::write(const QByteArray& bytes) {
    std::lock_guard lock(impl_->inputMutex);
    if (!impl_->running || !impl_->input) return false;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        DWORD count = 0;
        if (!WriteFile(impl_->input, bytes.constData() + offset, static_cast<DWORD>(bytes.size() - offset), &count, nullptr) || !count) {
            emit errorOccurred(tr("The shell could not receive input: %1").arg(windowsError(GetLastError())));
            return false;
        }
        offset += count;
    }
    return true;
}

bool ConPtySession::resize(int columns, int rows) {
    if (!impl_->console) return false;
    const COORD size{static_cast<SHORT>(std::clamp(columns, 2, 1000)), static_cast<SHORT>(std::clamp(rows, 1, 1000))};
    return SUCCEEDED(ResizePseudoConsole(impl_->console, size));
}

bool ConPtySession::isRunning() const { return impl_->running; }
QString ConPtySession::workingDirectory() const { return impl_->cwd; }

void ConPtySession::stop() {
    impl_->poll->stop();
    ++impl_->generation;
    impl_->running = false;
    if (impl_->job) {
        TerminateJobObject(impl_->job, 0);
    } else if (impl_->process && WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT) {
        TerminateProcess(impl_->process, 0);
    }
    {
        std::lock_guard lock(impl_->inputMutex);
        closeHandle(impl_->input);
    }
    if (impl_->console) {
        ClosePseudoConsole(impl_->console);
        impl_->console = nullptr;
    }
    if (impl_->reader.joinable()) {
        CancelSynchronousIo(impl_->reader.native_handle());
        impl_->reader.join();
    }
    closeHandle(impl_->output);
    closeHandle(impl_->process);
    closeHandle(impl_->job);
}
