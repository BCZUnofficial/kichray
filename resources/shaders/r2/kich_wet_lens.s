function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "kich_wet_lens")
        :fog(false)
        :zb(false, false)
        :blend(false, blend.one, blend.zero)
        :sorting(2, false)
    shader:sampler("s_lens_scene"):texture("$user$generic0"):clamp():f_linear()
end
