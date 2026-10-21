#include "protocol/Frame.h"

#include <QJsonDocument>
#include <QJsonParseError>

QByteArray encodeFrame(MsgType type, const QJsonObject& payload)
{
    // 载荷用紧凑 JSON：二进制帧头 + 可读载荷，抓包可读，且零额外依赖（见 TECH_DESIGN §5）
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const quint32 length = static_cast<quint32>(2 + body.size()); // 2 = type 字段字节数

    QByteArray frame;
    frame.reserve(4 + static_cast<int>(length));

    // 4B 大端长度前缀
    frame.append(static_cast<char>((length >> 24) & 0xFF));
    frame.append(static_cast<char>((length >> 16) & 0xFF));
    frame.append(static_cast<char>((length >> 8) & 0xFF));
    frame.append(static_cast<char>(length & 0xFF));

    // 2B 大端消息类型
    const quint16 t = static_cast<quint16>(type);
    frame.append(static_cast<char>((t >> 8) & 0xFF));
    frame.append(static_cast<char>(t & 0xFF));

    frame.append(body);
    return frame;
}

void FrameDecoder::feed(const QByteArray& bytes)
{
    if (m_fatal) {
        return; // 已判错，不再累积，等待上层断连
    }
    m_buffer.append(bytes);
}

bool FrameDecoder::next(MsgType& outType, QJsonObject& outPayload, QString* err)
{
    if (err) {
        err->clear();
    }
    if (m_fatal) {
        return false;
    }

    // 1) 长度前缀是否完整
    if (m_buffer.size() < 4) {
        return false; // 半包
    }

    const quint32 length =
        (static_cast<quint32>(static_cast<quint8>(m_buffer[0])) << 24) |
        (static_cast<quint32>(static_cast<quint8>(m_buffer[1])) << 16) |
        (static_cast<quint32>(static_cast<quint8>(m_buffer[2])) << 8)  |
         static_cast<quint32>(static_cast<quint8>(m_buffer[3]));

    // 2) 长度合法性：至少容纳 2 字节类型，且不超过上限（防内存膨胀）
    if (length < 2) {
        m_fatal = true;
        if (err) {
            *err = QStringLiteral("帧长度非法：length=%1").arg(length);
        }
        return false;
    }
    if (length > kMaxFrameBytes) {
        m_fatal = true;
        if (err) {
            *err = QStringLiteral("帧超长：length=%1 超过上限 %2").arg(length).arg(kMaxFrameBytes);
        }
        return false;
    }

    // 3) 整个帧是否到齐
    if (static_cast<quint32>(m_buffer.size()) < 4 + length) {
        return false; // 半包，等更多字节
    }

    const quint16 rawType = static_cast<quint16>(
        (static_cast<quint16>(static_cast<quint8>(m_buffer[4])) << 8) |
         static_cast<quint16>(static_cast<quint8>(m_buffer[5])));
    const QByteArray payloadBytes = m_buffer.mid(6, static_cast<int>(length) - 2);

    // 先消费掉这一帧，避免坏载荷把解码循环卡死（无论后续解析成功与否）
    m_buffer.remove(0, 4 + static_cast<int>(length));

    outType = static_cast<MsgType>(rawType);

    if (payloadBytes.isEmpty()) {
        outPayload = QJsonObject(); // 空载荷视为空对象
        return true;
    }

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(payloadBytes, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        // 坏帧：丢弃并上报；非致命，连接可继续（见 TECH_DESIGN §3/§10）
        if (err) {
            *err = QStringLiteral("载荷 JSON 解析失败：%1").arg(pe.errorString());
        }
        return false;
    }

    outPayload = doc.object();
    return true;
}
