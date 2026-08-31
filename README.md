# Monk_SKUD_UNEX

Модульная система контроля доступа на C++20 для Ubuntu Server 24.04/26.04 и контроллеров UNEX 721 через USB-COM/RS-485.

## Что уже реализовано

- модульный C++20: `serial`, `unex`, `controllers`, `attendance`, `users`, `storage`, `telegram`, `web`, `config`;
- USB-COM autodetect (`/dev/serial/by-id`, `/dev/ttyUSB*`, `/dev/ttyACM*`), 9600 8N1;
- polling UNEX/SOYAL-style протокола: 0x25 (старейшее событие), безопасное подтверждение 0x37 только после декодирования;
- синхронизация времени контроллера 0x23;
- автообнаружение Node ID и сохранение контроллеров в `config/controllers.csv`;
- файловое хранение без БД;
- пользователи и карты в `config/users.csv`;
- журнал событий по дням `data/events/YYYY-MM-DD.csv`;
- текущее состояние карт `data/card_state.csv`;
- список считанных карт `data/active_cards.csv`;
- правило посещаемости: первое чтение = приход; повтор от той же карты <=60 сек от ПРЕДЫДУЩЕГО физического чтения = случайное; следующее чтение >60 сек = смена состояния приход/уход;
- веб-авторизация, изменение пользователя, добавление/удаление/привязка карты;
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
./build/monk-skud-unex /opt/Monk_SKUD_UNEX
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
id;enabled;last_name;first_name;middle_name;department;position;card;valid_from;valid_until;telegram_arrival;telegram_departure
```

`config/controllers.csv`:

```text
node;name;model;enabled
```

## Telegram

В веб-интерфейсе задайте Bot Token и Chat ID, включите Telegram и нажмите тест. Модуль вызывает системный `curl` через `fork/exec` без shell-интерпретации аргументов.

## GitHub

Repository: `https://github.com/monkipnet-spec/Monk_SKUD_UNEX.git`
