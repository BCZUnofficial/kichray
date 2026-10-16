#ifndef	MBLUR_H
#define MBLUR_H

#ifndef 	USE_MBLUR
half3 	mblur	(float2 UV, half3 pos, half3 c_original)	{ return c_original; }
#else
#include "common.h"

uniform float4x4	m_current;
uniform float4x4	m_previous;
uniform half2 	m_blur;		// scale_x / 12, scale_y / 12

#define MBLUR_SAMPLES 	half(12)
#define MBLUR_CLAMP	half(0.001)

half3 	mblur	(float2 UV, half3 pos, half3 c_original)	{
	float4 	pos4		= float4	(pos,1.f);

	float4 	p_current	= mul	(m_current,	pos4);
	float4 	p_previous 	= mul	(m_previous,	pos4);
    // Soft particles clear empty G-buffer pixels to zero. Their projected w
    // is zero as well; never divide by it (NaNs can turn sky/foliage edges black).
    float current_w = max(abs(p_current.w), 0.0001f);
    float previous_w = max(abs(p_previous.w), 0.0001f);
    float valid = (p_current.w > 0.0001f && p_previous.w > 0.0001f) ? 1.0f : 0.0f;
    float2 p_velocity = m_blur * ((p_current.xy/current_w)-(p_previous.xy/previous_w)) * valid;
		p_velocity	= clamp	(p_velocity,-MBLUR_CLAMP,+MBLUR_CLAMP);

	// For each sample, sum up each sample's color in "Blurred" and then divide
	// to average the color after all the samples are added.
	half3 	blurred 	= 	c_original	;
        	blurred 	+= 	tex2D(s_image, p_velocity * 1.h  + UV).rgb;
		blurred		+= 	tex2D(s_image, p_velocity * 2.h  + UV).rgb;
		blurred		+= 	tex2D(s_image, p_velocity * 3.h  + UV).rgb;
		blurred		+= 	tex2D(s_image, p_velocity * 4.h  + UV).rgb;
        	blurred 	+= 	tex2D(s_image, p_velocity * 5.h  + UV).rgb;
		blurred		+= 	tex2D(s_image, p_velocity * 6.h  + UV).rgb;
		blurred		+= 	tex2D(s_image, p_velocity * 7.h  + UV).rgb;
		blurred		+= 	tex2D(s_image, p_velocity * 8.h  + UV).rgb;
        	blurred 	+= 	tex2D(s_image, p_velocity * 9.h  + UV).rgb;
		blurred		+= 	tex2D(s_image, p_velocity * 10.h + UV).rgb;
		blurred		+= 	tex2D(s_image, p_velocity * 11.h + UV).rgb;
	return 	blurred/MBLUR_SAMPLES;
}
#endif

#endif
