# KichRayDX11 — B4.1 v1: треугольник и 3D-геометрия

База: `feature/dx11-r4-bridge`, зафиксированный B3,
`6f7d09a681f7c9360655a70f5b097451866df1b6`.
Пакет содержит только новые и изменённые относительно этой базы файлы.
Содержимое `FILES` копируется в корень репозитория с заменой.

## Что изменилось

В отдельном окне DX11 теперь рисуется геометрия: сначала цветной треугольник,
по клавише **2** — два вращающихся куба с разным оттенком. Их перекрытие позволяет
проверить глубину. Есть управление камерой, остановка вращения и переключение
отсечения граней. Текущие режимы показаны в заголовке окна и записываются в лог.

На стороне моста добавлены input layout, состояния rasterizer/depth, привязка
vertex/index/constant buffers, обновление констант и DrawIndexed. Матрица объекта,
камеры и проекции передаётся в vertex shader через constant buffer. Pixel shader
выводит цвет. Буфер глубины D32 создаётся при первом рисовании с глубиной,
освобождается при resize и создаётся заново под новый размер.

Это B4.1 — проверка основы рисования перед подключением геометрии уровня ТЧ.
После закрытия теста продолжается обычная игра на R1/R2. Следующий этап B4.2 —
статическая геометрия уровня и текстуры. Материалы, свет, тени и остальная сцена
идут следующими подэтапами.

## Установка и сборка

1. Работай в `feature/dx11-r4-bridge` с уже установленным и зафиксированным B3.
   Сохрани проверенные EXE/DLL B3, закрой игру и Visual Studio.
2. Скопируй **содержимое папки `FILES`** из архива в корень KichRayDX11 с заменой.
   Старые накопительные архивы поверх B4.1 не накладывай.
3. Открой решение и пересобери **XR_3DA и xrRender_R4, Release / Win32**,
   либо свой согласованный Mixed / Win32.
4. Поставь свежие **XR_3DA.exe и xrRender_R4.dll** в игровой `bin`.
   DLL должна называться `xrRender_R4.dll`.

Остальные бинарники берутся из согласованной рабочей сборки.
Для компиляции шейдеров используется системный **d3dcompiler_47.dll**, как в B2.
Его отсутствие даст ошибку компиляции VS в логе; затем продолжится R1/R2.

Если сохраняешь собственный `.vcxproj`, вручную добавь в R4
`R4PipelineDX11.cpp`, headers `R4PipelineDX11.h`, `R4GeometryRules.h`,
`../KichR4Geometry.h`, а в EXE — headers `KichR4Geometry.h`,
`KichR4GeometrySession.h`, `KichR4TestScene.h`. Обнови остальные изменённые
исходники и `.def`. `KichR4FrameProbe.cpp` остаётся без engine PCH;
R4 — без PCH, с /MT, /EHa и прежним `user32.lib`.

## Запуск и управление

Для B4.1 достаточно одного нового ключа:

```text
-kich_r4_geometry
```

Он сам запускает аппаратную проверку B0. В начале появляется окно DX11,
после его закрытия загружается меню игры. В одном запуске обязательно посмотри
**оба режима — 1 и 2**, иначе итог будет INCOMPLETE.

| Клавиша / действие | Результат |
|---|---|
| 1 | Цветной треугольник |
| 2 | Два вращающихся куба |
| P | Остановить / продолжить вращение кубов |
| D | Глубина on / off |
| C | Отсечение back → front → none → back |
| Стрелки | Повернуть камеру вокруг кубов |
| + / −, включая NumPad | Приблизить / отдалить камеру |
| Home | Вернуть исходное положение камеры |
| Пробел | Сменить цвет фона |
| Растянуть / развернуть окно | Изменить размер вывода и проекцию |
| F11 или Alt+Enter | Окно / безрамочный режим |
| Alt+Tab, свернуть / восстановить | Пауза и продолжение вывода |
| Esc, крестик, Alt+F4 | Завершить тест и продолжить запуск игры |

Камера и пауза вращения относятся к кубам. По умолчанию depth=on, cull=back.
При cull=front треугольник исчезает — это ожидаемо: отсечена его лицевая сторона.
На кубах front показывает обратные стороны граней. При none отрисовываются
обе стороны; с включённой глубиной картинка может почти совпадать с back.

Старый `-kich_r4_frame` по-прежнему запускает B3 с цветной заливкой.
Можно повторить B2 перед B4.1: `-kich_r4_resources -kich_r4_geometry`.
Если указать одновременно frame и geometry, откроется одно окно B4.1.
Без диагностических ключей игра запускается обычным образом.

## Что проверить на Windows

1. Запусти с `-kich_r4_geometry`. Убедись, что треугольник виден и цвет плавно
   переходит между красной, зелёной и синей вершинами. При изменении пропорций
   окна треугольник сохраняет форму.
2. Нажми **2**. Должны появиться два цветных вращающихся куба с перекрытием.
   Проверь стрелки, приближение/отдаление и Home.
3. Останови вращение клавишей **P**, оставь камеру в исходном положении.
   Переключи **D** несколько раз. При depth=on ближние поверхности закрывают
   дальние. При off дальний, более холодный по цвету куб рисуется поверх ближнего
   в области перекрытия: он специально подаётся вторым. Верни depth=on.
4. Переключи **C** через front и none обратно в back. Можно вернуться на 1,
   чтобы проверить исчезновение треугольника в front, затем вернуть back и 2.
5. На кубах несколько раз измени размер окна, разверни и восстанови его.
   Затем F11 → Alt+Tab → возврат → F11. Сверни окно и восстанови.
   Проверь, что глубина после каждого изменения размера продолжает работать.
6. Закрой тест через Esc. На R2 загрузи сохранение, выполни `vid_restart`,
   проверь обычную картинку и штатно выйди.
7. Повтори короткую проверку B4.1 перед R1: режимы 1 и 2, resize, закрытие,
   загрузка сохранения и выход. Один запуск без ключа проверит обычный старт.

Пришли полные логи **B4.1 → R2** и **B4.1 → R1** и коротко напиши, как выглядели
треугольник, кубы, глубина и culling. Счётчики DrawIndexed/Present подтверждают
выполнение вызовов; видимую картинку подтверждает эта проверка.

## Ожидаемый лог

```text
[R4 bridge B4.1] begin: autonomous DX11 geometry test before R1/R2
[R4 bridge B4.1] create: 960x540; hardware FL11_0; RGBA8; vsync=1; windowed
[R4 bridge B4.1] geometry ready: VS/PS SM5, vertex/index/constant buffers, input layout; ...
[R4 bridge B4.1] scene=triangle depth=on cull=back rotation=running; camera ...
[R4 bridge B4.1] first DX11 geometry frame presented
[R4 bridge B4.1] scene=cubes depth=on cull=back rotation=running; camera ...
...
[R4 bridge B4.1] destroy: presented=... attempts=... clears=... resizes=...; views=N/N; flags=0x0; ...
[R4 bridge B4.1] geometry: draws=... triangles=... updates=...; pipelines=1/1 depth=N/N resources=6/6 live=0; triangle_frames=... cube_frames=...
[R4 bridge B4.1] PASS: triangle and cubes drawn; geometry/depth/target/device released; continuing R1/R2
```

Числа зависят от длительности и действий. Нужны оба ненулевых счётчика сцен,
успешный Present, баланс views/pipelines/depth/resources, live=0 и flags=0x0.
Внутренний итоговый отчёт дополнительно проверяет нулевой объём живых ресурсов.

При несовпадении ABI, старой DLL, отсутствии runtime/compiler либо ошибке
создания/рисования тест выдаёт диагностику и продолжает R1/R2. Если уничтожение
устройства не завершилось даже при повторной попытке, PASS не выдаётся,
DLL остаётся загруженной до выхода процесса. Полный автоматический перезапуск
DX11 после device removed относится к дальнейшему развитию рендера.

## Изменения архитектуры

| Файл | Назначение |
|---|---|
| `KichR4Geometry.h` | Новая C ABI-таблица рисования и финальный отчёт владения |
| `KichR4TestScene.h` | Тестовые вершины, индексы, шейдеры, матрицы и управление камерой |
| `KichR4GeometrySession.h` | EXE создаёт ресурсы и отправляет команды рисования через handles |
| `R4GeometryRules.h` | Проверка layout, форматов и диапазона индексов без зависимости от D3D |
| `R4PipelineDX11.*` | Реальные input layout, raster/depth states и вызовы рисования D3D11 |
| `R4FrameDX11.*` | Владение и пересоздание depth texture / depth view вместе с target |
| `R4Resources.cpp` | Привязка Geometry API к реестру устройств и ресурсов B2 |

Старые B0/B2/B3 ABI-заголовки сохранены побайтно. Новая Geometry API v1:
Win32, pack(4), __cdecl, таблица 36 байт, только plain descriptions/handles.
Устройство, shaders, layout и состояния принадлежат DLL. Pipeline удерживает
собственные ссылки на VS/PS. Вся работа выполняется в потоке создания устройства.
Старые layouts/vtable R1/R2/xrGame не менялись.

Первая версия рисует TRIANGLELIST из одного vertex stream; константы b0
поддерживаются в VS/PS. Полная запись в buffer копируется до возврата вызова.
Проверяются handles, виды/инициализация buffers, stride, размер constants,
параметры состояния, диапазон IB и порядок begin → clear → draw → present.
Корректность значений индексов относительно VB гарантирует вызывающий код;
для поставляемой сцены это отдельно проверено тестами.

При resize снимаются привязки, освобождаются RTV и depth, затем выполняется
ResizeBuffers. Depth создаётся лениво под новый размер и сразу очищается.
Нулевые размеры и occlusion обрабатываются контроллером B3. При завершении
сначала освобождается target, затем ресурсы, затем устройство; DXGI уничтожается
до HWND, DLL выгружается после успешного cleanup.

## Проверки перед выдачей

- Portable B4.1: правила layout/draw, индексы и направления граней, матрицы
  D3D depth 0..1, пропорции треугольника, камера и проекция, 120 положений кубов,
  реальная EXE-сессия с записывающими API, отказы во всех 21 позициях вызовов,
  неожиданные положительные результаты, частичное создание и повторный cleanup.
- B3: 2427 проверок контроллера/сессии, включая новый допуск рисования только
  после clear и до Present; resize/suspend/occlusion и старые сценарии сохранены.
- B2: 165 validation/ownership и 194 resource-probe проверки; loader B0–B4.1:
  147 проверок, включая точные ключи, ABI/fallback, FPU и удержание DLL.
- DX9: все 130 old/new lifecycle-трасс совпали в пяти конфигурациях.
- Geometry, frame и loader прошли ASan/UBSan; LeakSanitizer отключён из-за
  ограничений среды. Владение проверяется отдельными счётчиками и сценариями.
- Настоящие исходники R4 собраны и слинкованы MinGW-w64 в Win32 DLL с реальными
  D3D11/DXGI headers; шесть экспортов. Imports: kernel32/msvcrt/user32,
  без статических D3D9/D3D11/DXGI/compiler/xrCore.
- Три EXE probe units, DX9 adapter Release/Debug SDK seam, C11/Win32 ABI,
  XML и ссылки новых файлов проверены. Water/SSAO и публичные старые ABI
  сохранены, `git diff --check` пройден.

Команды при наличии Python 3 и gcc/g++ (для DX9 также нужна история базы):

```text
python tests/r4_geometry/run_tests.py
python tests/r4_frame/run_tests.py
python tests/r4_resources/run_tests.py
python tests/r4_device/run_tests.py
```

Полная сборка MSVC и работа настоящего Windows GPU здесь не запускались.
Их подтверждает твоя сборка и описанная визуальная проверка.

## Developer PowerShell: сохранить в тестовую ветку

После успешной проверки, **из корня KichRayDX11**:

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
    'xr_3da/KichR4Geometry.h'
    'xr_3da/KichR4GeometrySession.h'
    'xr_3da/KichR4TestScene.h'
    'xr_3da/KichR4FrameProbe.h'
    'xr_3da/KichR4FrameProbe.cpp'
    'xr_3da/KichR4FrameSession.h'
    'xr_3da/KichR4Probe.cpp'
    'xr_3da/XR_3DA.vcxproj'
    'xr_3da/XR_3DA.vcxproj.filters'
    'xr_3da/xrRender_R4/R4GeometryRules.h'
    'xr_3da/xrRender_R4/R4PipelineDX11.h'
    'xr_3da/xrRender_R4/R4PipelineDX11.cpp'
    'xr_3da/xrRender_R4/R4Resources.cpp'
    'xr_3da/xrRender_R4/R4FrameController.h'
    'xr_3da/xrRender_R4/R4FrameDX11.h'
    'xr_3da/xrRender_R4/R4FrameDX11.cpp'
    'xr_3da/xrRender_R4/xrRender_R4.def'
    'xr_3da/xrRender_R4/xrRender_R4.vcxproj'
    'tests/r4_bridge/loader_tests.cpp'
    'tests/r4_frame/controller_tests.cpp'
    'tests/r4_geometry'
    'docs/KICH_R4_BRIDGE_B4_1_RU.md'
)
Invoke-KichGit add -- @kichFiles
Invoke-KichGit diff --cached --stat
Invoke-KichGit commit -m 'Add B4.1 DX11 triangle, cubes, camera and depth pipeline'
Invoke-KichGit push -u origin HEAD:refs/heads/feature/dx11-r4-bridge
```

Команда push явно указывает `feature/dx11-r4-bridge`.

## Основание

Предоставленная архитектура моста и согласованный подэтап B4.1 после проверенного B3.

- [Microsoft: CreateInputLayout](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-createinputlayout)
- [Microsoft: Rasterizer description](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_rasterizer_desc)
- [Microsoft: UpdateSubresource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource)
- [Microsoft: DrawIndexed](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-drawindexed)
