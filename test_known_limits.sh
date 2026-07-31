#!/bin/bash
cd /home/ws/DemonCAT && D=./build/dcat

echo "########## 1. rNET_degrade ##########"
rm -f ~/.demoncat/state.json
$D inject rNET_degrade --iface=enp125s0f1 --speed_mbps=1000
$D query rNET_degrade --iface=enp125s0f1
$D clean rNET_degrade --iface=enp125s0f1

echo ""
echo "########## 2. rNPU_route_clear ##########"
rm -f ~/.demoncat/state.json
$D inject rNPU_route_clear --chip=2
$D query rNPU_route_clear --chip=2
$D clean rNPU_route_clear --chip=2

echo ""
echo "########## 3. rNPU_prio_tc_change ##########"
rm -f ~/.demoncat/state.json
$D inject rNPU_prio_tc_change --chip=2 --map=0,0,0,0,1,1,1,1
$D query rNPU_prio_tc_change --chip=2
$D clean rNPU_prio_tc_change --chip=2

echo ""
echo "########## 4. rNPU_pfc_change ##########"
rm -f ~/.demoncat/state.json
$D inject rNPU_pfc_change --chip=2 --bitmap=0,0,0,0,1,0,0,0
$D query rNPU_pfc_change --chip=2
$D clean rNPU_pfc_change --chip=2

echo ""
echo "########## 5. rNPU_link_down ##########"
rm -f ~/.demoncat/state.json
$D inject rNPU_link_down --chip=2
$D query rNPU_link_down --chip=2
$D clean rNPU_link_down --chip=2

echo ""
echo "########## done ##########"
