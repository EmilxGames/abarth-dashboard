#!/bin/sh
# Test sul computer (host) della logica indipendente dall'hardware.
set -e
cd "$(dirname "$0")"
c++ -std=c++17 -Istub -I../../main test_custom_layout.cpp ../../main/ui_v2/custom_layout.cpp -o /tmp/test_custom_layout
/tmp/test_custom_layout
c++ -std=c++17 -I../../main test_page_order.cpp ../../main/ui_v2/page_order.cpp -o /tmp/test_page_order
/tmp/test_page_order
