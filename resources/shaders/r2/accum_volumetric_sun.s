-- Kichray EX1.2: cast volumetric sunlight into a separate half-resolution RT.
function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "accum_volumetric_sun")
        :fog(false)
        :zb(false, false)
        :blend(true, blend.one, blend.one)
        :sorting(2, false)
    shader:sampler("s_smap")     :texture("null")
    shader:sampler("s_position") :texture("$user$position") :f_none()
end
