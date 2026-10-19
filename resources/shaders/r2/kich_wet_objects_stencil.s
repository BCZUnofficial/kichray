function normal(shader, t_base, t_second, t_detail)
    shader:begin("null", "kich_wet_objects_stencil")
        :fog(false)
        :zb(false, false)
        :blend(false, blend.one, blend.zero)
        :sorting(2, false)
end
