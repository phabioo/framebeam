#pragma once
// Command line of the launcher's child (no Qt, header only, testable on any platform).
#include <string>

namespace framebeam::launcher {

// Returns the part of a full process command line (as GetCommandLineW delivers it) after the program name,
// verbatim and without leading blanks. The program name follows CommandLineToArgvW: quoted up to the next
// quote (no escapes), otherwise up to the next space/tab. The remainder is passed on unchanged, so every
// argument keeps its quoting, backslashes and spaces exactly as the caller wrote them.
inline std::wstring argumentsOf(const std::wstring& commandLine) {
  size_t i = 0;
  const size_t n = commandLine.size();
  while (i < n && (commandLine[i] == L' ' || commandLine[i] == L'\t')) ++i;  // defensive: leading blanks
  if (i < n && commandLine[i] == L'"') {
    ++i;
    while (i < n && commandLine[i] != L'"') ++i;
    if (i < n) ++i;
  } else {
    while (i < n && commandLine[i] != L' ' && commandLine[i] != L'\t') ++i;
  }
  while (i < n && (commandLine[i] == L' ' || commandLine[i] == L'\t')) ++i;
  return commandLine.substr(i);
}

// "<childExe>" <arguments of the original command line>; the child path is always quoted.
inline std::wstring childCommandLine(const std::wstring& childExe, const std::wstring& commandLine) {
  std::wstring cmd = L"\"" + childExe + L"\"";
  const std::wstring rest = argumentsOf(commandLine);
  if (!rest.empty()) {
    cmd += L' ';
    cmd += rest;
  }
  return cmd;
}

}  // namespace framebeam::launcher
