#pragma once

#include <QString>

// 口令加盐哈希（见 docs/TECH_DESIGN.md §8）：
//   每个账号一个 16 字节随机盐；存储 salt 与 hash 的 hex 字符串，绝不存明文。
//   公式：password_hash = SHA-256(salt_bytes || password_utf8)
// 说明：MVP 强度有限（抗 GPU 暴力破解弱），公网部署前应升级为 PBKDF2/bcrypt（§12）。
namespace PasswordHash {

// 生成 16 字节系统随机盐，返回其 hex（32 字符）
QString makeSalt();

// 计算哈希：saltHex 为 makeSalt() 的产物，password 为明文；
// 返回 SHA-256(salt_bytes || password_utf8) 的 hex（64 字符）
QString hash(const QString& saltHex, const QString& password);

// 校验：按同样方式重算并与 expectedHash 比较
bool verify(const QString& saltHex, const QString& password, const QString& expectedHash);

} // namespace PasswordHash
