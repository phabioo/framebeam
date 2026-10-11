#include "plugin_value.h"

#include <cstdio>

#include "lauxlib.h"
#include "lua.h"

namespace framebeam::plugins {

namespace {

struct Reader {
  explicit Reader(lua_State* state) : L(state) {}
  lua_State* L;
  size_t budget = kMaxValueBytes;
  std::string err;

  bool spend(size_t n) {
    if (n > budget) {
      err = "value is larger than 1 MiB";
      return false;
    }
    budget -= n;
    return true;
  }

  bool read(int idx, Value& out, int depth) {
    idx = lua_absindex(L, idx);
    if (!spend(16)) return false;
    switch (lua_type(L, idx)) {
      case LUA_TNIL: out.type = Value::Type::Nil; return true;
      case LUA_TBOOLEAN:
        out.type = Value::Type::Bool;
        out.b = lua_toboolean(L, idx) != 0;
        return true;
      case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) {
          out.type = Value::Type::Int;
          out.i = lua_tointeger(L, idx);
        } else {
          out.type = Value::Type::Float;
          out.d = lua_tonumber(L, idx);
        }
        return true;
      case LUA_TSTRING: {
        size_t n = 0;
        const char* p = lua_tolstring(L, idx, &n);
        if (!spend(n)) return false;
        out.type = Value::Type::String;
        out.s.assign(p, n);
        return true;
      }
      case LUA_TTABLE: {
        if (depth >= kMaxValueDepth) {
          err = "value is nested deeper than 32 levels";
          return false;
        }
        out.type = Value::Type::Table;
        if (!lua_checkstack(L, 4)) {
          err = "value is too complex";
          return false;
        }
        lua_pushnil(L);
        while (lua_next(L, idx) != 0) {
          const int kt = lua_type(L, -2);
          if (kt != LUA_TNUMBER && kt != LUA_TSTRING && kt != LUA_TBOOLEAN) {
            lua_pop(L, 2);
            err = std::string("table keys of type ") + lua_typename(L, kt) + " cannot be passed";
            return false;
          }
          Value k, v;
          // lua_tolstring would convert a number key in place and confuse lua_next, so read the key via a copy.
          lua_pushvalue(L, -2);
          const bool okK = read(-1, k, depth + 1);
          lua_pop(L, 1);
          if (!okK || !read(-1, v, depth + 1)) {
            lua_pop(L, 2);
            return false;
          }
          out.keys.push_back(std::move(k));
          out.vals.push_back(std::move(v));
          lua_pop(L, 1);
        }
        return true;
      }
      default:
        err = std::string(lua_typename(L, lua_type(L, idx))) + " values cannot be passed between plugins";
        return false;
    }
  }
};

void quote(std::string& out, const std::string& s) {
  out += '"';
  for (char c : s) {
    if (c == '\n') out += "\\n";
    else if (c == '"') out += "\\\"";
    else if (static_cast<unsigned char>(c) < 0x20) out += '?';
    else out += c;
  }
  out += '"';
}

void render(const Value& v, std::string& out, size_t maxLen) {
  if (out.size() > maxLen) return;
  switch (v.type) {
    case Value::Type::Nil: out += "nil"; break;
    case Value::Type::Bool: out += v.b ? "true" : "false"; break;
    case Value::Type::Int: out += std::to_string(v.i); break;
    case Value::Type::Float: {
      char buf[48];
      std::snprintf(buf, sizeof buf, "%.14g", v.d);
      out += buf;
      break;
    }
    case Value::Type::String: quote(out, v.s); break;
    case Value::Type::Table:
      out += '{';
      for (size_t i = 0; i < v.keys.size() && out.size() <= maxLen; ++i) {
        if (i) out += ", ";
        if (!(v.keys[i].type == Value::Type::Int && v.keys[i].i == static_cast<int64_t>(i) + 1)) {
          if (v.keys[i].type == Value::Type::String) out += v.keys[i].s;
          else render(v.keys[i], out, maxLen);
          out += '=';
        }
        render(v.vals[i], out, maxLen);
      }
      out += '}';
      break;
  }
}

}  // namespace

bool readValue(lua_State* L, int idx, Value& out, std::string& err) {
  Reader r(L);
  out = Value();
  if (!r.read(idx, out, 0)) {
    err = r.err;
    out = Value();
    return false;
  }
  return true;
}

void pushValue(lua_State* L, const Value& v) {
  luaL_checkstack(L, 6, "value too deep");
  switch (v.type) {
    case Value::Type::Nil: lua_pushnil(L); break;
    case Value::Type::Bool: lua_pushboolean(L, v.b); break;
    case Value::Type::Int: lua_pushinteger(L, static_cast<lua_Integer>(v.i)); break;
    case Value::Type::Float: lua_pushnumber(L, v.d); break;
    case Value::Type::String: lua_pushlstring(L, v.s.data(), v.s.size()); break;
    case Value::Type::Table:
      lua_createtable(L, 0, static_cast<int>(v.keys.size()));
      for (size_t i = 0; i < v.keys.size(); ++i) {
        pushValue(L, v.keys[i]);
        pushValue(L, v.vals[i]);
        lua_rawset(L, -3);
      }
      break;
  }
}

std::string shortText(const Value& v, size_t maxLen) {
  std::string out;
  render(v, out, maxLen);
  if (out.size() > maxLen) {
    out.resize(maxLen > 3 ? maxLen - 3 : maxLen);
    out += "...";
  }
  return out;
}

}  // namespace framebeam::plugins
