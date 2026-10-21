#pragma once

#include <QtGlobal>

// 协议版本号：客户端登录时随 LoginReq 上报，服务端据此判断兼容性。
// 当前 MVP 只有一个版本；未来若不兼容升级，递增此值并由服务端回明确错误码。
namespace Protocol {

inline constexpr quint16 kVersion = 1;

} // namespace Protocol
