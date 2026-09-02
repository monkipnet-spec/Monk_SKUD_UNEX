# Monk SKUD Monitor Android v1.0

Полностью отдельное приложение-киоск для телевизора/Android TV приставки.

- Android 5.0+ (`minSdk 21`).
- Полноэкранный WebView, landscape, экран не гаснет.
- На первом запуске просит адрес, например `http://192.168.1.50:8080/monitor.html`.
- Каждые 3 секунды вызывает `window.refreshMonitor()` страницы без мерцания полной перезагрузки.
- Долгое нажатие по экрану или кнопка MENU открывает изменение URL.
- Автозапуск после загрузки приставки: `BOOT_COMPLETED` / `QUICKBOOT_POWERON`, задержка 8 секунд для поднятия сети.
- Автозапуск также после обновления пакета (`MY_PACKAGE_REPLACED`).
- Если сеть/сервер при старте еще недоступны, WebView автоматически повторяет загрузку каждые 3 секунды.
- Разрешен локальный HTTP (`usesCleartextTraffic=true`).

## Сборка

Откройте проект в Android Studio или выполните `gradle assembleDebug` / `./gradlew assembleDebug` после добавления Gradle Wrapper.

APK: `app/build/outputs/apk/debug/app-debug.apk`.
