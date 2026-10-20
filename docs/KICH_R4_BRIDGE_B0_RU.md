# KichRayDX11 — мост R4, B0 v1

База: `BCZUnofficial/kichray`, `feature/dx11-r4-bridge`, коммит
`6d832035330a004a2ffabce8d124f17409dcc563`.

Это реализация пакета **B0** из `Kichray_R4_bridge_architecture_v0_1(1).md`,
разделы 4 и 7. Проверяется граница EXE ↔ экспериментальная DLL и создание
аппаратного устройства D3D11 с feature level 11_0. Игровой кадр остаётся на
выбранном R1/R2. Следующий пакет B1 — отделение жизненного цикла устройства;
swap chain, Present и сцена R4 относятся к B3/B4.

## Что делает сборка

1. Без ключа запуска работает существующий путь выбора R1/R2. Новая DLL
   не загружается.
2. С `-kich_r4_probe` загрузчик ищет `xrRender_R4.dll` строго рядом с EXE.
3. Проверяются три именованных экспорта и точное совпадение ABI `0x00010000`.
4. DLL загружает системную `d3d11.dll`, запрашивает временное аппаратное
   устройство FL11_0, читает описание выбранной Windows видеокарты и
   освобождает COM-объекты и свою ссылку на runtime.
5. EXE проверяет отчёт, пишет результат в обычный игровой лог, выгружает
   пробную DLL и продолжает штатный выбор R1/R2.

Окно/swap chain для DX11 не создаются. Нет WARP-подмены аппаратного теста.
Команда `renderer_r4` не добавлена, конфиг выбора рендера пробник не меняет.
`DllMain` новой DLL только возвращает `TRUE`.

Контракт использует C ABI: `__cdecl`, целочисленные типы фиксированной ширины,
структуру 164 байта с размером и версией, буфер вызывающей стороны. C++-классы,
владение памятью, D3D9/D3D11-ресурсы через эту границу не передаются.
Загрузчик сохраняет окружение FPU вокруг диагностики. В MSVC предусмотрены
SEH-обработчики для загрузки, экспортов и выгрузки DLL. Это обработка обычных
ошибок и SEH-исключений, а не изоляция произвольного повреждения процесса
сторонней DLL или драйвером.

## Состав файлов

| Файл относительно корня исходников | Назначение |
|---|---|
| `XR_3DA.sln` | Добавлен проект R4; обычные конфигурации собирают его, Dedicated — нет |
| `xr_3da/EngineAPI.cpp` | Один вызов диагностики перед штатной загрузкой рендера, лог через `Msg` |
| `xr_3da/XR_3DA.vcxproj` и `.filters` | Подключены два заголовка и загрузчик без engine PCH |
| `xr_3da/KichR4Bridge.h` | Версия, экспортные имена, типы функций и фиксированный C ABI отчёта |
| `xr_3da/KichR4Probe.h` и `.cpp` | Проверка ключа, загрузка, handshake, валидация и журналирование |
| `xr_3da/xrRender_R4/xrRender_R4.cpp` | Самостоятельный аппаратный пробник D3D11 |
| `xr_3da/xrRender_R4/xrRender_R4.def` | Три стабильных имени экспортов для x86 |
| `xr_3da/xrRender_R4/xrRender_R4.vcxproj` | DLL Win32, Windows SDK, статический CRT; без связи с xrCore/R1/R2 |
| `tests/r4_bridge/` | Тесты реального кода загрузчика с подстановкой Win32-вызовов |
| `docs/KICH_R4_BRIDGE_B0_RU.md` | Эта инструкция |

Размеры `CEngineAPI`, `CRenderDevice` и интерфейс `Render.h` этим пакетом
не меняются. Шейдеров и игровых ресурсов в B0 нет.

## Подготовить тестовую ветку

Открой Developer PowerShell в **корне KichRayDX11**, где лежит `XR_3DA.sln`.
Ниже определён помощник, который останавливает последовательность при ошибке
Git и работает также в Windows PowerShell 5.1:

```powershell
function Invoke-KichGit {
    & git @args
    if ($LASTEXITCODE -ne 0) { throw "Git завершился с ошибкой: $LASTEXITCODE" }
}

Invoke-KichGit fetch origin
Invoke-KichGit switch feature/dx11-r4-bridge
Invoke-KichGit pull --ff-only origin feature/dx11-r4-bridge
Invoke-KichGit status --short --branch
Invoke-KichGit rev-parse HEAD
```

Для этой версии ожидается HEAD `6d832035330a004a2ffabce8d124f17409dcc563`.
Архив содержит полные изменённые файлы именно этой базы. При более новой
версии исходников перед заменой нужно сопоставить изменения.

Если KichRayDX11 распакован из ZIP и **в корне вообще нет `.git`**, вместо
предыдущего блока выполни следующий. `reset --mixed` здесь привязывает
только что созданный репозиторий к существующей тестовой ветке, сохраняя
файлы рабочей папки. Для уже существующего репозитория этот блок не нужен.

```powershell
function Invoke-KichGit {
    & git @args
    if ($LASTEXITCODE -ne 0) { throw "Git завершился с ошибкой: $LASTEXITCODE" }
}

if (Test-Path .git) { throw "В этой папке Git уже настроен: используй предыдущий блок" }
if (-not (Test-Path .\XR_3DA.sln)) { throw "Открой корень KichRayDX11" }
Invoke-KichGit init -b feature/dx11-r4-bridge
Invoke-KichGit remote add origin https://github.com/BCZUnofficial/kichray.git
Invoke-KichGit fetch origin
Invoke-KichGit reset --mixed origin/feature/dx11-r4-bridge
Invoke-KichGit status --short --branch
```

## Установка исходников и сборка

1. Закрой решение Visual Studio. Сохрани свою рабочую копию заменяемых файлов.
2. Скопируй **содержимое `FILES`** из архива в корень `KichRayDX11` с заменой.
   На одном уровне должны оказаться `XR_3DA.sln`, `xr_3da`, `docs` и `tests`.
   Саму папку `FILES` внутрь проекта переносить не нужно.
3. Если ты локально перенастраивал toolset/SDK в `XR_3DA.vcxproj`, сохрани эти
   настройки. Смысл изменения этого проекта — добавить `KichR4Bridge.h`,
   `KichR4Probe.h` и `KichR4Probe.cpp`; у последнего отключён precompiled header.
   При необходимости эти три элемента можно добавить в свой настроенный
   проект через Visual Studio вместо замены самого `.vcxproj`.
4. Открой обновлённый `XR_3DA.sln`. Выбери **Release / Win32**. В диспетчере
   конфигураций должен появиться `xrRender_R4` с включённой сборкой.
5. Собери `XR_3DA` и `xrRender_R4`, либо всё решение. Для Release новый R4
   использует `v142` и Windows SDK `10.0`, как базовые проекты этой ветки.
   Если твоя рабочая копия уже переведена на иной установленный toolset,
   выставь его также у нового проекта R4. Сборка остаётся Win32/x86.
6. По исходной настройке результаты будут в `..\bin\` относительно
   KichRayDX11: свежий `XR_3DA.exe` и `xrRender_R4.dll`. R4 использует только
   заголовки Windows SDK; `d3d11.lib`, `dxgi.lib`, D3DX11 и shader compiler
   для B0 не требуются.
7. В закрытой тестовой игре замени EXE и добавь рядом `xrRender_R4.dll`.
   Остальные игровые DLL должны соответствовать исходной рабочей сборке
   этой ветки. Для самого B0 пересборка xrGame/R1/R2 не требуется.

## Проверка в игре

1. Запусти обычным способом без нового ключа. Проверь меню и загрузку
   сохранения на привычном R2. Строк `[R4 bridge B0]` быть не должно.
2. В параметры того же ярлыка добавь **`-kich_r4_probe`**, сохранив остальные
   параметры и рабочую папку. Запусти игру.
3. Найди в игровом логе блок примерно такого вида:

```text
[R4 bridge B0] begin: diagnostics only; no DX11 scene or swap chain
[R4 bridge B0] ABI OK: 0x00010000
[R4 bridge B0] hardware=yes; status=supported; HRESULT=0x00000000; feature_level=0xB000
[R4 bridge B0] adapter=...; vendor=... device=...; dedicated VRAM=... MiB
[R4 bridge B0] PASS: temporary hardware FL11_0 device created and released
[R4 bridge B0] end: continuing normal R1/R2 selection
```

4. После блока должна продолжиться штатная загрузка R2 или выбранного R1.
   Проверь сохранение, выход из игры и обычный повторный запуск.
5. Проверь отсутствие DLL: при закрытой игре временно переименуй её в
   `xrRender_R4.dll.disabled` и запусти с тем же ключом. Ожидается
   `DLL unavailable` и продолжение обычного запуска. Верни имя файла.
6. Если используешь R1, повтори запуск с ключом и выбранным R1. Пробник
   не должен переключать режим освещения.

При отсутствии runtime/поддержки FL11_0 будет `hardware=no` с HRESULT;
игровой путь R1/R2 продолжится. Win32 `126` обычно означает отсутствие DLL
либо её зависимости, `193` — неподходящий формат/разрядность. Полный лог
поможет отличить причину. Название адаптера относится к стандартному выбору
Windows при создании устройства; отдельный выбор GPU ещё не реализован.
VRAM — значение, сообщённое DXGI в x86-процессе.

Успех B0 подтверждает handshake и временное D3D11-устройство. Он не проверяет
DX11-вывод уровня, swap chain, R4-шейдеры, Alt+Tab/resize нового backend
или совместимость ресурсов ТЧ с R4. С B0 картинка остаётся прежней.

Для разбора результата нужен обычный игровой лог целиком, особенно строки
`[R4 bridge B0]` и последующая загрузка выбранной DLL рендера.

## Отправить проверенную версию в GitHub

Оставаясь в корне KichRayDX11 и используя `Invoke-KichGit` из блока выше:

```powershell
$kichBranch = git branch --show-current
if ($LASTEXITCODE -ne 0) { throw "Не удалось определить текущую ветку" }
if ($kichBranch -ne 'feature/dx11-r4-bridge') { throw "Текущая ветка: $kichBranch" }
$kichStaged = git diff --cached --name-only
if ($LASTEXITCODE -ne 0) { throw "Не удалось проверить индекс Git" }
if ($kichStaged) { throw "В индексе уже есть файлы: сначала проверь их отдельно" }

Invoke-KichGit add -- XR_3DA.sln xr_3da/EngineAPI.cpp xr_3da/XR_3DA.vcxproj xr_3da/XR_3DA.vcxproj.filters xr_3da/KichR4Bridge.h xr_3da/KichR4Probe.h xr_3da/KichR4Probe.cpp xr_3da/xrRender_R4 tests/r4_bridge docs/KICH_R4_BRIDGE_B0_RU.md
Invoke-KichGit diff --cached --stat
Invoke-KichGit commit -m "Add B0 DX11 R4 diagnostic bridge"
Invoke-KichGit push -u origin HEAD:refs/heads/feature/dx11-r4-bridge
```

Последняя команда явно отправляет текущий коммит в
`feature/dx11-r4-bridge` на origin. `master` этой командой не обновляется.
Команд `--force` или слияния в пакет не включено. Сам архив не добавляй
в исходники: достаточно содержимого `FILES`.

## Проверки перед выдачей

- 75 проверок реального загрузчика: opt-in, абсолютный путь DLL, отсутствие
  библиотеки/экспортов, несовпадение ABI, отказы runtime/устройства,
  некорректные отчёты и сохранение FPU.
- Те же проверки прошли с AddressSanitizer и UndefinedBehaviorSanitizer
  (проверка утечек LSan отключена из-за ограничений среды).
- Новая DLL скомпилирована и слинкована MinGW-w64/GCC 13 под Win32/x86;
  загрузчик отдельно скомпилирован в x86 COFF. Экспорты проверены в PE:
  `KichR4_GetBridgeVersion`, `KichR4_ProbeHardware`, `KichR4_GetProbeInfo`.
- У проверочной DLL нет статических импортов D3D9/D3D11/DXGI. В архиве
  поставляются исходники; тестовая cross-compiled DLL в него не входит.
- Проверены XML проектов, ссылки на новые файлы, конфигурации решения,
  отсутствие сборки R4 в Dedicated и `git diff --check`.

Полная сборка решения MSVC, MSVC SEH, настоящий GPU и игровой запуск
в этой среде не проверялись. Эти результаты должны быть получены на Windows.

## Исходная база и документация

- [EngineAPI.cpp — точная база](https://github.com/BCZUnofficial/kichray/blob/6d832035330a004a2ffabce8d124f17409dcc563/xr_3da/EngineAPI.cpp)
- [EngineAPI.h — текущий интерфейс загрузчика](https://github.com/BCZUnofficial/kichray/blob/6d832035330a004a2ffabce8d124f17409dcc563/xr_3da/EngineAPI.h)
- [BUILD_RU.md — сборка базы](https://github.com/BCZUnofficial/kichray/blob/6d832035330a004a2ffabce8d124f17409dcc563/BUILD_RU.md)
- [Microsoft: D3D11CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice)
- [Microsoft: DllMain](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain)
- [Microsoft: Dynamic-Link Library Best Practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)

Код пробника написан для этого этапа; исходники CoP в пакет не копировались.
