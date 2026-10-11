#include <QTemporaryDir>
#include <QtTest>

#include "lauxlib.h"
#include "lua.h"
#include "lua_sandbox.h"
#include "plugin_host.h"
#include "plugin_test_util.h"

using namespace framebeam::plugins;
using namespace framebeam::plugins::testutil;

class SandboxTest : public QObject {
  Q_OBJECT

  static bool hasLog(const ConsoleSnapshot& c, const QString& needle) {
    for (const LogLine& l : c.log)
      if (l.text.contains(needle)) return true;
    return false;
  }

 private slots:
  void removedGlobals() {
    LuaSandbox sb;
    std::string err;
    const bool ok = sb.runChunk(R"(
      for _, n in ipairs{"io","os","package","debug","load","loadstring","dofile","loadfile","require"} do
        assert(_G[n] == nil, n .. " must be nil")
      end
      assert(string.dump == nil)
      assert(type(string.format) == "function" and type(table.concat) == "function" and type(math.floor) == "function")
      assert(type(utf8.char) == "function" and type(coroutine.wrap) == "function")
      assert(type(collectgarbage("count")) == "number")
      assert(collectgarbage("collect") == 0)
      assert(getmetatable("").__index == string)
    )", "@t.lua", err);
    QVERIFY2(ok, err.c_str());
  }

  void printGoesToSink() {
    LuaSandbox sb;
    std::vector<std::string> lines;
    sb.setPrintSink([&](LogLevel lv, const std::string& t) {
      QCOMPARE(lv, LogLevel::Info);
      lines.push_back(t);
    });
    std::string err;
    QVERIFY2(sb.runChunk("print('a', 1, nil, true)", "@t.lua", err), err.c_str());
    QCOMPARE(lines.size(), size_t(1));
    QCOMPARE(QString::fromStdString(lines[0]), QString("a\t1\tnil\ttrue"));
  }

  void bytecodeRejected() {
    LuaSandbox sb;
    std::string err;
    // Real bytecode produced by a throwaway state, then fed to the text-only loader.
    std::string dumped;
    {
      lua_State* L = luaL_newstate();
      QVERIFY(luaL_loadstring(L, "return 1") == LUA_OK);
      lua_dump(L, [](lua_State*, const void* p, size_t sz, void* ud) -> int {
        static_cast<std::string*>(ud)->append(static_cast<const char*>(p), sz);
        return 0;
      }, &dumped, 0);
      lua_close(L);
    }
    QVERIFY(dumped.size() > 4 && dumped[0] == '\x1b');
    QVERIFY(!sb.runChunk(dumped, "=bin", err));
    QVERIFY2(err.find("binary") != std::string::npos, err.c_str());
    QVERIFY(!sb.runChunk("\x1bLua", "=bin", err));
  }

  void errorHasTraceback() {
    LuaSandbox sb;
    std::string err;
    QVERIFY(!sb.runChunk("local function boom() error('kaputt') end\nboom()", "@main.lua", err));
    QVERIFY2(err.find("main.lua:1: kaputt") != std::string::npos, err.c_str());
    QVERIFY2(err.find("stack traceback") != std::string::npos, err.c_str());
    QVERIFY(!sb.runChunk("error({})", "@t.lua", err));
    QVERIFY(!sb.runChunk("this is not lua", "@t.lua", err));
    QVERIFY(err.find("syntax error") != std::string::npos || err.find("near") != std::string::npos);
  }

  void instructionBudget() {
    SandboxLimits lim;
    lim.instructions = 200000;
    LuaSandbox sb(lim);
    std::string err;
    QVERIFY(!sb.runChunk("while true do end", "@t.lua", err));
    QCOMPARE(sb.trip(), LuaSandbox::Trip::Instructions);
    QVERIFY2(err.find("instruction budget exceeded") != std::string::npos, err.c_str());
    // pcall must not be able to swallow the budget error
    QVERIFY(!sb.runChunk("while true do pcall(function() while true do end end) end", "@t.lua", err));
    QCOMPARE(sb.trip(), LuaSandbox::Trip::Instructions);
    // inside a coroutine too
    QVERIFY(!sb.runChunk("local co = coroutine.wrap(function() while true do end end) while true do pcall(co) end", "@t.lua", err));
    QCOMPARE(sb.trip(), LuaSandbox::Trip::Instructions);
    // the budget is per call: the state is usable again
    QVERIFY2(sb.runChunk("local n = 0 for i = 1, 1000 do n = n + i end", "@t.lua", err), err.c_str());
    QCOMPARE(sb.trip(), LuaSandbox::Trip::None);
  }

  void timeBudget() {
    SandboxLimits lim;
    lim.instructions = 4'000'000'000ll;
    lim.wallMs = 50;
    LuaSandbox sb(lim);
    std::string err;
    QElapsedTimer t;
    t.start();
    QVERIFY(!sb.runChunk("while true do end", "@t.lua", err));
    QCOMPARE(sb.trip(), LuaSandbox::Trip::Time);
    QVERIFY2(err.find("time budget exceeded") != std::string::npos, err.c_str());
    QVERIFY(t.elapsed() < 3000);
  }

  void memoryLimit() {
    SandboxLimits lim;
    lim.memoryBytes = 8u * 1024 * 1024;
    LuaSandbox sb(lim);
    std::string err;
    QVERIFY(!sb.runChunk("local t = {} for i = 1, 1e9 do t[i] = string.rep('x', 1000) .. i end", "@t.lua", err));
    QCOMPARE(sb.trip(), LuaSandbox::Trip::Memory);
    QVERIFY2(err.find("memory limit exceeded") != std::string::npos, err.c_str());
    QVERIFY(sb.memoryUsed() < lim.memoryBytes + 1024);
    LuaSandbox sb2(lim);
    QVERIFY(!sb2.runChunk("local s = string.rep('x', 1e10)", "@t.lua", err));
  }

  void hostPausesOnlyTheBadPlugin() {
    QTemporaryDir t;
    PluginSpec bad;
    bad.id = "net.test.bad";
    bad.lua = "fb.on_tick(function() while true do end end)";
    PluginSpec good;
    good.id = "net.test.good";
    good.lua = "local n = 0 fb.on_tick(function() n = n + 1 fb.log.info('tick ' .. n) end)";
    PluginSpec bomb;
    bomb.id = "net.test.bomb";
    bomb.lua = "local t = {} for i = 1, 1e9 do t[i] = string.rep('y', 1000) .. i end";
    PluginSpec loop;
    loop.id = "net.test.loop";
    loop.lua = "while true do end";
    const QString cache = t.path() + "/cache";
    QList<PluginInfo> infos;
    for (const PluginSpec* s : {&bad, &good, &bomb, &loop}) infos << infoFor(writePlugin(t.path(), *s), cache);

    auto mem = std::make_shared<FakeMemoryProvider>();
    PluginHost host;
    HostLimits lim;
    lim.instructions = 300000;
    lim.memoryBytes = 8u * 1024 * 1024;
    host.setLimits(lim);
    host.setTickHz(240);
    QSignalSpy status(&host, &PluginHost::statusChanged);
    host.start(GameContext{"nds", "ABCD", "", "", "", "en"}, mem, infos);
    QTRY_COMPARE_WITH_TIMEOUT(host.consoleSnapshot("net.test.loop").status, PluginStatus::Paused, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(host.consoleSnapshot("net.test.bomb").status, PluginStatus::Paused, 10000);
    QVERIFY(host.consoleSnapshot("net.test.loop").error.contains("budget exceeded"));
    QVERIFY(host.consoleSnapshot("net.test.loop").error.contains("traceback"));
    QVERIFY(host.consoleSnapshot("net.test.bomb").error.contains("memory limit exceeded"));
    QVERIFY(hasLog(host.consoleSnapshot("net.test.bomb"), "Paused"));
    QCOMPARE(host.consoleSnapshot("net.test.good").status, PluginStatus::Running);
    QTRY_VERIFY_WITH_TIMEOUT(host.consoleSnapshot("net.test.bad").status == PluginStatus::Running, 5000);

    // snapshots drive on_tick: the bad one gets paused in its tick, the good one keeps ticking
    for (int i = 0; i < 40; ++i) {
      mem->snapshot();
      QTest::qWait(10);
    }
    QTRY_COMPARE_WITH_TIMEOUT(host.consoleSnapshot("net.test.bad").status, PluginStatus::Paused, 10000);
    const int before = host.consoleSnapshot("net.test.good").log.size();
    for (int i = 0; i < 40; ++i) {
      mem->snapshot();
      QTest::qWait(10);
    }
    QCOMPARE(host.consoleSnapshot("net.test.good").status, PluginStatus::Running);
    QVERIFY(host.consoleSnapshot("net.test.good").log.size() > before);
    QTRY_VERIFY_WITH_TIMEOUT(!status.isEmpty(), 5000);
    host.stop();
    QCOMPARE(host.consoleSnapshot("net.test.good").status, PluginStatus::Stopped);
  }
};

QTEST_MAIN(SandboxTest)
#include "sandbox_test.moc"
