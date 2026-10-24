# P0-C2 — bloom R2 (low/high и `phase_bloom`)

Пакет патча «R4 P0 — Lighting, Exposure & Shadow Parity» (пункты 7–9 аудита, без
адаптации экспозиции). Пересобрать **XR_3DA и xrRender_R4, Release | Win32**.
Сам xrRender_R4 в этом пакете не менялся, но EXE должен работать с DLL из пакета
P0-S1 (cbuffer 1072 байта, A8 DDS). Шейдеры встроены в EXE.

## Как это устроено в R2

- `combine_1.ps` и `sky2.ps` пишут две 8-битные цели через `tonemap()`:
  low = цвет·scale (generic0) и high = low/8 (generic1). `combine` смешивается
  с небом по туманной альфе в обе цели сразу. Облака, прозрачные объекты и
  forward рисуются только в generic0, поэтому в bloom не попадают.
- `phase_bloom`: build из generic1 в 256×256, по четыре билинейных выборки
  через (w/2)/256 пикселей экрана; rgb = сумма/2, a = dot(rgb,1) − порог.
  В режиме KichRay `r2_bloom_mode 1` альфа — мягкое колено, rgb·strength.
- Фильтр: гаусс X → Y по 15 выборок на ±(2k−0,5) текселя с весами
  `CalcGauss_wave(kernel_g, kernel_g/3, kernel_scale)`, для Y ядро ×h/w. При
  `r2_ls_bloom_fast on` — два прохода по четырём диагональным выборкам.
- `combine_2`: low + bloom.rgb·bloom.a, затем постобработка.
- При `r2_tonemap off` экспозиция равна 1, этот режим здесь и реализован.

## Что сделано в R4

- Новые цели: `high_color` полного размера RGBA8 и `bloom1`/`bloom2` 256² RGBA8,
  как в R2. 8-битное ограничение сохранено намеренно: это часть картинки R2.
- Небо (`PSSky`) и `PSCombine` пишут в две цели, low и low/8. Смешивание
  `combine` по туману одно на обе цели. Солнечный диск, облака, forward и HUD
  идут только в low.
- Проходы `PSBloomBuild`, `PSBloomFilter` (X/Y), `PSBloomFilterFast` и
  `PSBloomComposite` повторяют координаты выборок D3D9 из `phase_bloom`
  (целочисленные центры пикселей D3D9 восстановлены через `floor(SV_Position)`).
  Веса Гаусса считаются на CPU тем же кодом (`KichR4Bloom.h`).
- Сложение с bloom выполняется после HUD и перед постобработкой (гамма,
  яркость, контраст, шум), как `combine_2` → PP в R2.
- Настройки берутся из тех же консольных переменных: `r2_ls_bloom_threshold`,
  `r2_ls_bloom_kernel_g/b/scale`, `r2_ls_bloom_fast`, `r2_bloom_mode`,
  `r2_bloom_strength`, `r2_bloom_knee`.
- Снимок: `[RECORD POSTPROCESS] id=POSTPROCESS/bloom` — настройки и контракт.

## Чего пока нет

- Адаптация экспозиции и tone mapping вынесены в отдельный пакет:
  см. `R4_P0_C1_RU.md`.
- Затухающего bloom при дисторсии (`combine_2_*_d`, blurred = bloom·8) и
  motion blur R2.
- Руки (HUD) в R2 рисуются deferred и попадают в high. В R4 они forward,
  поэтому сами блум не дают, хотя ореолы сцены поверх них ложатся.

## Проверки, выполненные здесь

- FXC: все 27 точек входа. У `PSCombine` и `PSSky` два выхода (SV_Target0/1).
  XR_3DA и xrRender_R4 компилируются.
- `tests/r4_deferred/bloom_tests.cpp` на рабочих функциях шейдера (MSVC,
  `run_bloom_tests.ps1`): координаты выборок build, gauss X/Y и быстрого
  фильтра для разных пикселей совпадают с интерполяцией вершин R2, веса
  совпадают с дословной копией `CalcGauss_k7/_wave` R2 для разных ядер и
  масштабов, режим KichRay и мягкое колено тоже проверены.

Игровая проверка не выполнялась.

## Как проверить в игре

Те же сцены и настройки, что и раньше (`r2_tonemap off`, `r2_bloom_mode 0`).
Ореол вокруг солнца и ярких участков неба, мягкое свечение светлых стен и ламп,
более светлое и мягкое небо на Кордоне. Для проверки параметров меняйте
`r2_ls_bloom_threshold` (меньше — сильнее) и `r2_ls_bloom_fast` прямо в игре:
R4 читает их каждый кадр.
