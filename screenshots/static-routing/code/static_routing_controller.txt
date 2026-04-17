"""Ryu controller that installs deterministic static routes on packet-in events."""

from __future__ import annotations

from ryu.base import app_manager
from ryu.controller import ofp_event
from ryu.controller.handler import CONFIG_DISPATCHER, DEAD_DISPATCHER, MAIN_DISPATCHER, set_ev_cls
from ryu.lib.packet import arp, ethernet, ether_types, ipv4, packet
from ryu.ofproto import ofproto_v1_3

from controller.routing_policy import StaticRoutingPolicy


class StaticRoutingController(app_manager.RyuApp):
    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    FLOW_PRIORITY = 300
    DROP_PRIORITY = 250

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.policy = StaticRoutingPolicy()
        self.datapaths = {}

    @set_ev_cls(ofp_event.EventOFPStateChange, [MAIN_DISPATCHER, DEAD_DISPATCHER])
    def state_change_handler(self, ev):
        datapath = ev.datapath
        if ev.state == MAIN_DISPATCHER:
            self.datapaths[datapath.id] = datapath
        elif ev.state == DEAD_DISPATCHER:
            self.datapaths.pop(datapath.id, None)

    @set_ev_cls(ofp_event.EventOFPSwitchFeatures, CONFIG_DISPATCHER)
    def switch_features_handler(self, ev):
        datapath = ev.msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser

        table_miss_match = parser.OFPMatch()
        table_miss_actions = [
            parser.OFPActionOutput(ofproto.OFPP_CONTROLLER, ofproto.OFPCML_NO_BUFFER)
        ]
        self._add_flow(datapath, 0, table_miss_match, table_miss_actions)
        self.logger.info("Switch s%s connected; table-miss rule installed", datapath.id)

    def _add_flow(self, datapath, priority, match, actions, idle_timeout=0, hard_timeout=0):
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser
        instructions = [parser.OFPInstructionActions(ofproto.OFPIT_APPLY_ACTIONS, actions)]
        mod = parser.OFPFlowMod(
            datapath=datapath,
            priority=priority,
            match=match,
            instructions=instructions,
            idle_timeout=idle_timeout,
            hard_timeout=hard_timeout,
        )
        datapath.send_msg(mod)

    def _add_drop_flow(self, datapath, in_port, src_ip, dst_ip):
        parser = datapath.ofproto_parser
        match = parser.OFPMatch(
            in_port=in_port,
            eth_type=ether_types.ETH_TYPE_IP,
            ipv4_src=src_ip,
            ipv4_dst=dst_ip,
        )
        self._add_flow(datapath, self.DROP_PRIORITY, match, [])

    def _install_route(self, src_ip: str, dst_ip: str):
        for spec in self.policy.build_flow_plan(src_ip, dst_ip):
            datapath = self.datapaths.get(spec.dpid)
            if datapath is None:
                raise RuntimeError(f"Datapath {spec.dpid} not available while installing route")

            parser = datapath.ofproto_parser
            match = parser.OFPMatch(
                in_port=spec.in_port,
                eth_type=ether_types.ETH_TYPE_IP,
                ipv4_src=spec.ipv4_src,
                ipv4_dst=spec.ipv4_dst,
            )
            actions = [parser.OFPActionOutput(spec.out_port)]
            self._add_flow(datapath, self.FLOW_PRIORITY, match, actions)
            self.logger.info(
                "Installed %s %s -> %s: in_port=%s out_port=%s",
                spec.switch,
                spec.ipv4_src,
                spec.ipv4_dst,
                spec.in_port,
                spec.out_port,
            )

    def _packet_out(self, datapath, data, in_port, out_port):
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser
        actions = [parser.OFPActionOutput(out_port)]
        out = parser.OFPPacketOut(
            datapath=datapath,
            buffer_id=ofproto.OFP_NO_BUFFER,
            in_port=in_port,
            actions=actions,
            data=data,
        )
        datapath.send_msg(out)

    def _send_proxy_arp(self, datapath, src_eth, src_ip, target_ip, in_port):
        target_host = self.policy.host_for_ip(target_ip)

        arp_reply = packet.Packet()
        arp_reply.add_protocol(
            ethernet.ethernet(
                ethertype=ether_types.ETH_TYPE_ARP,
                dst=src_eth,
                src=target_host.mac,
            )
        )
        arp_reply.add_protocol(
            arp.arp(
                opcode=arp.ARP_REPLY,
                src_mac=target_host.mac,
                src_ip=target_host.ip,
                dst_mac=src_eth,
                dst_ip=src_ip,
            )
        )
        arp_reply.serialize()
        self._packet_out(datapath, arp_reply.data, datapath.ofproto.OFPP_CONTROLLER, in_port)

    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def packet_in_handler(self, ev):
        msg = ev.msg
        datapath = msg.datapath
        in_port = msg.match["in_port"]

        pkt = packet.Packet(msg.data)
        eth = pkt.get_protocol(ethernet.ethernet)
        if eth is None or eth.ethertype == ether_types.ETH_TYPE_LLDP:
            return

        arp_pkt = pkt.get_protocol(arp.arp)
        if arp_pkt:
            self._handle_arp(datapath, in_port, eth, arp_pkt)
            return

        ipv4_pkt = pkt.get_protocol(ipv4.ipv4)
        if ipv4_pkt:
            self._handle_ipv4(datapath, in_port, ipv4_pkt.src, ipv4_pkt.dst, msg.data)
            return

        self.logger.info("Ignoring non-ARP/non-IPv4 packet on s%s", datapath.id)

    def _handle_arp(self, datapath, in_port, eth_pkt, arp_pkt):
        if arp_pkt.opcode != arp.ARP_REQUEST:
            return

        if not self.policy.is_allowed(arp_pkt.src_ip, arp_pkt.dst_ip):
            self.logger.info("Blocked ARP for disallowed pair %s -> %s", arp_pkt.src_ip, arp_pkt.dst_ip)
            return

        try:
            self._send_proxy_arp(datapath, eth_pkt.src, arp_pkt.src_ip, arp_pkt.dst_ip, in_port)
            self.logger.info("Proxy ARP sent for %s -> %s", arp_pkt.src_ip, arp_pkt.dst_ip)
        except ValueError:
            self.logger.warning("ARP target %s is unknown to the static policy", arp_pkt.dst_ip)

    def _handle_ipv4(self, datapath, in_port, src_ip, dst_ip, payload):
        if not self.policy.is_allowed(src_ip, dst_ip):
            self._add_drop_flow(datapath, in_port, src_ip, dst_ip)
            self.logger.info("Blocked IPv4 flow %s -> %s on s%s", src_ip, dst_ip, datapath.id)
            return

        self._install_route(src_ip, dst_ip)
        if self.policy.is_allowed(dst_ip, src_ip):
            self._install_route(dst_ip, src_ip)

        current_hop = next(
            spec
            for spec in self.policy.build_flow_plan(src_ip, dst_ip)
            if spec.dpid == datapath.id and spec.in_port == in_port
        )
        self._packet_out(datapath, payload, in_port, current_hop.out_port)
        self.logger.info("Forwarded first packet for %s -> %s through s%s", src_ip, dst_ip, datapath.id)
