# KichRayDX11 — B4.2 v1: просмотр статической локации на DX11

База: тестовая ветка `feature/dx11-r4-bridge`, коммит
`ca0ba2a95fa4f84645b2e7ca439f5053187da87a` (`DX11 B4.1 Geometry Test`).
В архиве только новые и изменённые относительно этой базы файлы.

## Что показывает B4.2

Отдельное DX11-окно читает настоящие `level` и `level.geom` выбранной локации
из твоей игры. Из таблицы шейдеров берутся имена базовых DDS-текстур. Рисуются
статические поверхности, progressive-меши в полной детализации и размещённые
в уровне деревья. Камера свободно летает по сцене; есть depth, culling,
отсечение объектов за пределами камеры и переключение текстур.

По умолчанию загружается **Кордон — `l01_escape`**. Камера начинает с общего
вида сверху и со стороны уровня. После закрытия просмотрщика продолжается
обычный запуск игры на R1/R2.

Это просмотр сцены перед игровой интеграцией R4. Здесь используется базовый
цвет текстуры и упрощённый alpha cutout. Поэтому освещение будет плоским:
lightmap, динамический свет, тени, настоящее небо, погода, эффекты воды и
исходные проходы материалов относятся к следующим этапам. Деревья стоят на
местах; их ветровая анимация пока не подключена. Динамических персонажей,
оружия, травы из detail-подсистемы и игрового HUD в просмотрщике ещё нет.

Для принятия B4.2 важны узнаваемая геометрия, правильное расположение объектов,
базовые текстуры и устойчивая работа при движении камеры и изменении окна.

## Установка

1. Закрой игру и Visual Studio. Сохрани проверенные EXE/DLL B4.1.
2. Скопируй **содержимое `FILES`** в корень обновлённого KichRayDX11 с заменой.
   Пакет накладывается поверх зафиксированного B4.1. Старые архивы поверх него
   не устанавливай.
3. Пересобери **XR_3DA и xrRender_R4, Release / Win32**, либо согласованный
   Mixed / Win32. Положи свежие **XR_3DA.exe и xrRender_R4.dll** в игровой `bin`.
4. Запускай эту сборку с обычным `fsgame.ltx` своей игры.

В архиве нет игровых карт и текстур: просмотрщик берёт их через файловую систему
движка. Используются `$game_levels$` и `$game_textures$`, включая данные игровых
архивов, которые видит FS. Текущий `$level$` не переключается. Файлы уровня и
текстуры просмотрщик читает без изменения.

Дополнительная библиотека вроде `d3d9.dll` для разработки этого пакета не нужна.
На запуске R4 использует те же системные D3D11 и `d3dcompiler_47.dll`, которые
уже отработали в B4.1. Старый R1/R2 продолжает пользоваться своим DX9-путём.

При сохранении своих `.vcxproj` добавь новые `KichR4Level*.h`,
`KichR4LevelFS.inl` и `KichR4Dds.h` в EXE, `../KichR4Level.h` и
`../KichR4Dds.h` в R4; обнови исходники и `.def` из пакета.
`KichR4FrameProbe.cpp`: **NotUsing PCH, ExceptionHandling = Async (/EHa)**.
R4 сохраняет /MT, /EHa и прежние зависимости. Новых `.cpp` для EXE или DLL
в этом пакете нет: загрузчик и сессия подключены заголовками.

## Запуск

Первый запуск:

```text
-kich_r4_level l01_escape
```

Один `-kich_r4_level` тоже выбирает Кордон. Другую доступную папку можно указать
явно, например `-kich_r4_level l02_garbage`. При отсутствии файлов в FS будет
диагностика, после которой продолжится запуск R1/R2.

Новый ключ сам включает B0. Старые `-kich_r4_frame` и `-kich_r4_geometry`
сохраняются. Если указать несколько ключей окна, приоритет имеет просмотр уровня,
затем геометрический тест B4.1. B2 можно выполнить перед ним отдельным ключом:

```text
-kich_r4_resources -kich_r4_level l01_escape
```

Во время чтения сцены в заголовке будет `Loading level...`. Esc отменяет загрузку;
после освобождения уже созданных ресурсов продолжится обычный запуск игры.

## Управление

| Клавиши | Действие |
|---|---|
| W / S | Лететь вперёд / назад по направлению взгляда |
| A / D | Двигаться влево / вправо |
| Q / E | Опускаться / подниматься по мировой вертикали |
| Стрелки | Поворачивать камеру |
| Shift | Ускорить полёт в четыре раза |
| Ctrl | Замедлить полёт для осмотра деталей |
| Home | Вернуться к общему виду локации |
| T | Текстуры / однотонная геометрия |
| C | Отсечение back → front → none → back |
| Z | Проверка глубины on / off |
| Пробел | Сменить фон |
| F11 / Alt+Enter | Окно / безрамочный режим |
| Esc / крестик / Alt+F4 | Закрыть просмотрщик и продолжить запуск игры |

В этом режиме **D — движение вправо, глубина переключается Z**.
Камера проходит сквозь поверхности: это свободный осмотр, коллизия игрока
появится при подключении игрового режима. Из общего вида можно удерживать
W + Shift, чтобы быстрее приблизиться к центру уровня, затем осматривать
его обычным движением. Если потерял ориентир — Home.

## Проверка на Windows

1. Запусти Кордон с новым ключом. Убедись, что появилась узнаваемая локация,
   видны земля, дороги, здания и статические объекты.
2. Приблизься к нескольким поверхностям. Проверь, что базовые текстуры на месте,
   координаты не растянуты случайным образом и деревья расположены правильно.
3. Нажми T и верни текстуры обратно: геометрия должна сохранять форму и положение.
4. Проверь стрелки, WASD, Q/E, ускорение, замедление и Home.
5. Несколько раз измени размеры окна. Затем **F11 → Alt+Tab → возврат → F11**:
   здесь нужен переход в безрамочный режим и обратно. Сверни и восстанови окно.
6. На небольшой области можно проверить Z и C, затем вернуть depth=on,
   cull=back. Это режимы диагностики, их влияние на целую сцену может выглядеть
   резко: при выключенной глубине нарушается взаимное перекрытие поверхностей.
7. Закрой просмотрщик. На R2 загрузи сохранение, выполни `vid_restart`, выйди
   штатно. Повтори короткий просмотр перед R1 и проверь сохранение и выход.
8. Один обычный запуск без ключа подтвердит привычный старт игры.

Пришли полные логи **B4.2 → R2 / R1** и 1–2 скриншота уровня, особенно если
видны неверные UV, отсутствующие поверхности или смещённые деревья.
Скриншоты нужны для оценки содержимого кадра; вызовы DrawIndexed и Present
сами по себе не подтверждают правильное изображение.

## Заглушки и ограничения загрузчика

Розово-тёмная клетка — заглушка для отсутствующей, неподдерживаемой или
не поместившейся в бюджет текстуры. Первые 24 таких имени записываются в лог,
общие количества — в `ready` и финальную статистику. Пустое имя текстуры даёт
однотонную поверхность. Материалы с несколькими текстурами пока используют
первую базовую текстуру из списка.

Поддерживаются стандартные форматы этой ветки: XRLC 14, OGF 4,
DX9 declarations с FLOAT3/4-позицией и FLOAT2/4, SHORT2/4 или FLOAT16 UV,
общий GCONTAINER или раздельные VCONTAINER/ICONTAINER,
normal/progressive/tree static/tree progressive.
Вершины переводятся в собственный POSITION + TEXCOORD формат DX11.
Base vertex добавляется в индексы, поэтому смещения общих буферов сохраняются.
Hierarchy и LOD-прокси пропускаются; их обычные листовые меши берутся из общей
таблицы visuals один раз. Другие типы nodes учитываются как пропущенные.
Варианты с inline FVF-вершинами или сжатыми запрошенными внутренними chunks
требуют расширения загрузчика.

DDS: 2D, вся доступная цепочка mip; DXT1/3/5 (BC1/2/3), BC4/5/7,
RGBA/BGRA/XRGB и L8/R8. Legacy luminance и перестановки каналов приводятся
к RGBA8. Cubemap, массивы и volume не используются как базовые текстуры.
В этой версии чтение DDS поддерживает плотно упакованные строки.

Ограничения просмотрщика для Win32:

- до 192 МиБ преобразованных вершин и индексов;
- до 384 МиБ суммарного полезного объёма текстур;
- до 2048 пикселей по стороне: при наличии mip chain большие верхние mip
  пропускаются; большой единственный mip не масштабируется и заменяется заглушкой;
- до 64 МиБ на один DDS-файл и до 256 МиБ на открываемый FS-файл.

Это ограничения тестового загрузчика, а не пределы DX11 или будущего игрового
рендера. Секторное/HOM-отсечение и группировка проходов материалов будут
подключаться дальше; здесь работает отсечение по границам камеры.

## Лог и принятие этапа

Ожидаемая последовательность:

```text
[R4 bridge B4.2] begin: autonomous DX11 level test before R1/R2
[R4 bridge B4.2] loading: $game_levels$/l01_escape/level + level.geom
[R4 bridge B4.2] level=l01_escape; visuals=... static_meshes=... trees=... skipped_nodes=... triangles=...; CPU geometry=... MiB
[R4 bridge B4.2] ready: VBs=... IBs=...; DDS loaded=... fallback=... budget_skips=...; texture payload=... MiB; base color / alpha cutout
[R4 bridge B4.2] first DX11 level frame presented
...
[R4 bridge B4.2] destroy: presented=...; views=N/N; flags=0x0; ...
[R4 bridge B4.2] geometry: draws=... triangles=... updates=...; pipelines=1/1 depth=N/N resources=N/N live=0; level_frames=... DDS=... fallback=... camera_steps=...; submitted_draws=... submitted_triangles=...
[R4 bridge B4.2] PASS: level geometry and DDS textures drawn; resources/depth/target/device released; continuing R1/R2
```

Нужны ненулевые Present, рисование мешей и загрузка хотя бы одной исходной DDS.
Проверяются баланс views/pipelines/depth/resources, live=0 и нулевой объём живых
ресурсов. Одни заглушки не дают PASS. Для принятия также нужна визуальная
проверка настоящего уровня.

`submitted_draws` и `submitted_triangles` — 64-битные счётчики отправленных
команд. Общий старый 32-битный счётчик `triangles` насыщается на UINT32_MAX;
это предотвращает его обнуление при длительном просмотре большой локации.

Неудача чтения/разбора/создания/рисования завершает тест с диагностикой и cleanup,
после чего продолжается R1/R2. При незавершённом cleanup после повторной попытки
DLL остаётся загруженной до выхода процесса; PASS не выдаётся.

## Архитектура и выполненные проверки

| Файл | Роль |
|---|---|
| `KichR4LevelFS.inl`, `KichR4LevelFiles.h` | Узкий адаптер FS движка, владение IReader остаётся в EXE |
| `KichR4LevelData.h` | Проверка chunks, буферов, UV, SWI, материалов и статических visuals |
| `KichR4LevelCamera.h` | Свободная камера, проекция, отсечение world-space AABB |
| `KichR4LevelSession.h` | Загрузка ресурсов через handles, текстуры, отправка мешей, диагностика |
| `KichR4Dds.h` | Общий проверяемый DDS reader и преобразование каналов |
| `KichR4Level.h` | Новая отдельная C API-таблица DDS/pipeline/textured draw |
| `R4Resources.cpp`, `R4PipelineDX11.*` | Реальные texture/SRV/sampler и привязка к устройству B2 |

Старые B0/B2/B3/B4.1 ABI-заголовки сохранены побайтно. Новая таблица B4.2
имеет Win32 pack(4), __cdecl, размер 28 байт. Engine FS и STL не передаются
в DLL: через границу идут plain descriptions, handles и заимствованные на
время вызова байты. Финальное освобождение использует проверенный B4.1 owner.

Проверено локально:

- Реальные level/DDS/camera функции на fixtures, составленных по XRLC writer
  этой ветки: базовые смещения, дробные UV, tree transforms, highest-detail SWI,
  пропуск hierarchy/LOD proxy, обрезанные и повреждённые файлы, DDS mip chains,
  маски/форматы/границы, каналы BGRA/XRGB/L8, камера и frustum.
- Реальная EXE-сессия с записывающими графическими API: текстурированный и
  однотонный кадры, resize, все 33 позиции отказа API, положительные ошибочные
  результаты, частичное создание, отсутствующая DDS, отсутствующий/испорченный
  уровень, отмена загрузки и повторный cleanup.
- Настоящий FS-адаптер проверен с recording FS/IReader; открытые views
  освобождаются при успехе, ошибке и отмене.
- Старые B2, B3, B4.1 и loader проверки прошли; все 130 DX9 old/new lifecycle
  traces совпали в пяти конфигурациях.
- Level reader, DDS, camera, session, FS и loader прошли ASan/UBSan.
  LeakSanitizer отключён из-за ограничений среды; владение проверяется отдельно.
- Win32 MinGW-w64 с настоящими D3D11/DXGI headers: R4 собран и слинкован;
  семь экспортов, imports только kernel32/msvcrt/user32. Три настоящих EXE
  probe units также собраны и слинкованы в проверочный host. Проверены Win32
  C11 ABI, DX9 adapter Release/Debug SDK seam, XML и новые project references.
- Старые layouts/vtable и исходники R1/R2, water/SSAO не менялись.

Полная сборка Visual Studio и вывод настоящих файлов твоей игры на Windows GPU
здесь не выполнялись. Их подтверждает описанный выше прогон.

```text
python tests/r4_level/run_tests.py
python tests/r4_geometry/run_tests.py
python tests/r4_frame/run_tests.py
python tests/r4_resources/run_tests.py
python tests/r4_device/run_tests.py
```

## Developer PowerShell: после успешной проверки

Из корня KichRayDX11:

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
    'xr_3da/KichR4Probe.h'
    'xr_3da/KichR4Probe.cpp'
    'xr_3da/KichR4FrameProbe.h'
    'xr_3da/KichR4FrameProbe.cpp'
    'xr_3da/KichR4Level.h'
    'xr_3da/KichR4LevelFiles.h'
    'xr_3da/KichR4LevelFS.inl'
    'xr_3da/KichR4LevelOption.h'
    'xr_3da/KichR4LevelData.h'
    'xr_3da/KichR4LevelCamera.h'
    'xr_3da/KichR4LevelSession.h'
    'xr_3da/KichR4Dds.h'
    'xr_3da/XR_3DA.vcxproj'
    'xr_3da/XR_3DA.vcxproj.filters'
    'xr_3da/xrRender_R4/R4PipelineDX11.h'
    'xr_3da/xrRender_R4/R4PipelineDX11.cpp'
    'xr_3da/xrRender_R4/R4Resources.cpp'
    'xr_3da/xrRender_R4/xrRender_R4.def'
    'xr_3da/xrRender_R4/xrRender_R4.vcxproj'
    'tests/r4_bridge/loader_tests.cpp'
    'tests/r4_level'
    'docs/KICH_R4_BRIDGE_B4_2_RU.md'
)
Invoke-KichGit add -- @kichFiles
Invoke-KichGit diff --cached --stat
Invoke-KichGit commit -m 'Add B4.2 DX11 level geometry and DDS texture viewer'
Invoke-KichGit push -u origin HEAD:refs/heads/feature/dx11-r4-bridge
```

Push явно направлен в тестовую `feature/dx11-r4-bridge`.

## Источники форматов

Форматы уровня взяты из `xr_3da/Fmesh.h`, `xr_3da/xrLevel.h`,
`xrLC/OGF_Face_Save.cpp`, `xrLC/xrMU_Model_export_geometry.cpp`, `xrLC/vbm.h`
и `resources/shaders/r2/shared/common.h` этой базы.

- [Microsoft: DDS_HEADER](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-header)
- [Microsoft: DDS_PIXELFORMAT](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-pixelformat)
- [Microsoft: создание texture и view](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-textures-how-to)
