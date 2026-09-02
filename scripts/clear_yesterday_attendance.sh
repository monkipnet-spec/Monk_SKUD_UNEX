#!/usr/bin/env bash
set -euo pipefail

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
CONF="$ROOT/config/system.conf"
YES="${2:-}"

if [[ ! -f "$CONF" ]]; then
  echo "Config not found: $CONF" >&2
  exit 1
fi

get_kv(){
  local key="$1" default="${2:-}"
  local value
  value="$(grep -m1 -E "^${key}=" "$CONF" 2>/dev/null | cut -d= -f2- || true)"
  printf '%s' "${value:-$default}"
}

TODAY="$(date +%F)"
YESTERDAY="$(date -d 'yesterday' +%F)"

if [[ "$(get_kv database.enabled false)" == "true" ]]; then
  command -v mariadb >/dev/null 2>&1 || { echo "mariadb client not found" >&2; exit 1; }
  DB_HOST="$(get_kv database.host 127.0.0.1)"
  DB_PORT="$(get_kv database.port 3306)"
  DB_NAME="$(get_kv database.name monk_skud_unex)"
  DB_USER="$(get_kv database.user monk_skud)"
  DB_PASS="$(get_kv database.password '')"
  [[ "$DB_NAME" =~ ^[A-Za-z0-9_]+$ ]] || { echo "Unsafe database.name" >&2; exit 1; }

  MYSQL=(mariadb --batch --skip-column-names --protocol=tcp -h "$DB_HOST" -P "$DB_PORT" -u "$DB_USER" "$DB_NAME")
  COUNT="$(MYSQL_PWD="$DB_PASS" "${MYSQL[@]}" -e "SELECT COUNT(*) FROM skud_attendance_events WHERE timestamp >= '${YESTERDAY} 00:00:00' AND timestamp < '${TODAY} 00:00:00' AND type IN ('arrival','departure','accidental','unknown_card');")"
  echo "Yesterday: $YESTERDAY"
  echo "Attendance event rows to delete: $COUNT"
  if [[ "$YES" != "--yes" ]]; then
    echo "Preview only. To delete, run:"
    echo "  $0 '$ROOT' --yes"
    exit 0
  fi
  MYSQL_PWD="$DB_PASS" "${MYSQL[@]}" -e "DELETE FROM skud_attendance_events WHERE timestamp >= '${YESTERDAY} 00:00:00' AND timestamp < '${TODAY} 00:00:00' AND type IN ('arrival','departure','accidental','unknown_card'); SELECT ROW_COUNT() AS deleted_rows;"
  echo "Yesterday attendance events deleted from MariaDB. Current attendance state and card catalog were not changed."
else
  FILE="$ROOT/data/events/${YESTERDAY}.csv"
  if [[ ! -f "$FILE" ]]; then
    echo "No CSV attendance file for $YESTERDAY: $FILE"
    exit 0
  fi
  COUNT="$(awk -F';' 'NR>1 && ($2=="arrival" || $2=="departure" || $2=="accidental" || $2=="unknown_card"){n++} END{print n+0}' "$FILE")"
  echo "Yesterday: $YESTERDAY"
  echo "Attendance rows to delete: $COUNT"
  if [[ "$YES" != "--yes" ]]; then
    echo "Preview only. To delete, run:"
    echo "  $0 '$ROOT' --yes"
    exit 0
  fi
  mkdir -p "$ROOT/backup"
  cp -a "$FILE" "$ROOT/backup/${YESTERDAY}_attendance_before_delete.csv"
  TMP="$FILE.tmp.$$"
  awk -F';' 'NR==1 || !($2=="arrival" || $2=="departure" || $2=="accidental" || $2=="unknown_card")' "$FILE" > "$TMP"
  mv "$TMP" "$FILE"
  echo "Deleted $COUNT attendance rows from $FILE; raw diagnostic rows were preserved. Backup saved in $ROOT/backup/."
fi
