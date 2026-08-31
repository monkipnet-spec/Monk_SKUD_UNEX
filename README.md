# Monk_SKUD_UNEX

Модульная система контроля доступа на C++20 для Ubuntu Server 24.04/26.04 и контроллеров UNEX 721 через USB-COM/RS-485.

## Что уже реализовано

- модульный C++20: `serial`, `unex`, `controllers`, `attendance`, `users`, `departments`, `storage`, `telegram`, `reports`, `system`, `web`, `config`;
- USB-COM autodetect (`/dev/serial/by-id`, `/dev/ttyUSB*`, `/dev/ttyACM*`), 9600 8N1;
- polling UNEX/SOYAL-style протокола: 0x25 (старейшее событие), безопасное подтверждение 0x37 только после декодирования;
- синхронизация времени контроллера 0x23;
- автообнаружение Node ID и сохранение контроллеров в `config/controllers.csv`;
- файловое хранение без БД;
- пользователи, двухчастный номер карты (серия + номер) и индивидуальный PIN в `config/users.csv`;
- отдельный справочник отделов в `config/departments.csv`: добавление, переименование и удаление через веб;
- отдел пользователя выбирается из выпадающего списка; переименование отдела автоматически обновляет пользователей; используемый отдел удалить нельзя;
- для каждого пользователя задаётся `controller_port` — адрес/слот записи пользователя в контроллере UNEX (0 = не задан, рабочий диапазон 1..16383);
- мастер «Выгрузить в контроллеры»: все/выбранные пользователи → все/выбранные контроллеры, очередь задания и подробный результат по каждой паре пользователь/контроллер;
- мастер удаления: все/выбранные пользователи можно удалить только из системы, только из выбранных/всех контроллеров или одновременно; при комбинированном удалении локальная карточка удаляется только после подтверждения всех выбранных контроллеров;
- журнал событий по дням `data/events/YYYY-MM-DD.csv`;
- текущее состояние карт `data/card_state.csv`;
- список считанных карт `data/active_cards.csv`;
- правило посещаемости: первое чтение = приход; повтор от той же карты <=60 сек от ПРЕДЫДУЩЕГО физического чтения = случайное; следующее чтение >60 сек = смена состояния приход/уход;
- веб-авторизация, изменение пользователя, добавление/удаление/привязка карты;
- в шапке веб-интерфейса показываются текущая загрузка CPU и использование RAM (процент и использовано/всего), данные читаются C++-модулем из `/proc`;
- страница активных считанных карт: карта, № пользователя, ФИО, отдел, время, последнее событие;
- импорт/экспорт пользователей CSV и настроек;
- Telegram Bot уведомления о приходе/уходе;
- отчёты посещаемости за сутки, текущую неделю, текущий месяц или произвольный диапазон дат; отчёт формируется как UTF-8 `.txt`, доступен для скачивания и отправки в Telegram вручную;
- встроенное расписание автоматической отправки TXT-отчётов в Telegram без cron: суточный, недельный или месячный завершённый период;
- гарантированная визуализация нажатия всех кнопок: состояние удерживается ~240 мс даже при быстром клике; web-ресурсы обновляются из бинарника и отдаются без browser-cache;
- systemd unit.

## Важно по UNEX 721

Каркас стандартного кадра, polling 0x25/0x37 и RTC 0x23 реализован по опубликованному SOYAL protocol, совместимому по семейству с 721. Однако **точные смещения Card ID/User Address в реальном event-frame UNEX 721 не угадываются**. Пока не получен один реальный RAW-ответ UNEX 721 на нормальное считывание карты, драйвер сохраняет/показывает RAW frame и НЕ отправляет 0x37 для недекодированного события, чтобы не удалить проход из памяти контроллера.

После первого захвата RAW кадра достаточно изменить `Unex721Protocol::decodeEvent()` — остальная система уже готова.

Запись пользователей вынесена в `Unex721Protocol::writeUser()` и вызывается только через очередь `ControllerManager`. В v0.1.9 интерфейс и очередь были готовы, но аппаратная запись была заблокирована. Начиная с v0.2.0 реализован SOYAL H-series Extended Protocol по открытому MIT-проекту `oommgg/Soyal`: команда `0x84` записывает пользователя, затем `0x87` обязательно считывает запись обратно и сверяет UID. Это уменьшает риск ложного успеха при проблемах линии/протокола. Реальная совместимость именно с UNEX 721 всё равно должна быть подтверждена на одном тестовом пользователе до массовой выгрузки.

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

`config/system.conf` создаётся при первом старте. Пример — `config/system.conf.example`. Сформированные отчёты сохраняются в `data/reports/` и исключены из Git.

`config/users.csv`:

```text
id;enabled;last_name;first_name;middle_name;department;position;card;card_series;card_number;pin_code;access_mode;controller_port;valid_from;valid_until;telegram_arrival;telegram_departure
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

Each user has `controller_port` (1..16383, `0` means not specified). The value is stored in `config/users.csv`, exposed by the web API/UI, and is reserved for the UNEX 721 user-download module. Old 12-column users CSV files remain import-compatible and are migrated with `controller_port=0`.

## v0.1.3 — Departments

Добавлен отдельный модуль `DepartmentManager`. В веб-интерфейсе появился раздел «Отделы» с добавлением, переименованием и удалением. В карточке пользователя отдел выбирается из выпадающего списка. Переименование отдела обновляет пользователей, удаление отдела блокируется, пока он назначен хотя бы одному пользователю. Старые отделы из `users.csv` автоматически мигрируют в `config/departments.csv`.


## v0.1.4 — CPU/RAM и uptime в шапке

Добавлен модуль `SystemMetrics`: сервер читает `/proc/stat` и `/proc/meminfo`, рассчитывает загрузку CPU и использование RAM. Также фиксируется время запуска процесса и через `/api/status` возвращается `uptime_seconds`. В шапке веб-интерфейса CPU, RAM и время работы Monk_SKUD_UNEX с последнего запуска обновляются автоматически каждые 3 секунды без перезагрузки страницы.

## v0.1.5 — Активность пользователей за сегодня

На главной странице добавлена таблица пользователей, отмечавшихся в текущий день. Для каждого пользователя отображаются ФИО, отдел, карта, первый приход, последний уход и текущий статус «На работе»/«Ушёл». Данные строятся из `data/events/YYYY-MM-DD.csv`, поэтому история текущего дня сохраняется после перезапуска программы. Случайные повторные чтения не меняют статус и не влияют на время прихода/ухода.


### Runtime CSV и Git

Рабочие `config/users.csv`, `config/controllers.csv` и `config/departments.csv` являются данными конкретного сервера и исключены из Git. В репозитории хранятся только `.example`-шаблоны. Это предотвращает конфликты `git pull` и исключает перезапись пользователей/отделов/обнаруженных контроллеров при обновлении кода.


## v0.1.6 — Автоматическая инициализация рабочей папки

Если программа запускается без аргумента пути, рабочей папкой становится каталог, из которого выполнен запуск (`current working directory`). При первом запуске приложение автоматически создаёт недостающие каталоги `config/`, `data/`, `data/events/`, `data/reports/`, `backup/`, `web/` и рабочие файлы `system.conf`, `users.csv`, `departments.csv`, `controllers.csv`, `card_state.csv`, `active_cards.csv`.

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


## v0.1.8 — Гарантированный визуальный отклик

CSS-состояние `:active` дополнено делегированным JavaScript-классом `skud-pressed`, который удерживается около 240 мс. Web-ресурсы получают версионные URL и отдаются с `Cache-Control: no-store`; встроенные web-файлы обновляются из бинарника при старте.

## v0.1.9 — Выгрузка пользователей в контроллеры

В разделе «Пользователи» добавлена кнопка **«Выгрузить в контроллеры»**. Диалог позволяет выбрать «все пользователи» или произвольный набор пользователей и «все контроллеры» или произвольный набор контроллеров. Сервер создаёт отдельное задание, возвращает `job_id`, показывает прогресс и результат по каждой паре пользователь/контроллер.

HTTP API:

- `POST /api/controllers/upload-users` — создаёт задание (`all_users`, `user_ids`, `all_controllers`, `controller_nodes`);
- `GET /api/controllers/upload-users/status?job_id=N` — состояние и подробные результаты.

Очередь находится внутри `ControllerManager`, поэтому HTTP-потоки не обращаются к последовательному порту напрямую и не конкурируют с polling контроллеров. Когда точный формат записи UNEX 721 будет подтверждён реальным захватом/документацией, достаточно реализовать `Unex721Protocol::writeUser()` и включить `userWriteSupported()` — UI/API/очередь менять не потребуется. До этого аппаратная запись возвращает состояние `blocked_protocol`.

## SOYAL H-series Extended Protocol / user upload (v0.2.0)

The UNEX driver now implements the card/user write layout used by the MIT-licensed `oommgg/Soyal` AR-727H library and its bundled SOYAL protocol reference. The write operation uses Extended Protocol command `0x84`, user address `1..16383`, a 27-byte user record, XOR+SUM checksums, and the `FF 00 5A A5` envelope. Every successful ACK is followed by command `0x87` to read the same address back and verify UID/status before the job reports success. Serial Extended Protocol responses are supported directly over RS485/USB-COM; legacy compact `0x7E` reads remain as a fallback for discovery/event experiments.

Starting with v0.2.3 the web form uses two decimal parts explicitly: **card series** (`0..65535`) followed by **card number** (`0..65535`). Each part maps directly to one 16-bit UID word used by the H-series record. The earlier v0.2.2 HEX-series interpretation was corrected. Erroneous HEX/alphabetic series are no longer accepted for controller upload and must be corrected by the administrator before use.

Protocol reference/code source: https://github.com/oommgg/Soyal (MIT). Hardware verification on the actual UNEX 721 is still required before bulk production upload.


## v0.2.1 — Удаление пользователей из системы и контроллеров

В разделе «Пользователи» добавлен мастер **«Удалить пользователей»**. Поддерживаются режимы:

- выбранные или все пользователи;
- только локальная система Monk SKUD;
- только выбранные или все контроллеры;
- одновременно локальная система + выбранные/все контроллеры.

Удаление записи в контроллере выполняется безопасно для одного точного адреса пользователя через SOYAL H-series Extended Protocol `0x84`: UID1/UID2 записываются как `0xFFFF/0xFFFF`, режим карты устанавливается в `0` (disabled). Затем команда `0x87` читает тот же адрес обратно. Успех фиксируется только когда запись отключена и UID действительно очищены до `FFFF:FFFF`. Команда диапазонного сброса `0x85` для одиночного удаления не используется, чтобы исключить риск очистки соседних адресов.

При режиме «из системы и контроллеров» локальный пользователь удаляется **только после успешного подтверждения удаления из каждого выбранного контроллера**. Если один контроллер вернул NACK, не отвечает или контрольное чтение не совпало, локальная карточка пользователя сохраняется, чтобы администратор мог повторить операцию после восстановления связи.

HTTP API:

- `POST /api/users/delete-selected`;
- `GET /api/users/delete-selected/status?job_id=N`.

Удаление из контроллеров выполняется через очередь `ControllerManager` и не конкурирует с polling COM-порта.


## v0.2.2 — Серия/номер карты и индивидуальный PIN

Карточка пользователя хранит карту как два отдельных поля: `card_series` и `card_number`. В v0.2.2 серия ошибочно трактовалась как HEX; начиная с v0.2.3 серия и номер являются десятичными значениями `0..65535`. Ошибочные HEX/буквенные серии из v0.2.2 автоматически не преобразуются: их нужно исправить вручную, чтобы исключить запись неверного UID в контроллер.

Добавлен необязательный `pin_code` из 4 цифр (`0001..9999`) и `access_mode`: `card`, `card_or_pin`, `card_and_pin`. При выгрузке командой `0x84` PIN записывается в четыре байта пользовательской записи; после ACK команда `0x87` проверяет обратно UID, PIN и режим доступа. Для ручного PIN-входа на H-series используется M4 и режим `card_or_pin`: на клавиатуре вводится 5-значный адрес пользователя + 4-значный PIN.


## v0.2.3 — Десятичная серия карты и компактное поле

Серия карты исправлена на десятичную (`0..65535`) и в форме пользователя расположена непосредственно перед номером карты в узком поле. Внутренний канонический формат новой записи: `series:number`, например `112:12345`. Оба значения напрямую передаются как два 16-битных UID-слова протокола. Ошибочный v0.2.2 формат с HEX/буквенной серией не допускается к новой записи и должен быть исправлен вручную; это исключает тихое преобразование неправильной серии в другой UID.

## v0.2.4 — Удаление симулятора считывания с главной

Блок тестового считывания карты удалён с главной страницы. Также удалены фронтенд-функция `simulate()` и HTTP endpoint `POST /api/simulate`, поэтому искусственное событие карты больше нельзя создать через web API. Рабочая обработка аппаратных событий контроллера и фильтр повторных считываний не изменены.


## v0.2.5 — TXT-отчёты и отправка в Telegram по расписанию

Добавлен отдельный раздел **«Отчёты»**. Вручную можно выбрать готовый период «Сегодня», «Текущая неделя», «Текущий месяц» или указать произвольные даты `from/to`. Для выбранного диапазона сервер формирует UTF-8 текстовый файл `data/reports/attendance_YYYY-MM-DD_YYYY-MM-DD.txt`. В отчёте по каждому дню и пользователю выводятся отдел, карта, первый приход, последний уход и состояние на конец дня. Случайные повторные чтения не попадают в расчёт — используются только события `arrival/departure`.

Доступны действия:

- **Сформировать** — показать текст отчёта в веб-интерфейсе;
- **Скачать TXT** — скачать тот же файл через браузер;
- **Отправить TXT в Telegram** — передать файл в настроенный Bot Token / Chat ID командой Telegram `sendDocument`.

Автоматическая отправка выполняется внутренним C++-планировщиком, поэтому отдельный `cron` не нужен. Настройки хранятся в `system.conf`:

```ini
reports.schedule.enabled=false
reports.schedule.period=daily
reports.schedule.time=18:00
reports.schedule.weekday=1
reports.schedule.month_day=1
```

Автоматические периоды специально формируются только по **завершённым** данным: `daily` отправляет предыдущие сутки, `weekly` — предыдущую календарную неделю Пн–Вс, `monthly` — предыдущий календарный месяц. `weekday` использует `1=Понедельник ... 7=Воскресенье`; `month_day` ограничен `1..28`, чтобы расписание существовало в каждом месяце. После неудачной Telegram-отправки статус и ошибка сохраняются и отображаются в веб-интерфейсе; бесконечный цикл повторов не запускается.

HTTP API:

- `GET /api/reports/settings`;
- `POST /api/reports/settings`;
- `GET /api/reports/preview?from=YYYY-MM-DD&to=YYYY-MM-DD`;
- `GET /api/reports/download?from=YYYY-MM-DD&to=YYYY-MM-DD`;
- `POST /api/reports/send`.
