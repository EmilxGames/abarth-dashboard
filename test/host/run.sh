#!/bin/sh
# Test sul computer (host) della logica indipendente dall'hardware.
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -Istub -I../../main test_custom_layout.cpp ../../main/ui_v2/custom_layout.cpp -o /tmp/test_custom_layout
/tmp/test_custom_layout
