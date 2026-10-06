#pragma once
// Shared by the non-UI tests: a crash must never be silent. stdout/stderr unbuffered (QtTest writes to stdout, which
// a pipe would buffer and lose on abort), std::terminate/abort/SEH/signal report to stderr, "-v2" so every test
// function is announced. FB_TEST_MAIN(Class) replaces QTEST_GUILESS_MAIN.
#include <QCoreApplication>
#include <QtTest>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <QDir>
#include <QFileInfo>
#include <exception>
#include <string>
#include <vector>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace fbtest {

inline void report(const char* what) {
  std::fprintf(stderr, "[fbtest] %s\n", what);
  std::fflush(stderr);
}
#ifdef Q_OS_WIN
inline LONG WINAPI sehFilter(EXCEPTION_POINTERS* ep) {
  std::fprintf(stderr, "[fbtest] unhandled exception 0x%08lx at %p\n", ep->ExceptionRecord->ExceptionCode,
               ep->ExceptionRecord->ExceptionAddress);
  std::fflush(stderr);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif
inline void onSignal(int sig) {
  std::fprintf(stderr, "[fbtest] signal %d (abort/crash)\n", sig);
  std::fflush(stderr);
  std::_Exit(3);
}
inline void onTerminate() {
  std::fprintf(stderr, "[fbtest] std::terminate (uncaught exception, possibly from a library thread)\n");
  std::fflush(stderr);
  std::_Exit(4);
}
inline void prepareProcess() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef Q_OS_WIN
  SetUnhandledExceptionFilter(sehFilter);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
  std::set_terminate(onTerminate);
  for (int sig : {SIGSEGV, SIGABRT, SIGILL, SIGFPE}) {
    std::signal(sig, onSignal);
  }
}

// FRAMEBEAM_TEST_LOG_DIR=<dir>: additionally write the QtTest log to <dir>/<test exe name>.txt (survives crashes and
// intermittent failures; stdout stays as is). Returns the extra arguments to append.
inline std::vector<std::string> logArgs(const char* argv0) {
  const QByteArray dir = qgetenv("FRAMEBEAM_TEST_LOG_DIR");
  if (dir.isEmpty()) {
    return {};
  }
  const QString name = QFileInfo(QString::fromLocal8Bit(argv0)).completeBaseName();
  QDir().mkpath(QString::fromLocal8Bit(dir));
  const QString file = QDir(QString::fromLocal8Bit(dir)).filePath(name + QStringLiteral(".txt"));
  return {"-o", (file + QStringLiteral(",txt")).toLocal8Bit().toStdString(), "-o", "-,txt"};
}

}  // namespace fbtest

#define FB_TEST_MAIN(TestClass)                                  \
  int main(int argc, char** argv) {                              \
    fbtest::prepareProcess();                                    \
    std::vector<char*> args(argv, argv + argc);                  \
    char verbose[] = "-v2";                                      \
    char maxw[] = "-maxwarnings";                                \
    char maxwN[] = "0";                                          \
    args.push_back(verbose);                                     \
    args.push_back(maxw);                                        \
    args.push_back(maxwN);                                       \
    const std::vector<std::string> logExtra = fbtest::logArgs(argv[0]); \
    for (const std::string& a : logExtra) args.push_back(const_cast<char*>(a.c_str())); \
    int n = static_cast<int>(args.size());                       \
    QCoreApplication app(n, args.data());                        \
    TestClass tc;                                                \
    const int rc = QTest::qExec(&tc, n, args.data());            \
    std::fprintf(stderr, "[fbtest] exit code %d\n", rc);         \
    return rc;                                                   \
  }
