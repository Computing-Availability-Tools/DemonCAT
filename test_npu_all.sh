#!/bin/bash
cd /home/ws/DemonCAT && D=./build/dcat

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  NPU 19 条故障完整测试                                         ║"
echo "║  每条: inject → 无参query → 带参query → clean → 带参query      ║"
echo "║  测试设备: chip=2 (Atlas 910B4)                                ║"
echo "╚══════════════════════════════════════════════════════════════╝"

# ---------- 1. rNPU_link_down ----------
echo ""; echo "========== 1. rNPU_link_down =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_link_down --chip=2
$D query  rNPU_link_down
$D query  rNPU_link_down --chip=2
$D clean rNPU_link_down --chip=2
$D query  rNPU_link_down --chip=2

# ---------- 2. rNPU_ip_change ----------
echo ""; echo "========== 2. rNPU_ip_change =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_ip_change --chip=2 --address=10.20.10.100 --netmask=255.255.255.0
$D query  rNPU_ip_change
$D query  rNPU_ip_change --chip=2
$D clean rNPU_ip_change --chip=2
$D query  rNPU_ip_change --chip=2

# ---------- 3. rNPU_gw_change ----------
echo ""; echo "========== 3. rNPU_gw_change =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_gw_change --chip=2 --gateway=10.20.10.254
$D query  rNPU_gw_change
$D query  rNPU_gw_change --chip=2
$D clean rNPU_gw_change --chip=2
$D query  rNPU_gw_change --chip=2

# ---------- 4. rNPU_netdetect_change ----------
echo ""; echo "========== 4. rNPU_netdetect_change =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_netdetect_change --chip=2 --address=10.20.10.254
$D query  rNPU_netdetect_change
$D query  rNPU_netdetect_change --chip=2
$D clean rNPU_netdetect_change --chip=2
$D query  rNPU_netdetect_change --chip=2

# ---------- 5. rNPU_arp_poison ----------
echo ""; echo "========== 5. rNPU_arp_poison =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_arp_poison --chip=2 --dev=eth0 --ip=10.20.10.200 --mac=de:ad:be:ef:00:01
$D query  rNPU_arp_poison
$D query  rNPU_arp_poison --chip=2 --dev=eth0 --ip=10.20.10.200
$D clean rNPU_arp_poison --chip=2 --dev=eth0 --ip=10.20.10.200
$D query  rNPU_arp_poison --chip=2 --dev=eth0 --ip=10.20.10.200

# ---------- 6. rNPU_arp_del (先 poison 再 del) ----------
echo ""; echo "========== 6. rNPU_arp_del (先注入假ARP再删) =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_arp_poison --chip=2 --dev=eth0 --ip=10.20.10.201 --mac=de:ad:be:ef:00:02
rm -f ~/.demoncat/state.json
$D inject rNPU_arp_del --chip=2 --dev=eth0 --ip=10.20.10.201
$D query  rNPU_arp_del
$D query  rNPU_arp_del --chip=2 --dev=eth0 --ip=10.20.10.201
$D clean rNPU_arp_del --chip=2 --dev=eth0 --ip=10.20.10.201
$D query  rNPU_arp_del --chip=2 --dev=eth0 --ip=10.20.10.201
hccn_tool -i 2 -arp -d dev eth0 ip 10.20.10.201 2>/dev/null

# ---------- 7. rNPU_route_add ----------
echo ""; echo "========== 7. rNPU_route_add =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_route_add --chip=2 --address=10.30.0.0 --netmask=255.255.255.0 --gateway=10.20.10.1
$D query  rNPU_route_add
$D query  rNPU_route_add --chip=2 --address=10.30.0.0 --netmask=255.255.255.0
$D clean rNPU_route_add --chip=2 --address=10.30.0.0 --netmask=255.255.255.0
$D query  rNPU_route_add --chip=2 --address=10.30.0.0 --netmask=255.255.255.0

# ---------- 8. rNPU_route_del (先 add 再 del) ----------
echo ""; echo "========== 8. rNPU_route_del (先加路由再删) =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_route_add --chip=2 --address=10.30.0.0 --netmask=255.255.255.0 --gateway=10.20.10.1
rm -f ~/.demoncat/state.json
$D inject rNPU_route_del --chip=2 --address=10.30.0.0 --netmask=255.255.255.0
$D query  rNPU_route_del
$D query  rNPU_route_del --chip=2 --address=10.30.0.0 --netmask=255.255.255.0
$D clean rNPU_route_del --chip=2 --address=10.30.0.0 --netmask=255.255.255.0
$D query  rNPU_route_del --chip=2 --address=10.30.0.0 --netmask=255.255.255.0
hccn_tool -i 2 -route -d address 10.30.0.0 netmask 255.255.255.0 2>/dev/null

# ---------- 9. rNPU_route_clear ----------
echo ""; echo "========== 9. rNPU_route_clear =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_route_clear --chip=2
$D query  rNPU_route_clear
$D query  rNPU_route_clear --chip=2
$D clean rNPU_route_clear --chip=2
$D query  rNPU_route_clear --chip=2

# ---------- 10. rNPU_iprule_add ----------
echo ""; echo "========== 10. rNPU_iprule_add =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_iprule_add --chip=2 --dir=from --ip=10.20.10.0 --table=100
$D query  rNPU_iprule_add
$D query  rNPU_iprule_add --chip=2 --dir=from --ip=10.20.10.0
$D clean rNPU_iprule_add --chip=2 --dir=from --ip=10.20.10.0
$D query  rNPU_iprule_add --chip=2 --dir=from --ip=10.20.10.0

# ---------- 11. rNPU_iprule_del (先 add 再 del) ----------
echo ""; echo "========== 11. rNPU_iprule_del (先加规则再删) =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_iprule_add --chip=2 --dir=from --ip=10.20.10.0 --table=100
rm -f ~/.demoncat/state.json
$D inject rNPU_iprule_del --chip=2 --dir=from --ip=10.20.10.0
$D query  rNPU_iprule_del
$D query  rNPU_iprule_del --chip=2 --dir=from --ip=10.20.10.0
$D clean rNPU_iprule_del --chip=2 --dir=from --ip=10.20.10.0
$D query  rNPU_iprule_del --chip=2 --dir=from --ip=10.20.10.0

# ---------- 12. rNPU_iproute_add ----------
echo ""; echo "========== 12. rNPU_iproute_add =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_iproute_add --chip=2 --ip=10.40.0.0 --ip_mask=24 --via=10.20.10.1 --dev=eth0 --table=0
$D query  rNPU_iproute_add
$D query  rNPU_iproute_add --chip=2 --ip=10.40.0.0 --ip_mask=24 --table=0
$D clean rNPU_iproute_add --chip=2 --ip=10.40.0.0 --ip_mask=24 --table=0
$D query  rNPU_iproute_add --chip=2 --ip=10.40.0.0 --ip_mask=24 --table=0

# ---------- 13. rNPU_iproute_del (先 add 再 del) ----------
echo ""; echo "========== 13. rNPU_iproute_del (先加路由再删) =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_iproute_add --chip=2 --ip=10.40.0.0 --ip_mask=24 --via=10.20.10.1 --dev=eth0 --table=0
rm -f ~/.demoncat/state.json
$D inject rNPU_iproute_del --chip=2 --ip=10.40.0.0 --ip_mask=24 --table=0
$D query  rNPU_iproute_del
$D query  rNPU_iproute_del --chip=2 --ip=10.40.0.0 --ip_mask=24 --table=0
$D clean rNPU_iproute_del --chip=2 --ip=10.40.0.0 --ip_mask=24 --table=0
$D query  rNPU_iproute_del --chip=2 --ip=10.40.0.0 --ip_mask=24 --table=0

# ---------- 14. rNPU_bw_limit ----------
echo ""; echo "========== 14. rNPU_bw_limit =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_bw_limit --chip=2 --bw_limit=50000
$D query  rNPU_bw_limit
$D query  rNPU_bw_limit --chip=2
$D clean rNPU_bw_limit --chip=2
$D query  rNPU_bw_limit --chip=2

# ---------- 15. rNPU_mtu_mismatch ----------
echo ""; echo "========== 15. rNPU_mtu_mismatch =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_mtu_mismatch --chip=2 --size=1500
$D query  rNPU_mtu_mismatch
$D query  rNPU_mtu_mismatch --chip=2
$D clean rNPU_mtu_mismatch --chip=2
$D query  rNPU_mtu_mismatch --chip=2

# ---------- 16. rNPU_dscp_tc_change ----------
echo ""; echo "========== 16. rNPU_dscp_tc_change =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_dscp_tc_change --chip=2 --dscp=10 --tc=2
$D query  rNPU_dscp_tc_change
$D query  rNPU_dscp_tc_change --chip=2 --dscp=10
$D clean rNPU_dscp_tc_change --chip=2 --dscp=10
$D query  rNPU_dscp_tc_change --chip=2 --dscp=10

# ---------- 17. rNPU_prio_tc_change ----------
echo ""; echo "========== 17. rNPU_prio_tc_change =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_prio_tc_change --chip=2 --map=0,0,0,0,1,1,1,1
$D query  rNPU_prio_tc_change
$D query  rNPU_prio_tc_change --chip=2
$D clean rNPU_prio_tc_change --chip=2
$D query  rNPU_prio_tc_change --chip=2

# ---------- 18. rNPU_pfc_change ----------
echo ""; echo "========== 18. rNPU_pfc_change =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_pfc_change --chip=2 --bitmap=0,0,0,0,1,0,0,0
$D query  rNPU_pfc_change
$D query  rNPU_pfc_change --chip=2
$D clean rNPU_pfc_change --chip=2
$D query  rNPU_pfc_change --chip=2

# ---------- 19. rNPU_roce_port_change ----------
echo ""; echo "========== 19. rNPU_roce_port_change =========="
rm -f ~/.demoncat/state.json /tmp/dcat-*.bak
$D inject rNPU_roce_port_change --chip=2 --port=45000
$D query  rNPU_roce_port_change
$D query  rNPU_roce_port_change --chip=2
$D clean rNPU_roce_port_change --chip=2
$D query  rNPU_roce_port_change --chip=2

echo ""; echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  全部 19 条 NPU 故障测试完成                                     ║"
echo "╚══════════════════════════════════════════════════════════════╝"
