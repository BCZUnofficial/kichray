function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "kich_wet_surfaces_normal_apply")
        :fog(false)
        :zb(false, false)
        :blend(false, blend.one, blend.zero)
        :sorting(2, false)
    shader:sampler("s_accumulator"):texture("$user$accum"):clamp():f_none()
end
