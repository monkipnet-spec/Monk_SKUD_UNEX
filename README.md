# Monk_SKUD_UNEX

Модульная система контроля доступа на C++20 для Ubuntu Server 24.04/26.04 и контроллеров UNEX 721 через USB-COM/RS-485.

## Что уже реализовано

- модульный C++20: `serial`, `unex`, `controllers`, `attendance`, `users`, `departments`, `storage`, `telegram`, `system`, `web`, `config`;
- USB-COM autodetect (`/dev/serial/by-id`, `/dev/ttyUSB*`, `/dev/ttyACM*`), 9600 8N1;
- polling UNEX/SOYAL-style протокола: 0x25 (старейшее событие), безопасное подтверждение 0x37 только после декодирования;
- синхронизация времени контроллера 0x23;
- автообнаружение Node ID и сохранение контроллеров в `config/controllers.csv`;
- файловое хранение без БД;
- пользователи и карты в `config/users.csv`;
- отдельный справочник отделов в `config/departments.csv`: добавление, переименование и удаление через веб;
- отдел пользователя выбирается из выпадающего списка; переименование отдела автоматически обновляет пользователей; используемый отдел удалить нельзя;
- для каждого пользователя задаётся `controller_port` — порт записи в контроллере UNEX (0 = не задан);
- журнал событий по дням `data/events/YYYY-MM-DD.csv`;
- текущее состояние карт `data/card_state.csv`;
- список считанных карт `data/active_cards.csv`;
- правило посещаемости: первое чтение = приход; повтор от той же карты <=60 сек от ПРЕДЫДУЩЕГО физического чтения = случайное; следующее чтение >60 сек = смена состояния приход/уход;
- веб-авторизация, изменение пользователя, добавление/удаление/привязка карты;
- в шапке веб-интерфейса показываются текущая загрузка CPU и использование RAM (процент и использовано/всего), данные читаются C++-модулем из `/proc`;
- страница активных считанных карт: карта, № пользователя, ФИО, отдел, время, последнее событие;
- импорт/экспорт пользователей CSV и настроек;
- Telegram Bot уведомления о приходе/уходе;
- симулятор считывания для проверки системы без контроллера;
- systemd unit.

## Важно по UNEX 721

Каркас стандартного кадра, polling 0x25/0x37 и RTC 0x23 реализован по опубликованному SOYAL protocol, совместимому по семейству с 721. Однако **точные смещения Card ID/User Address в реальном event-frame UNEX 721 не угадываются**. Пока не получен один реальный RAW-ответ UNEX 721 на нормальное считывание карты, драйвер сохраняет/показывает RAW frame и НЕ отправляет 0x37 для недекодированного события, чтобы не удалить проход из памяти контроллера.

После первого захвата RAW кадра достаточно изменить `Unex721Protocol::decodeEvent()` — остальная система уже готова.

## Сборка Ubuntu 24.04/26.04

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libssl-dev curl

cd /opt/Monk_SKUD_UNEX
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/monk-skud-unex
```

Первый вход: `admin / admin`. Сразу измените пароль в веб-интерфейсе.

## systemd

```bash
sudo useradd --system --home /opt/Monk_SKUD_UNEX --shell /usr/sbin/nologin skud || true
sudo usermod -aG dialout skud
sudo chown -R skud:skud /opt/Monk_SKUD_UNEX
sudo cp systemd/monk-skud-unex.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now monk-skud-unex
sudo systemctl status monk-skud-unex
```

## Файлы данных

`config/system.conf` создаётся при первом старте. Пример — `config/system.conf.example`.

`config/users.csv`:

```text
id;enabled;last_name;first_name;middle_name;department;position;card;controller_port;valid_from;valid_until;telegram_arrival;telegram_departure
```

`config/controllers.csv`:

```text
node;name;model;enabled
```

`config/departments.csv`:

```text
name
Склад
IT
Производство
```

При первом старте существующие названия отделов из `users.csv` автоматически добавляются в справочник.

## Telegram

В веб-интерфейсе задайте Bot Token и Chat ID, включите Telegram и нажмите тест. Модуль вызывает системный `curl` через `fork/exec` без shell-интерпретации аргументов.

## GitHub

Repository: `https://github.com/monkipnet-spec/Monk_SKUD_UNEX.git`


## v0.1.1 — Web UI path fix

v0.1.1 originally added project-root detection for the web UI. Starting with v0.1.6 the runtime rule is simpler: without an explicit path argument, the application uses the **current working directory** as its runtime root and creates missing runtime files there.

## User controller port

Each user has `controller_port` (0-255, `0` means not specified). The value is stored in `config/users.csv`, exposed by the web API/UI, and is reserved for the UNEX 721 user-download module. Old 12-column users CSV files remain import-compatible and are migrated with `controller_port=0`.

## v0.1.3 — Departments

Добавлен отдельный модуль `DepartmentManager`. В веб-интерфейсе появился раздел «Отделы» с добавлением, переименованием и удалением. В карточке пользователя отдел выбирается из выпадающего списка. Переименование отдела обновляет пользователей, удаление отдела блокируется, пока он назначен хотя бы одному пользователю. Старые отделы из `users.csv` автоматически мигрируют в `config/departments.csv`.


## v0.1.4 — CPU/RAM и uptime в шапке

Добавлен модуль `SystemMetrics`: сервер читает `/proc/stat` и `/proc/meminfo`, рассчитывает загрузку CPU и использование RAM. Также фиксируется время запуска процесса и через `/api/status` возвращается `uptime_seconds`. В шапке веб-интерфейса CPU, RAM и время работы Monk_SKUD_UNEX с последнего запуска обновляются автоматически каждые 3 секунды без перезагрузки страницы.

## v0.1.5 — Активность пользователей за сегодня

На главной странице добавлена таблица пользователей, отмечавшихся в текущий день. Для каждого пользователя отображаются ФИО, отдел, карта, первый приход, последний уход и текущий статус «На работе»/«Ушёл». Данные строятся из `data/events/YYYY-MM-DD.csv`, поэтому история текущего дня сохраняется после перезапуска программы. Случайные повторные чтения не меняют статус и не влияют на время прихода/ухода.


### Runtime CSV и Git

Рабочие `config/users.csv`, `config/controllers.csv` и `config/departments.csv` являются данными конкретного сервера и исключены из Git. В репозитории хранятся только `.example`-шаблоны. Это предотвращает конфликты `git pull` и исключает перезапись пользователей/отделов/обнаруженных контроллеров при обновлении кода.


## v0.1.6 — Автоматическая инициализация рабочей папки

Если программа запускается без аргумента пути, рабочей папкой становится каталог, из которого выполнен запуск (`current working directory`). При первом запуске приложение автоматически создаёт недостающие каталоги `config/`, `data/`, `data/events/`, `backup/`, `web/` и рабочие файлы `system.conf`, `users.csv`, `departments.csv`, `controllers.csv`, `card_state.csv`, `active_cards.csv`.

Веб-интерфейс (`login.html`, `index.html`, `app.js`, `style.css`) встраивается в бинарник при сборке и восстанавливается из него, если соответствующего файла нет в рабочей папке. Уже существующие конфиги, пользователи, отделы, контроллеры, журналы и web-файлы **никогда не перезаписываются** этой инициализацией.

Пример:

```bash
mkdir -p /opt/my-skud-runtime
cd /opt/my-skud-runtime
/path/to/monk-skud-unex
```

Все рабочие файлы будут созданы внутри `/opt/my-skud-runtime`. Для systemd используется `WorkingDirectory=/opt/Monk_SKUD_UNEX`, поэтому тот же принцип работает и при запуске как службы. При необходимости по-прежнему можно явно передать runtime-каталог первым аргументом.


## v0.1.7 — Визуальный отклик кнопок

Все кнопки веб-интерфейса получили единый визуальный отклик на нажатие. При удержании кнопка слегка утапливается и затемняется, поверх неё появляется короткая световая волна. Добавлены hover-состояние, заметный keyboard focus и корректное disabled-состояние. Эффект работает для обычных, навигационных, опасных и динамически создаваемых кнопок без дополнительной разметки или JavaScript.
