function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "kich_ssao_calc")
        :fog(false) :zb(false, false) :blend(false, blend.one, blend.zero)
        :sorting(2, false)
    shader:sampler("s_position"):texture("$user$position"):clamp():f_none()
    shader:sampler("s_normal"):texture("$user$normal"):clamp():f_none()
end
