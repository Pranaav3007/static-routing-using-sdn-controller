"""Deterministic static-route definitions shared by the controller and tests."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple


@dataclass(frozen=True)
class Host:
    name: str
    ip: str
    mac: str
    switch: str
    port: int


@dataclass(frozen=True)
class FlowRuleSpec:
    switch: str
    dpid: int
    in_port: int
    out_port: int
    ipv4_src: str
    ipv4_dst: str

    def signature(self) -> Tuple[str, int, int, int, str, str]:
        return (
            self.switch,
            self.dpid,
            self.in_port,
            self.out_port,
            self.ipv4_src,
            self.ipv4_dst,
        )


class StaticRoutingPolicy:
    """Encodes hosts, links, and the exact path to install for each approved flow."""

    def __init__(self) -> None:
        self.switch_dpids: Dict[str, int] = {
            "s1": 1,
            "s2": 2,
            "s3": 3,
            "s4": 4,
        }
        self.hosts_by_ip: Dict[str, Host] = {
            "10.0.0.1": Host("h1", "10.0.0.1", "00:00:00:00:00:01", "s1", 1),
            "10.0.0.2": Host("h2", "10.0.0.2", "00:00:00:00:00:02", "s4", 1),
            "10.0.0.3": Host("h3", "10.0.0.3", "00:00:00:00:00:03", "s2", 1),
            "10.0.0.4": Host("h4", "10.0.0.4", "00:00:00:00:00:04", "s3", 1),
        }
        self.links: Dict[str, Dict[str, int]] = {
            "s1": {"h1": 1, "s2": 2, "s3": 3},
            "s2": {"h3": 1, "s1": 2, "s4": 3},
            "s3": {"h4": 1, "s1": 2, "s4": 3},
            "s4": {"h2": 1, "s2": 2, "s3": 3},
        }
        self.allowed_routes: Dict[Tuple[str, str], List[str]] = {
            ("10.0.0.1", "10.0.0.2"): ["s1", "s2", "s4"],
            ("10.0.0.2", "10.0.0.1"): ["s4", "s2", "s1"],
            ("10.0.0.3", "10.0.0.4"): ["s2", "s1", "s3"],
            ("10.0.0.4", "10.0.0.3"): ["s3", "s1", "s2"],
        }

    def route_for_ips(self, src_ip: str, dst_ip: str) -> Optional[List[str]]:
        route = self.allowed_routes.get((src_ip, dst_ip))
        return list(route) if route else None

    def is_allowed(self, src_ip: str, dst_ip: str) -> bool:
        return (src_ip, dst_ip) in self.allowed_routes

    def host_for_ip(self, ip_address: str) -> Host:
        try:
            return self.hosts_by_ip[ip_address]
        except KeyError as exc:
            raise ValueError(f"Unknown host IP: {ip_address}") from exc

    def build_flow_plan(self, src_ip: str, dst_ip: str) -> List[FlowRuleSpec]:
        route = self.route_for_ips(src_ip, dst_ip)
        if not route:
            raise ValueError(f"No approved static route for {src_ip} -> {dst_ip}")

        src_host = self.host_for_ip(src_ip)
        dst_host = self.host_for_ip(dst_ip)

        plan: List[FlowRuleSpec] = []
        previous_node = src_host.name
        for index, switch_name in enumerate(route):
            next_node = dst_host.name if index == len(route) - 1 else route[index + 1]
            in_port = self.links[switch_name][previous_node]
            out_port = self.links[switch_name][next_node]
            plan.append(
                FlowRuleSpec(
                    switch=switch_name,
                    dpid=self.switch_dpids[switch_name],
                    in_port=in_port,
                    out_port=out_port,
                    ipv4_src=src_ip,
                    ipv4_dst=dst_ip,
                )
            )
            previous_node = switch_name
        return plan

    def bidirectional_plans(self, ip_a: str, ip_b: str) -> List[FlowRuleSpec]:
        plans: List[FlowRuleSpec] = []
        for src_ip, dst_ip in ((ip_a, ip_b), (ip_b, ip_a)):
            if self.is_allowed(src_ip, dst_ip):
                plans.extend(self.build_flow_plan(src_ip, dst_ip))
        return plans

    @staticmethod
    def flow_signature(plan: Iterable[FlowRuleSpec]) -> Tuple[Tuple[str, int, int, int, str, str], ...]:
        return tuple(spec.signature() for spec in plan)
