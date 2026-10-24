# P0-L1/L2 — освещение R2: hmodel, входы окружения, солнце, затухание POINT

Первый пакет патча «R4 P0 — Lighting, Exposure & Shadow Parity» (пункты 1, 2, 4, 5
аудита). Bloom, экспозиция, таблица `s_material`, `sunmask` и тени не менялись.

Пересобрать **XR_3DA и xrRender_R4, Release | Win32**, заменить EXE и DLL вместе:
frame cbuffer вырос с 1008 до 1040 байт (`DeferredConstantsBytes`). Шейдеры
встроены в EXE, gamedata не трогается. xrGame, R1 и R2 не меняются.

## Что подтверждено по исходникам

- `hmodel()` в R2 вызывается только из `combine_1.ps`. Непрямой свет берётся
  из двух погодных cubemap `sky_r_textures_env[0/1]`, выбранных по мировой
  нормали (diffuse) и по вектору отражения (specular), и смешивается по
  `CurrentEnv.weight`. R4 вместо этого считал `ambient + hemi_color·отклик`
  в солнечном проходе.
- R2 передаёт в `combine` `L_ambient = max(ambient·2, 0,001)·r2_sun_lumscale_amb`
  и `env_color = (sky_color·2 + EPS)·2·r2_sun_lumscale_hemi`. `hemi_color` в
  этой формуле не участвует. R4 брал сырые `ambient` и `hemi_color`.
- Forward-шейдеры R2 (`vert.vs`, `lmape.ps` и др.) `hmodel` не используют, у них
  `L_ambient` (сырой) + `L_hemi_color`. Поэтому forward-путь R4 сохранён.
- `accum_point` затухает по полному `range`, `·0,95` есть только в `accum_spot`.
  R4 применял `·0,95` к обоим типам.
- Цвет солнца R2 — `sun_adapted = sun_color·r2_sun_lumscale`. Проход солнца:
  `R2FLAG_SUN && u_diffuse2s(цвет) > EPS`, при `sunstatic` он выключен. В R4 эти
  условия не учитывались. `u_diffuse2s` содержит `r2_gloss_factor`, в R4 его тоже
  не было.

## Изменения

**Deferred-непрямой свет = R2 hmodel.** `PSCombine` считает `Hemisphere()`:
обе cubemap (t4/t5) и интерполяция `env_color.w`. Квадрат окружения — это
«contrast» R2, ремаппинг `reflect.y·2−1` сохранён, отклик материала берётся в
точке `(h, 0,5+0,5·dot(reflect, v))`, как в `tex3D(s_material,…)`. Итог
повторяет `combine_1.ps`: `albedo·(L.rgb + hdiffuse) + L.a + hspecular`.
Specular окружения остаётся RGB и не сводится к скалярной альфе аккумулятора.
Нормали и позиции R4 уже в мировом пространстве, поэтому `m_v2w` не нужна.
Ambient применяется один раз, в `combine`. Солнечный и локальные проходы его
больше не добавляют.

**Входы окружения.** `KichR4_CopyEngineEnvironment()` (EXE) заполняет кадр
освещения сырыми значениями `CurrentEnv` для forward и отдельно значениями
`L_ambient`/`env_color` по формулам R2. Туда же попадают имена обеих env-cubemap,
`r2_gloss_factor` и режим солнца. Константы `env_ambient`/`env_color`
добавлены в конец cbuffer, все прежние смещения не изменились. Cubemap
запрашиваются в preflight-плане текстур. Пока текстура не загружена,
подставляется нейтральная cubemap, и снимок пишет `NEUTRAL_FALLBACK_USED`.

**Солнце.** Цвет умножается на `r2_sun_lumscale`, режим выбирается по правилам
R2: `r2_sun 0` или нулевой specular-цвет выключают солнце, `renderer_r2a`
(R2 static sun) включает `USE_R2_STATIC_SUN`. В этом режиме нет солнечных
карт, а прямое солнце deferred-поверхностей умножается на запечённую
sun-occlusion ×2 из альфы G-buffer. С `renderer_r1` политика R2 не применяется,
солнце остаётся динамическим, как раньше. Исправление E1.6 сохранено:
динамическое солнце не умножается на запечённую маску.

**Затухание.** `LocalAttenuation` выбирает радиус по типу: POINT — `range`,
SPOT — `range·0,95`. q-linear формула и отбор источников не менялись.

**Specular.** `SpecularIntensity` = `u_diffuse2s` с `r2_gloss_factor`
(по умолчанию 1, на стандартных настройках изображение не меняется).

## Что осталось за этим пакетом

- `s_material` по-прежнему считается аналитически, без 8-битной LUT (P0-L3).
- Low/high, адаптация, tone mapping и bloom R2 (P0-C1/C2).
- `sunmask` облаков, stencil солнечного диска, аудит теней (P0-S1…S3).
- Forward static sun освещается без запечённой маски (в forward нет G-buffer).

## Проверки, выполненные здесь

- FXC (Windows SDK 10.0.26100, SM5) компилирует все 23 точки входа. Отражение
  cbuffer: `env_ambient`@1008, `env_color`@1024, всего 1040 байт; в `PSCombine`
  `env0`/`env1` — cube на t4/t5. Новых предупреждений нет, X4000 в `LocalLight`
  был и до патча.
- `KichR4DeferredShaders.h` пересобран. Для Windows без Python добавлен
  `tests/r4_deferred/embed_shaders.ps1`: побайтный аналог `embed_shaders.py`,
  поддерживает `-Check`.
- Обновлены тесты `run_sun_tests.py`/`sun_lighting_tests.cpp` (динамическое,
  статическое и выключенное солнце, forward hemi), `run_lighting_tests.py`
  (POINT/SPOT, `r2_gloss_factor`), `check_hlsl.py` (1040). На этой машине
  нет Python и g++, поэтому сами тесты здесь не запускались.

Игровая проверка не выполнялась.

## Как проверить в игре

Одинаковый `user.ltx` для R2 и R4, `renderer_r2`, bloom/tonemap R2 для сравнения
выключить (в R4 их пока нет). Сцены: Кордон днём (непрямой свет и цвет
теней), кирпич/металл под солнцем (отражения окружения), ночная комната
с лампой (край радиуса POINT), смена погоды (плавная интерполяция cubemap).
`d_snapshot`: секция `ENVIRONMENT` — `hmodel_env0/1`, `hmodel_env_resident`,
`L_ambient`, `env_color`, `sun_mode`.
