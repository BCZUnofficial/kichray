function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "kich_wet_lens_copy")
        :fog(false)
        :zb(false, false)
        :blend(false, blend.one, blend.zero)
        :sorting(2, false)
    shader:sampler("s_lens_copy"):texture("$user$kich_wet_lens"):clamp():f_linear()
end
