-- X-Ray Clear Sky DX9 R2 sun shafts, adapted for the SoC FP16 light accumulator.
function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "accum_volumetric_sun")
        :fog(false)
        :zb(false, false)
        :blend(true, blend.one, blend.one)
        :sorting(2, false)
    shader:sampler("s_smap")     :texture("null")
    shader:sampler("s_position") :texture("$user$position")
    shader:sampler("jitter0")    :texture("$user$jitter_0") :f_none()
end
