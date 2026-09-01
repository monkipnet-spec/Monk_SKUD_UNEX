#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
CONF="$ROOT/config/system.conf"
DB_NAME="${SKUD_DB_NAME:-monk_skud_unex}"
DB_USER="${SKUD_DB_USER:-monk_skud}"
DB_PASS="${SKUD_DB_PASS:-$(openssl rand -hex 24)}"

if [[ ! "$DB_NAME" =~ ^[A-Za-z0-9_]+$ ]] || [[ ! "$DB_USER" =~ ^[A-Za-z0-9_]+$ ]]; then
  echo "DB name/user may contain only letters, digits and underscore" >&2
  exit 1
fi

if ! command -v mariadb >/dev/null 2>&1 || ! dpkg -s libmariadb-dev >/dev/null 2>&1; then
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y mariadb-server libmariadb-dev openssl
fi
systemctl enable --now mariadb

mariadb <<SQL
CREATE DATABASE IF NOT EXISTS \`$DB_NAME\` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS '$DB_USER'@'localhost' IDENTIFIED BY '$DB_PASS';
ALTER USER '$DB_USER'@'localhost' IDENTIFIED BY '$DB_PASS';
CREATE USER IF NOT EXISTS '$DB_USER'@'127.0.0.1' IDENTIFIED BY '$DB_PASS';
ALTER USER '$DB_USER'@'127.0.0.1' IDENTIFIED BY '$DB_PASS';
GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'localhost';
GRANT ALL PRIVILEGES ON \`$DB_NAME\`.* TO '$DB_USER'@'127.0.0.1';
FLUSH PRIVILEGES;
SQL

mkdir -p "$(dirname "$CONF")"
touch "$CONF"
set_kv(){
  local k="$1" v="$2"
  if grep -qE "^${k}=" "$CONF"; then sed -i "s#^${k}=.*#${k}=${v}#" "$CONF"; else printf '%s=%s\n' "$k" "$v" >> "$CONF"; fi
}
set_kv database.enabled true
set_kv database.host 127.0.0.1
set_kv database.port 3306
set_kv database.name "$DB_NAME"
set_kv database.user "$DB_USER"
set_kv database.password "$DB_PASS"
set_kv database.auto_create true
set_kv database.migrate_users_csv true
set_kv database.migrate_runtime_csv true
set_kv database.remove_csv_after_migration true
chmod 600 "$CONF"
if id monk >/dev/null 2>&1; then chown monk:monk "$CONF"; fi

echo "MariaDB ready: $DB_NAME / $DB_USER"
echo "Config updated: $CONF"
echo "On the next daemon start users, departments, controllers, event journal and attendance state will be migrated, verified and backed up."
