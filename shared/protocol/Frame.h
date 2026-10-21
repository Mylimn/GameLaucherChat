#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include "protocol/MessageType.h"

// 帧格式（见 docs/TECH_DESIGN.md §3）：4B 大端 length + 2B 大端 type + JSON 载荷，
// 其中 length = 2(type) + payload 字节数，不含自身的 4 字节长度前缀。
// 客户端与服务端共用本编解码器。

// 最大帧长 1 MiB：length 字段超过此值视为非法帧，调用方应直接断开连接。
inline constexpr quint32 kMaxFrameBytes = 1024u * 1024u;

// 组帧：type + JSON 对象 → 完整字节流（已含长度前缀）
QByteArray encodeFrame(MsgType type, const QJsonObject& payload);

// 增量解码器：喂入任意长度字节（可能半包/粘包），循环取出完整帧。
// 用法：
//   decoder.feed(bytes);
//   while (decoder.next(type, payload, &err)) { 处理一帧 }
//   if (decoder.fatal()) { 断开连接 }
class FrameDecoder {
public:
    // 追加收到的原始字节。若已进入 fatal 状态则忽略（等待上层断连）。
    void feed(const QByteArray& bytes);

    // 尝试取出一个完整帧。
    //   返回 true  → outType/outPayload 有效。
    //   返回 false → 未取到有效帧；此时：
    //                *err 为空   → 数据不足（半包），等 feed 更多字节即可；
    //                *err 非空   → 出现帧错误。fatal()==true 表示帧结构非法（超长/长度<2），
    //                              调用方应断连；fatal()==false 表示坏帧（载荷 JSON 非法）
    //                              已被丢弃，连接可继续。
    bool next(MsgType& outType, QJsonObject& outPayload, QString* err);

    // 是否处于不可恢复的帧错误状态（需断连）。
    bool fatal() const { return m_fatal; }

    // 当前缓冲区中尚未解析的字节数。
    int buffered() const { return static_cast<int>(m_buffer.size()); }

private:
    QByteArray m_buffer;
    bool m_fatal = false;
};
