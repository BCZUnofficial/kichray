# KichRayDX11 — B3 v1: первый отображаемый DX11-кадр

База: обновлённая пользователем ветка `feature/dx11-r4-bridge`, коммит
`b0f1716868da98091454223c82a8e943026a0337` (`DX11B2`).
Пакет накладывается **на эту версию с установленным B2**. В отличие от прежнего
накопительного B2, здесь только новые и изменённые файлы B3. Изменения воды,
SSAO и их bindings из обновлённой ветки сохранены.

## Результаты Windows-проверки B2

| Лог | Подтверждено |
|---|---|
| `log_stable(20261020-075702).log` | B0 и B2 PASS на RTX 2080 / FL11_0; 11 ресурсов созданы и освобождены; VS/PS SM5 и reflection прошли; затем R1, сохранение, смены разрешения, 11 reset, штатный destroy |
| `log_stable(20261020-075847).log` | Обычный запуск R2 без диагностических ключей; загрузка сохранения, vid_restart, смены разрешения, 11 reset, штатный destroy |

Таким образом, аппаратный B2-тест подтверждён в первом запуске, а работа
обновлённого DX9-адаптера — на обоих рендерах. Во втором логе B2 resource smoke
не запускался: это ожидаемо для запуска без ключа.

## Что делает B3

Ключ `-kich_r4_frame` перед загрузкой игрового R1/R2 открывает **самостоятельное
окно DX11** внутри того же EXE. R4 создаёт hardware FL11_0 device, swap chain,
backbuffer view, очищает его цветом и вызывает Present. Цвет плавно меняет
яркость, чтобы было видно, что кадры продолжают выводиться.

Можно менять размеры, разворачивать окно, сворачивать его, переключаться
через Alt+Tab и переходить в безрамочный полноэкранный режим. Закрытие окна
или Esc завершает тест, освобождает DX11, затем продолжается обычный запуск
игры на выбранном R1/R2. Новый ключ сам включает проверку B0.

Это отображаемый тестовый кадр, пока без уровня, HUD и игровой геометрии R4.
После успешной проверки B3 следующий этап — B4, подключение сцены ТЧ.

## Установка

1. Закрой игру и Visual Studio. Сохрани проверенные EXE/DLL B2 и копию
   заменяемых исходников. Работай в `feature/dx11-r4-bridge`.
2. Скопируй **содержимое `FILES`** в корень обновлённого репозитория KichRayDX11
   с заменой. Не копируй сверху старые накопительные B0/B1/B2-архивы.
3. Открой своё решение и пересобери **XR_3DA и xrRender_R4, Release / Win32**
   либо привычный Mixed / Win32 с согласованным toolset. Настройки проектов
   взяты из обновлённой ветки; добавлены новые исходники B3.
4. Перенеси свежие `XR_3DA.exe` и `xrRender_R4.dll` в игровой `bin`.
   DLL должна называться именно `xrRender_R4.dll`, без `.disabled`.

При сохранении собственных настроек `.vcxproj` вручную добавь:

- в EXE: `KichR4FrameProbe.cpp` с **PrecompiledHeader = NotUsing**;
  headers `KichR4Frame.h`, `KichR4FrameProbe.h`, `KichR4FrameSession.h`;
- в R4: `R4FrameDX11.cpp`, headers `R4FrameDX11.h`, `R4FrameController.h`,
  `../KichR4Frame.h`; обнови `.def`, добавь `user32.lib`;
- скопируй изменённые `KichR4Probe.cpp`, `R4Resources.cpp` и остальные
  перечисленные в пакете исходники. R4 остаётся без engine PCH, с /MT и /EHa.

Для B3 достаточно двух новых бинарников EXE/R4; старые R1/R2/xrGame/xrCore
берутся из согласованной рабочей сборки. Публичные layouts и vtable старых
компонентов не менялись. R4 по-прежнему загружает системный D3D11 динамически;
для одной очистки/Present компилятор шейдеров не нужен.

## Параметры запуска и управление

Для первого B3-запуска достаточно:

```text
-kich_r4_frame
```

Для одновременной повторной проверки ресурсов B2:

```text
-kich_r4_resources -kich_r4_frame
```

Сначала пройдёт B0, затем B2, затем откроется B3-окно. Оставлять старый
`-kich_r4_probe` дополнительно можно, но необходимости в нём нет.
Без `-kich_r4_frame` тестовое окно не открывается.

| Действие | Результат |
|---|---|
| Потянуть границу окна, развернуть и восстановить | Меняется размер backbuffer DX11 |
| Alt+Tab | Пауза при потере активности, продолжение после возврата |
| Свернуть / восстановить | Приостановка при нулевом размере окна, затем продолжение |
| F11 или Alt+Enter | Переключение окно / безрамочный режим на текущем мониторе |
| Пробел | Смена синей, красной и зелёной палитры |
| Esc, крестик или Alt+F4 | Закрытие B3-окна и продолжение запуска игры |

Безрамочный режим использует разрешение рабочего стола; эксклюзивный видеорежим
DXGI не включается. В оконном режиме размер меняется растягиванием и кнопкой
разворачивания. Проверка эксклюзивного полноэкранного режима сюда не входит.
Игра продолжит запуск после закрытия B3-окна: пока окно открыто, меню игры
ещё не загружается. Тест не записывает `renderer_r4` в `user.ltx`.

## Проверка на Windows

1. Запусти с `-kich_r4_frame`. Должно появиться окно Kich-Ray DX11 B3 с
   плавно меняющей яркость синей заливкой. Проверь пробел и смену палитр.
2. Несколько раз измени размеры окна, разверни его и восстанови.
3. Сделай Alt+Tab и вернись. Затем сверни окно в панель задач и восстанови.
4. Нажми F11, повтори Alt+Tab и возврат, снова нажми F11.
5. Закрой окно через Esc. Должна загрузиться обычная игра. На R2 загрузи
   сохранение, выполни vid_restart и выйди штатно.
6. Повтори короткий запуск B3 перед R1, закрой тест крестиком, проверь
   загрузку сохранения и штатный выход.
7. Для fallback при закрытой игре временно переименуй R4 DLL в `.disabled`
   и запусти с тем же ключом. Должен запуститься обычный R1/R2 без B3-окна.
   После проверки верни имя DLL.
8. Один обычный запуск без ключа подтвердит, что окно B3 не появляется.

Пришли полные логи B3 → R2 и B3 → R1. При ошибке сборки пришли текст ошибки
с именем проекта; при вылете — также crash log. Напиши, прошли ли resize,
сворачивание, F11 и Alt+Tab визуально: счётчик Present сам по себе этого
не доказывает.

## Ожидаемый лог

```text
[R4 bridge B3] begin: autonomous DX11 frame test before R1/R2
[R4 bridge B3] create: 960x540; hardware FL11_0; RGBA8; vsync=1; windowed
[R4 bridge B3] first DX11 frame presented
[R4 bridge B3] resize: ...
[R4 bridge B3] window mode=borderless; changes=1
...
[R4 bridge B3] destroy: presented=... attempts=... clears=... resizes=... occlusions=...; views=N/N; flags=0x0; activations=... mode_changes=...
[R4 bridge B3] PASS: DX11 clear/Present completed; target/device released; continuing R1/R2
```

Размеры и счётчики зависят от действий. В нормальном завершении
`views_created == views_released`, в логе это `views=N/N`, и `flags=0x0`.
Затем идут прежние сообщения загрузки R1/R2 и DX9-адаптера B1/B2.

PASS означает наличие успешного Present, корректный отчёт и завершённое
освобождение. Для принятия B3 также нужны описанные визуальные проверки.
Если закрыть тест до первого успешного кадра, будет INCOMPLETE.

При отсутствии DLL, несовпадении ABI, ошибке создания/resize/Present тест
завершается с диагностикой и продолжается обычный R1/R2. `frame export missing`
означает старую R4 DLL: обнови её вместе с EXE. После device removed/reset
нет бесконечного повторения Present: тест освобождает устройство и завершается.
Автоматическое пересоздание работающего игрового renderer после device removed
в этот этап не входит. Если драйвер не дал закончить cleanup даже после
повторной попытки, PASS не выдаётся, а DLL сохраняется загруженной до выхода
из процесса, чтобы живые объекты не потеряли свой код.

## Архитектура

| Файл | Назначение |
|---|---|
| `KichR4Frame.h` | Версионированная C-таблица Frame API, описания окна/цвета, статистика |
| `KichR4FrameSession.h` | EXE-сессия: create/resize/begin/clear/present/destroy и сохранение первой ошибки |
| `KichR4FrameProbe.cpp` | Отдельное Win32-окно, сообщения, управление и диагностический цикл |
| `R4FrameController.h` | Порядок и состояния кадра, resize, suspend/occlusion, счётчики |
| `R4FrameDX11.*` | Реальные вызовы D3D11/DXGI и владение swap chain/RTV |
| `R4Resources.cpp` | Связывание Frame API с теми же устройствами/потоками/handles B2 |
| `KichR4Probe.cpp` | Новый opt-in ключ и запуск B3 после аппаратного handshake |

Frame ABI v1: Win32, pack(4), __cdecl, таблица 52 байта, version/size/pointer
size/flags проверяются до создания окна. HWND заимствован; EXE владеет окном,
R4 — DX11-объектами. Устройства используют существующий реестр B2.
Весь frame API вызывается в потоке создания устройства/окна.

При resize сначала отвязываются состояния и освобождается RTV; постоянной
GetBuffer-ссылки нет. Затем выполняются ResizeBuffers и создание нового RTV.
Нулевые размеры при сворачивании не передаются в ResizeBuffers. При occlusion
проверяется возможность вывода через Present(TEST), без рисования новых кадров.
При неактивности/occlusion цикл ждёт и обрабатывает сообщения своего окна.
DXGI уничтожается до HWND, а устройство — до выгрузки R4 DLL.
Обычное закрытие тестового окна не отправляет WM_QUIT игровому циклу.

## Проверки перед выдачей

- 2219 проверок настоящих frame controller/session: точный порядок вызовов,
  очистка перед Present, параметры/цвета, resize, 200 изменений размеров,
  suspend/restore, occlusion/test Present, ошибки и освобождение частично
  созданного target, повторное создание, отказы EXE API-вызовов и cleanup.
- 124 проверки loader B0/B2/B3, включая точные ключи, отсутствие DLL,
  аппаратный отказ, несовпадение ABI, сохранение FPU и удержание DLL при
  незавершённом освобождении. GUI-вход в этом тесте заменён тестовой функцией.
- B2: прежние 165 validation/ownership и 194 resource probe проверки прошли.
- 130 сравнений lifecycle-трасс DX9 против исходной базы совпали в пяти
  конфигурациях; дополнительные 1005 сценарных утверждений новой версии прошли.
- Frame/session и loader прошли AddressSanitizer/UBSan; LeakSanitizer отключён
  из-за ограничений среды, владельцы/освобождения проверяются отдельно.
- Настоящие R4-исходники собраны и слинкованы MinGW-w64 GCC 13 в Win32 DLL
  с реальными D3D11/DXGI SDK headers. Проверены пять экспортов: B0 × 3,
  Resource API B2 и Frame API B3. Imports: системные kernel32/msvcrt/user32;
  статических imports D3D9/D3D11/DXGI/compiler/xrCore нет.
- Три настоящих EXE probe units и DX9 adapter Release/Debug скомпилированы
  в Win32 SDK seam; C11/Win32 ABI header, XML проектов и ссылки проверены.
- Девять старых layout-заголовков, B0/B2 ABI headers и текущие water/SSAO
  исходники совпадают с базой. `git diff --check` прошёл.

Команды native-проверок при наличии Python 3, gcc/g++ и истории базы:

```text
python tests/r4_frame/run_tests.py
python tests/r4_resources/run_tests.py
python tests/r4_device/run_tests.py
```

Полная MSVC/PCH/линковка решения, Win32 message loop на настоящем рабочем
столе и драйверный DX11 Present здесь не выполнялись. Их проверяет твоя сборка.

## Developer PowerShell: коммит и тестовая ветка

После установки и успешной проверки, из корня репозитория:

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
    'xr_3da/EngineAPI.cpp'
    'xr_3da/KichR4Probe.cpp'
    'xr_3da/KichR4Frame.h'
    'xr_3da/KichR4FrameProbe.h'
    'xr_3da/KichR4FrameProbe.cpp'
    'xr_3da/KichR4FrameSession.h'
    'xr_3da/XR_3DA.vcxproj'
    'xr_3da/XR_3DA.vcxproj.filters'
    'xr_3da/xrRender_R4/R4Resources.cpp'
    'xr_3da/xrRender_R4/R4FrameController.h'
    'xr_3da/xrRender_R4/R4FrameDX11.h'
    'xr_3da/xrRender_R4/R4FrameDX11.cpp'
    'xr_3da/xrRender_R4/xrRender_R4.def'
    'xr_3da/xrRender_R4/xrRender_R4.vcxproj'
    'tests/r4_bridge/loader_tests.cpp'
    'tests/r4_frame'
    'docs/KICH_R4_BRIDGE_B3_RU.md'
)
Invoke-KichGit add -- @kichFiles
Invoke-KichGit diff --cached --stat
Invoke-KichGit commit -m 'Add B3 standalone DX11 frame, resize and fallback'
Invoke-KichGit push -u origin HEAD:refs/heads/feature/dx11-r4-bridge
```

Refspec отправляет коммит в тестовую `feature/dx11-r4-bridge`.

## Основание

Предоставленная архитектура моста, раздел 4 / B3, и обновлённая тестовая база.
Реализация самостоятельная, существующая vtable R1/R2 и CoP не копировалась.

- [Microsoft: ResizeBuffers и освобождение backbuffer-ссылок](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers)
- [Microsoft: MakeWindowAssociation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-makewindowassociation)
- [Microsoft: DXGI status / occlusion](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-status)
