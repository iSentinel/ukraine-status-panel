#!/usr/bin/env python3
"""Compile the real indoor delivery header against an LVGL test double."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include/usp_indoor_lcd_delivery.h"

LVGL = r"""#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct lv_obj_t { std::string name; std::string text; uint32_t color{}; std::vector<std::string> *trace{}; };
static constexpr int LV_PART_MAIN=0;
inline uint32_t lv_color_hex(uint32_t value){ return value; }
inline void lv_label_set_text(lv_obj_t *obj,const char *text){ obj->text=text; obj->trace->push_back("text:"+obj->name+":"+obj->text); }
inline void lv_obj_set_style_text_color(lv_obj_t *obj,uint32_t color,int){ obj->color=color; obj->trace->push_back("color:"+obj->name+":"+std::to_string(color)); }
"""
CPP = r"""#include <cassert>
#include <iostream>
#include "usp_indoor_lcd_delivery.h"
int main(){ const std::string texts[]={"","Indoor: sensor not connected","В приміщенні: очікування даних","В помещении: 21.5°C · 63%",std::string(95,static_cast<char>(88))}; const uint32_t colors[]={0U,0x94A3B8U,0xFACC15U,0x4ADE80U,0xFFFFFFU}; unsigned n=0; for(const auto &text:texts) for(auto color:colors){ std::vector<std::string> trace; lv_obj_t a{"normal","",0,&trace},b{"full","",0,&trace}; usp_indoor_lcd_delivery::render(text,color,&a,&b); assert(a.text==text&&b.text==text&&a.color==color&&b.color==color); assert(trace.size()==4); assert(trace[0].rfind("text:normal:",0)==0); assert(trace[1].rfind("color:normal:",0)==0); assert(trace[2].rfind("text:full:",0)==0); assert(trace[3].rfind("color:full:",0)==0); ++n; } std::cout<<"INDOOR_LCD_DELIVERY_NATIVE=PASS cases="<<n<<" operations=4\n"; }
"""


def main():
    """Compile and run one bounded real-header equivalence harness."""
    with tempfile.TemporaryDirectory(prefix="indoor-delivery-", dir=ROOT / "artifacts") as name:
        work = Path(name)
        (work / "lvgl.h").write_text(LVGL)
        (work / "test.cpp").write_text(CPP)
        command = ["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror",
                   "-I", str(work), "-I", str(ROOT / "include"), str(work / "test.cpp"),
                   "-o", str(work / "test")]
        subprocess.run(command, check=True, timeout=60)
        result = subprocess.run([str(work / "test")], check=True, text=True,
                                capture_output=True, timeout=30)
        print(result.stdout.strip())


if __name__ == "__main__":
    main()
