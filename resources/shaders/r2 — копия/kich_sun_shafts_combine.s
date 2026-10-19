-- Point samples are essential: bilinear filtering would mix depths before rejection.
function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "kich_sun_shafts_combine")
        :fog(false)
        :zb(false, false)
        :blend(true, blend.one, blend.one)
        :sorting(2, false)
    shader:sampler("s_kich_shafts") :texture("$user$kich_sun_shafts") :clamp() :f_none()
    shader:sampler("s_position") :texture("$user$position") :clamp() :f_none()
end
