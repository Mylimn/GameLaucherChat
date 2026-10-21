#include "PasswordHash.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRandomGenerator>

namespace PasswordHash {

QString makeSalt()
{
    // 用系统级随机源（非可预测的伪随机），保证盐不可复现
    QRandomGenerator* rng = QRandomGenerator::system();
    QByteArray salt(16, '\0');
    for (int i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<char>(rng->generate() & 0xFF);
    }
    return QString::fromLatin1(salt.toHex());
}

QString hash(const QString& saltHex, const QString& password)
{
    // 盐以字节参与哈希（而非 hex 文本），避免把编码差异带入结果
    const QByteArray saltBytes = QByteArray::fromHex(saltHex.toLatin1());
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(saltBytes);
    h.addData(password.toUtf8());
    return QString::fromLatin1(h.result().toHex());
}

bool verify(const QString& saltHex, const QString& password, const QString& expectedHash)
{
    // 定时安全比较非 MVP 目标，此处直接比较 hex 字符串
    return hash(saltHex, password) == expectedHash;
}

} // namespace PasswordHash
