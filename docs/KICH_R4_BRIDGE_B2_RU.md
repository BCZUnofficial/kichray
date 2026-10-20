# KichRayDX11 — мост R4, B2 v1

База: `BCZUnofficial/kichray`, коммит
`6d832035330a004a2ffabce8d124f17409dcc563`, ветка `feature/dx11-r4-bridge`,
проверенные пакеты B0 v1 + B1 v1. Архив **накопительный: B0 + B1 + B2**.
Содержимое `FILES` накладывается в корень исходников. В `BASE_MANIFEST.json`
есть SHA-256 базы, файлов пакета B1 и итоговых файлов.

## Что реализовано

Для R4 создан самостоятельный ресурсный backend D3D11. Он владеет устройством,
контекстом, текстурами и их views, буферами, SM5-шейдерами, байткодом и
отражёнными таблицами констант. EXE получает версионированную C-таблицу функций
`KichR4_GetResourceAPI`, описания и числовые handles. COM-указатели, STL,
выделенная память и графические форматы API через эту границу не передаются.

Реализованы:

- hardware FL11_0 device, без окна и без WARP;
- 2D-текстуры: RGBA8, RGBA8 sRGB, RGBA16F, R32F, Depth24/Stencil8, Depth32F;
- SRV/RTV для цветовых и SRV/DSV для depth-ресурсов;
- vertex/index/constant buffers с проверкой размеров и начальных данных;
- компиляция `vs_5_0` / `ps_5_0`, создание нативных shader objects;
- reflection: cbuffer slots/sizes, имена, байтовые смещения и типы переменных,
  ориентация матриц, texture/sampler slots;
- отказ для неверного владельца, повторного освобождения и устаревших handles;
- автоматическое освобождение оставшихся дочерних ресурсов при destroy device;
- счётчики созданных/освобождённых ресурсов и логического объёма данных;
- отдельный тест `-kich_r4_resources` перед обычным запуском R1/R2.

Управление жизненным циклом старого `CResourceManager` перенесено из общих
методов `CRenderDevice` в DX9-адаптер B1: allocate/release, create/destroy,
reset_begin/reset_end, DestroyNecessaryTextures. Сохраняется прежний порядок
вызовов относительно HW/cache/renderer/gamma/UI. Поле `Device.Resources`
остаётся для существующего кода DX9, а ресурсы R4 в него не помещаются.

## Граница DX9 / DX11

| Участок | Владелец и правило B2 |
|---|---|
| `HW.*`, `R_Backend.*` | Прежний DX9-домен R1/R2; в R4 не включаются |
| `ResourceManager.*`, `SH_Texture.*`, `SH_RT.*` | Старые DX9-ресурсы и ref-counted wrappers; layout не меняется |
| `r_constants.*` | Прежние D3D9 register tables; R4 их не читает |
| `Render.h` | Существующая vtable и `texture_load()` сохраняются для R1/R2; R4 её не реализует |
| `KichRenderDevice.h` | Внутренний интерфейс EXE; новые resource lifecycle hooks без D3D-типов |
| `KichR4Resources.h` | Отдельная согласованная C ABI EXE/R4; форматы и виды ресурсов определены явно |
| `R4Resources.cpp` | Нативные D3D11-объекты, views, SM5 compiler/reflection; один владелец внутри DLL |

Таким образом, существующую D3D9 vtable не пришлось расширять или выдавать
D3D11-текстуру за `IDirect3DBaseTexture9*`. Публичные layout-заголовки
`device.h`, `EngineAPI.h`, `Render.h`, `HW.h`, `R_Backend.h`,
`ResourceManager.h`, `SH_Texture.h`, `SH_RT.h`, `r_constants.h` совпадают
с базой. B2 не требует новой vtable от `xrGame.dll` или R1/R2.

Игровая сцена пока рисуется выбранным R1/R2. **Swap chain, показ DX11-кадра,
resize и Alt+Tab самого R4 — следующий B3.** Этот пакет ещё не загружает игровые
DDS/OGF/уровни в DX11 и не переносит эффекты. Для ресурсов v1 предусмотрены
один mip, один slice, sample count 1; нет cube/UAV/structured buffers.
Шейдеры v1 поддерживают простые scalar/vector/matrix/array constants,
Texture2D и samplers. Неподдерживаемые reflection-типы возвращают отказ,
а не неверную таблицу. Внешние `#include` и macros пока не подключены.

## Установка и сборка

1. Закрой игру и Visual Studio. Сохрани проверенные `XR_3DA.exe`,
   `xrRender_R4.dll` из B1 и свою копию заменяемых исходников.
2. В исходниках KichRayDX11 должна быть твоя ветка `feature/dx11-r4-bridge`.
   Скопируй **содержимое каталога `FILES`** в корень с заменой:
   `XR_3DA.sln` рядом с `xr_3da`, без дополнительной вложенности `FILES`.
3. Открой решение и пересобери **XR_3DA и xrRender_R4, Release / Win32**.
   Если используешь ранее настроенный Mixed, сохрани его toolset/SDK.
4. При ручном сохранении своих `.vcxproj` настроек добавь:
   в EXE — `KichR4ResourceProbe.cpp` **без PCH**, соответствующий `.h` и
   `KichR4Resources.h`; в R4 — `R4Resources.cpp`, `R4ResourceRules.h`,
   `../KichR4Resources.h`. В R4 также обновлены `.def` и ExceptionHandling
   (`Async`, /EHa). `KichRenderDevice_DX9.cpp` использует обычный `stdafx.h`.
   Остальные изменения исходников всё равно скопируй из пакета.
5. Положи **оба свежих файла** `XR_3DA.exe` и `xrRender_R4.dll` в игровой `bin`.
   У пробной DLL должно быть именно это имя, без `.disabled`.
   Старые R1/R2/xrGame/xrCore из согласованной рабочей сборки подходят:
   их публичные интерфейсы этим пакетом не менялись.

Для SM5-компиляции R4 динамически загружает **системную `d3dcompiler_47.dll`**.
Её отсутствие даст FAIL с HRESULT в B2, затем игра продолжит обычный R1/R2.
Дополнительной зависимости линкера от D3DX9/D3D11/DXGI у R4 нет. Основной
DX9-движок сохраняет свои прежние библиотеки. Исходники и тесты входят в архив;
готовых DLL/EXE или установщика в нём нет.

## Запуск B2

В параметры того же ярлыка/лаунчера добавь:

```text
-kich_r4_probe -kich_r4_resources
```

`-kich_r4_resources` сам включает handshake и аппаратную проверку B0,
поэтому достаточно и одного этого ключа. Прежний `-kich_r4_probe` отдельно
по-прежнему выполняет только B0, без создания ресурсов B2.

Ожидаемый успешный блок перед `Loading DLL: xrRender_R1.dll` или R2:

```text
[R4 bridge B2] begin: isolated DX11 resources; ABI v1; SM5 reflection
[R4 bridge B2] textures: 6 formats; color SRV/RTV and depth SRV/DSV OK
[R4 bridge B2] buffers: vertex/index/constant; descriptor validation OK
[R4 bridge B2] shaders: vs_5_0/ps_5_0; cbuffer offsets, matrix layout and binding slots OK
[R4 bridge B2] PASS: resources created=11 released=11 live=0; owners isolated; no swap chain
```

Два временных D3D11-устройства нужны для проверки разных владельцев.
Первое получает 6 текстур, 3 буфера, VS и PS. Одна текстура намеренно
остаётся до destroy device. После теста все ресурсы и оба устройства
освобождены, R4 DLL выгружена, затем загружается выбранный R1/R2.
Числа в PASS относятся к ресурсным записям; views и shader bytecode
принадлежат своей записи. Счётчик не заменяет проверку COM debug layer.

При обычной инициализации R1/R2 новый EXE также пишет:

```text
[R4 bridge B2] resource owner=DX9 legacy; lifecycle through backend adapter
```

Это подтверждает маршрут DX9 lifetime hooks, но **само по себе не означает
успех теста DX11**. Для этого нужна строка B2 PASS.

Если новый EXE встретит старую B0/B1 DLL, будет `resource export missing`.
С несовпадающей таблицей будет `resource ABI/table mismatch`, без вызовов
ресурсного API. DLL без B2 не мешает обычной игре, но для проверки B2
обновить нужно оба бинарника. Неисправный ресурсный тест также не переключает
renderer и не меняет `user.ltx`.

## Что проверить и прислать

1. Запусти с двумя ключами на R2. Дождись B2 PASS, загрузи прежнее сохранение,
   проверь изображение и HUD, выполни `vid_restart`, Alt+Tab и вернись.
2. Смени разрешение и верни исходное; выйди штатно.
3. Повтори короткий запуск на R1 с теми же ключами, загрузкой и `vid_restart`.
4. Один раз запусти без ключей: B2-тест DX11 не должен запускаться,
   обычный R1/R2 должен работать через обновлённый адаптер.
5. Пришли полные логи R2 и R1; при сбое сборки — текст ошибок компилятора,
   при вылете — также crash log. Имена логов лучше переименовать с суффиксами
   `_B2_R2` и `_B2_R1`, чтобы они не перезаписались.

После успешного Windows-теста можно зафиксировать B2 и перейти к B3.

## ABI и владение для дальнейшей разработки

Resource ABI имеет собственную версию `0x00010000`; версия B0 остаётся
`0x00010000` в своём независимом контракте. Таблица B2 имеет размер 56 байт
в Win32, pack(4), __cdecl, проверяемые size/version/pointer_size/flags.
Создание возвращает HRESULT-style result и uint64 handle, 0 означает null.
Неудачное создание обнуляет handle. Все output buffers принадлежат вызывающему.

Устройство и ресурсы уничтожаются в создавшем устройство потоке. Внутренний
реестр защищён SRW lock; вызов из другого потока для существующего устройства
возвращает E_THREAD. Одновременно допускается до четырёх временных устройств.
Handles не переиспользуются в течение загрузки DLL. После FreeLibrary они
недействительны, даже если числовое значение совпало при новой загрузке.
Устройства необходимо явно уничтожить до FreeLibrary; DllMain не вызывает D3D.

Constant buffers: кратность 16, максимум 65536 байт. Vertex/index buffers v1:
максимум 64 MiB, полные начальные данные или NULL; индекс — 16/32 bit.
Размер 2D-текстур — 1..16384 по каждой оси. Ошибки выделения/формата возвращает
D3D11; дескриптор не обещает успешное выделение предельного размера.
`payload_bytes` — только логические байты живых текстур/буферов, не занятая VRAM.

## Проверки перед выдачей

- **130 сравнений lifecycle-трасс** против исходного коммита совпали, включая
  Release/Debug/Dedicated/Debug Dedicated/ветви `_EDITOR`; 1005 дополнительных
  утверждений в новой версии. Это компиляция извлечённых реальных методов
  с тестовыми engine services, а не полная сборка редакторов или игры.
- **165 проверок** production validation/ownership store: форматы/views,
  размеры, выравнивание, данные, owner isolation, stale/double free, очистка,
  исчерпание идентификаторов и освобождение при неудачном insert.
- **194 проверки** реального EXE resource probe: нормальный проход, отказ
  в каждой из 53 позиций API-вызовов, 7 повреждённых resource/reflection отчётов,
  несовпадение ABI, отсутствующий экспорт, очистка созданного.
- **100 проверок** реального B0/B2 loader: opt-in, старые DLL, ABI/report
  validation, fallback, точные ключи, сохранение FPU environment после B2.
- C11-совместимость ABI header и восстановление исходного pack; фиксированные
  размеры структур и Win32 таблицы проверяются при компиляции.
- Portable rules/probe/loader также прошли AddressSanitizer и UBSan;
  LeakSanitizer отключён из-за ограничений среды. Освобождение модельных
  ресурсов отдельно контролируется счётчиками тестов.
- Настоящие исходники R4 собраны/слинкованы MinGW-w64 GCC 13 в Win32 PE DLL
  с реальными D3D11/DXGI/compiler SDK headers. Проверены 4 стабильных экспорта
  и отсутствие статических imports D3D9/D3D11/DXGI/compiler/xrCore.
- Новые EXE probe units и DX9-адаптер скомпилированы в Win32 SDK seam;
  проекты и ссылки на файлы проверены, публичные headers сравнены с базой,
  `git diff --check` прошёл.

Повторить native-тесты из корня при наличии Python 3, gcc/g++, Git-истории базы:

```text
python tests/r4_resources/run_tests.py
python tests/r4_device/run_tests.py
```

Полная MSVC/PCH/линковка решения, SEH-поведение и фактический D3D11-тест
на Windows/GPU здесь не запускались. Их проверяешь своей сборкой и логами.
Успешные логи B0/B1 на R1/R2 уже получены, они не заменяют этот B2-тест.

## Developer PowerShell: коммит и отправка в тестовую ветку

Из корня исходников KichRayDX11, после успешной проверки:

```powershell
function Invoke-KichGit {
    & git @args
    if ($LASTEXITCODE -ne 0) { throw "Git завершился с ошибкой: $LASTEXITCODE" }
}
$kichBranch = git branch --show-current
if ($LASTEXITCODE -ne 0) { throw 'Не удалось определить ветку' }
if ($kichBranch -ne 'feature/dx11-r4-bridge') { throw "Текущая ветка: $kichBranch" }
$kichStaged = git diff --cached --name-only
if ($LASTEXITCODE -ne 0) { throw 'Не удалось проверить индекс' }
if ($kichStaged) { throw 'Сначала проверь уже подготовленные к коммиту файлы' }

$kichFiles = @(
    'XR_3DA.sln'
    'xr_3da/EngineAPI.cpp'
    'xr_3da/XR_3DA.vcxproj'
    'xr_3da/XR_3DA.vcxproj.filters'
    'xr_3da/KichR4Bridge.h'
    'xr_3da/KichR4Probe.h'
    'xr_3da/KichR4Probe.cpp'
    'xr_3da/KichR4Resources.h'
    'xr_3da/KichR4ResourceProbe.h'
    'xr_3da/KichR4ResourceProbe.cpp'
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
    'tests/r4_resources'
    'docs/KICH_R4_BRIDGE_B0_RU.md'
    'docs/KICH_R4_BRIDGE_B1_RU.md'
    'docs/KICH_R4_BRIDGE_B2_RU.md'
)
Invoke-KichGit add -- @kichFiles
Invoke-KichGit diff --cached --stat
Invoke-KichGit commit -m 'Add B2 isolated DX11 resources and versioned resource ABI'
Invoke-KichGit push -u origin HEAD:refs/heads/feature/dx11-r4-bridge
```

Явный refspec направляет коммит в `feature/dx11-r4-bridge`.
Скрипт не отправит изменения, если выбрана другая локальная ветка.
Если B0/B1 ещё не закоммичены, список включает и их файлы.

## Основание и первичные источники

- Предоставленные `Kichray_R4_bridge_architecture_v0_1(1).md`, раздел 4/B2,
  и `Kichray_DX11_roadmap(1).md`. Реализация самостоятельная; vtable CoP не копировалась.
- [Исходная база Kichray](https://github.com/BCZUnofficial/kichray/tree/6d832035330a004a2ffabce8d124f17409dcc563)
- [Microsoft: depth resource, DSV и SRV](https://learn.microsoft.com/en-us/windows/uwp/gaming/create-depth-buffer-resource--view--and-sampler-state)
- [Microsoft: constant buffers](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-buffers-constant-how-to)
- [Microsoft: D3DReflect](https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dreflect)
- [Microsoft: exception handling model](https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-170)
