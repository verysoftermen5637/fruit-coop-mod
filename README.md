# Fruit Playground Co-op Multiplayer Mod

Полное руководство по компиляции, развертыванию и запуску кооперативного мода для мобильной игры **Fruit Playground** (Android ARM64, Unity IL2CPP).

---

## 1. Структура проекта
* `coop_mod.cpp` — исходный код нативного C++ хук-модуля под Dobby Hook.
* `CMakeLists.txt` — конфигурация сборки через Android NDK CMake.
* `coop_fp.js` — готовый скрипт динамической инструментации Frida для быстрого тестирования без рекомпиляции.
* `server.js` — легковесный сервер-маршрутизатор комнат на Node.js (WebSocket для команд + UDP для высокочастотного синка физики).

---

## 2. Сборка C++ библиотеки (`libcoop_fp.so`)

### Требования:
* Android NDK (r23b или выше).
* CMake 3.18+.
* Исходники/заголовки [Dobby Hook](https://github.com/jmpews/Dobby).

### Команда сборки:
```bash
$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DCMAKE_BUILD_TYPE=Release \
    -B build
cmake --build build --config Release
```
После сборки в `build/` появится библиотека `libcoop_fp.so`.

---

## 3. Способы инжекции в игру

### Вариант A: Интеграция в APK (Без Root)
1. Декомпилируйте APK игры:
   ```bash
   apktool d fruit_playground.apk -o fp_decompiled
   ```
2. Скопируйте `libcoop_fp.so` в папку нативных библиотек:
   ```bash
   cp build/libcoop_fp.so fp_decompiled/lib/arm64-v8a/
   ```
3. Добавьте автоматическую загрузку библиотеки в точку входа. Откройте `fp_decompiled/smali/com/unity3d/player/UnityPlayerActivity.smali` и в статическом блоке инициализации (`.method static constructor <clinit>()V`) добавьте:
   ```smali
   const-string v0, "coop_fp"
   invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
   ```
4. Пересоберите и подпишите APK:
   ```bash
   apktool b fp_decompiled -o fp_modded.apk
   zipalign -v -p 4 fp_modded.apk fp_aligned.apk
   apksigner sign --ks my-key.keystore fp_aligned.apk
   ```

### Вариант B: Запуск через Frida (Для быстрого тестирования / отладки)
1. Установите `frida-server` на телефон или эмулятор (ARM64) и запустите его с правами root:
   ```bash
   adb push frida-server /data/local/tmp/
   adb shell "chmod 755 /data/local/tmp/frida-server && /data/local/tmp/frida-server &"
   ```
2. Запустите игру и внедрите скрипт:
   ```bash
   frida -U -f com.fruitplayground.game -l coop_fp.js
   ```

### Вариант C: Zygisk модуль (Root / Magisk / KernelSU)
1. Оформить модуль под Zygisk API (перехват `companion_handler` и `preAppSpecialize`).
2. В функции специализации приложения фильтровать пакет `com.fruitplayground.game` и вызывать `dlopen("/data/local/tmp/libcoop_fp.so", RTLD_NOW)`.

---

## 4. Запуск сервера ретрансляции
1. Установите зависимости:
   ```bash
   npm install ws
   ```
2. Запустите сервер:
   ```bash
   node server.js
   ```
   Сервер запустит:
   * **WebSocket на порту 8080** для рукопожатия, выбора комнат (Lobby) и передачи дискретных событий (Spawn, Clear, Slice).
   * **UDP на порту 7777** для минимальной задержки при синхронизации координат физических тел рэгдоллов (FixedUpdate).
