#pragma once
// Render.h owns these declarations; stdafx.h only provides forward types.
struct IRender_interface {
    enum GenerationLevel {GENERATION_R1=81,GENERATION_R2=90};
    GenerationLevel generation=GENERATION_R2;
    GenerationLevel get_generation(){return generation;}
};
extern IRender_interface* Render;
