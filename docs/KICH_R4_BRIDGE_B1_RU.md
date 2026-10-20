# KichRayDX11 — мост R4, B1 v1

Ветка: `feature/dx11-r4-bridge`. Исходная база Kichray:
`6d832035330a004a2ffabce8d124f17409dcc563` + выданный пакет B0 v1.

Архив накопительный: включает B0 и B1. Его можно наложить на проверенный B0;
повторная установка прежнего архива не нужна. Файлы пробника B0 включены
в прежнем виде. В `BASE_MANIFEST.json` указаны исходные и новые SHA-256,
а также хэши соответствующих файлов из пакета B0.

## Результат B1

Добавлен внутренний интерфейс `IKichRenderDevice` и его единственная
пока реализация `CKichRenderDeviceDX9`. Общий жизненный цикл движка обращается
к устройству через этот интерфейс. Игровой кадр продолжает рисоваться
выбранным R1/R2. Ключ `-kich_r4_probe` по-прежнему выполняет отдельную
диагностику D3D11 из B0.

Через DX9-адаптер проходят:

- параметры `-gpu_sw`, `-gpu_nopure`, `-gpu_ref`;
- создание, проверка, сброс и уничтожение аппаратного устройства;
- размеры backbuffer после создания и сброса;
- состояния sampler/render, cache create/destroy;
- cooperative state: ready, lost, needs reset и прочая ошибка;
- BeginScene, очистка depth/color/stencil, EndScene и Present;
- overdraw и его отрисовка перед интерфейсом загрузки;
- матрицы cache и прежняя операция D3DXMatrixInverse;
- прежние CPU-счётчики времени рендера, без новых GPU queries.

В `device.cpp`, `Device_create.cpp`, `Device_destroy.cpp`,
`Device_Initialize.cpp`, `Device_overdraw.cpp` больше нет прямых обращений
к `HW`, `RCache` и D3DX. Эти операции собраны в адаптере.

Интерфейс B1 не содержит D3D-ресурсов или форматов. Для окна передаётся
заимствованный native handle, для размеров — два 32-битных числа. Матрицы
и CPU-таймеры остаются типами xrCore. Это **внутренний C++-интерфейс EXE**;
он не передаётся в `xrRender_R4.dll` и не заменяет версионированный C ABI B0.

## Порядок ресурсов и бинарная граница

В `CRenderDevice` не добавлено полей или виртуальных методов. Файлы
`device.h`, `EngineAPI.h`, `Render.h`, `HW.h` совпадают с исходной базой.
Перехода на x64, изменений скриптов, шейдеров, уровней или сохранений нет.

Порядок reset сохранён:

1. `Resources->reset_begin()` освобождает зависимые ресурсы.
2. Выполняется прежний `Memory.mem_compact()`.
3. Адаптер вызывает прежний `HW.Reset()`.
4. Движок обновляет ширину, высоту и половинные размеры.
5. Вызывается `Resources->reset_end()`.
6. Восстанавливаются состояния, precache, уведомления reset и HUD.

Порядок уничтожения также сохранён: renderer/resources/cache освобождаются
до аппаратного устройства. При device lost остаются пропуск кадра и
`Sleep(33)`; при needs reset — прежний Reset перед BeginScene. Обработка
прочих результатов BeginScene/Present сохраняет поведение исходного кода.
Политика восстановления драйвера этим пакетом не меняется.

Управление `CResourceManager`, `Gamma`, UI и ресурсами renderer пока остаётся
в прежнем DX9-домене. Разделение этих типов и владельцев относится к B2.
Нативный DX11 swap chain и тестовый кадр относятся к B3.

## Изменения относительно B0

| Файл | Изменение |
|---|---|
| `xr_3da/KichRenderDevice.h` | Новый внутренний интерфейс устройства |
| `xr_3da/KichRenderDevice_DX9.cpp` | Новый DX9-адаптер и событийная диагностика |
| `xr_3da/device.cpp` | Кадр, проверка потери устройства, матрицы и таймеры через адаптер |
| `xr_3da/Device_create.cpp` | Создание HW, размеры, состояния и cache create через адаптер |
| `xr_3da/Device_destroy.cpp` | Reset/destroy/validate и debug reference trace через адаптер |
| `xr_3da/Device_Initialize.cpp` | Передача прежних GPU-флагов адаптеру |
| `xr_3da/Device_overdraw.cpp` | Сохранены методы CRenderDevice, реализация перенесена в адаптер |
| `xr_3da/XR_3DA.vcxproj` и `.filters` | Добавлены новый заголовок и `.cpp` |
| `tests/r4_device/` | Сравнение с настоящими функциями исходной базы и SDK-проверка |
| `docs/KICH_R4_BRIDGE_B1_RU.md` | Эта инструкция |

## Установка и сборка

1. Закрой игру и Visual Studio. В исходниках KichRayDX11 должна быть выбрана
   ветка `feature/dx11-r4-bridge`. Сохрани рабочую копию заменяемых файлов.
2. Скопируй **содержимое `FILES`** в корень KichRayDX11 с заменой.
   `XR_3DA.sln` должен оказаться рядом с каталогом `xr_3da`.
3. Если после B0 ты менял SDK/toolset в своём проекте, сохрани эти настройки.
   Изменение `.vcxproj` относительно B0 — добавление `KichRenderDevice.h` и
   `KichRenderDevice_DX9.cpp`. Новый `.cpp` использует обычный engine PCH
   `stdafx.h`; отключать его не нужно. Можно добавить эти два файла в свой
   настроенный проект вручную вместо замены `.vcxproj`.
4. Открой решение и **пересобери XR_3DA, Release / Win32**. Убедись, что
   `KichRenderDevice_DX9.cpp` действительно участвует в сборке.
5. Перенеси свежий `XR_3DA.exe` в игровой `bin` при закрытой игре.
   Для B1 достаточно нового EXE; `xrCore`, `xrGame`, R1/R2 и R4-пробник
   из проверенного B0 можно оставить. При полной сборке используй согласованный
   набор бинарников своей рабочей ветки.
6. Если после проверки отсутствия R4 ты оставил DLL с именем
   `xrRender_R4.dll.disabled`, верни ей имя `xrRender_R4.dll` для повторного
   аппаратного теста B0. Работа B1 не зависит от наличия пробной DLL.

Сборочные настройки основной базы не менялись. Дополнительных SDK или
библиотек для B1 не добавлено. Исходная зависимость DX9 от D3DX9 сохранена
в DX9-адаптере.

## Что должно быть в логе

При обычном запуске, даже без `-kich_r4_probe`:

```text
[R4 bridge B1] backend=DX9 legacy (R1/R2); device interface active
[R4 bridge B1] create: 1920x1080
[R4 bridge B1] state=ready; HRESULT=0x00000000
[R4 bridge B1] first frame presented through DX9 adapter
```

Размеры будут твоими. Сообщение первого успешного Present выводится один
раз за создание устройства. Состояние выводится при изменении, а не каждый
кадр. При reset ожидаются:

```text
[R4 bridge B1] reset hardware: 1920x1080; count=1
*** RESET [... ms]
```

При выходе:

```text
[R4 bridge B1] destroy: frames=... clears=... resets=...
```

`frames` здесь считает обращения к Present, включая его отказ из-за потери
устройства. Эти числа нужны для проверки маршрута вызовов, а не для замера FPS.
С ключом `-kich_r4_probe` перед ними появится прежний блок B0.

## Проверка в игре

1. Обычный запуск без ключа: меню, загрузка прежнего сохранения, изображение,
   HUD, выход. Сравни с тем же местом на B0.
2. В загруженной игре выполни **`vid_restart`**, дождись завершения прогрева,
   затем проверь изображение и управление. Повтори ещё раз.
3. Проверь **Alt+Tab и возврат** в используемом полноэкранном режиме.
4. Измени разрешение через привычные настройки игры, примени его, затем верни
   исходное. Проверь правильный размер изображения и HUD.
5. Загрузись повторно и, по возможности, перейди на соседнюю локацию.
6. Повтори короткий запуск и `vid_restart` на **R1**, затем верни свой R2.
7. Один запуск с **`-kich_r4_probe`** подтвердит, что аппаратная проверка B0
   осталась рабочей вместе с новой границей устройства.

Результат B1: прежняя картинка и поведение R1/R2, успешные reset/возврат в
игру, новые строки маршрута B1. Увеличение FPS или новая графика не являются
целью этого этапа. После проверки пришли обычный игровой лог целиком;
при вылете — также соответствующий crash log.

## Проверки перед выдачей

Сценарный тест извлекает настоящие тела функций из коммита базы и из B1,
компилирует обе версии с одними и теми же подставными engine/D3D-службами
и сравнивает полный порядок вызовов. Проверяются:

- создание и повторное создание, уничтожение и повторное уничтожение;
- depth/stencil/backbuffer clear, wireframe и обе ветви fog;
- overdraw 0/1/2, обычные и прогревочные кадры;
- reset с precache и без него, публикация новых размеров до reset_end,
  нечётные размеры и переключение соотношения сторон;
- device lost, needs reset, отказ Present, прежний путь неожиданной ошибки;
- reference device и сохранение пропуска precache;
- GPU-флаги, передача матриц и CPU-счётчиков через интерфейс.

**130 сравнений трасс совпали**: 26 сценариев для Release, Debug, Dedicated,
Debug Dedicated и условных ветвей `_EDITOR`. Проверка `_EDITOR` относится
к извлечённым методам, а не к сборке редакторов. В версии B1 дополнительно
прошли 1005 сценарных утверждений.

Команда из корня репозитория, если есть Python 3, g++ и Git-история базы:

```text
python tests/r4_device/run_tests.py
```

Это тестовый runner; он создаёт временные сборки и не устанавливает файлы.
В архиве нет установщика или скрипта применения патча.

Адаптер также скомпилирован в Win32/x86 COFF через MinGW-w64/GCC 13 для
Release/Debug с реальными SDK-объявлениями Windows, D3D9 и D3DX9 и
объявлениями используемых engine-служб. Проверены XML проектов, новые
исходники, отсутствие изменений публичных layout-заголовков и
`git diff --check`. Содержимое B0 проверено на точное совпадение с B0 v1.

Полная сборка решения MSVC/PCH/линковка и реальная работа драйвера с B1
в этой среде не выполнялись. Их подтверждает описанная выше проверка Windows.
Успешные Windows-запуски B0 уже получены от пользователя; это отдельный этап.

## Отправка в тестовую ветку

В Developer PowerShell из корня KichRayDX11 после проверки:

```powershell
function Invoke-KichGit {
    & git @args
    if ($LASTEXITCODE -ne 0) { throw "Git завершился с ошибкой: $LASTEXITCODE" }
}

$kichBranch = git branch --show-current
if ($LASTEXITCODE -ne 0) { throw "Не удалось определить ветку" }
if ($kichBranch -ne 'feature/dx11-r4-bridge') { throw "Текущая ветка: $kichBranch" }
$kichStaged = git diff --cached --name-only
if ($LASTEXITCODE -ne 0) { throw "Не удалось проверить индекс" }
if ($kichStaged) { throw "Сначала проверь уже подготовленные к коммиту файлы" }

$kichFiles = @(
    'XR_3DA.sln'
    'xr_3da/EngineAPI.cpp'
    'xr_3da/XR_3DA.vcxproj'
    'xr_3da/XR_3DA.vcxproj.filters'
    'xr_3da/KichR4Bridge.h'
    'xr_3da/KichR4Probe.h'
    'xr_3da/KichR4Probe.cpp'
    'xr_3da/xrRender_R4'
    'xr_3da/KichRenderDevice.h'
    'xr_3da/KichRenderDevice_DX9.cpp'
    'xr_3da/device.cpp'
    'xr_3da/Device_create.cpp'
    'xr_3da/Device_destroy.cpp'
    'xr_3da/Device_Initialize.cpp'
    'xr_3da/Device_overdraw.cpp'
    'tests/r4_bridge'
    'tests/r4_device'
    'docs/KICH_R4_BRIDGE_B0_RU.md'
    'docs/KICH_R4_BRIDGE_B1_RU.md'
)
Invoke-KichGit add -- @kichFiles
Invoke-KichGit diff --cached --stat
Invoke-KichGit commit -m "Add B1 render device boundary with legacy DX9 adapter"
Invoke-KichGit push -u origin HEAD:refs/heads/feature/dx11-r4-bridge
```

Если B0 уже закоммичен, Git включит только фактически изменившиеся файлы.
Явный refspec отправляет коммит в `feature/dx11-r4-bridge`.
Для первоначального подключения папки из ZIP см. инструкцию B0.

## Основание

- Пакет B1 в `Kichray_R4_bridge_architecture_v0_1(1).md`, раздел 4.
- [Исходный device.cpp](https://github.com/BCZUnofficial/kichray/blob/6d832035330a004a2ffabce8d124f17409dcc563/xr_3da/device.cpp)
- [Исходный Device_destroy.cpp](https://github.com/BCZUnofficial/kichray/blob/6d832035330a004a2ffabce8d124f17409dcc563/xr_3da/Device_destroy.cpp)
- [ResourceManager_Reset.cpp: фактический порядок ресурсов ТЧ](https://github.com/BCZUnofficial/kichray/blob/6d832035330a004a2ffabce8d124f17409dcc563/xr_3da/ResourceManager_Reset.cpp)
- [CoP RenderDeviceRender.h: архитектурный ориентир](https://github.com/KaMeHb-UA/X-Ray-1.6/blob/e162d87600976f6fecb00b0a0861a8ae6ea5758a/src/Include/xrRender/RenderDeviceRender.h)
- [Microsoft: TestCooperativeLevel](https://learn.microsoft.com/en-us/windows/win32/api/d3d9helper/nf-d3d9helper-idirect3ddevice9-testcooperativelevel)
- [Microsoft: Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset)

Готовая vtable CoP не копировалась. Реализация адаптера переносит операции
из текущего Kichray и сохраняет их порядок.
