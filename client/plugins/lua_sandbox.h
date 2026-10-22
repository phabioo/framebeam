#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

struct lua_State;
struct lua_Debug;

namespace uzume::plugins {

enum class LogLevel { Debug, Info, Warn, Error };

struct SandboxLimits {
  size_t memoryBytes = 64u * 1024 * 1024;   // per state (custom allocator)
  int64_t instructions = 20'000'000;        // per entry call
  int wallMs = 500;                         // per entry call
  int64_t matchSteps = 20'000'000;          // pattern matcher steps per entry call (find/match/gmatch/gsub), ~0.2 s
};

// One sandboxed Lua 5.4 state (one per plugin): libs base/string/table/math/utf8/coroutine only, no load/require/
// dofile/loadfile/string.dump, text chunks only, a memory limit and an instruction + wall-clock budget per entry call.
// Not thread-safe: use from one thread only (the plugin thread).
class LuaSandbox {
 public:
  explicit LuaSandbox(const SandboxLimits& limits = {});
  ~LuaSandbox();
  LuaSandbox(const LuaSandbox&) = delete;
  LuaSandbox& operator=(const LuaSandbox&) = delete;

  lua_State* L() const { return m_L; }
  bool valid() const { return m_L != nullptr; }

  using LogSink = std::function<void(LogLevel, const std::string&)>;
  void setPrintSink(LogSink sink) { m_printSink = std::move(sink); }  // print() -> info
  const LogSink& printSink() const { return m_printSink; }
  void setOwner(void* owner) { m_owner = owner; }
  void* owner() const { return m_owner; }
  // The sandbox that owns `L` (also for coroutines of it).
  static LuaSandbox* from(lua_State* L);
  // Called by the patched pattern matcher (every 1024 steps): raises a Lua error when the budget or time is used up.
  static void matchStep(lua_State* L);

  enum class Trip { None, Instructions, Time, Memory };
  Trip trip() const { return m_trip; }
  size_t memoryUsed() const { return m_used; }
  const SandboxLimits& limits() const { return m_limits; }

  // Runs `body` as a C function under lua_pcall with a traceback message handler and a fresh budget. Inside `body`
  // use the Lua API freely (errors are caught). Returns false and sets `err` on a Lua error or a tripped budget;
  // `err` then starts with the message and continues with the traceback. The stack is restored.
  bool run(const std::function<void(lua_State*)>& body, std::string& err);

  // Convenience: loads `code` as a text chunk (never bytecode) and runs it. `chunkName` is shown in errors ("@main.lua").
  bool runChunk(const std::string& code, const std::string& chunkName, std::string& err);

  static std::string firstLine(const std::string& err);

 private:
  static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize);
  static void hook(lua_State* L, lua_Debug* ar);
  static int trampoline(lua_State* L);
  static int messageHandler(lua_State* L);
  void openLibs();
  static int openLibsFn(lua_State* L);

  lua_State* m_L = nullptr;
  SandboxLimits m_limits;
  size_t m_used = 0;
  int64_t m_instrLeft = 0;
  int64_t m_matchLeft = 0;
  std::chrono::steady_clock::time_point m_deadline;
  Trip m_trip = Trip::None;
  void* m_owner = nullptr;
  LogSink m_printSink;
};

}  // namespace uzume::plugins
