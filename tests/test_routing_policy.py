import unittest

from controller.routing_policy import StaticRoutingPolicy


class StaticRoutingPolicyTests(unittest.TestCase):
    def setUp(self):
        self.policy = StaticRoutingPolicy()

    def test_primary_route_is_selected_for_h1_to_h2(self):
        self.assertEqual(
            self.policy.route_for_ips("10.0.0.1", "10.0.0.2"),
            ["s1", "s2", "s4"],
        )

    def test_disallowed_pair_is_blocked(self):
        self.assertIsNone(self.policy.route_for_ips("10.0.0.1", "10.0.0.4"))
        self.assertFalse(self.policy.is_allowed("10.0.0.1", "10.0.0.4"))

    def test_flow_plan_matches_expected_ports(self):
        plan = self.policy.build_flow_plan("10.0.0.1", "10.0.0.2")
        self.assertEqual(
            [(rule.switch, rule.in_port, rule.out_port) for rule in plan],
            [("s1", 1, 2), ("s2", 2, 3), ("s4", 2, 1)],
        )

    def test_reinstall_keeps_same_flow_signature(self):
        first_plan = self.policy.build_flow_plan("10.0.0.1", "10.0.0.2")
        _other_plan = self.policy.build_flow_plan("10.0.0.3", "10.0.0.4")
        second_plan = self.policy.build_flow_plan("10.0.0.1", "10.0.0.2")
        self.assertEqual(
            self.policy.flow_signature(first_plan),
            self.policy.flow_signature(second_plan),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
