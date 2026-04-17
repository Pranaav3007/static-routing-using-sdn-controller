Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$captures = @(
    @{ Input = "README.md"; Output = "screenshots/01-readme-page" },
    @{ Input = "REPORT.md"; Output = "screenshots/02-report-page" },
    @{ Input = "controller/routing_policy.py"; Output = "screenshots/03-routing-policy-page" },
    @{ Input = "controller/static_routing_controller.py"; Output = "screenshots/04-static-routing-controller-page" },
    @{ Input = "topology/static_routing_topology.py"; Output = "screenshots/05-topology-page" },
    @{ Input = "tests/test_routing_policy.py"; Output = "screenshots/06-routing-test-page" },
    @{ Input = "validation/test-routing-policy.txt"; Output = "screenshots/07-routing-test-log" },
    @{ Input = "validation/compileall.txt"; Output = "screenshots/08-compileall-log" },
    @{ Input = "validation/live-demo-checklist.txt"; Output = "screenshots/09-live-demo-checklist" }
)

New-Item -ItemType Directory -Force "screenshots" | Out-Null
Get-ChildItem "screenshots" -Filter "*.png" -ErrorAction SilentlyContinue | Remove-Item -Force

foreach ($capture in $captures) {
    & "$PSScriptRoot/render_text_to_png.ps1" -InputPath $capture.Input -OutputPrefix $capture.Output
}
