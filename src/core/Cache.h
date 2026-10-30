#pragma once
#include <QString>

namespace bagua::cache {
// Root for all runtime scratch data. Prefer a portable "cache" directory next
// to the executable so installations stay self-contained; fall back to the
// user's generic cache location when the installation directory is read-only.
QString root();
QString ensure(const QString &category);
QString temporaryTemplate(const QString &category, const QString &prefix);
// Remove old scratch workspaces and, from older builds, random directories that
// were written directly into the application directory.
void prepare();
void cleanupStale();
void cleanupLegacyApplicationTemp();
void removeTree(const QString &path);
} // namespace bagua::cache
